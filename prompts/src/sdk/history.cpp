#include "history.hpp"
#include "../core/sanitize.hpp"
#include "../core/strings.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <format>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace prompt::history {

namespace {

using nlohmann::json;

constexpr std::string_view kMarkerPrefix = "<!-- history:";
constexpr std::string_view kMarkerSuffix = " -->";

std::string now_iso(std::chrono::system_clock::time_point when) {
    auto const value = std::chrono::system_clock::to_time_t(when);
    std::tm local{};
    localtime_r(&value, &local);
    std::array<char, 32> buffer{};
    std::strftime(buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%S", &local);
    return {buffer.data()};
}

// Base36, two digits: sorts like the millisecond it encodes, and the letters
// keep the id from ever looking like a phone number to the redaction pass.
std::string base36_millis(std::chrono::system_clock::time_point when) {
    auto const millis = std::chrono::duration_cast<std::chrono::milliseconds>(when.time_since_epoch()).count() % 1000;
    static constexpr std::string_view kDigits = "0123456789abcdefghijklmnopqrstuvwxyz";
    auto const value = static_cast<unsigned>(millis);
    std::string out(2, '0');
    out[1] = kDigits[value % 36];
    out[0] = kDigits[(value / 36) % 36];
    return out;
}

// `YYYYMMDD-HHMMSS-t<mm>-<xxxx>`: sortable to the millisecond, never shaped
// like a phone number, and the random tail keeps concurrent runs apart.
std::string make_id(std::chrono::system_clock::time_point when) {
    auto const value = std::chrono::system_clock::to_time_t(when);
    std::tm local{};
    localtime_r(&value, &local);
    std::array<char, 32> stamp{};
    std::strftime(stamp.data(), stamp.size(), "%Y%m%d-%H%M%S", &local);
    auto const tail = static_cast<unsigned long>(::getpid()) ^ (static_cast<unsigned long>(value) * 2654435761UL);
    return std::format("{}-t{}-{:04x}", stamp.data(), base36_millis(when), (tail & 0xffffU));
}

std::optional<std::string> read_all(std::filesystem::path const& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    std::string data{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    return data;
}

void write_all(int fd, std::string_view bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        auto const n = ::write(fd, bytes.data() + offset, bytes.size() - offset);
        if (n < 0) {
            if (errno == EINTR) continue;
            return;
        }
        if (n == 0) return;
        offset += static_cast<std::size_t>(n);
    }
}

std::string join_emitted(std::span<stored_segment const> segments, bool& any_redacted) {
    std::string body;
    any_redacted = false;
    for (auto const& segment : segments) {
        if (!segment.emitted) continue;
        if (prompt::text::append_block(body, segment.text)) {
            any_redacted = any_redacted || segment.redacted;
        }
    }
    return body;
}

// The chain-state fold: `append` keeps the context and adds a blank-line
// separated block, `replace` discards it, and the rest leave it untouched.
void apply_merge(std::string& text, bool& redacted, bool& legend_done, stored_segment const& segment) {
    if (segment.merge == "append") {
        if (prompt::text::append_block(text, segment.text)) redacted = redacted || segment.redacted;
        return;
    }
    if (segment.merge == "replace") {
        std::string_view const block = prompt::text::strip_newlines(segment.text);
        text = std::string(block);
        if (!text.empty()) text += '\n';
        redacted = segment.redacted;
        legend_done = false;
        return;
    }
    if (segment.merge == "clear") {
        text.clear();
        redacted = false;
        legend_done = false;
    }
    // passthrough / skip: nothing happened to the context.
}

std::string marker_line(std::string_view kind, std::string_view payload) {
    return std::format("{}{} {}{}\n", kMarkerPrefix, kind, payload, kMarkerSuffix);
}

// Split one `<!-- history:<kind> {json} -->` line. False for anything that is
// not a well-formed marker (a foreign or truncated line).
bool split_marker(std::string_view line, std::string_view& kind, json& payload) {
    if (!line.starts_with(kMarkerPrefix)) return false;
    auto const kind_start = kMarkerPrefix.size();
    auto const space = line.find(' ', kind_start);
    if (space == std::string_view::npos) return false;
    kind = line.substr(kind_start, space - kind_start);
    auto const body_start = space + 1;
    if (line.size() < body_start + kMarkerSuffix.size()) return false;
    auto const body_end = line.size() - kMarkerSuffix.size();
    payload = json::parse(line.substr(body_start, body_end - body_start), nullptr, false);
    return !payload.is_discarded() && payload.is_object();
}

// The `terminal` object of a run marker (sdk/terminal.hpp), absent on runs
// recorded before the identity existed.
void parse_terminal(stored_run& run, json const& payload) {
    if (!payload.contains("terminal") || !payload["terminal"].is_object()) return;
    auto const& term = payload["terminal"];
    run.terminal.sid = term.value("sid", 0LL);
    run.terminal.tty = term.value("tty", std::string{});
    run.terminal.key = term.value("key", std::string{});
}

// The run marker alone — enough to filter by terminal without parsing (or even
// reading) the segment bodies, which is what makes scanning a store full of
// other terminals cheap.
std::optional<stored_run> read_marker(std::filesystem::path const& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    std::string line;
    if (!std::getline(in, line)) return std::nullopt;
    std::string_view kind;
    json payload;
    if (!split_marker(line, kind, payload) || kind != "run") return std::nullopt;
    stored_run run;
    run.path = file;
    run.id = file.stem().string();
    run.ts = payload.value("ts", 0LL);
    run.started = payload.value("started", std::string{});
    parse_terminal(run, payload);
    return run;
}

} // namespace

std::filesystem::path root() {
    if (char const* dir = std::getenv("PROMPT_HISTORY_DIR"); dir != nullptr && *dir != '\0') return dir;
    if (char const* state = std::getenv("XDG_STATE_HOME"); state != nullptr && *state != '\0')
        return std::filesystem::path(state) / "prompt";
    if (char const* home = std::getenv("HOME"); home != nullptr && *home != '\0')
        return std::filesystem::path(home) / ".local" / "state" / "prompt";
    return {};
}

std::filesystem::path runs_dir() {
    auto const base = root();
    if (base.empty()) return {};
    return base / "runs";
}

std::string display_path(std::filesystem::path const& file) {
    std::error_code ec;
    auto const cwd = std::filesystem::current_path(ec);
    if (!ec) {
        auto const relative = file.lexically_relative(cwd);
        if (!relative.empty() && relative != ".") return relative.string();
    }
    return file.string();
}

bool enabled() {
    auto const* raw = std::getenv("PROMPT_HISTORY");
    if (raw == nullptr) return true;
    std::string value(raw);
    std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return !(value.empty() || value == "0" || value == "off" || value == "false" || value == "no");
}

bool record_inner() {
    auto const* raw = std::getenv("PROMPT_HISTORY_INNER");
    if (raw == nullptr) return true;
    std::string value(raw);
    std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return !(value == "0" || value == "off" || value == "false" || value == "no");
}

std::vector<std::filesystem::path> run_files() {
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    auto const dir = runs_dir();
    if (dir.empty()) return files;
    for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        std::error_code entry_ec;
        if (!entry.is_regular_file(entry_ec)) continue;
        if (entry.path().extension() != ".md") continue;
        files.push_back(entry.path());
    }
    // Filename order is chronological order: the id starts with the timestamp.
    std::sort(files.begin(), files.end(),
              [](auto const& a, auto const& b) { return a.filename().string() > b.filename().string(); });
    return files;
}

std::vector<std::string> run_ids() {
    std::vector<std::string> ids;
    for (auto const& file : run_files()) ids.push_back(file.stem().string());
    return ids;
}

std::optional<stored_run> load(std::filesystem::path const& file) {
    auto data = read_all(file);
    if (!data) return std::nullopt;

    stored_run run;
    run.path = file;
    run.id = file.stem().string();
    run.bytes = data->size();

    std::size_t pos = 0;
    while (pos < data->size()) {
        auto const eol = data->find('\n', pos);
        if (eol == std::string::npos) break;
        std::string_view const line(data->data() + pos, eol - pos);
        pos = eol + 1;

        std::string_view kind;
        json payload;
        if (!split_marker(line, kind, payload)) break; // truncated or foreign

        if (kind == "run") {
            run.ts = payload.value("ts", 0LL);
            run.started = payload.value("started", std::string{});
            run.argv = payload.value("argv", std::vector<std::string>{});
            run.cwd = payload.value("cwd", std::string{});
            run.git_root = payload.value("git_root", std::string{});
            run.parent = payload.value("parent", std::string{});
            parse_terminal(run, payload);
            continue;
        }
        if (kind == "end") {
            run.finished = true;
            run.exit_code = payload.value("exit", 0U);
            run.redacted = payload.value("redacted", false);
            run.incomplete = payload.value("incomplete", false);
            run.output_bytes = payload.value("output_bytes", std::size_t{0});
            break;
        }
        if (kind != "segment") break;

        auto const input_bytes = payload.value("input_bytes", std::size_t{0});
        auto const text_bytes = payload.value("bytes", std::size_t{0});
        if (pos + input_bytes + text_bytes > data->size()) break;
        stored_segment segment;
        segment.index = payload.value("i", std::size_t{0});
        segment.name = payload.value("name", std::string{});
        segment.role = payload.value("role", std::string{});
        segment.args = payload.value("args", std::vector<std::string>{});
        segment.body = payload.value("body", std::vector<std::string>{});
        segment.merge = payload.value("merge", std::string{"append"});
        segment.status = payload.value("status", std::string{"ok"});
        segment.emitted = payload.value("emitted", false);
        segment.redacted = payload.value("redacted", false);
        segment.original_bytes = payload.value("original_bytes", text_bytes);
        segment.input = data->substr(pos, input_bytes);
        pos += input_bytes;
        segment.text = data->substr(pos, text_bytes);
        pos += text_bytes;
        if (pos < data->size() && (*data)[pos] == '\n') ++pos;
        run.segments.push_back(std::move(segment));
    }

    if (!run.finished) run.incomplete = true;
    if (!run.finished) {
        for (auto const& segment : run.segments) run.redacted = run.redacted || segment.redacted;
    }
    return run;
}

std::vector<stored_run> load_all() {
    std::vector<stored_run> runs;
    for (auto const& file : run_files()) {
        if (auto run = load(file)) runs.push_back(std::move(*run));
    }
    return runs;
}

std::optional<stored_run> find_by_id(std::string_view key) {
    for (auto const& file : run_files()) {
        auto const id = file.stem().string();
        if (key.empty() || id.starts_with(key)) {
            if (auto run = load(file)) return run;
        }
    }
    return std::nullopt;
}

std::optional<stored_run> find_by_rank(std::size_t rank) {
    if (rank == 0) return std::nullopt;
    auto const files = run_files();
    if (rank > files.size()) return std::nullopt;
    return load(files[rank - 1]);
}

bool in_terminal(stored_run const& run, terminal::id const& who) { return terminal::same(run.terminal, who); }

std::optional<stored_run> find_last_in(terminal::id const& who) {
    if (who.empty()) return std::nullopt;
    for (auto const& file : run_files()) {
        auto const marker = read_marker(file);
        if (!marker || !in_terminal(*marker, who)) continue;
        return load(file);
    }
    return std::nullopt;
}

std::optional<stored_run> find_rank_in(terminal::id const& who, std::size_t rank) {
    if (who.empty() || rank == 0) return std::nullopt;
    std::size_t seen = 0;
    for (auto const& file : run_files()) {
        auto const marker = read_marker(file);
        if (!marker || !in_terminal(*marker, who)) continue;
        if (++seen < rank) continue;
        return load(file);
    }
    return std::nullopt;
}

std::string final_output(stored_run const& run) {
    bool redacted = false;
    std::string body = join_emitted(run.segments, redacted);
    if (redacted && !body.empty()) return std::string(sanitize::kRedactionLegend) + "\n" + body;
    return body;
}

std::string barrier_input(stored_run const& run, std::size_t index) {
    // The exact input is stored on the segment that received it (stdin,
    // --head, an elided context): when it is there, it already carries
    // whatever legend applied, so return it untouched.
    for (auto const& segment : run.segments) {
        if (segment.index == index && !segment.input.empty()) return segment.input;
    }

    std::string text;
    bool redacted = false;
    bool legend_done = false;
    for (auto const& segment : run.segments) {
        if (segment.index >= index) break;
        apply_merge(text, redacted, legend_done, segment);
    }
    if (redacted && !legend_done && !text.empty()) return std::string(sanitize::kRedactionLegend) + "\n\n" + text;
    return text;
}

std::optional<std::chrono::seconds> parse_duration(std::string_view text) {
    if (text.empty()) return std::nullopt;
    std::size_t i = 0;
    unsigned long long value = 0;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])) != 0) {
        value = (value * 10) + static_cast<unsigned long long>(text[i] - '0');
        ++i;
    }
    if (i == 0) return std::nullopt;
    std::string unit(text.substr(i));
    std::ranges::transform(unit, unit.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    unsigned long long multiplier = 86400; // a bare number is days
    if (unit.empty() || unit == "d") {
        multiplier = 86400;
    } else if (unit == "s") {
        multiplier = 1;
    } else if (unit == "m") {
        multiplier = 60;
    } else if (unit == "h") {
        multiplier = 3600;
    } else if (unit == "w") {
        multiplier = 7ULL * 86400;
    } else {
        return std::nullopt;
    }
    return std::chrono::seconds{static_cast<long long>(value * multiplier)};
}

std::optional<std::uintmax_t> parse_size(std::string_view text) {
    if (text.empty()) return std::nullopt;
    std::size_t i = 0;
    std::uintmax_t value = 0;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])) != 0) {
        value = (value * 10) + static_cast<std::uintmax_t>(text[i] - '0');
        ++i;
    }
    if (i == 0) return std::nullopt;
    std::string unit(text.substr(i));
    std::ranges::transform(unit, unit.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (!unit.empty() && unit.ends_with('b')) unit.pop_back();
    if (!unit.empty() && unit.ends_with('i')) unit.pop_back();
    std::uintmax_t multiplier = 1;
    if (unit.empty()) {
        multiplier = 1;
    } else if (unit == "k") {
        multiplier = 1024;
    } else if (unit == "m") {
        multiplier = 1024ULL * 1024;
    } else if (unit == "g") {
        multiplier = 1024ULL * 1024 * 1024;
    } else if (unit == "t") {
        multiplier = 1024ULL * 1024 * 1024 * 1024;
    } else {
        return std::nullopt;
    }
    return value * multiplier;
}

std::size_t keep_runs() {
    if (char const* raw = std::getenv("PROMPT_HISTORY_KEEP"); raw != nullptr && *raw != '\0') {
        try {
            return static_cast<std::size_t>(std::stoull(raw));
        } catch (...) { // NOLINT(bugprone-empty-catch) — a bad override keeps the default
        }
    }
    return 1000;
}

std::uintmax_t keep_bytes() {
    if (char const* raw = std::getenv("PROMPT_HISTORY_SIZE"); raw != nullptr && *raw != '\0') {
        if (auto size = parse_size(raw)) return *size;
    }
    return 256ULL * 1024 * 1024;
}

bool auto_prune() {
    char const* raw = std::getenv("PROMPT_HISTORY_PRUNE");
    if (raw == nullptr) return true;
    std::string value(raw);
    std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return !(value == "0" || value == "off" || value == "false" || value == "no");
}

void prune(std::filesystem::path const& keep) {
    if (!auto_prune()) return;
    auto const files = run_files();
    if (files.empty()) return;

    std::size_t count = files.size();
    std::uintmax_t total = 0;
    for (auto const& file : files) {
        std::error_code ec;
        auto const size = std::filesystem::file_size(file, ec);
        if (!ec) total += size;
    }

    auto const max_runs = keep_runs();
    auto const max_bytes = keep_bytes();
    auto over = [&] { return (max_runs != 0 && count > max_runs) || (max_bytes != 0 && total > max_bytes); };

    for (auto it = files.rbegin(); it != files.rend() && over(); ++it) {
        if (!keep.empty() && *it == keep) continue;
        std::error_code ec;
        auto const size = std::filesystem::file_size(*it, ec);
        if (std::filesystem::remove(*it, ec) && !ec) {
            if (count != 0) --count;
            if (!ec && size <= total) total -= size;
        }
    }
}

recorder::recorder(info const& run_info) : info_(run_info) {
    // Qualified: the unqualified `enabled()` inside a member would resolve to
    // the member function of the same name.
    active_ = history::enabled();
    if (active_ && run_info.inner && !history::record_inner()) active_ = false;
    if (active_ && history::runs_dir().empty()) active_ = false;
    if (!active_) return;
    started_ = std::chrono::system_clock::now();
    id_ = make_id(started_);
}

recorder::~recorder() {
    if (fd_ >= 0) ::close(fd_);
}

void recorder::write(std::string_view bytes) const {
    if (fd_ >= 0) write_all(fd_, bytes);
}

void recorder::frame(std::string_view kind, std::string_view payload, std::string_view first, std::string_view second) {
    write(marker_line(kind, payload));
    if (first.empty() && second.empty()) return;
    write(first);
    write(second);
    write("\n");
}

void recorder::open() {
    if (!active_ || fd_ >= 0) return;
    std::error_code ec;
    std::filesystem::create_directories(runs_dir(), ec);
    std::filesystem::permissions(runs_dir(), std::filesystem::perms::owner_all, std::filesystem::perm_options::replace,
                                 ec);
    path_ = runs_dir() / (id_ + ".md");
    fd_ = ::open(path_.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (fd_ < 0) {
        active_ = false;
        return;
    }
    json terminal_meta{
        {"sid", info_.terminal.sid},
        {"tty", info_.terminal.tty},
    };
    if (!info_.terminal.key.empty()) terminal_meta["key"] = info_.terminal.key;
    json meta{
        {"format", 1},
        {"id", id_},
        {"ts", static_cast<long long>(std::chrono::system_clock::to_time_t(started_))},
        {"started", now_iso(started_)},
        {"argv", info_.argv},
        {"cwd", info_.cwd.string()},
        {"git_root", info_.git_root.string()},
        {"tty", info_.tty},
        {"pid", static_cast<long long>(::getpid())},
        {"terminal", std::move(terminal_meta)},
    };
    if (!info_.parent.empty()) meta["parent"] = info_.parent;
    frame("run", meta.dump(), {}, {});
}

std::string recorder::replay(std::size_t count, bool& redacted_out) const {
    std::string text;
    bool redacted = false;
    bool legend_done = false;
    for (std::size_t i = 0; i < count && i < recorded_.size(); ++i) {
        apply_merge(text, redacted, legend_done, recorded_[i]);
    }
    redacted_out = redacted;
    return text;
}

void recorder::segment(stored_segment record, std::string_view barrier_input) {
    if (!active_) return;
    open();
    if (fd_ < 0) return;

    auto clean_args = [](std::vector<std::string> const& args) {
        std::vector<std::string> out;
        out.reserve(args.size());
        for (auto const& arg : args) out.push_back(sanitize::sanitize(arg).text);
        return out;
    };
    record.args = clean_args(record.args);
    record.body = clean_args(record.body);

    std::string body = std::move(record.text);
    record.original_bytes = body.size();
    std::size_t const cap = [] {
        if (char const* raw = std::getenv("PROMPT_HISTORY_MAX_BYTES"); raw != nullptr && *raw != '\0') {
            if (auto size = parse_size(raw)) return *size;
        }
        return std::size_t{4} * 1024 * 1024;
    }();
    if (body.size() > cap) {
        std::size_t const head = cap * 2 / 3;
        std::size_t const tail = cap - head;
        std::string elided = body.substr(0, head);
        elided += std::format("\n…[prompt history: {} bytes elided]…\n", body.size() - cap);
        elided += body.substr(body.size() - tail);
        body = std::move(elided);
    }

    std::string input;
    if (record.role == "sink" && !barrier_input.empty()) {
        bool redacted = false;
        std::string expected = replay(recorded_.size(), redacted);
        if (redacted && !expected.empty()) expected = std::string(sanitize::kRedactionLegend) + "\n\n" + expected;
        if (barrier_input != expected) input = std::string(barrier_input);
    }

    json marker{
        {"i", record.index},
        {"name", record.name},
        {"role", record.role},
        {"args", record.args},
        {"merge", record.merge},
        {"status", record.status},
        {"bytes", body.size()},
        {"emitted", record.emitted},
        {"redacted", record.redacted},
        {"input_bytes", input.size()},
        {"input", input.empty() ? "concat" : "stored"},
    };
    if (!record.body.empty()) marker["body"] = record.body;
    if (record.original_bytes > body.size()) marker["original_bytes"] = record.original_bytes;

    frame("segment", marker.dump(), input, body);
    record.text = std::move(body);
    recorded_.push_back(std::move(record));
}

void recorder::finish(int exit_code, bool incomplete) {
    if (fd_ < 0 || finished_) return;
    finished_ = true;

    auto const now = std::chrono::system_clock::now();
    auto const elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - started_).count();
    bool redacted = false;
    std::string const body = join_emitted(recorded_, redacted);
    json end{
        {"finished", now_iso(now)},
        {"ts", static_cast<long long>(std::chrono::system_clock::to_time_t(now))},
        {"ms", elapsed},
        {"exit", exit_code},
        {"incomplete", incomplete},
        {"redacted", redacted},
        {"output_bytes", body.size() + (redacted && !body.empty() ? sanitize::kRedactionLegend.size() + 1 : 0)},
        {"segments", recorded_.size()},
    };
    frame("end", end.dump(), {}, {});
    ::close(fd_);
    fd_ = -1;
    prune(path_);
}

std::string format_time(long long epoch_seconds) {
    auto const value = static_cast<std::time_t>(epoch_seconds);
    std::tm local{};
    localtime_r(&value, &local);
    std::array<char, 32> buffer{};
    std::strftime(buffer.data(), buffer.size(), "%Y-%m-%d %H:%M:%S", &local);
    return {buffer.data()};
}

std::string human_bytes(std::uintmax_t bytes) {
    constexpr std::array<std::string_view, 5> units{"B", "K", "M", "G", "T"};
    auto value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < units.size()) {
        value /= 1024.0;
        ++unit;
    }
    if (unit == 0) return std::format("{}B", bytes);
    return std::format("{:.1f}{}", value, units[unit]);
}

} // namespace prompt::history
