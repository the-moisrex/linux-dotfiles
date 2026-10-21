#pragma once

#include "sink_types.hpp"
#include <array>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace prompt {

// One invocation in the chain, exposed to every prompt so related prompts can
// read their siblings' arguments (e.g. `tse.codal` borrowing `tse`'s symbol).
struct chain_segment {
    std::string_view name;
    std::vector<std::string_view> args;
};

struct prompt_context {
    // Name of the prompt being executed (set by the dispatcher; used by
    // legacy scripts for $PROMPT_NAME and sub-dispatch in auto).
    std::string_view name;
    std::array<std::string_view, 32> args{};
    std::size_t args_count = 0;
    std::optional<std::size_t> head_lines;
    std::string stdin_content;
    // Set when stdin was piped, and by a prompt that reads it: the dispatcher
    // spends the shared pipe once either flag is set (see main.cpp).
    bool stdin_consumed = false;
    std::filesystem::path git_root;
    // Path of the running executable (for locating repo bin/ utilities).
    std::filesystem::path exe_path;

    // Every prompt invocation in this chain, in command-line order, plus this
    // prompt's index in it. Prompts may consult neighbouring segments for
    // defaults (see `tse_data::chain_identifier`); the dispatcher only fills
    // these in and never reads them back.
    std::vector<chain_segment> chain;
    std::size_t chain_index = 0;

    // Sink/chain text accumulated before this segment. The dispatcher fills
    // this in; wait_for_prompts(ctx) exposes it to prompt code. chain_stderr
    // is the same barrier for streams that were kept out of the AI context.
    std::string_view chain_context;
    std::string_view chain_stderr;
    // Parsed common sink flags (only meaningful for sinks).
    sink_options sink;

    // Streaming services. A prompt appends to its own output with
    // write_stdout()/write_stderr(); these writers decide whether the chunk is
    // also flushed to the real terminal now. Parallel/captured segments leave
    // stream_* false, so the dispatcher can emit their output in chain order.
    void (*stdout_writer)(std::string_view) = nullptr;
    void (*stderr_writer)(std::string_view) = nullptr;
    bool stream_stdout = false;
    bool stream_stderr = false;

    std::string (*clipboard_content_fn)() = nullptr;
    std::optional<std::string> (*clipboard_identifier_fn)() = nullptr;
    std::vector<std::filesystem::path> (*select_files_fn)() = nullptr;
    std::optional<std::filesystem::path> (*resolve_input_file_fn)(std::string_view) = nullptr;

    // --- outputs ---------------------------------------------------------
    // A prompt fills these instead of returning a value: `out` is the prompt
    // text for stdout (sanitized, pipelined, clipboard), `err` is stderr on
    // the *success* path (bash's `>&2` output with exit 0), and the flags
    // tell the dispatcher how to treat the result. Failure does not use
    // them: it throws (see sdk/errors.hpp).
    std::string out;
    std::string err;
    bool no_clipboard = false;
    // stderr written through write_stderr(): kept separate from `err`, which
    // is the success-path text the dispatcher prints itself.
    std::string stderr_buffer;
    bool stdout_streamed = false;
    bool stderr_streamed = false;
};

// The dispatcher-injected services and the sub-dispatch reset, as free
// functions taking the context (the struct stays a plain data bag). These are
// the wrapper methods prompt code used to call as clipboard_content(ctx) etc.
void clear_output(prompt_context& ctx);

[[nodiscard]] std::string clipboard_content(prompt_context const& ctx);

[[nodiscard]] std::optional<std::string> clipboard_identifier(prompt_context const& ctx);

[[nodiscard]] std::vector<std::filesystem::path> select_files(prompt_context const& ctx);

[[nodiscard]] std::optional<std::filesystem::path> resolve_input_file(prompt_context const& ctx,
                                                                      std::string_view wanted);

prompt_context make_base_context();

} // namespace prompt