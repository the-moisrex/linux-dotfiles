#include "../sdk/prompt_registry.hpp"
#include "prompt_entry.hpp"
#include "prompts.hpp"

namespace prompt {

void register_batch_git() {
    register_prompt({
        .name = "git",
        .help_summary = "Git workflow assistance prompt. Helps with branching strategies, merge conflicts, rebasing, "
                        "bisecting, and other git operations.",
        .help_full = "",
        .execute_fn = prompts::execute_git,
        .render_help_fn = prompts::render_help_git,
    });

    register_prompt({
        .name = "git.diff",
        .help_summary = "Shows Git diffs for the specified files. If no files are provided, shows all unstaged changes "
                        "and untracked files.",
        .help_full = "",
        .execute_fn = prompts::execute_git_diff,
        .render_help_fn = prompts::render_help_git_diff,
    });

    register_prompt({
        .name = "git-dirty",
        .help_summary =
            "Show uncommitted/changed files in the current Git repository. By default it lists all staged "
            "and unstaged changes (--all --files); the options below switch to diffs or full file contents.",
        .help_full = "",
        .execute_fn = prompts::execute_git_dirty,
        .render_help_fn = prompts::render_help_git_dirty,
    });

    register_prompt({
        .name = "git.files",
        .help_summary = "Embeds the given files as Markdown code blocks. File headings are printed relative to the Git "
                        "repository root. If no files are provided, `fzf -m` is used to choose from tracked files.",
        .help_full = "",
        .execute_fn = prompts::execute_git_files,
        .render_help_fn = prompts::render_help_git_files,
    });

    register_prompt({
        .name = "git.worktree.diff",
        .help_summary = "Shows Git diffs for files changed since the upstream branch. By default, compares against the "
                        "upstream tracking branch (or origin/main). Explicit FILE arguments restrict the diff to only "
                        "those files.",
        .help_full = "",
        .execute_fn = prompts::execute_git_worktree_diff,
        .render_help_fn = prompts::render_help_git_worktree_diff,
    });

    register_prompt({
        .name = "git.worktree.files",
        .help_summary = "Embeds files changed since the upstream branch as Markdown code blocks. By default, compares "
                        "against the upstream tracking branch (or origin/main). Explicit FILE arguments restrict "
                        "embedding to only those changed files.",
        .help_full = "",
        .execute_fn = prompts::execute_git_worktree_files,
        .render_help_fn = prompts::render_help_git_worktree_files,
    });

    register_prompt({
        .name = "repo",
        .help_summary =
            "Displays the repository's file structure using only git-tracked files. Useful for giving an AI "
            "a high-level view of the codebase.",
        .help_full = "",
        .execute_fn = prompts::execute_repo,
        .render_help_fn = prompts::render_help_repo,
    });

    register_prompt({
        .name = "verify",
        .help_summary = "Verify uncommitted changes in the current Git repository for correctness.",
        .help_full = "",
        .execute_fn = prompts::execute_verify,
        .render_help_fn = prompts::render_help_verify,
    });
}

} // namespace prompt
