#include "chaining.hpp"
#include "prompt_registry.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace prompt {

chain_result parse_chain(std::span<char const* const> argv, std::span<prompt_descriptor const> registry) {
    auto known = [&](std::string_view name) -> bool {
        return std::ranges::any_of(registry, [&](auto const& d) { return d.name == name; });
    };

    chain_result result;
    std::array<std::string_view, 16> current_args{};
    std::size_t current_args_count = 0;
    bool pending_name = false;
    std::size_t matched_names = 0; // bash's ${#_prompts[@]}

    auto save_current = [&] {
        // Only save when a prompt name was actually matched; unmatched
        // leading tokens stay in current_args and go to the auto fallback.
        if (pending_name) {
            if (result.count < result.invocations.size()) {
                result.invocations[result.count].args = current_args;
                result.invocations[result.count].args_count = current_args_count;
                result.count++;
            }
            current_args_count = 0;
            pending_name = false;
        }
    };

    for (std::size_t i = 1; i < argv.size(); ++i) {
        std::string_view arg{argv[i]};

        std::string_view prompt_name;
        if (arg.starts_with('.') || (arg.starts_with('-') && arg.size() > 1 && !arg.starts_with("--") &&
                                     (std::isalnum(static_cast<unsigned char>(arg[1])) != 0))) {
            prompt_name = arg.substr(1);
        } else if (matched_names == 0 && !arg.empty() && std::isalnum(static_cast<unsigned char>(arg[0])) != 0) {
            // Only the first bare token can name a prompt; later bare words
            // are arguments (the bash dispatcher's `elif [[ ${#_prompts[@]} -eq 0 ]]`).
            prompt_name = arg;
        }

        if (!prompt_name.empty() && known(prompt_name)) {
            save_current();
            if (result.count < result.invocations.size()) {
                result.invocations[result.count].name = prompt_name;
            }
            pending_name = true;
            ++matched_names;
        } else {
            if (current_args_count < current_args.size()) {
                current_args[current_args_count++] = arg;
            }
        }
    }

    save_current();

    if (result.count == 0 && known("auto")) {
        result.invocations[0].name = "auto";
        result.invocations[0].args = current_args;
        result.invocations[0].args_count = current_args_count;
        result.count = 1;
    }

    return result;
}

chain_plan plan_chain(chain_result const& chain, std::span<prompt_descriptor const> registry) {
    chain_plan plan;
    std::vector<prompt_descriptor const*> descriptors(chain.count, nullptr);
    std::size_t sink_count = 0;
    for (std::size_t i = 0; i < chain.count; ++i) {
        for (auto const& d : registry) {
            if (d.name == chain.invocations[i].name) {
                descriptors[i] = &d;
                break;
            }
        }
        if (descriptors[i] == nullptr) {
            plan.error = "prompt '" + std::string(chain.invocations[i].name) + "' not found";
            return plan;
        }
        if (descriptors[i]->role == chain_role::sink) ++sink_count;
    }

    bool has_exclusive = false;
    for (std::size_t i = 0; i < chain.count; ++i) {
        if (descriptors[i]->role == chain_role::sink && descriptors[i]->policy == sink_policy::exclusive) {
            has_exclusive = true;
        }
    }
    if (has_exclusive && sink_count > 1) {
        plan.error = "an exclusive sink cannot share the chain with another sink";
        return plan;
    }

    std::size_t i = 0;
    while (i < chain.count) {
        bool const sink = descriptors[i]->role == chain_role::sink;
        std::size_t j = i + 1;
        while (j < chain.count && (descriptors[j]->role == chain_role::sink) == sink) ++j;

        chain_run run{.begin = i, .end = j, .sink = sink, .policy = sink_policy::chain};
        if (sink) {
            bool has_chain = false;
            bool has_fanout = false;
            bool run_exclusive = false;
            for (std::size_t k = i; k < j; ++k) {
                has_chain = has_chain || descriptors[k]->policy == sink_policy::chain;
                has_fanout = has_fanout || descriptors[k]->policy == sink_policy::fanout;
                run_exclusive = run_exclusive || descriptors[k]->policy == sink_policy::exclusive;
            }
            if (has_chain && has_fanout) {
                plan.error = "cannot mix chained and fan-out sinks in one run";
                return plan;
            }
            if (run_exclusive) {
                run.policy = sink_policy::exclusive;
            } else if (has_fanout) {
                run.policy = sink_policy::fanout;
            } else {
                run.policy = sink_policy::chain;
            }
        }
        plan.runs.push_back(run);
        i = j;
    }
    return plan;
}

} // namespace prompt
