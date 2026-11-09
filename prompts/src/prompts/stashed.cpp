#include "../core/strings.hpp"
#include "../sdk/completion.hpp"
#include "../sdk/errors.hpp"
#include "../sdk/prompt_registry.hpp"
#include "../sdk/sink.hpp"
#include "../sdk/stage.hpp"
#include "../sdk/stash.hpp"
#include <algorithm>
#include <format>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::prompts {

static void render_help_stashed(std::ostream& os);

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

std::string join_names(std::vector<std::string> const& names) {
    std::string out;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i != 0) out += ", ";
        out += names[i];
    }
    return out;
}

struct restore_config {
    std::vector<std::string> names;
    bool merge = false;
    bool text_only = false;
    bool clean = false;
    bool quiet = false;
    std::optional<bool> here;
};

restore_config parse_restore_config(view const& v) {
    restore_config cfg;
    bool options = true;
    for (std::string_view const arg : v.args) {
        if (options && arg == "--") {
            options = false;
            continue;
        }
        if (options && arg == "--merge") {
            cfg.merge = true;
            continue;
        }
        if (options && arg == "--text-only") {
            cfg.text_only = true;
            continue;
        }
        if (options && arg == "--clean") {
            cfg.clean = true;
            continue;
        }
        if (options && arg == "--quiet") {
            cfg.quiet = true;
            continue;
        }
        if (options && parse_scope(arg, cfg.here)) continue;
        if (options && arg.starts_with('-')) {
            throw usage_error{
                std::format("prompt stashed: unknown option: {}\n{}", arg, help_text(&render_help_stashed))};
        }
        cfg.names.emplace_back(arg);
    }
    if (cfg.names.empty()) cfg.names.emplace_back("default");
    for (auto const& name : cfg.names) {
        if (!stash::valid_name(name)) {
            throw usage_error{std::format("prompt stashed: invalid stash name: {}\n", name)};
        }
    }
    return cfg;
}

void complete_stash_names(completion_request const& req, std::ostream& os) {
    std::vector<std::string> errors;
    auto const records = stash::list(std::nullopt, errors);
    std::vector<std::string> seen;
    for (auto const& rec : records) {
        if (!completion::matches(req.prefix, rec.name)) continue;
        bool const duplicate = std::find(seen.begin(), seen.end(), rec.name) != seen.end();
        if (duplicate) continue;
        seen.push_back(rec.name);
        completion::write_item(os, rec.name, rec.here ? "here" : "global");
    }
}

void execute_stashed(view const& v, result& r) {
    auto const cfg = parse_restore_config(v);

    stash::record combined;
    combined.name = cfg.names.back();
    for (auto const& name : cfg.names) {
        std::string error;
        auto rec = stash::load(name, cfg.here, error);
        if (!rec) throw soft_error{std::format("prompt stashed: {}\n", error)};
        text::append_block(combined.text, rec->text);
        combined.known.merge(rec->known, true);
        combined.redacted = combined.redacted || rec->redacted;
        combined.incomplete = combined.incomplete || rec->incomplete;
        combined.facts_redacted = combined.facts_redacted || rec->facts_redacted;
    }
    if (cfg.clean) combined.incomplete = false;
    if (v.head) combined.text = std::string(trim_context_lines(combined.text, *v.head));

    chain_state restored;
    restored.text = combined.text;
    restored.redacted = combined.redacted;
    restored.incomplete = combined.incomplete;
    if (cfg.text_only) {
        if (v.chain != nullptr) restored.known = v.chain->known;
    } else {
        restored.known = combined.known;
    }

    if (cfg.merge && v.chain != nullptr) {
        chain_state merged = *v.chain;
        text::append_block(merged.text, combined.text);
        if (!cfg.text_only) merged.known.merge(combined.known, true);
        merged.redacted = merged.redacted || combined.redacted;
        merged.incomplete = cfg.clean ? false : (merged.incomplete || combined.incomplete);
        r.restore = std::move(merged);
        r.restore_append = true;
    } else {
        r.restore = std::move(restored);
        r.restore_append = false;
    }

    r.out = std::move(combined.text);
    r.silent = cfg.quiet;
    r.no_clipboard = cfg.quiet;
    if (!cfg.quiet) {
        r.err = std::format("prompt stashed: loaded {} ({} bytes, {} facts)\n", join_names(cfg.names), r.out.size(),
                            combined.known.size());
    }
}

} // namespace

static void render_help_stashed(std::ostream& os) {
    os << R"EOF(Usage: prompt <other prompts...> .stashed [NAME...] [--merge] [--text-only] [--clean] [--here|--global] [--quiet]

Restore a saved chain snapshot: its context becomes the chain context, its facts
become the effective facts, and its redaction/failure flags are restored too, so
later segments behave as if they had run in the original invocation.

With no NAME, `default` is restored. Several names are merged in the given order
(text appended, facts last-writer-wins). A lookup checks this terminal's scope
first and falls back to the global one; `--here`/`--global` force one scope.

By default the restored context replaces the current one and is printed. The
sink is therefore also a source: `.stashed` alone prints the saved context.

)EOF";
    render_sink_help(os);
    os << R"EOF(
Restore options:
  --merge       Append the restored context to the current one instead of
                replacing it (the current context is kept first)
  --text-only   Restore the text but keep the current effective facts
  --clean       Clear the saved failure taint, so later sinks do not refuse to
                run because of a failure recorded in the original invocation
  --here        Read this terminal's scope only
  --global      Read the global scope only
  --quiet       Restore silently (do not print the context or the status line)
  -h, --help    Show this help message

Notes:
  - --head N trims the restored context. --with-context and --show-context have
    no effect because the snapshot already carries the context.

Examples:
  prompt .stashed
  prompt .stashed review
  prompt .stashed review tests
  prompt .stashed --quiet .note "now compare" .ai.dseek
  prompt .note "new section" .stashed --merge .ai.grok

See also: `.stash` saves, `.stash.list`/`.stash.show`/`.stash.drop` manage the
store.

)EOF";
}

bool const registered_stashed = register_sink(
    "stashed", "Restore a saved chain snapshot (context, facts and flags) so this invocation continues it.",
    &execute_stashed, &render_help_stashed, &complete_stash_names, false, nullptr, nullptr, false, true, nullptr, true);

} // namespace prompt::prompts
