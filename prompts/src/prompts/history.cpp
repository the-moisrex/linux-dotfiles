#include "../sdk/history.hpp"
#include "../sdk/args.hpp"
#include "../sdk/errors.hpp"
#include "../sdk/prompt_registry.hpp"
#include "../sdk/terminal.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <format>
#include <nlohmann/json.hpp>
#include <optional>
#include <ostream>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::prompts {

namespace {

using nlohmann::json;

struct get_options {
    std::optional<std::size_t> head;
    std::string key;
    std::optional<std::size_t> segment_index;
    std::string segment_name;
    bool all = false;
    bool meta = false;
    bool input = false;
    // Scope the lookup to this terminal. `.last` defaults it on; `.history`
    // defaults it off and takes `--here`. `--any` turns it back off.
    bool here = false;
    bool any = false;
};

get_options parse_get(view const& v) {
    get_options opts;
    std::string const prefix = std::format("prompt .{}", v.name);
    opts.here = v.name == "last" || v.name == "history.last";
    opts.head = head_lines_or_throw(v);
    for (std::size_t i = 0; i < v.args.size(); ++i) {
        std::string_view const arg = v.args[i];
        if (arg == "--head") {
            ++i;
            continue;
        }
        if (arg == "--here") {
            opts.here = true;
            continue;
        }
        if (arg == "--any" || arg == "--global") {
            opts.any = true;
            continue;
        }
        if (arg == "--all") {
            opts.all = true;
            continue;
        }
        if (arg == "--meta") {
            opts.meta = true;
            continue;
        }
        if (arg == "--input") {
            opts.input = true;
            continue;
        }
        if (arg == "--segment") {
            if (i + 1 >= v.args.size()) throw usage_error{std::format("{}: --segment needs a name or index\n", prefix)};
            std::string value(v.args[++i]);
            bool const digits = !value.empty() && value.find_first_not_of("0123456789") == std::string::npos;
            if (digits) {
                try {
                    opts.segment_index = static_cast<std::size_t>(std::stoull(value));
                } catch (...) {
                    throw usage_error{std::format("{}: --segment index is out of range\n", prefix)};
                }
            } else {
                opts.segment_name = std::move(value);
            }
            continue;
        }
        if (arg.starts_with('-')) {
            throw usage_error{std::format("{}: unknown option: {}\n", prefix, arg)};
        }
        if (!opts.key.empty()) throw usage_error{std::format("{}: provide at most one run id or rank\n", prefix)};
        opts.key = std::string(arg);
    }
    if (opts.all && (opts.segment_index || !opts.segment_name.empty()))
        throw usage_error{std::format("{}: --all cannot be combined with --segment\n", prefix)};
    if (opts.meta && (opts.all || opts.segment_index || !opts.segment_name.empty() || opts.input))
        throw usage_error{std::format("{}: --meta cannot be combined with a segment view\n", prefix)};
    return opts;
}

// Resolve the run a `.history`/`.last` invocation means. A run id is always
// global (it names exactly one run); the no-argument form and a rank are scoped
// to this terminal when `here` is on. `fell_back` is set when the terminal had
// no run at all and the global store answered instead, so the caller can say so.
std::optional<history::stored_run> resolve_run(get_options const& opts, bool& fell_back) {
    bool const here = opts.here && !opts.any;
    std::string_view const key = opts.key;
    if (key.empty() || key == "latest" || key == "last") {
        if (here) {
            if (auto run = history::find_last_in(terminal::current())) return run;
            fell_back = true;
        }
        return history::find_by_id({});
    }
    // A pure integer is a rank when one could exist (`1..run count`), so
    // `.last 2` means "the second newest" rather than "an id that begins with
    // the year 2". A date-like key (`20261024`) is never a valid rank and falls
    // through to the id-prefix lookup, which is how a day's newest run is named.
    if (key.find_first_not_of("0123456789") == std::string_view::npos) {
        auto const rank = std::stoull(std::string(key));
        if (rank != 0 && rank <= history::run_files().size()) {
            if (here) {
                // With runs in this terminal, a missing rank is an error rather
                // than a silent fallback to an unrelated global run.
                if (auto run = history::find_rank_in(terminal::current(), rank)) return run;
                if (history::find_last_in(terminal::current())) return std::nullopt;
                fell_back = true;
            }
            if (auto run = history::find_by_rank(static_cast<std::size_t>(rank))) return run;
        }
    }
    // An explicit id (or id prefix) names exactly one run, whichever terminal
    // it came from.
    return history::find_by_id(key);
}

history::stored_segment const* find_segment(history::stored_run const& run, get_options const& opts) {
    if (opts.segment_index) {
        for (auto const& segment : run.segments) {
            if (segment.index == *opts.segment_index) return &segment;
        }
        return nullptr;
    }
    if (!opts.segment_name.empty()) {
        history::stored_segment const* found = nullptr;
        for (auto const& segment : run.segments) {
            if (segment.name == opts.segment_name) found = &segment;
        }
        return found;
    }
    // No selector: the last sink is what `--input` means by default.
    for (auto const& segment : std::views::reverse(run.segments)) {
        if (segment.role == "sink") return &segment;
    }
    return nullptr;
}

std::string first_line(std::string_view text, std::size_t limit = 72) {
    while (!text.empty() && (text.front() == '\n' || text.front() == ' ')) text.remove_prefix(1);
    auto const eol = text.find('\n');
    if (eol != std::string_view::npos) text = text.substr(0, eol);
    std::string out(text);
    if (out.size() > limit) {
        out.resize(limit);
        out += "…";
    }
    return out;
}

// The terminal of a run as JSON, for `--json` (an empty object when the run
// predates the identity).
json terminal_json(terminal::id const& who) {
    json out = json::object();
    if (who.sid != 0) out["sid"] = who.sid;
    if (!who.tty.empty()) out["tty"] = who.tty;
    if (!who.key.empty()) out["key"] = who.key;
    return out;
}

std::string meta_text(history::stored_run const& run) {
    std::string out;
    out += std::format("id: {}\n", run.id);
    out += std::format("path: {}\n", history::display_path(run.path));
    out += std::format("started: {}\n", run.started.empty() ? "-" : run.started);
    if (run.finished) {
        out += std::format("exit: {} ({})\n", run.exit_code, run.incomplete ? "incomplete" : "complete");
    } else {
        out += "exit: - (interrupted)\n";
    }
    if (!run.terminal.empty()) out += std::format("terminal: {}\n", terminal::describe(run.terminal));
    out += std::format("argv: {}", run.argv.empty() ? "-" : run.argv.front());
    for (std::size_t i = 1; i < run.argv.size(); ++i) out += std::format(" {}", run.argv[i]);
    out += '\n';
    if (!run.cwd.empty()) out += std::format("cwd: {}\n", run.cwd);
    if (!run.git_root.empty()) out += std::format("git-root: {}\n", run.git_root);
    if (!run.parent.empty()) out += std::format("parent: {}\n", run.parent);
    out += std::format("size: {} ({} bytes), segments: {}\n", history::human_bytes(run.bytes), run.bytes,
                       run.segments.size());
    for (auto const& segment : run.segments) {
        std::string args;
        for (auto const& arg : segment.args) args += " " + arg;
        out += std::format("  {}. {} [{}]{} — {} {}, {}\n", segment.index, segment.name, segment.role, args,
                           segment.status, segment.merge, history::human_bytes(segment.text.size()));
    }
    return out;
}

void execute_history(view const& v, result& r) {
    get_options const opts = parse_get(v);
    std::string const prefix = std::format("prompt .{}", v.name);
    bool fell_back = false;
    auto run = resolve_run(opts, fell_back);
    if (!run) {
        if (opts.key.empty()) {
            throw soft_error{
                std::format("{}: nothing recorded yet{}\n", prefix, opts.here && !opts.any ? " in this terminal" : "")};
        }
        throw soft_error{std::format("{}: no run matches '{}'\n", prefix, opts.key)};
    }
    if (fell_back) {
        r.err = std::format("{}: no run from this terminal; using the newest overall (add --here to require one)\n",
                            prefix);
    }

    std::string out;
    if (opts.meta) {
        out = meta_text(*run);
    } else if (opts.all) {
        for (auto const& segment : run->segments) {
            if (!out.empty()) out += '\n';
            out += std::format("## {} ({}, {}, {}, {})\n\n", segment.name, segment.role, segment.merge, segment.status,
                               history::human_bytes(segment.text.size()));
            out += segment.text;
            if (!segment.text.empty() && segment.text.back() != '\n') out += '\n';
        }
    } else if (opts.input || opts.segment_index || !opts.segment_name.empty()) {
        auto const* segment = find_segment(*run, opts);
        if (segment == nullptr) {
            throw soft_error{std::format("{}: run {} has no matching segment\n", prefix, run->id)};
        }
        out = opts.input ? history::barrier_input(*run, segment->index) : segment->text;
    } else {
        out = history::final_output(*run);
    }

    if (out.empty()) throw soft_error{std::format("{}: run {} produced no text\n", prefix, run->id)};
    out = trim_context(out, opts.head);
    if (out.empty() || out.back() != '\n') out += '\n';
    r.out = std::move(out);
    r.published.set(keys::save_stem, run->id);
}

struct list_options {
    std::optional<std::size_t> head;
    std::size_t limit = 20;
    std::optional<std::chrono::seconds> since;
    bool json = false;
    bool paths = false;
    bool here = false;
};

list_options parse_list(view const& v) {
    list_options opts;
    opts.head = head_lines_or_throw(v);
    for (std::size_t i = 0; i < v.args.size(); ++i) {
        std::string_view const arg = v.args[i];
        if (arg == "--head") {
            ++i;
            continue;
        }
        if (arg == "--json") {
            opts.json = true;
            continue;
        }
        if (arg == "--paths") {
            opts.paths = true;
            continue;
        }
        if (arg == "--here") {
            opts.here = true;
            continue;
        }
        if (arg == "--limit" || arg == "--since") {
            if (i + 1 >= v.args.size()) throw usage_error{std::format("prompt history.list: {} needs a value\n", arg)};
            std::string_view const value = v.args[++i];
            if (arg == "--limit") {
                try {
                    opts.limit = static_cast<std::size_t>(std::stoull(std::string(value)));
                } catch (...) {
                    throw usage_error{"prompt history.list: --limit needs a count\n"};
                }
            } else {
                opts.since = history::parse_duration(value);
                if (!opts.since) {
                    throw usage_error{
                        std::format("prompt history.list: bad duration '{}' (use 7d, 12h, 30m)\n", value)};
                }
            }
            continue;
        }
        throw usage_error{std::format("prompt history.list: unknown option: {}\n", arg)};
    }
    return opts;
}

void execute_history_list(view const& v, result& r) {
    list_options const opts = parse_list(v);
    auto runs = history::load_all();
    auto const now = std::chrono::system_clock::now();
    terminal::id const who = terminal::current();
    std::string out;
    json array = json::array();
    std::size_t shown = 0;
    for (auto const& run : runs) {
        if (shown >= opts.limit) break;
        if (opts.here && !history::in_terminal(run, who)) continue;
        if (opts.since) {
            auto const started = std::chrono::system_clock::from_time_t(static_cast<std::time_t>(run.ts));
            if (run.ts != 0 && now - started > *opts.since) continue;
        }
        ++shown;
        if (opts.paths) {
            out += history::display_path(run.path);
            out += '\n';
            continue;
        }
        if (opts.json) {
            json entry{
                {"id", run.id},
                {"path", history::display_path(run.path)},
                {"argv", run.argv},
                {"started", run.started},
                {"ts", run.ts},
                {"exit", run.exit_code},
                {"finished", run.finished},
                {"incomplete", run.incomplete},
                {"segments", run.segments.size()},
                {"bytes", run.bytes},
                {"terminal", terminal_json(run.terminal)},
            };
            std::string preview;
            for (auto const& segment : run.segments) {
                if (!segment.emitted) continue;
                preview = first_line(segment.text);
                if (!preview.empty()) break;
            }
            entry["preview"] = preview;
            array.push_back(std::move(entry));
            continue;
        }
        std::string preview;
        for (auto const& segment : run.segments) {
            if (!segment.emitted) continue;
            preview = first_line(segment.text);
            if (!preview.empty()) break;
        }
        out += std::format("{}  {}  exit {}  {} seg  {}  {}\n", run.id, run.started.empty() ? "-" : run.started,
                           run.finished ? run.exit_code : 0, run.segments.size(), history::human_bytes(run.bytes),
                           preview.empty() ? "(no output)" : preview);
    }
    if (opts.json) {
        out = array.dump(2);
        out += '\n';
    }
    if (out.empty()) {
        throw soft_error{opts.here ? "prompt history.list: nothing recorded yet in this terminal\n"
                                   : "prompt history.list: nothing recorded yet\n"};
    }
    out = trim_context(out, opts.head);
    if (!out.empty() && out.back() != '\n') out += '\n';
    r.out = std::move(out);
}

void execute_history_dir(view const& v, result& r) {
    if (!v.args.empty()) throw usage_error{"prompt history.dir takes no arguments\n"};
    auto const dir = history::runs_dir();
    if (dir.empty()) throw soft_error{"prompt history.dir: no history directory (HOME and XDG_STATE_HOME are unset)\n"};
    r.out = history::display_path(dir);
    r.out += '\n';
}

void complete_history(completion_request const& request, std::ostream& out) {
    for (auto const& id : history::run_ids()) {
        if (completion::matches(request.prefix, id)) completion::write_item(out, id, {});
    }
}

constexpr std::array<std::string_view, 4> kDurations{"1d", "7d", "30d", "90d"};

void complete_list(completion_request const& request, std::ostream& out) {
    if (completion::write_values_after_flag(out, request, "--since", kDurations)) return;
}

void render_help_history(std::ostream& os) {
    os << R"EOF(Usage: prompt .history [ID|RANK] [--segment NAME|INDEX] [--input] [--all] [--meta] [--head N] [--here]

Print a recorded run: the text the dispatcher produced, exactly as it went to
stdout and the clipboard. With no argument the newest run is used. A bare
integer is a rank counted from the newest (1 = latest) unless it matches the
start of a run id — ids begin with the date, so 20261024 selects that day.

Every `prompt` invocation is recorded under $XDG_STATE_HOME/prompt/runs as one
append-only Markdown file: the command line, the working directory, each
segment's own output and how it entered the chain. `.history*` runs are not
recorded themselves.

Views:
  (default)   the run's final output
  --segment   one segment by name (ai.dseek) or 1-based index
  --input     the context that segment (default: the last sink) actually saw
  --all       every segment with its merge mode and status
  --meta      metadata only: argv, times, exit, sizes, segments, terminal

Options:
  --here      Rank and default the lookup within this terminal (see `.last`)
  --head N    Keep only the first N lines
  -h, --help  Show this help message

Examples:
  prompt .history
  prompt .history 3
  prompt .history --segment ai.dseek
  prompt .history --meta
  prompt .history --here
  prompt .history .save answer.md
  prompt .history .ai.grok

See also: `.last` (the newest run from this terminal), `.history.list` (what is
stored), `.history.find` (search), and `.history.purge` (retention).
Set PROMPT_HISTORY=0 to skip recording one run.

)EOF";
}

void render_help_history_last(std::ostream& os) {
    os << R"EOF(Usage: prompt .last [N|ID] [--segment NAME|INDEX] [--input] [--all] [--meta] [--head N] [--any]

Print the newest run from THIS terminal — the answer a previous invocation
produced here — so it can be chained further, e.g.
`prompt .last .note "compare with the above" .ai.chatgpt`. When this terminal
has no recorded run yet, the globally newest one is used and a note is printed
on stderr; `--any` always reads the globally newest. `.history.last` is an alias.

A bare integer is the Nth newest run from this terminal (1 = the last one here),
unless it matches the start of a run id (ids begin with the date). An explicit
id always means exactly that run, whichever terminal it came from.

Views:
  (default)   the run's final output
  --segment   one segment by name (ai.dseek) or 1-based index
  --input     the context that segment (default: the last sink) actually saw
  --all       every segment with its merge mode and status
  --meta      metadata only: argv, times, exit, sizes, segments, terminal

Options:
  --any       Ignore this terminal and use the globally newest runs
  --head N    Keep only the first N lines
  -h, --help  Show this help message

Examples:
  prompt .last
  prompt .last .note "compare with the above" .ai.chatgpt
  prompt .last 2 --segment ai.gap
  prompt .last --meta
  prompt .last --any

See also: `.history` (the same views, global by default; `--here` scopes it).
Set PROMPT_HISTORY=0 to skip recording one run.

)EOF";
}

void render_help_history_list(std::ostream& os) {
    os << R"EOF(Usage: prompt .history.list [--limit N] [--since DUR] [--paths] [--json] [--head N] [--here]

List recorded runs, newest first: id, start time, exit status, segment count,
size and the first line of output.

Options:
  --limit N    Show at most N runs (default: 20)
  --since DUR  Only runs newer than DUR (7d, 12h, 30m, 90s; a bare number is days)
  --paths      Print file paths only, one per line
  --json       Emit one JSON object per run (includes its terminal)
  --here       Only runs from this terminal
  --head N     Keep only the first N lines
  -h, --help   Show this help message

Examples:
  prompt .history.list
  prompt .history.list --since 7d
  prompt .history.list --here
  prompt .history.list --paths | xargs rg -n TODO

)EOF";
}

void render_help_history_dir(std::ostream& os) {
    os << R"EOF(Usage: prompt .history.dir

Print the directory that holds the recorded runs (one Markdown file each), for
`rg`, `ls`, `cd` and friends.

Options:
  -h, --help  Show this help message

)EOF";
}

} // namespace

static void execute_history_entry(view const& v, result& r) { execute_history(v, r); }
static void execute_history_last_entry(view const& v, result& r) { execute_history(v, r); }
static void execute_history_list_entry(view const& v, result& r) { execute_history_list(v, r); }
static void execute_history_dir_entry(view const& v, result& r) { execute_history_dir(v, r); }

// This file is the prompt: it registers its entry points at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered_history = prompt::register_prompt(
    "history", "Print a recorded prompt run: the answer/text a previous invocation produced, by id, rank or segment.",
    &execute_history_entry, &render_help_history, &complete_history, chain_role::normal, true, nullptr, nullptr, false,
    false);

bool const registered_history_last = prompt::register_prompt(
    "history.last", "Print the newest recorded run from this terminal, to chain into a later invocation.",
    &execute_history_last_entry, &render_help_history_last, &complete_history, chain_role::normal, true, nullptr,
    nullptr, false, false);

bool const registered_last = prompt::register_prompt(
    "last", "Print the newest recorded run from this terminal, to chain into a later invocation.",
    &execute_history_last_entry, &render_help_history_last, &complete_history, chain_role::normal, true, nullptr,
    nullptr, false, false);

bool const registered_history_list = prompt::register_prompt(
    "history.list", "List recorded prompt runs, newest first: id, start time, exit status, segments, size.",
    &execute_history_list_entry, &render_help_history_list, &complete_list, chain_role::normal, true, nullptr, nullptr,
    false, false);

bool const registered_history_dir = prompt::register_prompt(
    "history.dir", "Print the directory that holds the recorded prompt runs.", &execute_history_dir_entry,
    &render_help_history_dir, nullptr, chain_role::normal, true, nullptr, nullptr, false, false);
} // namespace prompt::prompts
