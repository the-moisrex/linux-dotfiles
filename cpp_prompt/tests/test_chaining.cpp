#include "prompt/sdk/chaining.hpp"
#include "prompt/sdk/prompt_registry.hpp"
#include <cassert>
#include <cstdio>
#include <span>
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

} // namespace

int main() {
    init_prompt_registry();

    test_bare_prompt_name();
    test_prompt_with_args();
    test_chain_two_prompts();
    test_unknown_defaults_to_auto();

    if (failures == 0) {
        std::printf("all chaining tests passed\n");
        return 0;
    }
    return 1;
}
