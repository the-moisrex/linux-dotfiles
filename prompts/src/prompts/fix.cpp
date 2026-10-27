#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/facts.hpp"
#include "../sdk/prompt_registry.hpp"
#include "../sdk/signals.hpp"
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::prompts {

namespace {

// The FILE arguments, ignoring the `--head <value>` pair and the line-number
// flags — enough to name the files under repair as the chain's focus.
std::vector<std::string> focus_files(std::span<std::string_view const> args) {
    std::vector<std::string> files;
    for (std::size_t i = 0; i < args.size(); ++i) {
        std::string_view const arg = args[i];
        if (arg == "--head") {
            ++i;
        } else if (is_number_flag(arg) || (arg.size() > 1 && arg.front() == '-')) {
            continue;
        } else {
            files.emplace_back(arg);
        }
    }
    return files;
}

// A declaration, not an artifact: it is computed from argv alone and runs before
// the batch forks, so `.compress` in the same batch reads the intent even
// without a sink barrier (parallel siblings never see each other's artifacts).
void declare_fix(declaration& d) {
    if (!d.prior.get(keys::mode)) d.declared.set(keys::mode, signals::to_string(signals::intent::fix));
    std::vector<std::string> const files = focus_files(d.args);
    if (!files.empty()) d.declared.set(keys::focus, signals::join_focus(files));
}

} // namespace

static void execute_fix(view const& v, result& r) {
    auto opts = file_options_or_throw(v);

    std::string output = R"PROMPT(Find the root problem here and propose the smallest useful fix.
Explain the issue briefly, mention any important assumptions, and provide the answer primarily as a git diff that can be applied directly.
Prefer minimal, surgical changes over broad rewrites.

)PROMPT";

    std::string error;
    auto sources = collect_file_args(v);
    for (auto const& file : sources.files) {
        output += std::format("File: {}\n\n```{}\n", file.filename().string(), infer_lang(file));
        output += number_lines(trim_context_nl(read_file(file), opts.head_lines), opts.numbered);
        output += "\n```\n";
    }
    for (auto arg : sources.missing) {
        warn_missing_file(error, arg);
    }

    r.out = std::move(output);
    r.err = std::move(error);
}

static void render_help_fix(std::ostream& os) {
    os << file_usage_help("fix") << R"EOF(
Find the root problem here and propose the smallest useful fix.

Options:
)EOF" << file_options_help("the embedded context");
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered =
    prompt::register_prompt("fix", "Find the root problem here and propose the smallest useful fix.", &execute_fix,
                            &render_help_fix, nullptr, chain_role::normal, true, nullptr, &declare_fix);

} // namespace prompt::prompts
