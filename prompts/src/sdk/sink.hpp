#pragma once

#include "../core/process.hpp"
#include "prompt_context.hpp"
#include "sink_types.hpp"
#include <chrono>
#include <iosfwd>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace prompt {

// A sink invocation after the shared flags have been removed. `rest` is what
// the sink's own implementation (or the tool it calls) should see.
struct sink_args {
    sink_options options;
    std::vector<std::string_view> rest;
    std::string error; // empty on success
};

// Parse every sink flag the dispatcher owns. Unknown arguments are copied to
// rest untouched, so provider/sink-specific flags do not need to be listed
// here. Stops looking for sink flags after `--`.
sink_args parse_sink_args(std::span<std::string_view const> args);

// The common Options: block every sink appends to its own help.
void render_sink_help(std::ostream& out);

// The chain text accumulated before this segment. The dispatcher already ran
// every earlier batch, so this is a barrier result rather than a blocking wait.
[[nodiscard]] std::string_view wait_for_prompts(prompt_context const& ctx);

// The `--head N` helper the dispatcher applies to every sink (so the flag is
// genuinely shared): the first `limit` lines, including their newline; limit 0
// yields an empty view. It is applied before the redaction legend is prepended.
[[nodiscard]] std::string_view trim_context_lines(std::string_view text, std::size_t limit);

// Stream a chunk to the real terminal (when the dispatcher enabled streaming)
// and retain it: stdout in ctx.out for the next sink, stderr in a separate
// buffer that is never part of the AI chain context automatically.
void write_stdout(prompt_context& ctx, std::string_view chunk);
void write_stderr(prompt_context& ctx, std::string_view chunk);

// Run a tool-backed sink: stdout goes through write_stdout, stderr through
// write_stderr. The command still returns its captured result as usual.
process::command_result run_sink_tool(prompt_context& ctx, std::vector<std::string> argv, std::string stdin_data,
                                      std::chrono::milliseconds timeout = std::chrono::milliseconds{0});

} // namespace prompt
