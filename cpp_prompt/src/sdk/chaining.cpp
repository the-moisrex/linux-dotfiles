#include "prompt/sdk/chaining.hpp"
#include "prompt/sdk/prompt_registry.hpp"
#include <array>
#include <cctype>
#include <span>
#include <string_view>

namespace prompt {

chain_result parse_chain(std::span<char const* const> argv, std::span<prompt_descriptor const> registry) noexcept {

    auto known = [&](std::string_view name) -> bool {
        for (auto const& d : registry) {
            if (d.name == name) return true;
        }
        return false;
    };

    chain_result result;
    std::array<std::string_view, 16> current_args{};
    std::size_t current_args_count = 0;
    bool pending_name = false;
    std::size_t matched_names = 0; // bash's ${#_prompts[@]}

    auto save_current = [&]() {
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
            // are arguments (bin/prompt's `elif [[ ${#_prompts[@]} -eq 0 ]]`).
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

} // namespace prompt
