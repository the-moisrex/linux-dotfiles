#include "../sdk/args.hpp"
#include "../sdk/errors.hpp"
#include "../sdk/history.hpp"
#include "../sdk/prompt_registry.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <format>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::prompts {

namespace {

struct purge_options {
    std::optional<std::size_t> head;
    std::optional<std::chrono::seconds> older_than;
    std::optional<std::size_t> keep;
    std::optional<std::uintmax_t> max_size;
    bool all = false;
    bool dry_run = false;
};

purge_options parse_purge(view const& v) {
    purge_options opts;
    opts.head = head_lines_or_throw(v);
    for (std::size_t i = 0; i < v.args.size(); ++i) {
        std::string_view const arg = v.args[i];
        if (arg == "--head") {
            ++i;
            continue;
        }
        if (arg == "--all") {
            opts.all = true;
            continue;
        }
        if (arg == "--dry-run") {
            opts.dry_run = true;
            continue;
        }
        if (arg == "--older-than" || arg == "--keep" || arg == "--max-size") {
            if (i + 1 >= v.args.size()) throw usage_error{std::format("prompt history.purge: {} needs a value\n", arg)};
            std::string_view const value = v.args[++i];
            if (arg == "--older-than") {
                opts.older_than = history::parse_duration(value);
                if (!opts.older_than)
                    throw usage_error{
                        std::format("prompt history.purge: bad duration '{}' (use 7d, 12h, 30m)\n", value)};
            } else if (arg == "--keep") {
                try {
                    opts.keep = static_cast<std::size_t>(std::stoull(std::string(value)));
                } catch (...) {
                    throw usage_error{"prompt history.purge: --keep needs a count\n"};
                }
            } else {
                opts.max_size = history::parse_size(value);
                if (!opts.max_size)
                    throw usage_error{std::format("prompt history.purge: bad size '{}' (use 256M, 1G, 512K)\n", value)};
            }
            continue;
        }
        if (arg.starts_with('-')) {
            throw usage_error{std::format("prompt history.purge: unknown option: {}\n", arg)};
        }
        throw usage_error{std::format("prompt history.purge: unexpected argument '{}'\n", arg)};
    }
    return opts;
}

void execute_purge(view const& v, result& r) {
    purge_options const opts = parse_purge(v);

    auto const files = history::run_files(); // newest first
    if (files.empty()) {
        throw soft_error{"prompt history.purge: nothing recorded yet\n"};
    }

    // --keep is a floor; the run being written (exported by the dispatcher) is
    // never a candidate.
    std::size_t const protect_count = opts.keep.value_or(0);
    std::filesystem::path current;
    if (char const* id = std::getenv("PROMPT_HISTORY_CURRENT"); id != nullptr && *id != '\0')
        current = history::runs_dir() / (std::string(id) + ".md");

    std::vector<std::uintmax_t> sizes(files.size(), 0);
    std::uintmax_t total = 0;
    for (std::size_t i = 0; i < files.size(); ++i) {
        std::error_code ec;
        sizes[i] = std::filesystem::file_size(files[i], ec);
        if (ec) sizes[i] = 0;
        total += sizes[i];
    }

    // No selector means "apply the configured retention", exactly what the
    // automatic prune after a run does.
    bool const defaults = !opts.older_than && !opts.max_size && !opts.all;
    std::size_t const default_keep = defaults ? history::keep_runs() : 0;
    std::uintmax_t const default_size = defaults ? history::keep_bytes() : 0;

    std::vector<std::size_t> victims;
    for (std::size_t rank = 0; rank < files.size(); ++rank) {
        if (rank < protect_count) continue;
        if (!current.empty() && files[rank] == current) continue;
        bool selected = false;
        if (defaults) {
            selected = (default_keep != 0 && files.size() - rank > default_keep) ||
                       (default_size != 0 && total > default_size);
        } else {
            selected = opts.all;
            if (opts.older_than) {
                std::error_code ec;
                auto const written = std::filesystem::last_write_time(files[rank], ec);
                if (!ec) {
                    auto const age = std::chrono::duration_cast<std::chrono::seconds>(
                        std::filesystem::file_time_type::clock::now() - written);
                    selected = selected || age > *opts.older_than;
                }
            }
            if (opts.max_size) selected = selected || total > *opts.max_size;
        }
        if (!selected) continue;
        victims.push_back(rank);
        total = total > sizes[rank] ? total - sizes[rank] : 0;
    }

    if (victims.empty()) {
        r.out = std::format("prompt history.purge: nothing to remove ({} runs, {})\n", files.size(),
                            history::human_bytes(total));
        r.no_clipboard = true;
        return;
    }

    std::uintmax_t victim_total = 0;
    for (auto const rank : victims) victim_total += sizes[rank];

    std::string out;
    std::size_t removed = 0;
    std::uintmax_t removed_bytes = 0;
    if (opts.dry_run) {
        out += std::format("prompt history.purge: would remove {} runs ({})\n", victims.size(),
                           history::human_bytes(victim_total));
    } else {
        for (auto const rank : victims) {
            std::error_code ec;
            if (std::filesystem::remove(files[rank], ec) && !ec) {
                ++removed;
                removed_bytes += sizes[rank];
            }
        }
        out +=
            std::format("prompt history.purge: removed {} runs ({})\n", removed, history::human_bytes(removed_bytes));
    }
    for (auto const rank : victims) out += std::format("  {}\n", files[rank].filename().string());
    out += std::format("kept {} runs ({})\n", files.size() - victims.size(), history::human_bytes(total));
    out = trim_context(out, opts.head);
    r.out = std::move(out);
    r.no_clipboard = true;
}

void complete_purge(completion_request const& request, std::ostream& out) {
    static constexpr std::array<std::string_view, 4> kDurations{"1d", "7d", "30d", "90d"};
    static constexpr std::array<std::string_view, 4> kSizes{"256M", "512M", "1G", "2G"};
    if (completion::write_values_after_flag(out, request, "--older-than", kDurations)) return;
    if (completion::write_values_after_flag(out, request, "--max-size", kSizes)) return;
}

void render_help_history_purge(std::ostream& os) {
    os << R"EOF(Usage: prompt .history.purge [--older-than DUR] [--keep N] [--max-size SIZE]
                               [--all] [--dry-run] [--head N]

Delete recorded runs. With no selector the configured retention is applied
(PROMPT_HISTORY_KEEP/run count and PROMPT_HISTORY_SIZE/total bytes, the same
caps the automatic prune uses). --keep always protects the newest N runs, and
the run currently being written is never touched.

Options:
  --older-than DUR  Remove runs older than DUR (7d, 12h, 30m; bare number = days)
  --keep N          Never remove the newest N runs
  --max-size SIZE   Remove the oldest runs until the store fits SIZE (256M, 1G)
  --all             Remove everything except the runs --keep protects
  --dry-run         List what would be removed without deleting anything
  --head N          Keep only the first N lines of the report
  -h, --help        Show this help message

Examples:
  prompt .history.purge --dry-run
  prompt .history.purge --older-than 30d --keep 20
  prompt .history.purge --max-size 512M
  prompt .history.purge --all --keep 5

)EOF";
}

} // namespace

static void execute_history_purge_entry(view const& v, result& r) { execute_purge(v, r); }

bool const registered_history_purge = prompt::register_prompt(
    "history.purge", "Delete recorded prompt runs by age, count or total size (with --dry-run).",
    &execute_history_purge_entry, &render_help_history_purge, &complete_purge, chain_role::normal, true, nullptr,
    nullptr, false, false);

} // namespace prompt::prompts
