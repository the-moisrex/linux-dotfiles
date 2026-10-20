#pragma once

// Shared TSETMC data collection for the `tse` prompt (the analysis prompt,
// `tse.analyze`, is a data-free lens chained after it). `tse` parses these
// arguments, resolves the symbol (or `.stock` snapshots) and renders the
// dataset — so the logic lives here once. This is not a prompt: it registers
// nothing.

#include "../core/fs.hpp"
#include "../core/process.hpp"
#include "../sdk/args.hpp"
#include "../sdk/completion.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <format>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace prompt::prompts::tse_data {

// The four lines `tse snapshot` prints (fetched, stale, note, instrument).
struct snapshot_info {
    std::string fetched = "unknown";
    std::string stale;
    std::string note;
    std::string instrument;
};

struct config {
    std::string name; // the invoking prompt name, for "prompt <name>:" errors
    std::optional<std::size_t> head_lines;
    std::vector<std::string> inputs;
    std::string symbol;
    int days = 0;
    bool days_set = false;
    bool unadjusted = false;
    std::vector<std::string> skip;
    bool full = false;
    bool fetch_flags = false;
    bool cache_mode = false;
    std::vector<std::filesystem::path> snapshot_files;
    std::vector<snapshot_info> snapshot_infos;
    bool failed = false;
    int exit_code = 2;
    std::string error;
};

inline bool is_positive_int(std::string_view s) {
    if (s.empty()) return false;
    return std::ranges::all_of(s, [](char c) { return c >= '0' && c <= '9'; });
}

inline std::string rstrip_newlines(std::string_view text) {
    while (!text.empty() && text.back() == '\n') text.remove_suffix(1);
    return std::string(text);
}

inline std::vector<std::string> split_lines(std::string_view text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            lines.emplace_back(text.substr(start));
            break;
        }
        lines.emplace_back(text.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

inline snapshot_info parse_snapshot_info(std::string_view output) {
    auto lines = split_lines(rstrip_newlines(output));
    auto at = [&lines](std::size_t i) -> std::string { return i < lines.size() ? lines[i] : std::string{}; };
    snapshot_info info;
    if (!at(0).empty()) info.fetched = at(0);
    info.stale = at(1);
    info.note = at(2);
    info.instrument = at(3);
    return info;
}

// Parses the arguments the `tse` prompt accepts. `help_fn` renders the
// invoking prompt's usage, appended to the "no identifier" error.
template <typename RenderFn>
config parse_args(prompt_context const& ctx, std::string_view name, RenderFn help_fn) {
    config cfg;
    cfg.name = std::string(name);
    std::string const pfx = "prompt " + std::string(name) + ": ";

    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, cfg.head_lines); !msg.empty()) {
        cfg.failed = true;
        cfg.exit_code = 2;
        cfg.error = std::move(msg);
        return cfg;
    }

    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        std::string_view arg = ctx.args[i];
        if (arg == "--head") {
            ++i; // value parsed by parse_head_option above
            continue;
        }
        if (arg == "--days") {
            cfg.fetch_flags = true;
            if (i + 1 >= ctx.args_count || !is_positive_int(ctx.args[i + 1])) {
                cfg.failed = true;
                cfg.error = std::format("{}--days requires a positive integer\n", pfx);
                return cfg;
            }
            cfg.days = std::stoi(std::string(ctx.args[i + 1]));
            cfg.days_set = true;
            ++i;
            continue;
        }
        if (arg == "--full") {
            cfg.fetch_flags = true;
            cfg.full = true;
            continue;
        }
        if (arg == "--without") {
            cfg.fetch_flags = true;
            if (i + 1 >= ctx.args_count) {
                cfg.failed = true;
                cfg.error = std::format("{}--without requires a section name\n", pfx);
                return cfg;
            }
            std::string_view names = ctx.args[++i];
            std::size_t start = 0;
            while (start <= names.size()) {
                auto end = names.find(',', start);
                std::string_view section =
                    names.substr(start, end == std::string_view::npos ? names.size() - start : end - start);
                while (!section.empty() && (section.front() == ' ' || section.front() == '\t'))
                    section.remove_prefix(1);
                while (!section.empty() && (section.back() == ' ' || section.back() == '\t')) section.remove_suffix(1);
                if (!section.empty()) {
                    if (section != "book" && section != "statistics" && section != "breadth") {
                        cfg.failed = true;
                        cfg.error = std::format(
                            "{}unknown --without section: {} (choose from book, statistics, breadth)\n", pfx, section);
                        return cfg;
                    }
                    if (std::ranges::find(cfg.skip, section) == cfg.skip.end()) cfg.skip.emplace_back(section);
                }
                if (end == std::string_view::npos) break;
                start = end + 1;
            }
            continue;
        }
        if (arg == "--unadjusted") {
            cfg.fetch_flags = true;
            cfg.unadjusted = true;
            continue;
        }
        if (arg == "--") continue;
        if (arg.starts_with('-')) {
            cfg.failed = true;
            cfg.error = std::format("{}unknown option: {}\n", pfx, arg);
            return cfg;
        }
        cfg.inputs.emplace_back(arg);
    }

    if (cfg.inputs.empty()) {
        if (auto id = clipboard_identifier(ctx)) cfg.inputs.push_back(*id);
        if (cfg.inputs.empty()) {
            cfg.failed = true;
            cfg.error = pfx + "no symbol given and no identifier found in the clipboard\n";
            cfg.error += help_text(help_fn);
            return cfg;
        }
    }

    std::vector<std::string> file_inputs;
    std::vector<std::string> ident_inputs;
    for (auto const& input : cfg.inputs) {
        std::filesystem::path p(input);
        if (std::filesystem::is_regular_file(p) || p.extension() == ".stock") {
            file_inputs.push_back(input);
        } else {
            ident_inputs.push_back(input);
        }
    }

    if (!file_inputs.empty() && !ident_inputs.empty()) {
        cfg.failed = true;
        cfg.error = pfx + "cannot mix .stock snapshot files with a live identifier\n";
        return cfg;
    }
    if (cfg.full && cfg.days_set) {
        cfg.failed = true;
        cfg.error = pfx + "--full and --days cannot be used together\n";
        return cfg;
    }
    if (!file_inputs.empty() && cfg.fetch_flags) {
        cfg.failed = true;
        cfg.error = pfx + "--days/--full/--without/--unadjusted need a live fetch; snapshot files are used as-is\n";
        return cfg;
    }
    if (ident_inputs.size() > 1) {
        cfg.failed = true;
        cfg.error = pfx + "provide exactly one symbol/insCode (or several .stock files)\n";
        return cfg;
    }
    cfg.symbol = ident_inputs.empty() ? std::string{} : ident_inputs[0];

    if (!file_inputs.empty()) {
        cfg.cache_mode = true;
        auto git_root = prompt::fs::find_git_root();
        std::string tse_tool = prompt::fs::bin_tool("tse", ctx.exe_path, std::nullopt).string();
        for (auto const& input : file_inputs) {
            std::filesystem::path resolved;
            if (std::filesystem::is_regular_file(input)) {
                resolved = input;
            } else if (git_root) {
                auto candidate = *git_root / input;
                if (std::filesystem::is_regular_file(candidate)) resolved = candidate;
            }
            if (resolved.empty()) {
                cfg.failed = true;
                cfg.error = std::format("{}snapshot file not found: {}\n", pfx, input);
                return cfg;
            }
            if (::access(resolved.c_str(), R_OK) != 0) {
                cfg.failed = true;
                cfg.error = std::format("{}snapshot file not readable: {}\n", pfx, resolved.string());
                return cfg;
            }
            auto probe =
                prompt::process::command{"python3", tse_tool, "snapshot", resolved.string()}.stderr_to_stdout().run();
            if (probe.exit_code != 0) {
                cfg.failed = true;
                cfg.exit_code = 2;
                cfg.error = std::format("{}{}\n", pfx, rstrip_newlines(probe.stdout_data));
                return cfg;
            }
            cfg.snapshot_files.push_back(resolved);
            cfg.snapshot_infos.push_back(parse_snapshot_info(probe.stdout_data));
        }
    }

    return cfg;
}

// Renders one cached snapshot: the `tse snapshot` header, then the Markdown
// `tse markdown` produces (with the scripts' raw-JSON fallback on failure).
inline std::string render_snapshot(std::filesystem::path const& path, snapshot_info const& info,
                                   std::optional<std::size_t> head_lines, std::string const& tse_tool,
                                   std::string const& name, std::string& errors) {
    std::error_code ec;
    auto base = prompt::fs::find_git_root().value_or(std::filesystem::current_path(ec));
    auto rel = std::filesystem::relative(std::filesystem::weakly_canonical(path, ec), base, ec);
    if (ec || rel.empty()) rel = path;
    std::string rel_path = rel.string();

    std::string output;
    output += std::format("\n## Cached snapshot: {} (fetched {})\n", rel_path, info.fetched);
    if (info.stale == "yes") {
        output += "Data is older than 24 hours — the latest session is probably missing; re-run "
                  "tse.snapshot to refresh.\n";
    }
    if (!info.note.empty()) output += "Snapshot note: " + info.note + "\n";
    if (!info.instrument.empty()) output += info.instrument + "\n";

    auto render = prompt::process::command{"python3", tse_tool, "markdown", path.string()}.stderr_to_stdout().run();
    if (render.exit_code == 0) {
        output += '\n';
        output += trim_context_nl(render.stdout_data, head_lines);
        output += '\n';
    } else {
        errors += std::format("prompt {}: {}: snapshot render failed ({}); embedding raw JSON instead\n", name,
                              rel_path, rstrip_newlines(render.stdout_data));
        output += std::format("\nFile: {}\n```json\n", rel_path);
        output += trim_context_nl(prompt::read_file(path), head_lines);
        output += "\n```\n";
    }
    return output;
}

// `python3 bin/tse all <symbol> ... --markdown`. The tool prints
// `tse: <message>` on stderr (inherited, exactly like bash's `$(...)`) and
// exits 1; the script runs under `set -e`, so the prompt aborts printing
// nothing.
inline std::string fetch_live(config const& cfg, std::string const& tse_tool) {
    std::vector<std::string> fetch_args = {"all", cfg.symbol, "--markdown"};
    if (cfg.days_set) {
        fetch_args.emplace_back("--days");
        fetch_args.push_back(std::to_string(cfg.days));
    } else {
        fetch_args.emplace_back("--full");
    }
    for (auto const& name : cfg.skip) {
        fetch_args.emplace_back("--without");
        fetch_args.push_back(name);
    }
    if (cfg.unadjusted) fetch_args.emplace_back("--unadjusted");
    std::vector<std::string> tool_argv = {"python3", tse_tool};
    for (auto const& a : fetch_args) tool_argv.push_back(a);
    auto fetch = prompt::process::run_command(tool_argv);
    if (fetch.exit_code != 0) {
        throw soft_error{std::string{}};
    }
    return fetch.stdout_data;
}

// The collected data block: the live Markdown (capped by --head) or the
// rendered snapshot files. Fetch failures throw, exactly as before.
inline std::string collect(prompt_context& ctx, config const& cfg, std::string& errors) {
    std::string tse_tool = prompt::fs::bin_tool("tse", ctx.exe_path, std::nullopt).string();
    if (cfg.cache_mode) {
        std::string out;
        for (std::size_t i = 0; i < cfg.snapshot_files.size(); ++i) {
            out += render_snapshot(cfg.snapshot_files[i], cfg.snapshot_infos[i], cfg.head_lines, tse_tool, cfg.name,
                                   errors);
        }
        return out;
    }
    return trim_context(fetch_live(cfg, tse_tool), cfg.head_lines);
}

// Shell completion for the identifier argument. The lookup is delegated to the
// shared resolver (`bin/tse.resolve --complete`), so `prompt tse|tse.intraday`
// suggest exactly what `tse quote` does, from the same cached index and without
// touching the network. Flags are added by the dispatcher from the prompt's
// help text, so only positional candidates are emitted here.
inline void complete_instrument(completion_request const& req, std::ostream& os) {
    if (req.prefix.starts_with('-')) return;
    auto exe = prompt::fs::self_path().value_or(std::filesystem::path{});
    auto tool = prompt::fs::bin_tool("tse.resolve", exe, prompt::fs::find_git_root());
    auto result = prompt::process::command{"python3", tool.string(), "--complete", std::string(req.prefix)}
                      .discard_stderr()
                      .run();
    if (result.exit_code == 0) os << result.stdout_data;
}

} // namespace prompt::prompts::tse_data
