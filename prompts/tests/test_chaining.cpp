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
    check(chain.invocations.size() == 1, "bare name: one invocation");
    check(chain.invocations.size() == 1 && chain.invocations[0].name == "fix", "bare name resolves to fix");
}

void test_prompt_with_args() {
    std::array<char const*, 4> argv = {"prompt", "fix", "--head", "10"};
    auto chain = parse_chain(std::span<char const* const>(argv.data(), argv.size()), get_all_prompts());
    check(chain.invocations.size() == 1, "args: one invocation");
    check(chain.invocations.size() == 1 && chain.invocations[0].name == "fix", "args: name is fix");
    check(chain.invocations.size() == 1 && chain.invocations[0].args.size() == 2, "args: --head 10 kept");
    check(chain.invocations.size() == 1 && chain.invocations[0].args[0] == "--head", "args: --head first");
}

void test_chain_two_prompts() {
    std::array<char const*, 5> argv = {"prompt", "tse", "فولاد", ".note", "compare"};
    auto chain = parse_chain(std::span<char const* const>(argv.data(), argv.size()), get_all_prompts());
    check(chain.invocations.size() == 2, "chain: two invocations");
    check(chain.invocations.size() == 2 && chain.invocations[0].name == "tse", "chain: first is tse");
    check(chain.invocations.size() == 2 && chain.invocations[1].name == "note", "chain: second is note");
    check(chain.invocations.size() == 2 && !chain.invocations[1].args.empty(), "chain: note has args");
}

void test_unknown_defaults_to_auto() {
    std::array<char const*, 1> argv = {"prompt"};
    auto chain = parse_chain(std::span<char const* const>(argv.data(), argv.size()), get_all_prompts());
    check(chain.invocations.size() == 1, "empty argv: one invocation");
    check(chain.invocations.size() == 1 && chain.invocations[0].name == "auto", "empty argv: defaults to auto");
}

void test_all_prompts_registered() {
    // Every prompt the command ships must be a native, executable prompt in
    // this build (there is no script fallback).
    check(find_prompt("summarize") != nullptr, "summarize is registered");
    check(find_prompt("explain") != nullptr, "explain is registered");
    check(find_prompt("git.worktree.files") != nullptr, "dotted name registered");

    auto const* summarize = find_prompt("summarize");
    if (summarize != nullptr) {
        check(!summarize->help_summary.empty(), "summarize has a help summary");
        check(summarize->execute_fn != nullptr, "summarize has an executor");
    }

    // The whole catalog (75 prompts today) is registered natively.
    check(get_all_prompts().size() >= 75, "registry includes every prompt");

    // A full registry aborts at startup, so this catches a build that grows
    // past the fixed capacity before the binary does.
    check(get_all_prompts().size() < prompt_registry_capacity, "the catalog fits the fixed registry");

    for (auto const& desc : get_all_prompts()) {
        std::string exec_msg = "prompt has an executor: " + std::string(desc.name);
        std::string summary_msg = "prompt has a summary: " + std::string(desc.name);
        check(desc.execute_fn != nullptr, exec_msg.c_str());
        check(!desc.help_summary.empty(), summary_msg.c_str());
    }

    // Dotted names resolve through the chain parser directly.
    std::array<char const*, 2> argv = {"prompt", "explain"};
    auto chain = parse_chain(std::span<char const* const>(argv.data(), argv.size()), get_all_prompts());
    check(chain.invocations.size() == 1, "dotted name: one invocation");
    check(chain.invocations.size() == 1 && chain.invocations[0].name == "explain",
          "dotted name resolves to explain, not auto");
}

void test_tse_identifier_declaration() {
    // `.tse foolad .tse.codal` gives the symbol once: the first declaration
    // publishes it and the sibling borrows it from `prior`, so both orders work
    // and every segment sees the same instrument.
    environment env;
    std::array<std::string_view, 1> foolad = {"foolad"};
    std::array<std::string_view, 0> none{};
    facts declared;
    {
        declaration d{.args = foolad, .env = env, .prior = declared, .declared = declared};
        prompts::tse_data::declare_identifier(d);
    }
    check(declared.get(keys::tse_symbol) == "foolad", "tse declare: the symbol is published for siblings");

    stdin_source in;
    {
        view const sibling{.name = "tse.codal", .args = none, .text = {}, .prior = declared, .env = env, .in = in};
        auto id = prompts::tse_data::prior_identifier(sibling);
        check(id && *id == "foolad", "tse declare: a sibling with no identifier borrows the declared one");
    }

    // An existing declaration is never overwritten: the first writer wins, so a
    // second identifier in the chain cannot silently retarget the batch.
    std::array<std::string_view, 1> khodro = {"khodro"};
    {
        declaration d{.args = khodro, .env = env, .prior = declared, .declared = declared};
        prompts::tse_data::declare_identifier(d);
    }
    check(declared.get(keys::tse_symbol) == "foolad", "tse declare: the first declaration wins");

    // Flags and their values are skipped when looking for the identifier.
    facts fresh;
    std::array<std::string_view, 3> flagged = {"--top", "3", "khodro"};
    {
        declaration d{.args = flagged, .env = env, .prior = fresh, .declared = fresh};
        prompts::tse_data::declare_identifier(d);
    }
    check(fresh.get(keys::tse_symbol) == "khodro", "tse declare: flag values are skipped");

    // A `.stock` snapshot is not an instrument identifier for the other prompts.
    facts snapshot;
    std::array<std::string_view, 1> file = {"stocks/14050717_فولاد_IRO1FOLD0009.stock"};
    {
        declaration d{.args = file, .env = env, .prior = snapshot, .declared = snapshot};
        prompts::tse_data::declare_identifier(d);
    }
    check(!snapshot.get(keys::tse_symbol), "tse declare: a .stock snapshot is not borrowed as a symbol");
}

void test_ai_sinks_registered() {
    for (std::string_view name : {"ai.chatgpt", "ai.dseek", "ai.gap", "ai.grok", "ai.kimi"}) {
        auto const* desc = find_prompt(name);
        std::string msg = "ai sink registered: " + std::string(name);
        check(desc != nullptr, msg.c_str());
        if (desc != nullptr) {
            check(desc->role == chain_role::sink, "ai sink role");
            check(desc->stream_output, "ai sink streams its answer");
        }
    }
}

void test_save_sink_registered() {
    auto const* desc = find_prompt("save");
    check(desc != nullptr, "save sink registered");
    if (desc != nullptr) {
        check(desc->role == chain_role::sink, "save sink role");
        check(!desc->stream_output, "save sink is silent");
        check(desc->preflight != nullptr, "save sink has a preflight");
    }
}

void test_clear_sinks_registered() {
    auto const* clear = find_prompt("clear");
    check(clear != nullptr, "clear sink registered");
    if (clear != nullptr) {
        check(clear->role == chain_role::sink, "clear sink role");
        check(!clear->stream_output, "clear sink is silent");
        check(clear->preflight == nullptr, "clear sink needs no preflight");
    }

    auto const* flush = find_prompt("flush");
    check(flush != nullptr, "flush sink registered");
    if (flush != nullptr) {
        check(flush->role == chain_role::sink, "flush sink role");
        check(!flush->stream_output, "flush sink is silent");
        check(flush->preflight != nullptr, "flush sink shares save's preflight");
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

prompt_descriptor synthetic_prompt(std::string_view name, chain_role role) {
    prompt_descriptor desc{};
    desc.name = name;
    desc.role = role;
    return desc;
}

chain_result synthetic_chain(std::initializer_list<std::string_view> names) {
    chain_result chain;
    for (auto name : names) chain.invocations.push_back({.name = name, .args = {}});
    return chain;
}

void test_plan_chain() {
    std::array prompts = {
        synthetic_prompt("fix", chain_role::normal),
        synthetic_prompt("review", chain_role::normal),
        synthetic_prompt("ai.dseek", chain_role::sink),
        synthetic_prompt("ai.grok", chain_role::sink),
    };

    auto plan = plan_chain(synthetic_chain({"fix", "ai.dseek", "ai.grok", "review"}), prompts);
    check(plan.error.empty(), "plan: chained sinks accepted");
    check(plan.runs.size() == 3, "plan: normal, sink, normal");
    check(plan.runs.size() == 3 && !plan.runs[0].sink, "plan: first run is normal");
    check(plan.runs.size() == 3 && plan.runs[1].sink, "plan: the middle run is a sink barrier");
    check(plan.runs.size() == 3 && plan.runs[1].begin == 1 && plan.runs[1].end == 3,
          "plan: contiguous sinks form one run");
    check(plan.runs.size() == 3 && plan.runs[2].begin == 3 && plan.runs[2].end == 4,
          "plan: the trailing normal prompt is its own run");

    plan = plan_chain(synthetic_chain({"ai.dseek"}), prompts);
    check(plan.error.empty() && plan.runs.size() == 1 && plan.runs[0].sink, "plan: a lone sink is one run");

    plan = plan_chain(synthetic_chain({"fix", "missing"}), prompts);
    check(!plan.error.empty(), "plan: an unknown prompt is an error");
}

} // namespace

int main() {
    test_bare_prompt_name();
    test_prompt_with_args();
    test_chain_two_prompts();
    test_unknown_defaults_to_auto();
    test_tse_identifier_declaration();
    test_all_prompts_registered();
    test_ai_sinks_registered();
    test_save_sink_registered();
    test_clear_sinks_registered();
    test_tse_save_stem();
    test_plan_chain();

    if (failures == 0) {
        std::printf("all chaining tests passed\n");
        return 0;
    }
    return 1;
}
