#include "../sdk/stash.hpp"
#include "../core/sanitize.hpp"
#include "../core/strings.hpp"
#include "../sdk/args.hpp"
#include "../sdk/completion.hpp"
#include "../sdk/errors.hpp"
#include "../sdk/prompt_registry.hpp"
#include "../sdk/sink.hpp"
#include "../sdk/stage.hpp"
#include "../sdk/terminal.hpp"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace prompt::prompts {

static void render_help_stash(std::ostream& os);
static void render_help_stash_list(std::ostream& os);
static void render_help_stash_show(std::ostream& os);
static void render_help_stash_drop(std::ostream& os);

namespace {

bool parse_scope(std::string_view arg, std::optional<bool>& here) {
    if (arg == "--here") {
        here = true;
        return true;
    }
    if (arg == "--global") {
        here = false;
        return true;
    }
    return false;
}

struct stash_config {
    std::string name = "default";
    bool named = false;
    bool replace = false;
    bool no_clobber = false;
    bool artifacts_only = false;
    bool quiet = false;
    std::optional<bool> here;
};

stash_config parse_stash_config(view const& v) {
    stash_config cfg;
    bool options = true;
    for (std::string_view const arg : v.args) {
        if (options && arg == "--") {
            options = false;
            continue;
        }
        if (options && arg == "--replace") {
            cfg.replace = true;
            continue;
        }
        if (options && arg == "--no-clobber") {
            cfg.no_clobber = true;
            continue;
        }
        if (options && arg == "--artifacts-only") {
            cfg.artifacts_only = true;
            continue;
        }
        if (options && arg == "--quiet") {
            cfg.quiet = true;
            continue;
        }
        if (options && parse_scope(arg, cfg.here)) continue;
        if (options && arg.starts_with('-')) {
            throw usage_error{std::format("prompt stash: unknown option: {}\n{}", arg, help_text(&render_help_stash))};
        }
        if (cfg.named) throw usage_error{"prompt stash: provide at most one name\n"};
        cfg.name = std::string(arg);
        cfg.named = true;
    }
    if (cfg.replace && cfg.no_clobber) {
        throw usage_error{"prompt stash: --replace and --no-clobber cannot be combined\n"};
    }
    if (!stash::valid_name(cfg.name)) {
        throw usage_error{std::format("prompt stash: invalid stash name: {}\n", cfg.name)};
    }
    return cfg;
}

struct list_config {
    std::optional<bool> here;
    bool json = false;
    bool long_form = false;
};

list_config parse_list_config(view const& v) {
    list_config cfg;
    bool options = true;
    for (std::string_view const arg : positional_args(v)) {
        if (options && arg == "--") {
            options = false;
            continue;
        }
        if (options && arg == "--json") {
            cfg.json = true;
            continue;
        }
        if (options && (arg == "--long" || arg == "-l")) {
            cfg.long_form = true;
            continue;
        }
        if (options && parse_scope(arg, cfg.here)) continue;
        if (options && arg.starts_with('-')) {
            throw usage_error{
                std::format("prompt stash.list: unknown option: {}\n{}", arg, help_text(&render_help_stash_list))};
        }
        throw usage_error{std::format("prompt stash.list: unexpected argument: {}\n", arg)};
    }
    return cfg;
}

struct show_config {
    std::string name = "default";
    bool named = false;
    bool facts = false;
    bool json = false;
    std::optional<bool> here;
};

show_config parse_show_config(view const& v) {
    show_config cfg;
    bool options = true;
    for (std::string_view const arg : positional_args(v)) {
        if (options && arg == "--") {
            options = false;
            continue;
        }
        if (options && arg == "--facts") {
            cfg.facts = true;
            continue;
        }
        if (options && arg == "--json") {
            cfg.json = true;
            continue;
        }
        if (options && parse_scope(arg, cfg.here)) continue;
        if (options && arg.starts_with('-')) {
            throw usage_error{
                std::format("prompt stash.show: unknown option: {}\n{}", arg, help_text(&render_help_stash_show))};
        }
        if (cfg.named) throw usage_error{"prompt stash.show: provide at most one name\n"};
        cfg.name = std::string(arg);
        cfg.named = true;
    }
    if (!stash::valid_name(cfg.name)) {
        throw usage_error{std::format("prompt stash.show: invalid stash name: {}\n", cfg.name)};
    }
    return cfg;
}

struct drop_config {
    std::string name = "default";
    bool named = false;
    bool all = false;
    bool dry_run = false;
    bool quiet = false;
    std::optional<bool> here;
};

drop_config parse_drop_config(view const& v) {
    drop_config cfg;
    bool options = true;
    for (std::string_view const arg : v.args) {
        if (options && arg == "--") {
            options = false;
            continue;
        }
        if (options && arg == "--all") {
            cfg.all = true;
            continue;
        }
        if (options && arg == "--dry-run") {
            cfg.dry_run = true;
            continue;
        }
        if (options && arg == "--quiet") {
            cfg.quiet = true;
            continue;
        }
        if (options && parse_scope(arg, cfg.here)) continue;
        if (options && arg.starts_with('-')) {
            throw usage_error{
                std::format("prompt stash.drop: unknown option: {}\n{}", arg, help_text(&render_help_stash_drop))};
        }
        if (cfg.named) throw usage_error{"prompt stash.drop: provide at most one name\n"};
        cfg.name = std::string(arg);
        cfg.named = true;
    }
    if (!cfg.all && !stash::valid_name(cfg.name)) {
        throw usage_error{std::format("prompt stash.drop: invalid stash name: {}\n", cfg.name)};
    }
    return cfg;
}

void complete_stash_names(completion_request const& req, std::ostream& os) {
    std::vector<std::string> errors;
    auto const records = stash::list(std::nullopt, errors);
    std::vector<std::string> seen;
    for (auto const& rec : records) {
        if (!completion::matches(req.prefix, rec.name)) continue;
        if (std::find(seen.begin(), seen.end(), rec.name) != seen.end()) continue;
        seen.push_back(rec.name);
        completion::write_item(os, rec.name, rec.here ? "here" : "global");
    }
}

void preflight_stash(view const& v) {
    auto const cfg = parse_stash_config(v);
    std::string error;
    if (!stash::ensure_directory(cfg.here.value_or(false), error)) {
        throw soft_error{std::format("prompt stash: {}\n", error)};
    }
}

void execute_stash(view const& v, result& r) {
    auto const cfg = parse_stash_config(v);
    if (v.chain == nullptr) throw soft_error{"prompt stash: no chain state available\n"};

    stash::record rec;
    rec.name = cfg.name;
    rec.created = stash::now_iso();
    rec.created_ts = stash::now_ts();
    rec.updated = rec.created;
    rec.updated_ts = rec.created_ts;
    std::error_code ec;
    auto const cwd = std::filesystem::current_path(ec);
    if (!ec) rec.cwd = cwd.string();
    rec.git_root = v.env.git_root.string();
    rec.terminal = terminal::current();
    if (char const* run = std::getenv("PROMPT_HISTORY_CURRENT"); run != nullptr) rec.source_run = run;
    rec.redacted = v.chain->redacted;
    rec.incomplete = v.chain->incomplete;
    rec.text = v.chain->text;
    if (v.head) rec.text = std::string(trim_context_lines(rec.text, *v.head));
    rec.known = cfg.artifacts_only ? v.chain->known : v.prior;

    bool const here = cfg.here.value_or(false);
    auto const path = stash::path_for(cfg.name, here);
    std::error_code exists_ec;
    bool const existed = !path.empty() && std::filesystem::exists(path, exists_ec);
    bool const append = !cfg.replace && !cfg.no_clobber;

    std::string error;
    if (!stash::save(rec, here, append, cfg.no_clobber, error)) {
        throw soft_error{std::format("prompt stash: {}\n", error)};
    }

    r.no_clipboard = true;
    if (!cfg.quiet) {
        r.err = std::format("prompt stash: {} {} ({} bytes, {} facts)\n", append && existed ? "appended to" : "wrote",
                            path.string(), rec.text.size(), rec.known.size());
    }
}

void execute_stash_list(view const& v, result& r) {
    auto const head = head_lines_or_throw(v);
    auto const cfg = parse_list_config(v);

    std::vector<std::string> errors;
    auto const records = stash::list(cfg.here, errors);
    for (auto const& error : errors) r.err += std::format("prompt stash.list: {}\n", error);

    if (cfg.json) {
        nlohmann::json array = nlohmann::json::array();
        for (auto const& rec : records) array.push_back(nlohmann::json::parse(stash::display_json(rec)));
        r.out = array.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
        r.out += '\n';
        // JSON is machine-readable: redact in place so the dispatcher does not
        // prepend the human redaction legend and corrupt the document.
        auto clean = sanitize::sanitize(r.out);
        r.out = std::move(clean.text);
        return;
    }

    std::map<std::string, bool> has_here;
    for (auto const& rec : records) {
        if (rec.here) has_here[rec.name] = true;
    }

    std::string out;
    if (records.empty()) {
        out = "no stashes\n";
    } else {
        out += std::format("{:<21} {:<7} {:<19} {:>8} {:>5}\n", "NAME", "SCOPE", "UPDATED", "BYTES", "FACTS");
        for (auto const& rec : records) {
            bool const effective = rec.here == has_here.contains(rec.name);
            out += std::format("{}{:<19} {:<7} {:<19} {:>8} {:>5}\n", effective ? "* " : "  ", rec.name.substr(0, 19),
                               rec.here ? "here" : "global", rec.updated, rec.text.size(), rec.known.size());
            if (!cfg.long_form) continue;
            out += std::format("    path: {}\n", rec.path.string());
            if (!rec.cwd.empty()) out += std::format("    cwd: {}\n", rec.cwd);
            if (!rec.git_root.empty()) out += std::format("    git root: {}\n", rec.git_root);
            out += std::format("    terminal: {}\n", terminal::describe(rec.terminal));
            if (!rec.source_run.empty()) out += std::format("    source run: {}\n", rec.source_run);
            std::string flags;
            if (rec.redacted) flags += "redacted ";
            if (rec.incomplete) flags += "incomplete ";
            if (rec.facts_redacted) flags += "facts-redacted ";
            if (!flags.empty()) out += std::format("    flags: {}\n", flags);
        }
    }
    if (head) out = std::string(trim_context_lines(out, *head));
    r.out = std::move(out);
}

void execute_stash_show(view const& v, result& r) {
    auto const head = head_lines_or_throw(v);
    auto const cfg = parse_show_config(v);

    std::string error;
    auto rec = stash::load(cfg.name, cfg.here, error);
    if (!rec) throw soft_error{std::format("prompt stash.show: {}\n", error)};

    if (cfg.json) {
        r.out = stash::display_json(*rec);
        r.out += '\n';
        auto clean = sanitize::sanitize(r.out);
        r.out = std::move(clean.text);
    } else {
        std::string out = rec->text;
        if (cfg.facts) {
            if (!out.empty() && out.back() != '\n') out += '\n';
            out += "\nFacts:\n";
            if (rec->known.empty()) {
                out += "  (none)\n";
            } else {
                for (auto const& entry : rec->known.entries()) out += std::format("  {}: {}\n", entry.key, entry.value);
            }
        }
        if (head) out = std::string(trim_context_lines(out, *head));
        r.out = std::move(out);
    }
    if (rec->redacted) r.err += "prompt stash.show: note: the text contains redacted personal data\n";
    if (rec->facts_redacted) r.err += "prompt stash.show: note: some facts contain redacted personal data\n";
}

void execute_stash_drop(view const& v, result& r) {
    auto const cfg = parse_drop_config(v);

    std::vector<std::string> removed;
    std::vector<std::string> errors;

    auto drop_one = [&](std::string_view name, std::optional<bool> here) {
        if (cfg.dry_run) {
            auto const path = stash::resolve_path(name, here);
            std::error_code ec;
            if (path.empty() || !std::filesystem::exists(path, ec)) {
                errors.push_back(std::format("no stash named '{}'", name));
                return;
            }
            removed.push_back(path.string());
            return;
        }
        std::string error;
        if (!stash::remove(name, here, error)) {
            errors.push_back(std::move(error));
            return;
        }
        removed.emplace_back(name);
    };

    // `--all NAME` without an explicit scope drops both copies; with `--here`
    // or `--global` it drops only that one. A missing copy is not an error.
    auto drop_both = [&](std::string_view name) {
        for (bool const scope : {true, false}) {
            auto const path = stash::path_for(name, scope);
            std::error_code ec;
            if (path.empty() || !std::filesystem::exists(path, ec)) continue;
            drop_one(name, scope);
        }
    };

    if (cfg.all) {
        if (cfg.named) {
            if (cfg.here) {
                drop_one(cfg.name, cfg.here);
            } else {
                drop_both(cfg.name);
            }
        } else {
            std::vector<std::string> list_errors;
            auto const records = stash::list(cfg.here, list_errors);
            errors.insert(errors.end(), list_errors.begin(), list_errors.end());
            for (auto const& rec : records) drop_one(rec.name, rec.here);
        }
    } else {
        drop_one(cfg.name, cfg.here);
    }

    if (removed.empty() && !errors.empty()) {
        std::string message = "prompt stash.drop: ";
        for (auto const& error : errors) message += error + "\n";
        throw soft_error{std::move(message)};
    }
    for (auto const& error : errors) r.err += std::format("prompt stash.drop: {}\n", error);
    if (!cfg.quiet) {
        for (auto const& item : removed) {
            r.err += std::format("prompt stash.drop: {} {}\n", cfg.dry_run ? "would remove" : "removed", item);
        }
    }
    r.no_clipboard = true;
}

} // namespace

static void render_help_stash(std::ostream& os) {
    os << R"EOF(Usage: prompt <other prompts...> .stash [NAME] [--replace|--no-clobber] [--artifacts-only] [--here|--global] [--quiet]

Save the whole chain state — the accumulated context, every effective fact, the
redaction flag and the failure taint — so a later invocation can continue as if
it were the same command. `.stashed` restores it.

The default action appends to an existing stash: the new context is added after
the old text, facts are last-writer-wins, and the redaction/failure flags are
combined. A new stash is named `default` unless NAME is given. The default scope
is global; `--here` stores it for this terminal only, shadowing a global stash of
the same name when it is restored.

)EOF";
    render_sink_help(os);
    os << R"EOF(
Stash options:
  --replace        Overwrite an existing stash instead of appending
  --no-clobber     Refuse to write when the stash already exists
  --artifacts-only Save only the completed segments' artifacts, not the
                   declarations later segments contributed
  --here           Store in this terminal's scope
  --global         Store in the global scope (the default)
  --quiet          Do not print the status line
  -h, --help       Show this help message

Notes:
  - --head N trims the saved text. --with-context and --show-context have no
    effect because this sink emits no answer.
  - An empty context and no facts are saved silently; `.stashed` then restores
    an empty state.
  - The stored text is the raw chain context (without the redaction legend a
    sink receives); personal data is already redacted as it accumulated.
  - Stashes live under ${XDG_STATE_HOME:-~/.local/state}/prompt/stashes, or
    $PROMPT_STASH_DIR when set.

Examples:
  prompt .tse foolad .tse.analyze .stash
  prompt .files src/a.cpp .stash review
  prompt .note "context" .stash --here
  prompt .note "more" .stash review            # append to `review`

See also: `.stashed` restores, `.stash.list`/`.stash.show`/`.stash.drop`
manage the store.

)EOF";
}

static void render_help_stash_list(std::ostream& os) {
    os << R"EOF(Usage: prompt stash.list [--here|--global] [--long] [--json] [--head N]

List the saved stashes, newest first. The effective copy of each name is marked
with `*`: a terminal-scoped stash shadows a global one of the same name.

Options:
  --here        Show this terminal's stashes only
  --global      Show the global stashes only
  -l, --long    Add the path, cwd, git root, terminal and flags
  --json        Print a JSON array of the full records
  --head N      Show at most N lines
  -h, --help    Show this help message

)EOF";
}

static void render_help_stash_show(std::ostream& os) {
    os << R"EOF(Usage: prompt stash.show [NAME] [--facts] [--json] [--here|--global] [--head N]

Print one stash's context. With no NAME, `default` is shown. The output is a
normal prompt result, so it is copied to the clipboard and can be piped or
chained like any other source.

Options:
  --facts       Append the saved facts after the context
  --json        Print the full record as JSON
  --here        Read this terminal's scope only
  --global      Read the global scope only
  --head N      Show at most N lines
  -h, --help    Show this help message

)EOF";
}

static void render_help_stash_drop(std::ostream& os) {
    os << R"EOF(Usage: prompt stash.drop [NAME] [--all] [--here|--global] [--dry-run] [--quiet]

Remove a saved stash. With no NAME, `default` is removed. Without a scope flag
the effective copy is removed (a terminal stash first, otherwise the global
one).

Options:
  --all         With NAME, remove both scopes; without NAME, remove every stash
                in the selected scope(s)
  --here        Operate on this terminal's scope only
  --global      Operate on the global scope only
  --dry-run     Print what would be removed without deleting it
  --quiet       Do not print the removed names
  -h, --help    Show this help message

)EOF";
}

bool const registered_stash = register_sink(
    "stash",
    "Save the whole chain state (context, facts and flags) so a later invocation can continue it with `.stashed`.",
    &execute_stash, &render_help_stash, &complete_stash_names, false, &preflight_stash);

bool const registered_stash_list =
    register_prompt("stash.list", "List the saved chain snapshots, newest first.", &execute_stash_list,
                    &render_help_stash_list, nullptr, chain_role::normal, true, nullptr, nullptr, false, false);

bool const registered_stash_show = register_prompt("stash.show", "Print one saved chain snapshot's context.",
                                                   &execute_stash_show, &render_help_stash_show, &complete_stash_names,
                                                   chain_role::normal, true, nullptr, nullptr, false, false);

bool const registered_stash_drop =
    register_prompt("stash.drop", "Remove a saved chain snapshot.", &execute_stash_drop, &render_help_stash_drop,
                    &complete_stash_names, chain_role::normal, true, nullptr, nullptr, false, false);

} // namespace prompt::prompts
