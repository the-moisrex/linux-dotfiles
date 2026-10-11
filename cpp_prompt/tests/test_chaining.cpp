#include "prompt/sdk/chaining.hpp"
#include "prompt/sdk/prompt_registry.hpp"
#include <cassert>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>

using namespace prompt;

namespace {

int failures = 0;

void check(bool cond, char const* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

void test_bare_prompt_name() {
    // Regression: a bare "prompt fix" (no args) must select "fix", not fall
    // back to "auto".
    char const* argv[] = {"prompt", "fix"};
    auto chain = parse_chain(std::span<char const* const>(argv, 2), get_all_prompts());
    check(chain.count == 1, "bare name: one invocation");
    check(chain.count == 1 && chain.invocations[0].name == "fix", "bare name resolves to fix");
}

void test_prompt_with_args() {
    char const* argv[] = {"prompt", "fix", "--head", "10"};
    auto chain = parse_chain(std::span<char const* const>(argv, 4), get_all_prompts());
    check(chain.count == 1, "args: one invocation");
    check(chain.count == 1 && chain.invocations[0].name == "fix", "args: name is fix");
    check(chain.count == 1 && chain.invocations[0].args_count == 2, "args: --head 10 kept");
    check(chain.count == 1 && chain.invocations[0].args[0] == "--head", "args: --head first");
}

void test_chain_two_prompts() {
    char const* argv[] = {"prompt", "stock", "فولاد", ".note", "compare"};
    auto chain = parse_chain(std::span<char const* const>(argv, 5), get_all_prompts());
    check(chain.count == 2, "chain: two invocations");
    check(chain.count == 2 && chain.invocations[0].name == "stock", "chain: first is stock");
    check(chain.count == 2 && chain.invocations[1].name == "note", "chain: second is note");
    check(chain.count == 2 && chain.invocations[1].args_count >= 1, "chain: note has args");
}

void test_unknown_defaults_to_auto() {
    char const* argv[] = {"prompt"};
    auto chain = parse_chain(std::span<char const* const>(argv, 1), get_all_prompts());
    check(chain.count == 1, "empty argv: one invocation");
    check(chain.count == 1 && chain.invocations[0].name == "auto", "empty argv: defaults to auto");
}

void test_all_prompts_registered() {
    // Every prompt that the bash dispatcher served must be a native,
    // executable prompt in this build (no script fallback anymore).
    check(find_prompt("summarize") != nullptr, "summarize is registered");
    check(find_prompt("explain") != nullptr, "explain is registered");
    check(find_prompt("git.worktree.files") != nullptr, "dotted name registered");

    auto const* summarize = find_prompt("summarize");
    if (summarize) {
        check(!summarize->help_summary.empty(), "summarize has a help summary");
        check(summarize->execute_fn != nullptr, "summarize has an executor");
    }

    // The whole bash catalog (75 prompt files today) is native.
    check(get_all_prompts().size() >= 75, "registry includes every prompt file");

    for (auto const& desc : get_all_prompts()) {
        std::string exec_msg = "prompt has an executor: " + std::string(desc.name);
        std::string summary_msg = "prompt has a summary: " + std::string(desc.name);
        check(desc.execute_fn != nullptr, exec_msg.c_str());
        check(!desc.help_summary.empty(), summary_msg.c_str());
    }

    // Dotted names resolve through the chain parser directly.
    char const* argv[] = {"prompt", "explain"};
    auto chain = parse_chain(std::span<char const* const>(argv, 2), get_all_prompts());
    check(chain.count == 1, "dotted name: one invocation");
    check(chain.count == 1 && chain.invocations[0].name == "explain", "dotted name resolves to explain, not auto");
}

} // namespace

int main() {
    init_prompt_registry();

    test_bare_prompt_name();
    test_prompt_with_args();
    test_chain_two_prompts();
    test_unknown_defaults_to_auto();
    test_all_prompts_registered();

    if (failures == 0) {
        std::printf("all chaining tests passed\n");
        return 0;
    }
    return 1;
}
