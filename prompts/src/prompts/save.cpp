#include "../sdk/errors.hpp"
#include "../sdk/prompt_context.hpp"
#include "../sdk/prompt_registry.hpp"
#include "../sdk/sink.hpp"
#include <cctype>
#include <chrono>
#include <ctime>
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

struct save_config {
    std::optional<std::filesystem::path> target;
    bool force = false;
    bool allow_empty = false;
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

std::filesystem::path resolve_target(save_config const& cfg, prompt_context const& ctx) {
    if (!cfg.target) return std::filesystem::current_path() / derived_name(ctx);

    std::filesystem::path target = *cfg.target;
    std::error_code ec;
    bool const directory = std::filesystem::is_directory(target, ec) || target.string().ends_with('/');
    if (directory) target /= derived_name(ctx);
    return target;
}

save_config parse_args(prompt_context const& ctx) {
    save_config cfg;
    bool options = true;
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
    return cfg;
}

} // namespace

static void execute_save(prompt_context& ctx) {
    save_config const cfg = parse_args(ctx);
    std::string const content(wait_for_prompts(ctx));
    if (content.empty() && !cfg.allow_empty) {
        throw soft_error{"prompt save: no context to save (pass --allow-empty to write an empty file)\n"};
    }

    std::filesystem::path const target = resolve_target(cfg, ctx);
    std::error_code ec;
    auto const parent = target.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            std::error_code check;
            if (!std::filesystem::is_directory(parent, check)) {
                throw soft_error{"prompt save: cannot create " + parent.string() + ": " + ec.message() + "\n"};
            }
        }
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
    os << R"EOF(Usage: prompt <other prompts...> .save [FILE|DIR] [--force] [--allow-empty]

Save the accumulated chain context to a file. The sink is passthrough: it writes
the file and leaves the chain context unchanged, so it can sit before another
sink such as `.ai.grok`.

Without FILE, the sink uses the filename hint from the nearest successful
preceding prompt (for example `.tse khodro` suggests `tse-khodro`) and writes it
in the current directory with a `.md` extension. An existing directory, or a
path ending in `/`, is treated as a directory; any other path is the exact file
name to write. Missing parent directories are created.

Examples:
  prompt .tse khodro .save
  prompt .tse khodro .tse.codal .save /tmp
  prompt .note "review this" .files a.cpp .save review.md
  prompt .note "review this" .files a.cpp .save .ai.grok
)EOF";
    render_sink_help(os);
    os << "\nSave options:\n"
          "  --force         Overwrite an existing file (default: refuse)\n"
          "  --allow-empty   Write an empty file when the chain context is empty\n"
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
    &execute_save, &render_help_save, nullptr, prompt::sink_policy::chain, false);

} // namespace prompt::prompts
