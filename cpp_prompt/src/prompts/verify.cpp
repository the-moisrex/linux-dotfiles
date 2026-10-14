#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "prompts.hpp"
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

prompt_output execute_verify(prompt_context&& ctx) {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        throw usage_error{std::move(msg)};
    }

    // verify.sh runs `bash git-dirty.sh ...` (matches byte-for-byte when we run
    // the same script via the legacy runner). Its stderr is discarded and its
    // exit status ignored (2>/dev/null || true).
    std::vector<std::string> git_dirty_args;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head") {
            ++i;
            continue;
        }
        git_dirty_args.emplace_back(ctx.args[i]);
    }
    if (git_dirty_args.empty()) git_dirty_args = {"--full-diff"};

    // Deciding what to verify: piped stdin wins (verify.sh calls read_stdin);
    // otherwise run git-dirty --full-diff (or the forwarded args).
    std::string context;
    bool piped = ctx.stdin_consumed;
    if (piped) {
        context = ctx.stdin_content;
    } else {
        std::vector<char const*> argv;
        argv.reserve(git_dirty_args.size() + 1);
        for (auto const& a : git_dirty_args) argv.push_back(a.c_str());
        argv.push_back(nullptr);
        prompt_context sub = ctx;
        sub.name = "git-dirty";
        std::size_t n = 0;
        for (auto const& a : git_dirty_args) sub.args[n++] = a;
        sub.args_count = n;
        auto result = prompts::execute_git_dirty(std::move(sub));
        context = strip_trailing_newlines(result.text);
        if (context.empty()) {
            throw soft_error{"No changes found in the repository. Nothing to verify.\n"};
        }
    }

    std::string output;
    output += "You are verifying uncommitted changes in a Git repository.\nYour job is to validate correctness: check "
              "that the changes do\nwhat they claim, handle edge cases, preserve invariants, and\nintroduce no bugs or "
              "regressions.\n\nFor each issue found:\n  1. State whether the change is CORRECT or INCORRECT.\n  2. If "
              "incorrect, identify the file and line, explain the failure,\n     and provide a git diff that fixes "
              "it.\n\nIf all changes are correct, state that clearly.\n\n";
    output += trim_context_nl(context, head_lines);

    return {.text = std::move(output), .error = std::string{}, .no_clipboard = false, .stdin_consumed = piped};
}

void render_help_verify(std::ostream& os) {
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

} // namespace prompt::prompts
