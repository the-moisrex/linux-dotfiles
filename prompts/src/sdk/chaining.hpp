#pragma once

#include "prompt_registry.hpp"
#include "sink_types.hpp"
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace prompt {

struct prompt_invocation {
    std::string_view name;
    std::vector<std::string_view> args;
};

struct chain_result {
    std::vector<prompt_invocation> invocations;
};

// No fixed limits: the arguments are views into argv, so an arbitrary chain
// costs one small vector per invocation and nothing is silently truncated.
chain_result parse_chain(std::span<char const* const> argv, std::span<prompt_descriptor const> registry);

// A maximal run of ordinary prompts or sinks, in argv order. A normal run is
// one batch (parallel when no stdin is pending); a sink run chains its sinks
// in order, each answer becoming the next sink's context.
struct chain_run {
    std::size_t begin = 0;
    std::size_t end = 0;
    bool sink = false;
};

struct chain_plan {
    std::vector<chain_run> runs;
    std::string error; // non-empty: the chain cannot be executed
};

// Partition a parsed chain into maximal normal/sink runs.
chain_plan plan_chain(chain_result const& chain, std::span<prompt_descriptor const> registry);

} // namespace prompt
