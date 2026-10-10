#pragma once

#include "prompt/core/process.hpp"
#include <filesystem>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::legacy {

// Prompt search directories mirroring the bash dispatcher's search order:
// each $XDG_CONFIG_DIRS entry's prompts/ subdir, then the repo prompts/
// directories relative to this executable.
std::vector<std::filesystem::path> search_dirs();

// Run a legacy prompt script (prompts/<name>.sh/.txt/.md or bare name)
// directly, bypassing the prompt dispatcher to avoid recursion. Mirrors the
// bash dispatcher: .sh runs under bash with the prompt's args and stdin;
// .txt/.md prints the file then appends stdin (args are ignored for those).
// Exports $PROMPT_NAME, $PROMPT_OUTPUT_TTY and $PROMPT_NO_CLIPBOARD_FILE
// like the bash dispatcher and reports a no-clipboard opt-out in the
// result's no_clipboard flag.
process::command_result run_prompt_script(std::string_view name, std::string_view stdin_data,
                                          std::span<char const* const> args) noexcept;

// Render a legacy prompt's help output: `bash <file> --help` for .sh (the
// bash dispatcher passes --help through to the script), the file contents
// for .txt/.md (args are ignored for those).
void render_script_help(std::filesystem::path const& file, std::ostream& os) noexcept;

// The prompt's show_help() text as a string (with its trailing newline),
// for prompts whose bash script prints it to stderr on a usage error
// (`show_help >&2`). Empty when no help source exists.
std::string help_text(std::string_view name) noexcept;

// The resolved prompt file for a name (search order like the bash
// dispatcher), or an empty path when there is none.
std::filesystem::path prompt_file(std::string_view name) noexcept;

// The prompts/ directory holding _common.sh — scripts that need their own
// directory (new.sh embeds its siblings) resolve it the same way.
std::filesystem::path prompts_dir() noexcept;

struct static_help {
    bool found = false;  // a help source was found
    std::string summary; // first help paragraph, joined to one line
    std::string full;    // full help text, no trailing newlines
};

// Statically extract a prompt script's show_help() heredoc without running
// it (C++ port of prompts/_extract-help.awk). .txt/.md files have no
// show_help — their contents are the help. When a .sh has no static
// heredoc, falls back to running `bash <file> --help` (parity with the
// bash list prompt).
static_help extract_static_help(std::filesystem::path const& file) noexcept;

} // namespace prompt::legacy
