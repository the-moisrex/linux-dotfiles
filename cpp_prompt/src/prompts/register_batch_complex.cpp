#include "prompt/prompts/agents_prompt.hpp"
#include "prompt/prompts/ai_said_prompt.hpp"
#include "prompt/prompts/ask_for_help_prompt.hpp"
#include "prompt/prompts/fs8_mod_prompt.hpp"
#include "prompt/prompts/fs8_test_prompt.hpp"
#include "prompt/prompts/opportunities_prompt.hpp"
#include "prompt/prompts/prompt_entry.hpp"
#include "prompt/prompts/skill_prompt.hpp"

// The "complex" batch: prompts with nontrivial control flow (clipboard
// watching, subcommands, repo detection, dynamic listing). Summaries match
// `prompts/list.sh`'s first-paragraph extraction byte-for-byte.
namespace prompt {

void register_batch_complex() noexcept {
    register_prompt({"agents",
                     "Find and embed agent instruction files (AGENTS.md, CLAUDE.md, etc.) from the current Git "
                     "repository root. This gives the AI project conventions, coding standards, and architectural "
                     "context before it answers a question.",
                     "", prompts::execute_agents, prompts::render_help_agents});

    register_prompt({"ai-said",
                     "Collect multiple AI chat outputs via the clipboard and combine them into a single prompt.", "",
                     prompts::execute_ai_said, prompts::render_help_ai_said});

    register_prompt({"ask-for-help",
                     "Generates a ready-to-run `prompt` command line that bundles all the context needed to ask "
                     "another AI for help with a coding problem.",
                     "", prompts::execute_ask_for_help, prompts::render_help_ask_for_help});

    register_prompt({"skill",
                     "Loads a cached skill and outputs its content as an AI prompt. If the skill is not cached, it is "
                     "downloaded automatically. Piped stdin and file arguments are embedded as context. It also "
                     "manages skills: list, install, and uninstall (see below).",
                     "", prompts::execute_skill, prompts::render_help_skill});

    register_prompt({"opportunities",
                     "Find opportunities in a specific category such as money, tech, cpp, career, or business. Use "
                     "the category `all` for a brief overview of every category.",
                     "", prompts::execute_opportunities, prompts::render_help_opportunities});

    register_prompt({"fs8.mod",
                     "Builds a prompt for writing a new pipeline mod for the foresight project. Must be run from "
                     "inside the foresight git repo (or a subdirectory of it).",
                     "", prompts::execute_fs8_mod, prompts::render_help_fs8_mod});

    register_prompt({"fs8.test",
                     "Builds a prompt for writing GoogleTest tests for a foresight pipeline mod. Must be run from "
                     "inside the foresight git repo (or a subdirectory of it).",
                     "", prompts::execute_fs8_test, prompts::render_help_fs8_test});
}

} // namespace prompt
