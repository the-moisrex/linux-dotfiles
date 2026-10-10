#pragma once

#include "prompt/sdk/prompt_registry.hpp"
#include <functional>

namespace prompt {

// Forward declarations
namespace prompts {
prompt_result execute_auto(prompt_context&&) noexcept;
void render_help_auto(std::ostream&) noexcept;

prompt_result execute_fix(prompt_context&&) noexcept;
void render_help_fix(std::ostream&) noexcept;

prompt_result execute_note(prompt_context&&) noexcept;
void render_help_note(std::ostream&) noexcept;

prompt_result execute_files(prompt_context&&) noexcept;
void render_help_files(std::ostream&) noexcept;

prompt_result execute_stock(prompt_context&&) noexcept;
void render_help_stock(std::ostream&) noexcept;

prompt_result execute_intraday(prompt_context&&) noexcept;
void render_help_intraday(std::ostream&) noexcept;

prompt_result execute_tse_find(prompt_context&&) noexcept;
void render_help_tse_find(std::ostream&) noexcept;

prompt_result execute_cpp(prompt_context&&) noexcept;
void render_help_cpp(std::ostream&) noexcept;

prompt_result execute_cpp_reviewer(prompt_context&&) noexcept;
void render_help_cpp_reviewer(std::ostream&) noexcept;

prompt_result execute_review(prompt_context&&) noexcept;
void render_help_review(std::ostream&) noexcept;

prompt_result execute_tests(prompt_context&&) noexcept;
void render_help_tests(std::ostream&) noexcept;

prompt_result execute_refactor(prompt_context&&) noexcept;
void render_help_refactor(std::ostream&) noexcept;

prompt_result execute_run(prompt_context&&) noexcept;
void render_help_run(std::ostream&) noexcept;

prompt_result execute_commit(prompt_context&&) noexcept;
void render_help_commit(std::ostream&) noexcept;

prompt_result execute_list(prompt_context&&) noexcept;
void render_help_list(std::ostream&) noexcept;

prompt_result execute_list_prompts(prompt_context&&) noexcept;
void render_help_list_prompts(std::ostream&) noexcept;

prompt_result execute_new(prompt_context&&) noexcept;
void render_help_new(std::ostream&) noexcept;

prompt_result execute_gtest(prompt_context&&) noexcept;
void render_help_gtest(std::ostream&) noexcept;

prompt_result execute_gtest_case(prompt_context&&) noexcept;
void render_help_gtest_case(std::ostream&) noexcept;

prompt_result execute_spp(prompt_context&&) noexcept;
void render_help_spp(std::ostream&) noexcept;

prompt_result execute_cli(prompt_context&&) noexcept;
void render_help_cli(std::ostream&) noexcept;
} // namespace prompts

void init_prompt_registry() noexcept;

} // namespace prompt