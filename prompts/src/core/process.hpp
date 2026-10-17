#pragma once

#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::process {

struct command_result {
    std::string stdout_data;
    std::string stderr_data;
    int exit_code = 0;
    bool timed_out = false;
    // Set by run_prompt_script when the script opted out of the clipboard
    // via $PROMPT_NO_CLIPBOARD_FILE (bash dispatcher parity).
    bool no_clipboard = false;
};

// How a child's stderr is handled.
enum class stderr_mode : std::uint8_t {
    inherit,   // the child writes straight to our stderr — what bash's `$(...)`
               // capture does (the tool's diagnostics appear as they run).
    capture,   // piped into command_result::stderr_data.
    to_stdout, // `2>&1` — merged into command_result::stdout_data.
    discard,   // `2>/dev/null` — thrown away.
};

// A subprocess, built fluently:
//
//     process::run_command({"git", "status"}, stdin_data, timeout);
//     process::command{"bash", "-c", "echo hi"}.capture_stderr().run();
//
// `argv` is executed directly (execvp — no shell, no quoting games: an
// argument containing spaces, quotes or newlines reaches the child intact).
// Use command::shell() when the work genuinely needs /bin/bash semantics
// (pipes, globs, $(...) substitution).
class command {
  public:
    command() = default;
    command(std::initializer_list<std::string> argv) : argv_(argv) {}
    explicit command(std::vector<std::string> argv) : argv_(std::move(argv)) {}

    command& arg(std::string_view a);
    command& arg(std::string const& a);
    command& args(std::vector<std::string> const& as);
    command& stdin_data(std::string data);
    command& env(std::vector<std::string> const& extra); // "KEY=VALUE" entries
    command& timeout(std::chrono::milliseconds t);
    command& stderr(stderr_mode mode);
    command& capture_stderr();   // stderr_mode::capture
    command& stderr_to_stdout(); // stderr_mode::to_stdout
    command& inherit_stderr();   // stderr_mode::inherit (default)
    command& discard_stderr();   // stderr_mode::discard (`2>/dev/null`)

    command_result run();

  private:
    std::vector<std::string> argv_;
    std::string stdin_data_;
    std::vector<std::string> env_;
    std::chrono::milliseconds timeout_{0};
    stderr_mode stderr_mode_{stderr_mode::inherit};
};

// Runs a bash script snippet (`/bin/bash -c <script>`), with "extras..." passed
// as $0 $1 ... like prompt scripts do. This is the escape hatch: prefer argv
// construction above unless the command needs a real shell.
command_result run_script(std::string const& script, std::vector<std::string> const& extras = {},
                          std::string_view stdin_data = {},
                          std::chrono::milliseconds timeout = std::chrono::seconds(30));

command_result run_command(std::span<char const* const> argv, std::string_view stdin_data = {},
                           std::span<char const* const> env = {},
                           std::chrono::milliseconds timeout = std::chrono::seconds(30));

command_result run_command(std::vector<std::string> const& argv, std::string_view stdin_data = {},
                           std::vector<std::string> const& env = {},
                           std::chrono::milliseconds timeout = std::chrono::seconds(30));

} // namespace prompt::process