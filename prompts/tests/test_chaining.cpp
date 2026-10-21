#include "../src/prompts/tse_data.hpp"
#include "../src/sdk/chaining.hpp"
#include "../src/sdk/prompt_registry.hpp"
#include <array>
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
    std::array<char const*, 2> argv = {"prompt", "fix"};
    auto chain = parse_chain(std::span<char const* const>(argv.data(), argv.size()), get_all_prompts());
    check(chain.count == 1, "bare name: one invocation");
    check(chain.count == 1 && chain.invocations[0].name == "fix", "bare name resolves to fix");
}

void test_prompt_with_args() {
    std::array<char const*, 4> argv = {"prompt", "fix", "--head", "10"};
    auto chain = parse_chain(std::span<char const* const>(argv.data(), argv.size()), get_all_prompts());
    check(chain.count == 1, "args: one invocation");
    check(chain.count == 1 && chain.invocations[0].name == "fix", "args: name is fix");
    check(chain.count == 1 && chain.invocations[0].args_count == 2, "args: --head 10 kept");
    check(chain.count == 1 && chain.invocations[0].args[0] == "--head", "args: --head first");
}

void test_chain_two_prompts() {
    std::array<char const*, 5> argv = {"prompt", "tse", "فولاد", ".note", "compare"};
    auto chain = parse_chain(std::span<char const* const>(argv.data(), argv.size()), get_all_prompts());
    check(chain.count == 2, "chain: two invocations");
    check(chain.count == 2 && chain.invocations[0].name == "tse", "chain: first is tse");
    check(chain.count == 2 && chain.invocations[1].name == "note", "chain: second is note");
    check(chain.count == 2 && chain.invocations[1].args_count >= 1, "chain: note has args");
}

void test_unknown_defaults_to_auto() {
    std::array<char const*, 1> argv = {"prompt"};
    auto chain = parse_chain(std::span<char const* const>(argv.data(), argv.size()), get_all_prompts());
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
    if (summarize != nullptr) {
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
    std::array<char const*, 2> argv = {"prompt", "explain"};
    auto chain = parse_chain(std::span<char const* const>(argv.data(), argv.size()), get_all_prompts());
    check(chain.count == 1, "dotted name: one invocation");
    check(chain.count == 1 && chain.invocations[0].name == "explain", "dotted name resolves to explain, not auto");
}

void test_tse_chain_identifier() {
    // The `tse` family may give the symbol once: `.tse foolad .tse.codal`
    // (and the reverse order) both resolve to foolad.
    prompt_context ctx;
    ctx.chain = {
        {.name = "tse", .args = {"foolad"}},
        {.name = "tse.codal", .args = {}},
    };
    ctx.chain_index = 1;
    auto id = prompts::tse_data::chain_identifier(ctx);
    check(id && *id == "foolad", "tse chain: later prompt borrows the preceding symbol");

    ctx.chain_index = 0;
    id = prompts::tse_data::chain_identifier(ctx);
    check(!id, "tse chain: the prompt with its own symbol does not need a fallback");

    ctx.chain = {
        {.name = "tse.codal", .args = {}},
        {.name = "tse.intraday", .args = {"khodro"}},
    };
    ctx.chain_index = 0;
    id = prompts::tse_data::chain_identifier(ctx);
    check(id && *id == "khodro", "tse chain: an earlier prompt borrows from a later one");

    // The nearest preceding symbol wins, and flags/values are skipped.
    ctx.chain = {
        {.name = "tse", .args = {"foolad"}},
        {.name = "tse.codal", .args = {"--top", "3", "khodro"}},
        {.name = "tse.intraday", .args = {"--days", "5"}},
    };
    ctx.chain_index = 2;
    id = prompts::tse_data::chain_identifier(ctx);
    check(id && *id == "khodro", "tse chain: nearest preceding symbol wins and flag values are skipped");

    // A `.stock` snapshot is not an instrument identifier for the other prompts.
    ctx.chain = {
        {.name = "tse", .args = {"stocks/14050717_فولاد_IRO1FOLD0009.stock"}},
        {.name = "tse.codal", .args = {}},
    };
    ctx.chain_index = 1;
    id = prompts::tse_data::chain_identifier(ctx);
    check(!id, "tse chain: a .stock snapshot is not borrowed as a symbol");
}

void test_ai_sinks_registered() {
    for (std::string_view name : {"ai.chatgpt", "ai.dseek", "ai.gap", "ai.grok", "ai.kimi"}) {
        auto const* desc = find_prompt(name);
        std::string msg = "ai sink registered: " + std::string(name);
        check(desc != nullptr, msg.c_str());
        if (desc != nullptr) {
            check(desc->role == chain_role::sink, "ai sink role");
            check(desc->policy == sink_policy::chain, "ai sink default policy");
            check(desc->stream_output, "ai sink streams its answer");
        }
    }
}

void test_save_sink_registered() {
    auto const* desc = find_prompt("save");
    check(desc != nullptr, "save sink registered");
    if (desc != nullptr) {
        check(desc->role == chain_role::sink, "save sink role");
        check(desc->policy == sink_policy::chain, "save sink default policy");
        check(!desc->stream_output, "save sink is silent");
    }
}

void test_tse_save_stem() {
    prompts::tse_data::config cfg;
    cfg.symbol = "khodro";
    check(prompts::tse_data::save_stem(cfg) == "tse-khodro", "tse save stem uses the symbol");
    check(prompts::tse_data::save_stem("فولاد") == "tse-فولاد", "tse save stem keeps Persian symbols");

    cfg.cache_mode = true;
    cfg.snapshot_files.emplace_back("stocks/14050717_فولاد_IRO1FOLD0009.stock");
    check(prompts::tse_data::save_stem(cfg) == "14050717_فولاد_IRO1FOLD0009",
          "tse snapshot save stem uses the snapshot name");
}

prompt_descriptor synthetic_prompt(std::string_view name, chain_role role, sink_policy policy) {
    prompt_descriptor desc{};
    desc.name = name;
    desc.role = role;
    desc.policy = policy;
    return desc;
}

chain_result synthetic_chain(std::initializer_list<std::string_view> names) {
    chain_result chain;
    for (auto name : names) {
        if (chain.count >= chain.invocations.size()) break;
        chain.invocations[chain.count].name = name;
        ++chain.count;
    }
    return chain;
}

void test_plan_chain() {
    std::array prompts = {
        synthetic_prompt("fix", chain_role::normal, sink_policy::chain),
        synthetic_prompt("ai.dseek", chain_role::sink, sink_policy::chain),
        synthetic_prompt("ai.grok", chain_role::sink, sink_policy::chain),
        synthetic_prompt("ai.kimi", chain_role::sink, sink_policy::fanout),
        synthetic_prompt("only", chain_role::sink, sink_policy::exclusive),
    };

    auto plan = plan_chain(synthetic_chain({"fix", "ai.dseek", "ai.grok", "fix"}), prompts);
    check(plan.error.empty(), "plan: chained sinks accepted");
    check(plan.runs.size() == 3, "plan: normal, sink, normal");
    check(plan.runs.size() == 3 && !plan.runs[0].sink, "plan: first run is normal");
    check(plan.runs.size() == 3 && plan.runs[1].sink && plan.runs[1].policy == sink_policy::chain,
          "plan: contiguous chain sinks form one run");
    check(plan.runs.size() == 3 && plan.runs[1].begin == 1 && plan.runs[1].end == 3,
          "plan: chain sink run spans both invocations");

    plan = plan_chain(synthetic_chain({"fix", "ai.dseek", "ai.kimi"}), prompts);
    check(!plan.error.empty(), "plan: mixing chain and fanout in one run is rejected");

    plan = plan_chain(synthetic_chain({"fix", "only", "ai.dseek"}), prompts);
    check(!plan.error.empty(), "plan: exclusive sink rejects a second sink");

    plan = plan_chain(synthetic_chain({"fix", "only"}), prompts);
    check(plan.error.empty(), "plan: a lone exclusive sink is accepted");
    check(plan.runs.size() == 2 && plan.runs[1].policy == sink_policy::exclusive,
          "plan: lone exclusive policy preserved");

    plan = plan_chain(synthetic_chain({"fix", "ai.kimi", "ai.kimi"}), prompts);
    check(plan.error.empty(), "plan: fanout run accepted");
    check(plan.runs.size() == 2 && plan.runs[1].policy == sink_policy::fanout, "plan: fanout policy preserved");
}

} // namespace

int main() {
    init_prompt_registry();

    test_bare_prompt_name();
    test_prompt_with_args();
    test_chain_two_prompts();
    test_unknown_defaults_to_auto();
    test_tse_chain_identifier();
    test_all_prompts_registered();
    test_ai_sinks_registered();
    test_save_sink_registered();
    test_tse_save_stem();
    test_plan_chain();

    if (failures == 0) {
        std::printf("all chaining tests passed\n");
        return 0;
    }
    return 1;
}
