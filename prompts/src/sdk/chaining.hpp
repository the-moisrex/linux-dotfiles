#pragma once

#include "prompt_registry.hpp"
#include "sink_types.hpp"
#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace prompt {

struct prompt_invocation {
    std::string_view name;
    std::array<std::string_view, 16> args{};
    std::size_t args_count = 0;
};

struct chain_result {
    std::array<prompt_invocation, 8> invocations{};
    std::size_t count = 0;
};

chain_result parse_chain(std::span<char const* const> argv, std::span<prompt_descriptor const> registry);

// A maximal run of ordinary prompts or sinks, in argv order. Normal runs are
// executed as one batch (parallel when no stdin is pending); sink runs follow
// the sink_policy of their descriptors.
struct chain_run {
    std::size_t begin = 0;
    std::size_t end = 0;
    bool sink = false;
    sink_policy policy = sink_policy::chain;
};

struct chain_plan {
    std::vector<chain_run> runs;
    std::string error; // non-empty: the chain cannot be executed
};

// Partition a parsed chain into normal/sink runs and validate sink policies:
// contiguous sinks must all be chain or all fanout, and an exclusive sink may
// not share the chain with any other sink.
chain_plan plan_chain(chain_result const& chain, std::span<prompt_descriptor const> registry);

} // namespace prompt