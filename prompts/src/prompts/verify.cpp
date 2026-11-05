#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace prompt::prompts {

static void execute_verify(view const& v, result& r) {
    auto head_lines = head_lines_or_throw(v);

    auto git_dirty_args = positional_args(v);
    if (git_dirty_args.empty()) git_dirty_args = {"--full-diff"};

    // Piped stdin wins; otherwise run git-dirty with the forwarded arguments.
    // Preserve context formatting: file rendering belongs to git-dirty, and
    // unified diffs already carry line coordinates in their hunk headers.
    std::string context;
    if (v.in.piped()) {
        context = v.in.text();
    } else {
        std::vector<std::string_view> args;
        args.reserve(git_dirty_args.size());
        for (auto const& arg : git_dirty_args) args.push_back(arg);

        auto sub = run_sub_prompt(v, "git-dirty", args);

        // Remove trailing line endings without copying the entire context.
        while (!sub.out.empty() && (sub.out.back() == '\n' || sub.out.back() == '\r')) {
            sub.out.pop_back();
        }

        if (sub.out.empty()) {
            throw soft_error{"No changes found in the repository. Nothing to verify.\n"};
        }

        context = std::move(sub.out);
    }

    std::string output = R"PROMPT(You are verifying uncommitted changes in a Git repository.
Your job is to validate correctness: check that the changes do
what they claim, handle edge cases, preserve invariants, and
introduce no bugs or regressions.

For each issue found:
  1. State whether the change is CORRECT or INCORRECT.
  2. If incorrect, identify the file and line, explain the failure,
     and provide a git diff that includes accurate hunk line numbers
     (e.g., @@ -start,count +start,count @@) to fix it.

If all changes are correct, state that clearly.

Unified diffs are provided without added line-number annotations.
Use their hunk headers to determine old-file and new-file coordinates:
  - Context lines advance both coordinates.
  - Deleted lines advance only the old-file coordinate.
  - Added lines advance only the new-file coordinate.
  - No-newline markers advance neither coordinate.
An omitted hunk count means one line.

If a file block is explicitly presented with line-number labels,
those labels identify lines in that displayed snapshot; they are
not source content. Do not copy them into proposed patches.
Do not assume working-tree file coordinates match a staged diff's
destination when additional unstaged edits exist.

Use the context actually provided. If it is truncated or lacks the
source needed for an accurate patch, explain that limitation rather
than inventing missing source or hunk coordinates.

)PROMPT";

    output += trim_context_nl(context, head_lines);
    if (!output.ends_with('\n')) output.push_back('\n');

    r.out = std::move(output);
}

static void render_help_verify(std::ostream& os) {
    os << R"EOF(Usage: prompt verify [--head N] [GIT-DIRTY OPTIONS...]
       some-command | prompt verify [--head N] [GIT-DIRTY OPTIONS...]

Verify uncommitted changes in the current Git repository for correctness.

When stdin is piped, that content is used as the context instead of
git-dirty (e.g. from a clipboard, a saved diff, or the output of
`git show`). Otherwise the full diff context (files + diffs) is embedded
via git-dirty and the AI is asked to validate correctness, catch bugs,
regressions, and missing edge cases.

All extra arguments are forwarded to git-dirty (e.g. --staged,
--uncommitted, --except, --diff, --full, --full-diff). Default mode is
--full-diff so the AI sees both the current file state and the diff
markers.

Context is embedded without adding line-number annotations. Unified
diff hunk headers provide old-file and new-file line coordinates.
File-block formatting is supplied by git-dirty or the piped input.

Options:
  --head N   Keep only the first N lines of each embedded context file
  -h, --help Show this help
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered =
    prompt::register_prompt("verify", "Verify uncommitted changes in the current Git repository for correctness.",
                            &execute_verify, &render_help_verify);

} // namespace prompt::prompts
