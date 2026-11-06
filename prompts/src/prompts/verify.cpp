#include "../core/git.hpp"
#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace prompt::prompts {

namespace {

struct verify_options {
    std::vector<std::string> exclude;
    std::vector<std::string> only;
    bool prompt_only = false;
};

struct parsed_verify_args {
    verify_options opts;
    std::vector<std::string> forward; // args still destined for git-dirty
};

parsed_verify_args parse_verify_args(std::vector<std::string> const& args) {
    parsed_verify_args result;
    for (std::size_t i = 0; i < args.size(); ++i) {
        std::string_view a = args[i];
        auto take_value = [&](std::string_view flag) -> std::optional<std::string_view> {
            if (a == flag) {
                if (i + 1 >= args.size())
                    throw soft_error{std::format("prompt verify: option '{}' requires a value.\n", flag)};
                return std::string_view{args[++i]};
            }
            if (a.size() > flag.size() && a.starts_with(flag) && a[flag.size()] == '=')
                return a.substr(flag.size() + 1);
            return std::nullopt;
        };
        if (auto x = take_value("--exclude")) {
            result.opts.exclude.emplace_back(*x);
            continue;
        }
        if (auto x = take_value("--only")) {
            result.opts.only.emplace_back(*x);
            continue;
        }
        if (a == "--prompt-only") {
            result.opts.prompt_only = true;
            continue;
        }
        result.forward.push_back(args[i]);
    }
    return result;
}

std::string_view verify_instructions() {
    return R"PROMPT(You are verifying uncommitted changes in a Git repository.
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
}

} // namespace

static void execute_verify(view const& v, result& r) {
    auto head_lines = head_lines_or_throw(v);
    auto pos_args = positional_args(v);
    auto parsed = parse_verify_args(pos_args);
    auto const& opts = parsed.opts;

    // Prompt-only: emit just the instructions, skip git/context entirely.
    if (opts.prompt_only) {
        std::string output{verify_instructions()};
        r.out = std::move(output);
        return;
    }

    // Piped stdin wins; otherwise run git-dirty with the forwarded arguments.
    // Preserve context formatting: file rendering belongs to git-dirty, and
    // unified diffs already carry line coordinates in their hunk headers.
    std::string context;
    if (v.in.piped()) {
        context = v.in.text();
    } else {
        std::vector<std::string_view> args;
        if (parsed.forward.empty()) {
            args = {"--full-diff"};
        } else {
            args.reserve(parsed.forward.size());
            for (auto const& arg : parsed.forward) {
                args.emplace_back(arg);
            }
        }

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

    // Apply file selection to whatever context we have (git-dirty or piped).
    if (!opts.exclude.empty() || !opts.only.empty()) {
        context = prompt::git::filter_diff_by_path(context, opts.only, opts.exclude);
        if (context.empty()) {
            throw soft_error{"No changes left after applying --exclude/--only filters.\n"};
        }
    }

    std::string output{verify_instructions()};
    output += trim_context_nl(context, head_lines);
    if (!output.ends_with('\n')) output.push_back('\n');

    r.out = std::move(output);
}

static void render_help_verify(std::ostream& os) {
    os << R"EOF(Usage: prompt verify [--head N] [--exclude GLOB]... [--only GLOB]... [--prompt-only] [GIT-DIRTY OPTIONS...]
       some-command | prompt verify [--head N] [--exclude GLOB]... [--only GLOB]... [--prompt-only]

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
  --head N       Keep only the first N lines of each embedded context file
  --exclude GLOB Skip files matching GLOB (repeatable; matched against path and basename)
  --only GLOB    Show only files matching GLOB (repeatable)
  --prompt-only  Emit only the prompt text, without any context
  -h, --help     Show this help
)EOF";
}

bool const registered =
    prompt::register_prompt("verify", "Verify uncommitted changes in the current Git repository for correctness.",
                            &execute_verify, &render_help_verify);

} // namespace prompt::prompts
