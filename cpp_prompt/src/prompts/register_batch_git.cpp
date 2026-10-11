#include "prompt/prompts/git_diff_prompt.hpp"
#include "prompt/prompts/git_dirty_prompt.hpp"
#include "prompt/prompts/git_files_prompt.hpp"
#include "prompt/prompts/git_prompt.hpp"
#include "prompt/prompts/git_worktree_diff_prompt.hpp"
#include "prompt/prompts/git_worktree_files_prompt.hpp"
#include "prompt/prompts/prompt_entry.hpp"
#include "prompt/prompts/repo_prompt.hpp"
#include "prompt/prompts/verify_prompt.hpp"

namespace prompt {

void register_batch_git() noexcept {
    register_prompt({"git",
                     "Git workflow assistance prompt. Helps with branching strategies, merge conflicts, rebasing, "
                     "bisecting, and other git operations.",
                     "", prompts::execute_git, prompts::render_help_git});

    register_prompt({"git.diff",
                     "Shows Git diffs for the specified files. If no files are provided, shows all unstaged changes "
                     "and untracked files.",
                     "", prompts::execute_git_diff, prompts::render_help_git_diff});

    register_prompt({"git-dirty",
                     "Show uncommitted/changed files in the current Git repository. By default it lists all staged "
                     "and unstaged changes (--all --files); the options below switch to diffs or full file contents.",
                     "", prompts::execute_git_dirty, prompts::render_help_git_dirty});

    register_prompt({"git.files",
                     "Embeds the given files as Markdown code blocks. File headings are printed relative to the Git "
                     "repository root. If no files are provided, `fzf -m` is used to choose from tracked files.",
                     "", prompts::execute_git_files, prompts::render_help_git_files});

    register_prompt({"git.worktree.diff",
                     "Shows Git diffs for files changed since the upstream branch. By default, compares against the "
                     "upstream tracking branch (or origin/main). Explicit FILE arguments restrict the diff to only "
                     "those files.",
                     "", prompts::execute_git_worktree_diff, prompts::render_help_git_worktree_diff});

    register_prompt({"git.worktree.files",
                     "Embeds files changed since the upstream branch as Markdown code blocks. By default, compares "
                     "against the upstream tracking branch (or origin/main). Explicit FILE arguments restrict "
                     "embedding to only those changed files.",
                     "", prompts::execute_git_worktree_files, prompts::render_help_git_worktree_files});

    register_prompt({"repo",
                     "Displays the repository's file structure using only git-tracked files. Useful for giving an AI "
                     "a high-level view of the codebase.",
                     "", prompts::execute_repo, prompts::render_help_repo});

    register_prompt({"verify", "Verify uncommitted changes in the current Git repository for correctness.", "",
                     prompts::execute_verify, prompts::render_help_verify});
}

} // namespace prompt
