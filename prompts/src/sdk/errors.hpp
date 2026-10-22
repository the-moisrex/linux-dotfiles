#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

namespace prompt {

// A failing prompt throws this; the dispatcher catches it once and turns it
// back into the process result: `message` on stderr (verbatim, unsanitized,
// including its trailing newline), `partial_output` prepended to stdout (some
// prompts print their prompt text *and* fail), and `exit_code` as the process
// status.
//
// The chain keeps running after a failure — the first non-zero status wins.
class prompt_error : public std::runtime_error {
  public:
    // NOLINTNEXTLINE(performance-unnecessary-value-param) — by value on purpose: callers std::move the message in.
    prompt_error(int exit_code, std::string message, std::string partial_output = {}, bool no_clipboard = false,
                 bool stdin_consumed = false)
        // runtime_error's ctor takes a const& and copies; the message is
        // consumed there, partial_ still moves. NOLINT: no way to move into it.
        : std::runtime_error(std::move(message)), // NOLINT(performance-move-const-arg)
          exit_code_(exit_code), partial_(std::move(partial_output)), no_clipboard_(no_clipboard),
          stdin_consumed_(stdin_consumed) {}

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

// Argument/usage failures (bash `exit 2`).
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
