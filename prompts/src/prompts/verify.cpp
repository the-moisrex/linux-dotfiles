#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::prompts {

namespace {

std::string strip_trailing_newlines(std::string_view s) {
    std::string out(s);
    while (!out.empty() && out.back() == '\n') out.pop_back();
    return out;
}

} // namespace

static void execute_verify(view const& v, result& r) {
    auto head_lines = head_lines_or_throw(v);

    auto git_dirty_args = positional_args(v);
    if (git_dirty_args.empty()) git_dirty_args = {"--full-diff"};

    // Deciding what to verify: piped stdin wins; otherwise run git-dirty
    // --full-diff (or the forwarded args).
    std::string context;
    bool piped = v.in.piped();
    if (piped) {
        context = v.in.text();
    } else {
        std::vector<std::string_view> args;
        args.reserve(git_dirty_args.size());
        for (auto const& arg : git_dirty_args) args.push_back(arg);
        auto sub = run_sub_prompt(v, "git-dirty", args);
        context = strip_trailing_newlines(sub.out);
        if (context.empty()) {
            throw soft_error{"No changes found in the repository. Nothing to verify.\n"};
        }
    }

    std::string output;
    output += R"PROMPT(You are verifying uncommitted changes in a Git repository.
Your job is to validate correctness: check that the changes do
what they claim, handle edge cases, preserve invariants, and
introduce no bugs or regressions.

For each issue found:
  1. State whether the change is CORRECT or INCORRECT.
  2. If incorrect, identify the file and line, explain the failure,
     and provide a git diff that fixes it.

If all changes are correct, state that clearly.

)PROMPT";
    output += trim_context_nl(context, head_lines);

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
--uncommitted, --except, --diff, --full, --full-diff).  Default mode is
--full-diff so the AI sees both the current file state and the diff
markers.

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
