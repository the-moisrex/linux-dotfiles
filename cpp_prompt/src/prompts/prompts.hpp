#pragma once

// Every prompt implemented in this build. One header instead of a file pair
// per prompt: the dispatcher (`main.cpp`), the registry (`prompt_entry.cpp` /
// `register_batch_*.cpp`) and the prompt implementations all include exactly
// this. Adding a prompt means editing this file plus its `.cpp` — nothing else.

#include "../sdk/errors.hpp"
#include "../sdk/prompt_context.hpp"
#include <ostream>
#include <sstream>
#include <string>

namespace prompt::prompts {

prompt_output execute_agents(prompt_context&& ctx);
prompt_output execute_ai_said(prompt_context&& ctx);
prompt_output execute_api(prompt_context&& ctx);
prompt_output execute_ask_for_help(prompt_context&& ctx);
prompt_output execute_auto(prompt_context&& ctx);
prompt_output execute_benchmark(prompt_context&& ctx);
prompt_output execute_bigo(prompt_context&& ctx);
prompt_output execute_branchless(prompt_context&& ctx);
prompt_output execute_ci(prompt_context&& ctx);
prompt_output execute_clang_tidy(prompt_context&& ctx);
prompt_output execute_cli(prompt_context&& ctx);
prompt_output execute_clipboard(prompt_context&& ctx);
prompt_output execute_cmake(prompt_context&& ctx);
prompt_output execute_comments(prompt_context&& ctx);
prompt_output execute_commit(prompt_context&& ctx);
prompt_output execute_compile_time(prompt_context&& ctx);
prompt_output execute_cpp(prompt_context&& ctx);
prompt_output execute_cpp_reviewer(prompt_context&& ctx);
prompt_output execute_cppman(prompt_context&& ctx);
prompt_output execute_de_export(prompt_context&& ctx);
prompt_output execute_debug(prompt_context&& ctx);
prompt_output execute_diagram(prompt_context&& ctx);
prompt_output execute_docker(prompt_context&& ctx);
prompt_output execute_docstring(prompt_context&& ctx);
prompt_output execute_english(prompt_context&& ctx);
prompt_output execute_explain(prompt_context&& ctx);
prompt_output execute_farsi(prompt_context&& ctx);
prompt_output execute_files(prompt_context&& ctx);
prompt_output execute_fix(prompt_context&& ctx);
prompt_output execute_fs8_mod(prompt_context&& ctx);
prompt_output execute_fs8_test(prompt_context&& ctx);
prompt_output execute_gdb(prompt_context&& ctx);
prompt_output execute_gh_issue(prompt_context&& ctx);
prompt_output execute_git_diff(prompt_context&& ctx);
prompt_output execute_git_dirty(prompt_context&& ctx);
prompt_output execute_git_files(prompt_context&& ctx);
prompt_output execute_git(prompt_context&& ctx);
prompt_output execute_git_worktree_diff(prompt_context&& ctx);
prompt_output execute_git_worktree_files(prompt_context&& ctx);
prompt_output execute_gtest_case(prompt_context&& ctx);
prompt_output execute_gtest(prompt_context&& ctx);
prompt_output execute_hoist_if(prompt_context&& ctx);
prompt_output execute_intraday(prompt_context&& ctx);
prompt_output execute_list(prompt_context&& ctx);
prompt_output execute_list_prompts(prompt_context&& ctx);
prompt_output execute_man(prompt_context&& ctx);
prompt_output execute_metadata(prompt_context&& ctx);
prompt_output execute_migrate(prompt_context&& ctx);
prompt_output execute_new(prompt_context&& ctx);
prompt_output execute_note(prompt_context&& ctx);
prompt_output execute_opportunities(prompt_context&& ctx);
prompt_output execute_optimize_prompt(prompt_context&& ctx);
prompt_output execute_overengineering(prompt_context&& ctx);
prompt_output execute_paths(prompt_context&& ctx);
prompt_output execute_perf(prompt_context&& ctx);
prompt_output execute_plan(prompt_context&& ctx);
prompt_output execute_readme(prompt_context&& ctx);
prompt_output execute_refactor(prompt_context&& ctx);
prompt_output execute_repo(prompt_context&& ctx);
prompt_output execute_review(prompt_context&& ctx);
prompt_output execute_run(prompt_context&& ctx);
prompt_output execute_security(prompt_context&& ctx);
prompt_output execute_skill(prompt_context&& ctx);
prompt_output execute_spp(prompt_context&& ctx);
prompt_output execute_stdin(prompt_context&& ctx);
prompt_output execute_stock(prompt_context&& ctx);
prompt_output execute_stupid(prompt_context&& ctx);
prompt_output execute_summarize(prompt_context&& ctx);
prompt_output execute_symbols(prompt_context&& ctx);
prompt_output execute_table_lookup(prompt_context&& ctx);
prompt_output execute_tests(prompt_context&& ctx);
prompt_output execute_tse_find(prompt_context&& ctx);
prompt_output execute_tweets(prompt_context&& ctx);
prompt_output execute_verify(prompt_context&& ctx);
prompt_output execute_whatwg_url_specs(prompt_context&& ctx);
prompt_output execute_yt(prompt_context&& ctx);

void render_help_agents(std::ostream& os);
void render_help_ai_said(std::ostream& os);
void render_help_api(std::ostream& os);
void render_help_ask_for_help(std::ostream& os);
void render_help_auto(std::ostream& os);
void render_help_benchmark(std::ostream& os);
void render_help_bigo(std::ostream& os);
void render_help_branchless(std::ostream& os);
void render_help_ci(std::ostream& os);
void render_help_clang_tidy(std::ostream& os);
void render_help_cli(std::ostream& os);
void render_help_clipboard(std::ostream& os);
void render_help_cmake(std::ostream& os);
void render_help_comments(std::ostream& os);
void render_help_commit(std::ostream& os);
void render_help_compile_time(std::ostream& os);
void render_help_cpp(std::ostream& os);
void render_help_cpp_reviewer(std::ostream& os);
void render_help_cppman(std::ostream& os);
void render_help_de_export(std::ostream& os);
void render_help_debug(std::ostream& os);
void render_help_diagram(std::ostream& os);
void render_help_docker(std::ostream& os);
void render_help_docstring(std::ostream& os);
void render_help_english(std::ostream& os);
void render_help_explain(std::ostream& os);
void render_help_farsi(std::ostream& os);
void render_help_files(std::ostream& os);
void render_help_fix(std::ostream& os);
void render_help_fs8_mod(std::ostream& os);
void render_help_fs8_test(std::ostream& os);
void render_help_gdb(std::ostream& os);
void render_help_gh_issue(std::ostream& os);
void render_help_git_diff(std::ostream& os);
void render_help_git_dirty(std::ostream& os);
void render_help_git_files(std::ostream& os);
void render_help_git(std::ostream& os);
void render_help_git_worktree_diff(std::ostream& os);
void render_help_git_worktree_files(std::ostream& os);
void render_help_gtest_case(std::ostream& os);
void render_help_gtest(std::ostream& os);
void render_help_hoist_if(std::ostream& os);
void render_help_intraday(std::ostream& os);
void render_help_list(std::ostream& os);
void render_help_list_prompts(std::ostream& os);
void render_help_man(std::ostream& os);
void render_help_metadata(std::ostream& os);
void render_help_migrate(std::ostream& os);
void render_help_new(std::ostream& os);
void render_help_note(std::ostream& os);
void render_help_opportunities(std::ostream& os);
void render_help_optimize_prompt(std::ostream& os);
void render_help_overengineering(std::ostream& os);
void render_help_paths(std::ostream& os);
void render_help_perf(std::ostream& os);
void render_help_plan(std::ostream& os);
void render_help_readme(std::ostream& os);
void render_help_refactor(std::ostream& os);
void render_help_repo(std::ostream& os);
void render_help_review(std::ostream& os);
void render_help_run(std::ostream& os);
void render_help_security(std::ostream& os);
void render_help_skill(std::ostream& os);
void render_help_spp(std::ostream& os);
void render_help_stdin(std::ostream& os);
void render_help_stock(std::ostream& os);
void render_help_stupid(std::ostream& os);
void render_help_summarize(std::ostream& os);
void render_help_symbols(std::ostream& os);
void render_help_table_lookup(std::ostream& os);
void render_help_tests(std::ostream& os);
void render_help_tse_find(std::ostream& os);
void render_help_tweets(std::ostream& os);
void render_help_verify(std::ostream& os);
void render_help_whatwg_url_specs(std::ostream& os);
void render_help_yt(std::ostream& os);

prompt_output execute_tse_find_impl(prompt_context&& ctx);
void render_help_tse_find_impl(std::ostream& os);

// Captures a prompt's own `render_help_*` output into a string, for prompts
// that append it to a failure message (cli, man, stock, intraday) instead of
// printing it directly (`help_text(render_help_cli)` instead of the
// ostringstream dance).
template <typename RenderFn>
std::string help_text(RenderFn render) {
    std::ostringstream os;
    render(os);
    return os.str();
}

} // namespace prompt::prompts
