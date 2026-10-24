#include "chaining.hpp"
#include "prompt_registry.hpp"
#include <algorithm>
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
    std::vector<std::string_view> current_args;
    std::string_view pending;
    bool pending_name = false;
    std::size_t matched_names = 0; // bash's ${#_prompts[@]}

    auto save_current = [&] {
        // Only save when a prompt name was actually matched; unmatched
        // leading tokens stay in current_args and go to the auto fallback.
        if (pending_name) {
            result.invocations.push_back(prompt_invocation{.name = pending, .args = std::move(current_args)});
            current_args.clear();
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
            // are arguments.
            prompt_name = arg;
        }

        if (!prompt_name.empty() && known(prompt_name)) {
            save_current();
            pending = prompt_name;
            pending_name = true;
            ++matched_names;
        } else {
            current_args.push_back(arg);
        }
    }

    save_current();

    if (result.invocations.empty() && known("auto")) {
        result.invocations.push_back(prompt_invocation{.name = "auto", .args = std::move(current_args)});
    }

    return result;
}

chain_plan plan_chain(chain_result const& chain, std::span<prompt_descriptor const> registry) {
    chain_plan plan;
    std::vector<prompt_descriptor const*> descriptors(chain.invocations.size(), nullptr);
    for (std::size_t i = 0; i < chain.invocations.size(); ++i) {
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
    }

    std::size_t i = 0;
    while (i < chain.invocations.size()) {
        bool const sink = descriptors[i]->role == chain_role::sink;
        std::size_t j = i + 1;
        while (j < chain.invocations.size() && (descriptors[j]->role == chain_role::sink) == sink) ++j;
        plan.runs.push_back(chain_run{.begin = i, .end = j, .sink = sink});
        i = j;
    }
    return plan;
}

} // namespace prompt
