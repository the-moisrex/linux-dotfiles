#include "stash.hpp"
#include "../core/sanitize.hpp"
#include "../core/strings.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <format>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <string>
#include <sys/file.h>
#include <system_error>
#include <unistd.h>

namespace prompt::stash {

namespace {

using nlohmann::json;

constexpr int kFormat = 1;

std::string env_value(char const* name) {
    char const* raw = std::getenv(name);
    return raw == nullptr ? std::string{} : std::string(raw);
}

std::optional<std::string> read_all(std::filesystem::path const& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

bool write_all(int fd, std::string_view bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        auto const n = ::write(fd, bytes.data() + offset, bytes.size() - offset);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        offset += static_cast<std::size_t>(n);
    }
    return true;
}

// An exclusive advisory lock around a read-modify-write append. The lock file
// lives beside the stash and is never removed, so two processes always meet on
// the same inode.
class file_lock {
  public:
    file_lock(std::filesystem::path const& path, std::string& error) {
        fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        if (fd_ < 0) {
            error = std::format("cannot open lock {}: {}", path.string(), std::strerror(errno));
            return;
        }
        if (::flock(fd_, LOCK_EX) != 0) {
            error = std::format("cannot lock {}: {}", path.string(), std::strerror(errno));
            ::close(fd_);
            fd_ = -1;
        }
    }

    file_lock(file_lock const&) = delete;
    file_lock& operator=(file_lock const&) = delete;

    ~file_lock() {
        if (fd_ < 0) return;
        ::flock(fd_, LOCK_UN);
        ::close(fd_);
    }

    [[nodiscard]] bool ok() const { return fd_ >= 0; }

  private:
    int fd_ = -1;
};

json to_json(record const& value, bool runtime) {
    json j;
    j["format"] = kFormat;
    j["name"] = value.name;
    j["created"] = value.created;
    j["created_ts"] = value.created_ts;
    j["updated"] = value.updated;
    j["updated_ts"] = value.updated_ts;
    j["cwd"] = value.cwd;
    j["git_root"] = value.git_root;
    json terminal_json{{"sid", value.terminal.sid}, {"tty", value.terminal.tty}};
    if (!value.terminal.key.empty()) terminal_json["key"] = value.terminal.key;
    j["terminal"] = std::move(terminal_json);
    j["source_run"] = value.source_run;
    j["redacted"] = value.redacted;
    j["incomplete"] = value.incomplete;
    j["facts_redacted"] = value.facts_redacted;

    json facts_json = json::object();
    for (auto const& entry : value.known.entries()) facts_json[entry.key] = entry.value;
    j["facts"] = std::move(facts_json);
    j["text"] = value.text;

    if (runtime) {
        j["path"] = value.path.string();
        j["scope"] = value.here ? "here" : "global";
    }
    return j;
}

std::optional<record> load_file(std::filesystem::path const& path, std::string& error) {
    auto data = read_all(path);
    if (!data) {
        error = std::format("cannot read {}", path.string());
        return std::nullopt;
    }
    auto parsed = json::parse(*data, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        error = std::format("{} is not a valid stash file", path.string());
        return std::nullopt;
    }
    if (parsed.value("format", 0) != kFormat) {
        error = std::format("{} uses an unsupported stash format", path.string());
        return std::nullopt;
    }

    record value;
    value.name = parsed.value("name", path.stem().string());
    value.created = parsed.value("created", std::string{});
    value.created_ts = parsed.value("created_ts", 0LL);
    value.updated = parsed.value("updated", std::string{});
    value.updated_ts = parsed.value("updated_ts", 0LL);
    value.cwd = parsed.value("cwd", std::string{});
    value.git_root = parsed.value("git_root", std::string{});
    if (parsed.contains("terminal") && parsed["terminal"].is_object()) {
        auto const& term = parsed["terminal"];
        value.terminal.sid = term.value("sid", 0LL);
        value.terminal.tty = term.value("tty", std::string{});
        value.terminal.key = term.value("key", std::string{});
    }
    value.source_run = parsed.value("source_run", std::string{});
    value.redacted = parsed.value("redacted", false);
    value.incomplete = parsed.value("incomplete", false);
    value.facts_redacted = parsed.value("facts_redacted", false);
    value.text = parsed.value("text", std::string{});
    if (parsed.contains("facts") && parsed["facts"].is_object()) {
        for (auto const& [key, item] : parsed["facts"].items()) {
            if (item.is_string()) value.known.set(key, item.get<std::string>());
        }
    }
    value.path = path;

    // Defence in depth: a hand-edited stash must not inject unsanitized state
    // into the chain. The writer already sanitizes facts, and the dispatcher
    // sanitizes the text as it accumulates, so this is normally a no-op.
    auto clean_text = sanitize::sanitize(value.text);
    value.text = std::move(clean_text.text);
    value.redacted = value.redacted || clean_text.redacted;

    facts clean_facts;
    bool facts_redacted = value.facts_redacted;
    for (auto const& entry : value.known.entries()) {
        auto clean = sanitize::sanitize(entry.value);
        if (clean.redacted) facts_redacted = true;
        clean_facts.set(entry.key, clean.text);
    }
    value.known = std::move(clean_facts);
    value.facts_redacted = facts_redacted;
    return value;
}

// Merge a freshly built snapshot into an existing record: the text is appended
// with the chain's one blank-line rule, facts are last-writer-wins, and the
// provenance timestamps/identity follow the newest invocation.
record append_record(record existing, record const& value) {
    existing.name = value.name;
    text::append_block(existing.text, value.text);
    existing.known.merge(value.known, true);
    existing.redacted = existing.redacted || value.redacted;
    existing.incomplete = existing.incomplete || value.incomplete;
    existing.facts_redacted = existing.facts_redacted || value.facts_redacted;
    existing.updated = value.updated;
    existing.updated_ts = value.updated_ts;
    existing.cwd = value.cwd;
    existing.git_root = value.git_root;
    existing.terminal = value.terminal;
    existing.source_run = value.source_run;
    return existing;
}

// Sanitize every fact value before it reaches the disk, recording whether any
// value changed so `--facts` can say so.
facts sanitize_facts(facts const& source, bool& facts_redacted) {
    facts clean;
    for (auto const& entry : source.entries()) {
        auto result = sanitize::sanitize(entry.value);
        if (result.redacted) facts_redacted = true;
        clean.set(entry.key, result.text);
    }
    return clean;
}

bool write_atomic(std::filesystem::path const& path, std::string_view bytes, std::string& error) {
    std::error_code ec;
    auto temp = path;
    static std::atomic<unsigned int> counter{0};
    temp += ".tmp." + std::to_string(static_cast<long long>(::getpid())) + "." + std::to_string(counter.fetch_add(1));
    for (unsigned int attempt = 0; std::filesystem::exists(temp, ec) && attempt < 100; ++attempt) {
        temp = path;
        temp +=
            ".tmp." + std::to_string(static_cast<long long>(::getpid())) + "." + std::to_string(counter.fetch_add(1));
    }

    int const fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) {
        error = std::format("cannot write {}: {}", temp.string(), std::strerror(errno));
        return false;
    }
    if (!write_all(fd, bytes)) {
        int const write_error = errno;
        ::close(fd);
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        error = std::format("cannot write {}: {}", temp.string(), std::strerror(write_error));
        return false;
    }
    ::close(fd);

    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        error = std::format("cannot write {}: {}", path.string(), ec.message());
        return false;
    }
    return true;
}

} // namespace

std::string now_iso() {
    auto const now = std::chrono::system_clock::now();
    std::time_t const value = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_r(&value, &local);
    std::array<char, 32> buffer{};
    std::strftime(buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%S", &local);
    return {buffer.data()};
}

long long now_ts() {
    return static_cast<long long>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
}

std::filesystem::path root() {
    auto raw = env_value("PROMPT_STASH_DIR");
    if (!raw.empty()) return raw;
    std::string base;
    if (auto const xdg = env_value("XDG_STATE_HOME"); !xdg.empty()) {
        base = xdg;
    } else if (auto const home = env_value("HOME"); !home.empty()) {
        base = home + "/.local/state";
    } else {
        return {};
    }
    return std::filesystem::path(base) / "prompt" / "stashes";
}

std::filesystem::path directory(bool here) {
    auto base = root();
    if (base.empty()) return {};
    if (!here) return base;
    return base / "terminals" / terminal::scope(terminal::current());
}

std::filesystem::path path_for(std::string_view name, bool here) {
    auto const dir = directory(here);
    if (dir.empty()) return {};
    return dir / (std::string(name) + ".json");
}

bool valid_name(std::string_view name) {
    if (name.empty() || name.size() > 200) return false;
    if (name == "." || name == "..") return false;
    if (name.front() == '-') return false;
    return std::ranges::all_of(name, [](char const ch) {
        auto const c = static_cast<unsigned char>(ch);
        return c >= 0x20U && c != 0x7fU && ch != '/' && ch != '\\';
    });
}

std::filesystem::path resolve_path(std::string_view name, std::optional<bool> here) {
    if (here) return path_for(name, *here);
    auto local = path_for(name, true);
    std::error_code ec;
    if (!local.empty() && std::filesystem::exists(local, ec)) return local;
    return path_for(name, false);
}

std::optional<record> load(std::string_view name, std::optional<bool> here, std::string& error) {
    if (!valid_name(name)) {
        error = std::format("invalid stash name: {}", name);
        return std::nullopt;
    }
    auto const path = resolve_path(name, here);
    std::error_code ec;
    if (path.empty() || !std::filesystem::exists(path, ec)) {
        error = std::format("no stash named '{}'", name);
        return std::nullopt;
    }
    auto value = load_file(path, error);
    if (!value) return std::nullopt;
    value->here = (path == path_for(name, true));
    return value;
}

bool save(record const& value, bool here, bool append, bool no_clobber, std::string& error) {
    if (!valid_name(value.name)) {
        error = std::format("invalid stash name: {}", value.name);
        return false;
    }
    auto const dir = directory(here);
    if (dir.empty()) {
        error = "cannot determine the stash directory";
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        error = std::format("cannot create {}: {}", dir.string(), ec.message());
        return false;
    }
    std::filesystem::permissions(dir, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, ec);

    auto const path = dir / (value.name + ".json");
    file_lock lock(path.string() + ".lock", error);
    if (!lock.ok()) return false;

    record merged = value;
    if (append) {
        std::error_code exists_ec;
        if (std::filesystem::exists(path, exists_ec)) {
            std::string load_error;
            auto existing = load_file(path, load_error);
            if (!existing) {
                error = load_error;
                return false;
            }
            merged = append_record(std::move(*existing), value);
        }
    } else if (no_clobber) {
        std::error_code exists_ec;
        if (std::filesystem::exists(path, exists_ec)) {
            error = std::format("{} already exists (pass --replace to overwrite)", path.string());
            return false;
        }
    }

    auto clean_text = sanitize::sanitize(merged.text);
    merged.text = std::move(clean_text.text);
    merged.redacted = merged.redacted || clean_text.redacted;

    bool facts_redacted = merged.facts_redacted;
    merged.known = sanitize_facts(merged.known, facts_redacted);
    merged.facts_redacted = facts_redacted;

    std::string bytes = encode(merged);
    bytes += '\n';
    return write_atomic(path, bytes, error);
}

std::vector<record> list(std::optional<bool> here, std::vector<std::string>& errors) {
    std::vector<record> out;
    auto scan = [&](bool scope) {
        auto const dir = directory(scope);
        if (dir.empty()) return;
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) return;
        for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (ec) break;
            if (!entry.is_regular_file(ec)) continue;
            auto const& path = entry.path();
            if (path.extension() != ".json") continue;
            std::string error;
            auto value = load_file(path, error);
            if (!value) {
                errors.push_back(std::move(error));
                continue;
            }
            value->here = scope;
            out.push_back(std::move(*value));
        }
    };
    if (!here || !*here) scan(false);
    if (!here || *here) scan(true);

    std::sort(out.begin(), out.end(), [](record const& a, record const& b) {
        if (a.updated_ts != b.updated_ts) return a.updated_ts > b.updated_ts;
        if (a.name != b.name) return a.name < b.name;
        return static_cast<int>(a.here) < static_cast<int>(b.here);
    });
    return out;
}

bool remove(std::string_view name, std::optional<bool> here, std::string& error) {
    if (!valid_name(name)) {
        error = std::format("invalid stash name: {}", name);
        return false;
    }
    auto const path = resolve_path(name, here);
    std::error_code ec;
    if (path.empty() || !std::filesystem::exists(path, ec)) {
        error = std::format("no stash named '{}'", name);
        return false;
    }
    if (!std::filesystem::remove(path, ec) || ec) {
        error = std::format("cannot remove {}: {}", path.string(), ec.message());
        return false;
    }
    return true;
}

bool ensure_directory(bool here, std::string& error) {
    auto const dir = directory(here);
    if (dir.empty()) {
        error = "cannot determine the stash directory";
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        error = std::format("cannot create {}: {}", dir.string(), ec.message());
        return false;
    }
    std::filesystem::permissions(dir, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, ec);

    static std::atomic<unsigned int> counter{0};
    auto probe = dir / (".prompt-stash-check-" + std::to_string(static_cast<long long>(::getpid())) + "." +
                        std::to_string(counter.fetch_add(1)));
    int const fd = ::open(probe.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) {
        error = std::format("cannot write in {}: {}", dir.string(), std::strerror(errno));
        return false;
    }
    ::close(fd);
    std::error_code ignored;
    std::filesystem::remove(probe, ignored);
    return true;
}

std::string encode(record const& value) {
    return to_json(value, false).dump(2, ' ', false, json::error_handler_t::replace);
}

std::string display_json(record const& value) {
    return to_json(value, true).dump(2, ' ', false, json::error_handler_t::replace);
}

} // namespace prompt::stash
