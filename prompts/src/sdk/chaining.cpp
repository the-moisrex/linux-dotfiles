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
    auto find = [&](std::string_view name) -> prompt_descriptor const* {
        for (auto const& d : registry) {
            if (d.name == name) return &d;
        }
        return nullptr;
    };

    chain_result result;
    std::vector<std::string_view> current_args;
    std::vector<std::string_view> current_body;
    std::string_view pending;
    bool pending_name = false;
    std::size_t matched_names = 0; // bash's ${#_prompts[@]}

    // A body-taking invocation (`.each`) owns the raw tokens up to its matching
    // `.end`. `awaiting_body` collects the flags/selector into `args` until the
    // first dot token starts the body; `body_depth` then counts nested `.each`
    // openers so the outer `.end` is the one that closes this invocation.
    bool awaiting_body = false;
    bool in_body = false;
    int body_depth = 0;

    auto save_current = [&] {
        // Only save when a prompt name was actually matched; unmatched
        // leading tokens stay in current_args and go to the auto fallback.
        if (pending_name) {
            result.invocations.push_back(prompt_invocation{
                .name = pending,
                .args = std::move(current_args),
                .body = std::move(current_body),
            });
            current_args.clear();
            current_body.clear();
            pending_name = false;
            awaiting_body = false;
            in_body = false;
            body_depth = 0;
        }
    };

    for (std::size_t i = 1; i < argv.size(); ++i) {
        std::string_view arg{argv[i]};

        if (in_body) {
            if (arg == ".end") {
                if (--body_depth == 0) {
                    save_current();
                } else {
                    current_body.push_back(arg);
                }
                continue;
            }
            // A nested body opener keeps this body open for one more `.end`.
            if (arg.starts_with('.') && arg.size() > 1) {
                if (auto const* nested = find(arg.substr(1)); nested != nullptr && nested->takes_body) ++body_depth;
            }
            current_body.push_back(arg);
            continue;
        }

        if (awaiting_body) {
            // The body starts at the first dot token; everything before it is
            // the loop's own flags and selector.
            if (arg == ".end") {
                result.error = "prompt: unexpected .end";
                return result;
            }
            if (arg.starts_with('.') && arg.size() > 1) {
                in_body = true;
                body_depth = 1;
                if (auto const* first = find(arg.substr(1)); first != nullptr && first->takes_body) ++body_depth;
                current_body.push_back(arg);
                continue;
            }
            current_args.push_back(arg);
            continue;
        }

        // `.exec -- COMMAND ...` owns the rest of argv: the command may itself
        // be a `prompt` chain (`.exec -- prompt .tse …`), whose dotted names
        // must not split this chain.
        if (pending_name && pending == "exec" && arg == "--") {
            current_args.push_back(arg);
            for (++i; i < argv.size(); ++i) current_args.emplace_back(argv[i]);
            break;
        }

        std::string_view prompt_name;
        if (arg.starts_with('.') || (arg.starts_with('-') && arg.size() > 1 && !arg.starts_with("--") &&
                                     (std::isalnum(static_cast<unsigned char>(arg[1])) != 0))) {
            prompt_name = arg.substr(1);
        } else if (matched_names == 0 && !arg.empty() && std::isalnum(static_cast<unsigned char>(arg[0])) != 0) {
            // Only the first bare token can name a prompt; later bare words
            // are arguments.
            prompt_name = arg;
        }

        if (!prompt_name.empty()) {
            if (auto const* desc = find(prompt_name); desc != nullptr) {
                save_current();
                pending = prompt_name;
                pending_name = true;
                ++matched_names;
                awaiting_body = desc->takes_body;
                continue;
            }
        }
        if (arg == ".end") {
            result.error = "prompt: unexpected .end";
            return result;
        }
        current_args.push_back(arg);
    }

    if (in_body) {
        result.error = "prompt: ." + std::string(pending) + " body is missing .end";
        return result;
    }

    save_current();

    if (result.invocations.empty() && find("auto") != nullptr) {
        result.invocations.push_back(prompt_invocation{.name = "auto", .args = std::move(current_args), .body = {}});
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
