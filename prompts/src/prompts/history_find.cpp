#include "../sdk/args.hpp"
#include "../sdk/errors.hpp"
#include "../sdk/history.hpp"
#include "../sdk/prompt_registry.hpp"
#include "../sdk/terminal.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <format>
#include <nlohmann/json.hpp>
#include <optional>
#include <ostream>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::prompts {

namespace {

using nlohmann::json;

struct find_options {
    std::optional<std::size_t> head;
    std::string query;
    std::string segment_name;
    std::optional<std::chrono::seconds> since;
    std::size_t limit = 20;
    std::size_t context = 0;
    bool regex = false;
    bool paths = false;
    bool json = false;
    bool ignore_case = true;
    bool here = false;
};

find_options parse_find(view const& v) {
    find_options opts;
    opts.head = head_lines_or_throw(v);
    for (std::size_t i = 0; i < v.args.size(); ++i) {
        std::string_view const arg = v.args[i];
        if (arg == "--head") {
            ++i;
            continue;
        }
        if (arg == "--regex") {
            opts.regex = true;
            continue;
        }
        if (arg == "--case-sensitive") {
            opts.ignore_case = false;
            continue;
        }
        if (arg == "--paths") {
            opts.paths = true;
            continue;
        }
        if (arg == "--json") {
            opts.json = true;
            continue;
        }
        if (arg == "--here") {
            opts.here = true;
            continue;
        }
        if (arg == "--name" || arg == "--since" || arg == "--limit" || arg == "--context") {
            if (i + 1 >= v.args.size()) throw usage_error{std::format("prompt history.find: {} needs a value\n", arg)};
            std::string_view const value = v.args[++i];
            if (arg == "--name") {
                opts.segment_name = std::string(value);
            } else if (arg == "--since") {
                opts.since = history::parse_duration(value);
                if (!opts.since)
                    throw usage_error{
                        std::format("prompt history.find: bad duration '{}' (use 7d, 12h, 30m)\n", value)};
            } else {
                std::size_t parsed = 0;
                try {
                    parsed = static_cast<std::size_t>(std::stoull(std::string(value)));
                } catch (...) {
                    throw usage_error{std::format("prompt history.find: {} needs a count\n", arg)};
                }
                if (arg == "--limit") {
                    opts.limit = parsed;
                } else {
                    opts.context = parsed;
                }
            }
            continue;
        }
        if (arg.starts_with('-')) {
            throw usage_error{std::format("prompt history.find: unknown option: {}\n", arg)};
        }
        if (!opts.query.empty()) throw usage_error{"prompt history.find: providing two queries is ambiguous\n"};
        opts.query = std::string(arg);
    }
    if (opts.query.empty()) throw usage_error{"prompt history.find: a query is required\n"};
    return opts;
}

bool contains(std::string_view haystack, std::string_view needle, bool ignore_case) {
    if (needle.empty()) return true;
    if (!ignore_case) return haystack.contains(needle);
    auto const* it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
    return it != haystack.end();
}

std::vector<std::string_view> lines_of(std::string const& text) {
    std::vector<std::string_view> lines;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        auto const eol = text.find('\n', pos);
        if (eol == std::string::npos) {
            if (pos < text.size()) lines.push_back(std::string_view(text).substr(pos));
            break;
        }
        lines.push_back(std::string_view(text).substr(pos, eol - pos));
        pos = eol + 1;
    }
    return lines;
}

struct hit {
    std::string id;
    std::string path;
    std::string name;
    std::size_t index = 0;
    std::size_t line = 0;
    std::string text;
    std::vector<std::string> context;
    terminal::id terminal;
};

json terminal_json(terminal::id const& who) {
    json out = json::object();
    if (who.sid != 0) out["sid"] = who.sid;
    if (!who.tty.empty()) out["tty"] = who.tty;
    if (!who.key.empty()) out["key"] = who.key;
    return out;
}

void execute_find(view const& v, result& r) {
    find_options const opts = parse_find(v);

    std::optional<std::regex> pattern;
    if (opts.regex) {
        try {
            auto flags = std::regex::ECMAScript;
            if (opts.ignore_case) flags |= std::regex::icase;
            pattern.emplace(std::string(opts.query), flags);
        } catch (std::regex_error const& error) {
            throw usage_error{std::format("prompt history.find: bad regex: {}\n", error.what())};
        }
    }

    std::vector<hit> hits;
    auto const now = std::chrono::system_clock::now();
    terminal::id const who = terminal::current();
    for (auto const& run : history::load_all()) {
        if (hits.size() >= opts.limit) break;
        if (opts.here && !history::in_terminal(run, who)) continue;
        if (opts.since) {
            auto const started = std::chrono::system_clock::from_time_t(static_cast<std::time_t>(run.ts));
            if (run.ts != 0 && now - started > *opts.since) continue;
        }
        // The command line is searchable too: `prompt .history.find ai.dseek`.
        bool run_level = false;
        for (auto const& arg : run.argv) {
            if (contains(arg, opts.query, opts.ignore_case)) run_level = true;
        }
        if (run_level) {
            std::string command;
            for (auto const& arg : run.argv) {
                if (!command.empty()) command += ' ';
                command += arg;
            }
            hits.push_back(hit{
                .id = run.id,
                .path = history::display_path(run.path),
                .name = "argv",
                .index = 0,
                .line = 0,
                .text = std::move(command),
                .context = {},
                .terminal = run.terminal,
            });
        }
        for (auto const& segment : run.segments) {
            if (hits.size() >= opts.limit) break;
            if (!opts.segment_name.empty() && segment.name != opts.segment_name) continue;
            auto const lines = lines_of(segment.text);
            for (std::size_t i = 0; i < lines.size(); ++i) {
                bool const matched = pattern ? std::regex_search(std::string(lines[i]), *pattern)
                                             : contains(lines[i], opts.query, opts.ignore_case);
                if (!matched) continue;
                hit entry{
                    .id = run.id,
                    .path = history::display_path(run.path),
                    .name = segment.name,
                    .index = segment.index,
                    .line = i + 1,
                    .text = std::string(lines[i]),
                    .context = {},
                    .terminal = run.terminal,
                };
                auto const from = i > opts.context ? i - opts.context : 0;
                auto const to = std::min(lines.size(), i + opts.context + 1);
                for (std::size_t j = from; j < to; ++j) entry.context.emplace_back(lines[j]);
                hits.push_back(std::move(entry));
                if (hits.size() >= opts.limit) break;
            }
        }
    }

    if (hits.empty()) {
        throw soft_error{std::format("prompt history.find: no match for '{}'{}\n", opts.query,
                                     opts.here ? " in this terminal" : "")};
    }

    std::string out;
    json array = json::array();
    for (auto const& entry : hits) {
        if (opts.paths) {
            out += entry.path;
            out += '\n';
            continue;
        }
        if (opts.json) {
            array.push_back(json{
                {"id", entry.id},
                {"path", entry.path},
                {"segment", entry.name},
                {"index", entry.index},
                {"line", entry.line},
                {"text", entry.text},
                {"context", entry.context},
                {"terminal", terminal_json(entry.terminal)},
            });
            continue;
        }
        if (opts.context == 0) {
            if (entry.name == "argv") {
                out += std::format("{}  argv  {}\n", entry.id, entry.text);
            } else {
                out += std::format("{}  {}:{}  {}\n", entry.id, entry.name, entry.line, entry.text);
            }
            continue;
        }
        out += std::format("{}  {}:{}\n", entry.id, entry.name, entry.line);
        for (auto const& line : entry.context) {
            out += "  ";
            out += line;
            out += '\n';
        }
    }
    if (opts.json) {
        out = array.dump(2);
        out += '\n';
    }
    out = trim_context(out, opts.head);
    if (!out.empty() && out.back() != '\n') out += '\n';
    r.out = std::move(out);
}

void render_help_history_find(std::ostream& os) {
    os << R"EOF(Usage: prompt .history.find QUERY [--regex] [--name SEGMENT] [--since DUR]
                            [--limit N] [--context N] [--paths] [--json] [--here] [--head N]

Search the recorded runs: every segment body plus the recorded command line.
Matching is case-insensitive and literal unless --regex is given; each hit
prints the run id, the segment, the line number and the matching line.

Options:
  --regex            Treat QUERY as an ECMAScript regular expression
  --case-sensitive   Match case exactly
  --name SEGMENT     Only search that segment (ai.dseek, tse, ...)
  --since DUR        Only runs newer than DUR (7d, 12h, 30m; bare number = days)
  --limit N          Stop after N hits (default: 20)
  --context N        Print N lines around each hit
  --paths            Print the run files only, one per line
  --json             Emit one JSON object per hit (includes its terminal)
  --here             Only search runs from this terminal
  --head N           Keep only the first N lines
  -h, --help         Show this help message

Examples:
  prompt .history.find "race condition"
  prompt .history.find --name ai.dseek --since 7d --context 2 "mutex"
  prompt .history.find --here "TODO"
  prompt .history.find --regex 'TODO|FIXME' --paths | xargs -r ls -l

)EOF";
}

} // namespace

static void execute_history_find_entry(view const& v, result& r) { execute_find(v, r); }

void complete_find(completion_request const& request, std::ostream& out) {
    static constexpr std::array<std::string_view, 4> kDurations{"1d", "7d", "30d", "90d"};
    if (completion::write_values_after_flag(out, request, "--since", kDurations)) return;
    if (request.prior_args.empty() || request.prior_args.back() != "--name") return;
    auto const names = prompt::list_prompt_names();
    completion::write_values(out, request.prefix, names);
}

bool const registered_history_find = prompt::register_prompt(
    "history.find", "Search recorded prompt runs — segment text and command lines — case-insensitively or by regex.",
    &execute_history_find_entry, &render_help_history_find, &complete_find, chain_role::normal, true, nullptr, nullptr,
    false, false);

} // namespace prompt::prompts
