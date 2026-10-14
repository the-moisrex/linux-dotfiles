#include "../sdk/prompt_registry.hpp"
#include "prompt_entry.hpp"
#include "prompts.hpp"

// Native ports of the tool-invoking prompt batch. The help_summary here is a
// placeholder: register_legacy_prompts() re-reads each backing .sh script and
// overrides both the summary and full help for bash-parity of `list`/--help.
namespace prompt {

void register_batch_tools() {
    register_prompt({
        .name = "clang-tidy",
        .help_summary = "Runs `clang-tidy` on the provided files and outputs a prompt to fix the identified "
                        "issues. If no files are provided, `fzf -m` is used to choose them interactively.",
        .help_full = "",
        .execute_fn = prompts::execute_clang_tidy,
        .render_help_fn = prompts::render_help_clang_tidy,
    });

    register_prompt({
        .name = "cmake",
        .help_summary = "Gathers all CMakeLists.txt and *.cmake files in the current project to provide full "
                        "context on the CMake build setup.",
        .help_full = "",
        .execute_fn = prompts::execute_cmake,
        .render_help_fn = prompts::render_help_cmake,
    });

    register_prompt({
        .name = "cppman",
        .help_summary = "Fetches C++ documentation for the specified standard library components using `cppman` "
                        "(which pulls from cppreference.com) and appends it to the prompt context.",
        .help_full = "",
        .execute_fn = prompts::execute_cppman,
        .render_help_fn = prompts::render_help_cppman,
    });

    register_prompt({
        .name = "gdb",
        .help_summary = "Generates a prompt asking the LLM to write a GDB script that helps debug the specified "
                        "code (e.g., tracking variables in loops, pretty-printing).",
        .help_full = "",
        .execute_fn = prompts::execute_gdb,
        .render_help_fn = prompts::render_help_gdb,
    });

    register_prompt({
        .name = "man",
        .help_summary = "Fetches the manual page for the given command(s) and appends it as a Markdown code "
                        "block. This is essentially a shorthand for `prompt cli man -P cat <page>`.",
        .help_full = "",
        .execute_fn = prompts::execute_man,
        .render_help_fn = prompts::render_help_man,
    });

    register_prompt({
        .name = "whatwg-url-specs",
        .help_summary = "Fetches relevant sections of the WHATWG URL specification using `whatwg-url-specs` and "
                        "adds them to the prompt context.",
        .help_full = "",
        .execute_fn = prompts::execute_whatwg_url_specs,
        .render_help_fn = prompts::render_help_whatwg_url_specs,
    });

    register_prompt({
        .name = "ci",
        .help_summary = "CI/CD pipeline review prompt. Automatically finds and embeds all CI/CD configuration "
                        "files from the repository (GitHub Actions, GitLab CI, Jenkins, Docker, etc.).",
        .help_full = "",
        .execute_fn = prompts::execute_ci,
        .render_help_fn = prompts::render_help_ci,
    });

    register_prompt({
        .name = "docker",
        .help_summary = "Docker and container review prompt. Analyzes Dockerfiles, docker-compose files, and "
                        "container configurations for best practices.",
        .help_full = "",
        .execute_fn = prompts::execute_docker,
        .render_help_fn = prompts::render_help_docker,
    });

    register_prompt({
        .name = "gh.issue",
        .help_summary = "Fetch a GitHub issue and generate a prompt asking the AI to implement it. Uses `gh issue "
                        "view` to retrieve issue details. If `gh` is unavailable, reads issue context from stdin "
                        "or produces a generic implementation prompt.",
        .help_full = "",
        .execute_fn = prompts::execute_gh_issue,
        .render_help_fn = prompts::render_help_gh_issue,
    });

    register_prompt({
        .name = "paths",
        .help_summary = "Extracts file paths from stdin (via bin/paths) and/or arguments, resolves each, and "
                        "embeds only Git-tracked files.",
        .help_full = "",
        .execute_fn = prompts::execute_paths,
        .render_help_fn = prompts::render_help_paths,
    });

    register_prompt({
        .name = "clipboard",
        .help_summary = "Read from the clipboard and embed the content as context for the AI.",
        .help_full = "",
        .execute_fn = prompts::execute_clipboard,
        .render_help_fn = prompts::render_help_clipboard,
    });
}

} // namespace prompt
