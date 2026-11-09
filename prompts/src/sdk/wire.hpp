#pragma once

#include "chain_state.hpp"
#include "facts.hpp"
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace prompt {

// The dispatcher-side result of one segment: its `result` plus the status and
// the bookkeeping the prompt never sees. The fork wire is a codec of this type.
struct segment_outcome {
    std::string out;
    std::string err;
    std::string stderr_buffer;
    facts published;
    bool no_clipboard = false;
    bool clear = false;
    bool redacted = false;
    bool failed = false;
    bool stdin_consumed = false;
    bool stdout_streamed = false;
    bool stderr_streamed = false;
    // Sink-only: a whole chain snapshot (`.stashed`), the history merge kind it
    // asks for, and whether its output stays silent. Sinks run in the parent,
    // so `restore` never crosses the fork wire; the two flags do.
    std::optional<chain_state> restore;
    bool restore_append = false;
    bool silent = false;
    int exit_code = 0;
};

[[nodiscard]] std::string encode_outcome(segment_outcome const& outcome);
[[nodiscard]] std::optional<segment_outcome> decode_outcome(std::string_view blob);

// Read every fd until EOF, concurrently. Returns one blob per fd, in the order
// the fds were given. Closes each fd. A child that writes more than the pipe
// buffer no longer serializes the batch: poll() hands the parent whichever
// child is ready next.
[[nodiscard]] std::vector<std::string> drain_all(std::span<int const> fds);

} // namespace prompt
