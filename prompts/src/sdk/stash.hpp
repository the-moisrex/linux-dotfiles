#pragma once

#include "facts.hpp"
#include "terminal.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::stash {

// A saved chain snapshot: the accumulated text, the effective facts, the
// redaction/failure flags and the provenance needed to explain it later. The
// store is one JSON file per stash, written atomically.
//
// The scope is either global (`<root>/<name>.json`) or terminal-scoped
// (`<root>/terminals/<scope>/<name>.json`). A lookup with no explicit scope
// checks the terminal store first, so a `--here` stash shadows a global one.
struct record {
    std::string name;
    std::string created;
    std::string updated;
    long long created_ts = 0;
    long long updated_ts = 0;
    std::string cwd;
    std::string git_root;
    terminal::id terminal;
    std::string source_run;
    bool redacted = false;
    bool incomplete = false;
    bool facts_redacted = false;
    facts known;
    std::string text;

    // Runtime fields, set by load/list; never part of the stored JSON.
    std::filesystem::path path;
    bool here = false;
};

// The store root: $PROMPT_STASH_DIR, else $XDG_STATE_HOME/prompt/stashes, else
// $HOME/.local/state/prompt/stashes. Empty when none can be determined.
std::filesystem::path root();

// The directory for one scope (global or terminal-scoped).
std::filesystem::path directory(bool here);

// The file a stash would use in one scope.
std::filesystem::path path_for(std::string_view name, bool here);

// A stash name is a single path component: non-empty, at most 200 bytes, not
// "." or "..", not starting with '-', and free of '/', '\\', NUL and control
// bytes. UTF-8 is allowed.
bool valid_name(std::string_view name);

// The path a lookup with `here` would use. nullopt means the effective scope
// (terminal first, then global); when neither exists the global path is
// returned so an error can name it.
std::filesystem::path resolve_path(std::string_view name, std::optional<bool> here);

// Load one stash. `here`: nullopt = effective (terminal first, then global),
// true = terminal, false = global. On failure `error` is a one-line message.
std::optional<record> load(std::string_view name, std::optional<bool> here, std::string& error);

// Write `value` atomically. `append` merges into an existing stash (text
// appended, facts current-wins, flags OR, `created` kept); `no_clobber` refuses
// an existing file. The two are mutually exclusive; the caller validates that.
bool save(record const& value, bool here, bool append, bool no_clobber, std::string& error);

// Metadata for every stash in the selected scope(s). Malformed files are
// reported in `errors` and skipped. `here`: nullopt = both scopes, true =
// terminal only, false = global only.
std::vector<record> list(std::optional<bool> here, std::vector<std::string>& errors);

// Remove the effective/selected stash.
bool remove(std::string_view name, std::optional<bool> here, std::string& error);

// Create the scope directory and check that it is writable, for a preflight.
bool ensure_directory(bool here, std::string& error);

// The record as the store's JSON (no runtime path/scope fields), pretty
// printed.
std::string encode(record const& value);

// The record as JSON plus its runtime `path` and `scope`, for `.stash.show
// --json` and `.stash.list --json`.
std::string display_json(record const& value);

// The timestamps a new snapshot carries (`YYYY-MM-DDTHH:MM:SS` local time and
// epoch seconds), shared by the stash prompt and the store.
std::string now_iso();
long long now_ts();

} // namespace prompt::stash
