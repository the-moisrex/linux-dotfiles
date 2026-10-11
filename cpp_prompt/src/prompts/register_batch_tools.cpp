#include "prompt/prompts/ci_prompt.hpp"
#include "prompt/prompts/clang_tidy_prompt.hpp"
#include "prompt/prompts/clipboard_prompt.hpp"
#include "prompt/prompts/cmake_prompt.hpp"
#include "prompt/prompts/cppman_prompt.hpp"
#include "prompt/prompts/docker_prompt.hpp"
#include "prompt/prompts/gdb_prompt.hpp"
#include "prompt/prompts/gh_issue_prompt.hpp"
#include "prompt/prompts/man_prompt.hpp"
#include "prompt/prompts/paths_prompt.hpp"
#include "prompt/prompts/prompt_entry.hpp"
#include "prompt/prompts/whatwg_url_specs_prompt.hpp"
#include "prompt/sdk/prompt_registry.hpp"

// Native ports of the tool-invoking prompt batch. The help_summary here is a
// placeholder: register_legacy_prompts() re-reads each backing .sh script and
// overrides both the summary and full help for bash-parity of `list`/--help.
namespace prompt {

void register_batch_tools() noexcept {
    register_prompt({"clang-tidy",
                     "Runs `clang-tidy` on the provided files and outputs a prompt to fix the identified "
                     "issues. If no files are provided, `fzf -m` is used to choose them interactively.",
                     "", prompts::execute_clang_tidy, prompts::render_help_clang_tidy});

    register_prompt({"cmake",
                     "Gathers all CMakeLists.txt and *.cmake files in the current project to provide full "
                     "context on the CMake build setup.",
                     "", prompts::execute_cmake, prompts::render_help_cmake});

    register_prompt({"cppman",
                     "Fetches C++ documentation for the specified standard library components using `cppman` "
                     "(which pulls from cppreference.com) and appends it to the prompt context.",
                     "", prompts::execute_cppman, prompts::render_help_cppman});

    register_prompt({"gdb",
                     "Generates a prompt asking the LLM to write a GDB script that helps debug the specified "
                     "code (e.g., tracking variables in loops, pretty-printing).",
                     "", prompts::execute_gdb, prompts::render_help_gdb});

    register_prompt({"man",
                     "Fetches the manual page for the given command(s) and appends it as a Markdown code "
                     "block. This is essentially a shorthand for `prompt cli man -P cat <page>`.",
                     "", prompts::execute_man, prompts::render_help_man});

    register_prompt({"whatwg-url-specs",
                     "Fetches relevant sections of the WHATWG URL specification using `whatwg-url-specs` and "
                     "adds them to the prompt context.",
                     "", prompts::execute_whatwg_url_specs, prompts::render_help_whatwg_url_specs});

    register_prompt({"ci",
                     "CI/CD pipeline review prompt. Automatically finds and embeds all CI/CD configuration "
                     "files from the repository (GitHub Actions, GitLab CI, Jenkins, Docker, etc.).",
                     "", prompts::execute_ci, prompts::render_help_ci});

    register_prompt({"docker",
                     "Docker and container review prompt. Analyzes Dockerfiles, docker-compose files, and "
                     "container configurations for best practices.",
                     "", prompts::execute_docker, prompts::render_help_docker});

    register_prompt({"gh.issue",
                     "Fetch a GitHub issue and generate a prompt asking the AI to implement it. Uses `gh issue "
                     "view` to retrieve issue details. If `gh` is unavailable, reads issue context from stdin "
                     "or produces a generic implementation prompt.",
                     "", prompts::execute_gh_issue, prompts::render_help_gh_issue});

    register_prompt({"paths",
                     "Extracts file paths from stdin (via bin/paths) and/or arguments, resolves each, and "
                     "embeds only Git-tracked files.",
                     "", prompts::execute_paths, prompts::render_help_paths});

    register_prompt({"clipboard", "Read from the clipboard and embed the content as context for the AI.", "",
                     prompts::execute_clipboard, prompts::render_help_clipboard});
}

} // namespace prompt
