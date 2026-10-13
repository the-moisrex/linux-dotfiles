#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

namespace prompt {

// What a successful prompt execution returns: the text for stdout (sanitized
// and pipelined by the dispatcher), optional diagnostics for stderr (bash's
// `>&2` output on the success path — exit code stays 0), and the two flags
// the dispatcher needs.
struct prompt_output {
    std::string text;
    std::string error;
    bool no_clipboard = false;
    bool stdin_consumed = false;
};

// A failing prompt throws this; the dispatcher catches it once and turns it
// back into exactly what the bash dispatcher produced: `message` on stderr
// (verbatim, unsanitized, including its trailing newline), `partial_output`
// prepended to stdout (some prompts print their prompt text *and* fail), and
// `exit_code` as the process status.
//
// The chain keeps running after a failure — the first non-zero status wins,
// exactly like the bash loop.
class prompt_error : public std::runtime_error {
  public:
    prompt_error(int exit_code, std::string message, std::string partial_output = {}, bool no_clipboard = false,
                 bool stdin_consumed = false)
        : std::runtime_error(message), exit_code_(exit_code), partial_(std::move(partial_output)),
          no_clipboard_(no_clipboard), stdin_consumed_(stdin_consumed) {}

    [[nodiscard]] int exit_code() const { return exit_code_; }
    [[nodiscard]] std::string const& partial_output() const { return partial_; }
    [[nodiscard]] bool no_clipboard() const { return no_clipboard_; }
    [[nodiscard]] bool stdin_consumed() const { return stdin_consumed_; }

  private:
    int exit_code_;
    std::string partial_;
    bool no_clipboard_;
    bool stdin_consumed_;
};

// Argument/usage failures (bash `exit 2`) — 89 call sites.
class usage_error : public prompt_error {
  public:
    explicit usage_error(std::string message, std::string partial_output = {}, bool no_clipboard = false,
                         bool stdin_consumed = false)
        : prompt_error(2, std::move(message), std::move(partial_output), no_clipboard, stdin_consumed) {}
};

// Runtime failures (bash `exit 1`).
class soft_error : public prompt_error {
  public:
    explicit soft_error(std::string message, std::string partial_output = {}, bool no_clipboard = false,
                        bool stdin_consumed = false)
        : prompt_error(1, std::move(message), std::move(partial_output), no_clipboard, stdin_consumed) {}
};

} // namespace prompt
