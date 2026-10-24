#pragma once

#include "facts.hpp"
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace prompt {

// Process-wide, built once at startup and never written again: where the repo
// is, where this executable lives, and the clipboard/file-picker services.
struct environment {
    std::filesystem::path git_root;
    std::filesystem::path exe_path;
    std::string (*clipboard_content)() = nullptr;
    std::optional<std::string> (*clipboard_identifier)() = nullptr;
    std::vector<std::filesystem::path> (*select_files)() = nullptr;
    std::optional<std::filesystem::path> (*resolve_input_file)(std::string_view) = nullptr;
};

// The live-terminal writers. A parallel or captured segment leaves the stream
// flags false, so write_stdout() only accumulates and the dispatcher emits the
// result in chain order; a streaming sink tees each chunk as it arrives.
struct stream_config {
    void (*stdout_writer)(std::string_view) = nullptr;
    void (*stderr_writer)(std::string_view) = nullptr;
    bool stream_stdout = false;
    bool stream_stderr = false;
};

// The shared, spend-once stdin. `piped()` says a pipe was attached;
// `available()` says nobody has read it yet; `text()` is empty once spent.
class stdin_source {
  public:
    void load(std::string content);

    [[nodiscard]] bool piped() const { return piped_; }
    [[nodiscard]] bool spent() const { return spent_; }
    [[nodiscard]] bool available() const { return piped_ && !spent_; }
    [[nodiscard]] std::string_view text() const { return spent_ ? std::string_view{} : content_; }

    void spend() { spent_ = true; }

  private:
    std::string content_;
    bool piped_ = false;
    bool spent_ = false;
};

// Everything a segment may read. All views point into dispatcher-owned storage
// that outlives the call; a segment never mutates chain state.
struct view {
    std::string_view name;
    std::span<std::string_view const> args;
    // The accumulated chain text before this segment (the sink barrier).
    std::string_view text;
    // Declarations for this batch plus the artifacts of completed segments.
    facts const& prior;
    environment const& env;
    stdin_source& in;
    // Default-constructed unless the dispatcher is streaming this segment, so a
    // segment only reads it and an in-process child inherits the defaults.
    stream_config stream{};
};

// Everything a segment may produce. The dispatcher sanitizes `out`, merges
// `published`, prints `err`/`stderr_buffer`, and applies the flags.
struct result {
    std::string out;
    std::string err;
    std::string stderr_buffer;
    facts published;
    bool no_clipboard = false;
    // Sink-only: drop the accumulated context after this sink succeeds
    // (`.clear`, and `.flush` once its file is written).
    bool clear = false;
    bool stdout_streamed = false;
    bool stderr_streamed = false;
};

// The cheap, argv/env-derived pre-pass: run in the dispatcher (the parent)
// before a batch forks, so parallel siblings agree on one identity. A hook must
// not print, must not touch the network, and must treat `args`/`environment`
// as its only inputs.
struct declaration {
    std::span<std::string_view const> args;
    environment const& env;
    facts const& prior;
    facts& declared;
};

// The dispatcher-injected services, as free functions taking the environment.
[[nodiscard]] std::string clipboard_content(environment const& env);
[[nodiscard]] std::optional<std::string> clipboard_identifier(environment const& env);
[[nodiscard]] std::vector<std::filesystem::path> select_files(environment const& env);
[[nodiscard]] std::optional<std::filesystem::path> resolve_input_file(environment const& env, std::string_view wanted);

// Run another registered prompt in-process with a fresh result. The child sees
// the parent's environment and facts (plus `extra_facts`); its own stdin is the
// optional `stdin_text`. Errors propagate to the caller as usual.
result run_sub_prompt(view const& parent, std::string_view name, std::span<std::string_view const> args,
                      std::optional<std::string_view> stdin_text = std::nullopt, facts const* extra_facts = nullptr);

} // namespace prompt
