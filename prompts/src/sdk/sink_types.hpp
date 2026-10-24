#pragma once

#include <cstddef>
#include <optional>

namespace prompt {

// A chain segment is either an ordinary prompt or a sink. Sinks run at a
// barrier: after every prompt before them, with the accumulated chain text as
// their input, and their answer becomes the text later prompts/sinks see.
// Contiguous sinks chain in argv order (each answer feeds the next).
enum class chain_role : unsigned char {
    normal,
    sink,
};

// What a sink does when the batch feeding it failed.
enum class sink_failure : unsigned char {
    abort,   // stop before running this sink
    proceed, // run with the partial context
    skip,    // skip this sink and keep the prior context
};

// The flags every sink accepts. The dispatcher parses them before it runs the
// batch that feeds the sink, applies `head_lines` to the context, and owns the
// failure/output policies, so a sink implementation never has to handle
// --on-error/--with-context/--show-context itself.
struct sink_options {
    sink_failure failure = sink_failure::abort;
    bool with_context = false;
    bool show_context = false;
    std::optional<std::size_t> head_lines;
};

} // namespace prompt
