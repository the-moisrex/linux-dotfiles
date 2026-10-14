#include "../sdk/prompt_registry.hpp"
#include "prompt_entry.hpp"
#include "prompts.hpp"

// The "complex" batch: prompts with nontrivial control flow (clipboard
// watching, subcommands, repo detection, dynamic listing). Summaries match
// `prompts/list.sh`'s first-paragraph extraction byte-for-byte.
namespace prompt {

void register_batch_complex() {
    register_prompt({
        .name = "agents",
        .help_summary = "Find and embed agent instruction files (AGENTS.md, CLAUDE.md, etc.) from the current Git "
                        "repository root. This gives the AI project conventions, coding standards, and architectural "
                        "context before it answers a question.",
        .help_full = "",
        .execute_fn = prompts::execute_agents,
        .render_help_fn = prompts::render_help_agents,
    });

    register_prompt({
        .name = "ai-said",
        .help_summary = "Collect multiple AI chat outputs via the clipboard and combine them into a single prompt.",
        .help_full = "",
        .execute_fn = prompts::execute_ai_said,
        .render_help_fn = prompts::render_help_ai_said,
    });

    register_prompt({
        .name = "ask-for-help",
        .help_summary = "Generates a ready-to-run `prompt` command line that bundles all the context needed to ask "
                        "another AI for help with a coding problem.",
        .help_full = "",
        .execute_fn = prompts::execute_ask_for_help,
        .render_help_fn = prompts::render_help_ask_for_help,
    });

    register_prompt({
        .name = "skill",
        .help_summary =
            "Loads a cached skill and outputs its content as an AI prompt. If the skill is not cached, it is "
            "downloaded automatically. Piped stdin and file arguments are embedded as context. It also "
            "manages skills: list, install, and uninstall (see below).",
        .help_full = "",
        .execute_fn = prompts::execute_skill,
        .render_help_fn = prompts::render_help_skill,
    });

    register_prompt({
        .name = "opportunities",
        .help_summary = "Find opportunities in a specific category such as money, tech, cpp, career, or business. Use "
                        "the category `all` for a brief overview of every category.",
        .help_full = "",
        .execute_fn = prompts::execute_opportunities,
        .render_help_fn = prompts::render_help_opportunities,
    });

    register_prompt({
        .name = "fs8.mod",
        .help_summary = "Builds a prompt for writing a new pipeline mod for the foresight project. Must be run from "
                        "inside the foresight git repo (or a subdirectory of it).",
        .help_full = "",
        .execute_fn = prompts::execute_fs8_mod,
        .render_help_fn = prompts::render_help_fs8_mod,
    });

    register_prompt({
        .name = "fs8.test",
        .help_summary = "Builds a prompt for writing GoogleTest tests for a foresight pipeline mod. Must be run from "
                        "inside the foresight git repo (or a subdirectory of it).",
        .help_full = "",
        .execute_fn = prompts::execute_fs8_test,
        .render_help_fn = prompts::render_help_fs8_test,
    });
}

} // namespace prompt
