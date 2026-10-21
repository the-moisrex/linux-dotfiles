#include "../sdk/errors.hpp"
#include "../sdk/prompt_context.hpp"
#include "../sdk/prompt_registry.hpp"
#include "../sdk/sink.hpp"
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>

namespace prompt::prompts {

static void render_help_save(std::ostream& os);

namespace {

enum class target_kind : std::uint8_t { auto_detect, directory, file };

struct save_config {
    std::optional<std::filesystem::path> target;
    bool force = false;
    bool allow_empty = false;
    target_kind kind = target_kind::auto_detect;
};

struct target_plan {
    std::filesystem::path directory;
    std::optional<std::filesystem::path> file;
};

std::string now_stamp() {
    auto const now = std::chrono::system_clock::now();
    std::time_t const value = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_r(&value, &local);
    std::ostringstream out;
    out << std::put_time(&local, "%Y%m%d-%H%M%S");
    return out.str();
}

std::string safe_stem(std::string_view raw) {
    std::string out;
    for (char const ch : raw) {
        auto const c = static_cast<unsigned char>(ch);
        bool const keep = c >= 0x80U || std::isalnum(c) != 0 || c == '-' || c == '_' || c == '.';
        if (keep) {
            out.push_back(static_cast<char>(c));
        } else if (!out.empty() && out.back() != '-') {
            out.push_back('-');
        }
    }
    while (!out.empty() && (out.back() == '-' || out.back() == '.')) out.pop_back();
    while (!out.empty() && (out.front() == '-' || out.front() == '.')) out.erase(out.begin());
    if (out.empty() || out == "." || out == "..") return "prompt-" + now_stamp();
    return out;
}

std::filesystem::path derived_name(prompt_context const& ctx) {
    std::filesystem::path const hinted{std::string(ctx.chain_save_hint)};
    std::filesystem::path name{safe_stem(hinted.filename().string())};
    if (name.extension().empty()) name += ".md";
    return name;
}

// A target is a directory when it exists as one, ends in '/', or has no file
// extension. An existing non-directory (including a regular file) or a name
// with an extension is an exact file path. --dir/--file override this.
bool looks_like_directory(std::filesystem::path const& target) {
    std::error_code ec;
    if (std::filesystem::is_directory(target, ec)) return true;
    if (target.string().ends_with('/')) return true;
    if (std::filesystem::exists(target, ec)) return false;
    auto const name = target.filename().string();
    if (!name.empty() && name.front() == '.') return false;
    return target.extension().empty();
}

target_plan plan_target(save_config const& cfg) {
    auto const cwd = std::filesystem::current_path();
    if (!cfg.target) return target_plan{.directory = cwd, .file = std::nullopt};

    auto const& target = *cfg.target;
    bool const directory =
        cfg.kind == target_kind::directory || (cfg.kind == target_kind::auto_detect && looks_like_directory(target));
    if (directory) return target_plan{.directory = target, .file = std::nullopt};

    auto parent = target.parent_path();
    if (parent.empty()) parent = cwd;
    return target_plan{.directory = parent, .file = target};
}

// Find the first existing path component that is not a directory, so an
// ENOTDIR from create_directories can be reported against the real offender
// (for example `/tmp/test` when `/tmp/test` is a regular file).
std::filesystem::path first_non_directory(std::filesystem::path path) {
    while (!path.empty()) {
        std::error_code ec;
        if (std::filesystem::exists(path, ec) && !std::filesystem::is_directory(path, ec)) return path;
        auto const parent = path.parent_path();
        if (parent.empty() || parent == path) break;
        path = parent;
    }
    return {};
}

void ensure_directory(std::filesystem::path const& directory) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        auto const offender = first_non_directory(directory);
        if (!offender.empty()) {
            throw soft_error{"prompt save: " + offender.string() + " exists but is not a directory\n"};
        }
        throw soft_error{"prompt save: cannot create " + directory.string() + ": " + ec.message() + "\n"};
    }
    if (!std::filesystem::is_directory(directory, ec)) {
        throw soft_error{"prompt save: " + directory.string() + " is not a directory\n"};
    }
}

// Creating and removing a probe catches read-only mounts and ACLs, which
// access(W_OK) would miss. The probe is never the saved file.
void check_writable_directory(std::filesystem::path const& directory) {
    static std::atomic<unsigned int> counter{0};

    std::filesystem::path probe;
    int fd = -1;
    for (int attempt = 0; attempt < 100; ++attempt) {
        probe = directory / (".prompt-save-check-" + std::to_string(static_cast<long long>(::getpid())) + "-" +
                             std::to_string(counter.fetch_add(1)));
        fd = ::open(probe.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (fd >= 0 || errno != EEXIST) break;
    }
    if (fd < 0) {
        throw soft_error{"prompt save: cannot write in " + directory.string() + ": " + std::strerror(errno) + "\n"};
    }

    char const byte = '\n';
    ssize_t const written = ::write(fd, &byte, 1);
    int const write_error = errno;
    ::close(fd);
    std::error_code ignored;
    std::filesystem::remove(probe, ignored);
    if (written != 1) {
        throw soft_error{"prompt save: cannot write in " + directory.string() + ": " + std::strerror(write_error) +
                         "\n"};
    }
}

save_config parse_args(prompt_context const& ctx) {
    save_config cfg;
    bool options = true;
    auto set_kind = [&](target_kind kind) {
        if (cfg.kind != target_kind::auto_detect && cfg.kind != kind) {
            throw usage_error{"prompt save: --dir and --file cannot be combined\n" + help_text(render_help_save)};
        }
        cfg.kind = kind;
    };

    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        std::string_view const arg = ctx.args[i];
        if (options && arg == "--") {
            options = false;
            continue;
        }
        if (options && arg == "--force") {
            cfg.force = true;
            continue;
        }
        if (options && arg == "--allow-empty") {
            cfg.allow_empty = true;
            continue;
        }
        if (options && (arg == "--dir" || arg == "--directory")) {
            set_kind(target_kind::directory);
            continue;
        }
        if (options && arg == "--file") {
            set_kind(target_kind::file);
            continue;
        }
        if (options && arg.starts_with('-')) {
            throw usage_error{"prompt save: unknown option: " + std::string(arg) + "\n" + help_text(render_help_save)};
        }
        if (cfg.target) {
            throw usage_error{"prompt save: provide at most one file or directory\n" + help_text(render_help_save)};
        }
        if (arg.empty()) {
            throw usage_error{"prompt save: the target path is empty\n"};
        }
        cfg.target = std::filesystem::path{std::string(arg)};
    }

    if (cfg.kind == target_kind::file && cfg.target && cfg.target->string().ends_with('/')) {
        throw usage_error{"prompt save: --file needs a file name, not a directory path\n" +
                          help_text(render_help_save)};
    }
    return cfg;
}

void preflight_save(prompt_context& ctx) {
    save_config const cfg = parse_args(ctx);
    target_plan const plan = plan_target(cfg);

    ensure_directory(plan.directory);
    if (plan.file) {
        std::error_code ec;
        if (std::filesystem::is_directory(*plan.file, ec)) {
            throw soft_error{"prompt save: " + plan.file->string() +
                             " is a directory (pass --dir to save inside it)\n"};
        }
        if (!cfg.force && std::filesystem::exists(*plan.file, ec)) {
            throw soft_error{"prompt save: " + plan.file->string() + " already exists (pass --force to overwrite)\n"};
        }
    }
    check_writable_directory(plan.directory);
}

} // namespace

static void execute_save(prompt_context& ctx) {
    save_config const cfg = parse_args(ctx);
    std::string const content(wait_for_prompts(ctx));
    if (content.empty() && !cfg.allow_empty) {
        throw soft_error{"prompt save: no context to save (pass --allow-empty to write an empty file)\n"};
    }

    target_plan const plan = plan_target(cfg);
    ensure_directory(plan.directory);

    std::filesystem::path const target = plan.file ? *plan.file : plan.directory / derived_name(ctx);
    std::error_code ec;
    if (std::filesystem::is_directory(target, ec)) {
        throw soft_error{"prompt save: " + target.string() + " is a directory\n"};
    }
    if (!cfg.force && std::filesystem::exists(target, ec)) {
        throw soft_error{"prompt save: " + target.string() + " already exists (pass --force to overwrite)\n"};
    }

    std::filesystem::path temp = target;
    temp += ".tmp." + std::to_string(static_cast<long long>(::getpid()));
    for (unsigned int attempt = 0; std::filesystem::exists(temp, ec) && attempt < 100; ++attempt) {
        temp = target;
        temp += ".tmp." + std::to_string(static_cast<long long>(::getpid())) + "." + std::to_string(attempt);
    }

    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    if (!file) {
        throw soft_error{"prompt save: cannot write " + temp.string() + "\n"};
    }
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!content.empty() && content.back() != '\n') file.put('\n');
    file.close();
    if (!file) {
        std::filesystem::remove(temp, ec);
        throw soft_error{"prompt save: failed while writing " + target.string() + "\n"};
    }

    if (!cfg.force && std::filesystem::exists(target, ec)) {
        std::filesystem::remove(temp, ec);
        throw soft_error{"prompt save: " + target.string() + " already exists (pass --force to overwrite)\n"};
    }
    std::filesystem::rename(temp, target, ec);
    if (ec) {
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        throw soft_error{"prompt save: cannot write " + target.string() + ": " + ec.message() + "\n"};
    }

    ctx.no_clipboard = true;
    ctx.err = "prompt save: wrote " + target.string() + "\n";
}

static void render_help_save(std::ostream& os) {
    os << R"EOF(Usage: prompt <other prompts...> .save [FILE|DIR] [--force] [--allow-empty] [--dir|--file]

Save the accumulated chain context to a file. The sink is passthrough: it writes
the file and leaves the chain context unchanged, so it can sit before another
sink such as `.ai.grok`.

Without a target, the sink uses the filename hint from the nearest successful
preceding prompt (for example `.tse khodro` suggests `tse-khodro`) and writes it
in the current directory with a `.md` extension.

A target that is an existing directory, ends in `/`, or has no file extension
is treated as a directory and created automatically. An existing non-directory,
or a path with an extension such as `report.md`, is the exact file name. Use
--dir or --file to override this. Missing parent directories are created.

The destination directory is created and checked for writability before any
prompt runs, so an invalid target fails fast.

Examples:
  prompt .tse khodro .save
  prompt .tse khodro .tse.codal .save /tmp
  prompt .tse khodro .save /tmp/test        # /tmp/test/tse-khodro.md
  prompt .note "review this" .files a.cpp .save report.md
  prompt .note "review this" .files a.cpp .save --file notes
  prompt .note "review this" .files a.cpp .save .ai.grok
)EOF";
    render_sink_help(os);
    os << "\nSave options:\n"
          "  --force         Overwrite an existing file (default: refuse)\n"
          "  --allow-empty   Write an empty file when the chain context is empty\n"
          "  --dir, --directory\n"
          "                  Treat the target as a directory and create it\n"
          "  --file          Treat the target as the exact file name\n"
          "  -h, --help      Show this help message\n"
          "\nNotes:\n"
          "  - The filename hint is only a basename; directories and unsafe characters\n"
          "    are sanitized.\n"
          "  - --with-context has no effect because this sink produces no answer.\n"
          "  - The saved text is the same sanitized context an AI sink would receive,\n"
          "    including --head trimming and the redaction legend when applicable.\n";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_sink(
    "save", "Save the accumulated chain context to a file, then pass the context through unchanged to later sinks.",
    &execute_save, &render_help_save, nullptr, prompt::sink_policy::chain, false, &preflight_save);

} // namespace prompt::prompts
