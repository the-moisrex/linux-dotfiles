#include "../core/fs.hpp"
#include "../core/process.hpp"
#include "../sdk/agent.hpp"
#include "../sdk/chaining.hpp"
#include "../sdk/errors.hpp"
#include "../sdk/facts.hpp"
#include "../sdk/policy.hpp"
#include "../sdk/prompt_registry.hpp"
#include "../sdk/sink.hpp"
#include "../sdk/stage.hpp"
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <format>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::prompts {

static void render_help_exec(std::ostream& os);

namespace {

[[nodiscard]] std::optional<std::filesystem::path> git_root_of(view const& v) {
    if (v.env.git_root.empty()) return std::nullopt;
    return v.env.git_root;
}

[[nodiscard]] std::string extract_text(view const& v, std::string_view selector) {
    auto const tool = fs::bin_tool("md.extract", v.env.exe_path, git_root_of(v));
    std::vector<std::string> argv{"python3", tool.string(), "--select", std::string(selector)};
    auto const result =
        process::command{argv}.stdin_data(std::string(v.text)).capture_stderr().timeout(std::chrono::minutes(5)).run();
    if (result.exit_code != 0) {
        std::string message = result.stderr_data;
        if (message.empty()) message = std::format("prompt exec: md.extract failed for {}\n", selector);
        if (result.exit_code == 2) throw usage_error{std::move(message)};
        throw soft_error{std::move(message)};
    }
    return result.stdout_data;
}

[[nodiscard]] std::vector<std::string> command_lines(std::string const& text) {
    std::vector<std::string> lines;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        auto const end = text.find('\n', begin);
        std::string_view line(text.data() + begin, (end == std::string::npos ? text.size() : end) - begin);
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r'))
            line.remove_suffix(1);
        if (!line.empty() && line.front() != '#') lines.emplace_back(line);
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return lines;
}

[[nodiscard]] bool is_prompt_command(std::string_view program, view const& v) {
    if (program == "prompt") return true;
    return program == agent::program_name(v.env.exe_path.string());
}

// `.extract .exec` leaves the context as exactly the selected command. Running
// that directly keeps the two-stage flow, while a multi-line AI answer still
// goes through the extractor. Only a `prompt` command is recognised this way:
// one line of prose must never become one.
[[nodiscard]] std::optional<std::string> direct_prompt_command(std::string_view text, view const& v) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) text.remove_suffix(1);
    if (text.empty()) return std::nullopt;
    auto const newline = text.find('\n');
    std::string_view const first = newline == std::string_view::npos ? text : text.substr(0, newline);
    if (newline != std::string_view::npos) {
        std::string_view rest = text.substr(newline + 1);
        while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest.front())) != 0) rest.remove_prefix(1);
        if (!rest.empty()) return std::nullopt;
    }
    std::string const exe = agent::program_name(v.env.exe_path.string());
    bool const prompt_line =
        first == "prompt" || first.starts_with("prompt ") || first == exe || first.starts_with(exe + " ");
    if (!prompt_line) return std::nullopt;
    return std::string(first);
}

void validate_chain(std::vector<std::string> const& tokens) {
    if (tokens.empty()) throw usage_error{"prompt exec: the `prompt` command has no prompts\n"};
    std::vector<char const*> argv;
    argv.reserve(tokens.size() + 1);
    argv.push_back("prompt");
    for (auto const& token : tokens) argv.push_back(token.c_str());
    auto const chain = parse_chain(argv, get_all_prompts());
    if (!chain.error.empty()) throw usage_error{chain.error + "\n"};
}

[[nodiscard]] std::string child_depth() {
    char const* depth = std::getenv("PROMPT_INNER_DEPTH");
    return depth == nullptr ? std::string("0") : std::string(depth);
}

void append_command_output(std::string& out, std::string_view text) {
    if (text.empty()) return;
    if (!out.empty()) out += '\n';
    out += text;
}

} // namespace

static void execute_exec(view const& v, result& r) {
    auto spec = agent::parse_exec_args(v.args);

    policy::engine policy_engine(v.env);
    policy_engine.set_yes(spec.yes);

    std::optional<std::string> const direct = spec.selector.empty() ? direct_prompt_command(v.text, v) : std::nullopt;

    std::vector<std::vector<std::string>> commands;
    if (!spec.command.empty()) {
        // A single token containing whitespace is a command line (`prompt
        // .exec 'git status'`); several tokens are already argv and are used
        // verbatim, so an argument may contain spaces or shell metacharacters.
        if (spec.command.size() == 1 && spec.command.front().find_first_of(" \t\n") != std::string::npos) {
            commands.push_back(agent::split_command(spec.command.front()));
        } else {
            commands.push_back(spec.command);
        }
    } else if (direct.has_value()) {
        commands.push_back(agent::split_command(*direct));
    } else {
        std::string selector = spec.selector;
        if (selector.empty()) {
            if (auto expected = v.prior.get(keys::expect)) selector = agent::primary_selector(*expected);
        }
        if (selector.empty()) selector = "auto";
        if (v.text.empty()) throw soft_error{"prompt exec: no accumulated context to extract a command from\n"};
        for (auto& line : command_lines(extract_text(v, selector))) commands.push_back(agent::split_command(line));
        if (commands.empty()) throw soft_error{"prompt exec: no command in the extracted context\n"};
    }

    // `prompt` alone means the outer chain parser split the intended child
    // chain (`prompt .exec prompt .note hi`): point at the `--` escape.
    if (commands.size() == 1 && commands.front().size() == 1 && is_prompt_command(commands.front().front(), v)) {
        throw usage_error{"prompt exec: put `--` before a `prompt` command so its prompts stay its own:\n"
                          "  prompt ... .exec -- prompt .tse foolad .tse.analyze\n"};
    }

    if (spec.print_only) {
        for (auto const& argv : commands) {
            r.out += agent::join_argv(argv);
            r.out += '\n';
        }
        return;
    }

    for (auto argv : commands) {
        if (argv.empty()) continue;

        std::vector<std::string> extra_env;
        if (is_prompt_command(argv.front(), v)) {
            std::vector<std::string> tokens(argv.begin() + 1, argv.end());
            validate_chain(tokens);
            policy_engine.require_chain(tokens);
            argv.front() = v.env.exe_path.string();
            extra_env = {"PROMPT_INNER=1", "PROMPT_INNER_DEPTH=" + child_depth()};
            if (char const* current = std::getenv("PROMPT_HISTORY_CURRENT"); current != nullptr && *current != '\0')
                extra_env.push_back(std::format("PROMPT_HISTORY_PARENT={}", current));
        } else {
            policy_engine.require(argv);
        }

        process::command command{argv};
        command.timeout(spec.timeout).capture_stderr();
        if (!extra_env.empty()) command.env(extra_env);
        if (spec.stdin_data) command.stdin_data(std::string(v.text));

        auto const result = command.run();
        if (result.timed_out) {
            throw prompt_error{
                1, std::format("prompt exec: timed out after {}s: {}\n", spec.timeout.count(), agent::join_argv(argv)),
                r.out};
        }
        if (result.exit_code != 0) {
            throw prompt_error{result.exit_code, result.stderr_data, r.out};
        }
        r.err += result.stderr_data;
        append_command_output(r.out, result.stdout_data);
    }
}

static void render_help_exec(std::ostream& os) {
    os << R"EOF(Usage: prompt <prompts...> .exec [SELECTOR] [--stdin] [--print] [--yes] [--timeout SEC]
       prompt <prompts...> .exec [--] COMMAND [ARG...]

Run one command and replace the chain context with its output. With no command
arguments the command is extracted from the context with `bin/md.extract`
(selector -> the shape `.expect`/`.delegate` published -> auto). A command that
starts with `prompt` runs a child prompt chain, so an AI-generated delegate
command executes with the chain's normal semantics.

No shell is involved: an extracted command line is split with quotes and
backslashes only; program arguments given directly are used verbatim. Pipes,
redirections, `&&`, substitutions and globs in a command line are refused.
Commands are checked by the policy (`~/.config/prompt/policy`,
`<git-root>/.prompt/policy`, then built-ins); a prompt chain is shown whole
before it runs.

Put `--` before a `prompt` command passed as arguments, or the outer chain
would parse its prompts as its own:
  prompt .exec -- prompt .tse foolad .tse.analyze
Extracted and `--print`ed commands do not need it.
)EOF";
    render_sink_help(os);
    os << R"EOF(
Execution options:
  --stdin         Feed the chain context to the command on stdin
  --print         Print the resolved command(s) without running them
  --yes           Turn policy asks into allows (never overrides a deny)
  --timeout SEC   Per-command timeout (default 600)

Examples:
  prompt .delegate "fix the test" .ai.dseek .extract .exec
  prompt .tse khodro --days 5 .ai.grok .extract command:prompt .exec
  prompt .exec git status
  prompt .exec --print git push origin main
  prompt .exec -- prompt .tse foolad .tse.analyze

)EOF";
}

bool const registered = prompt::register_sink(
    "exec", "Run a command or an AI-generated `prompt` chain and continue the chain with its output.", &execute_exec,
    &render_help_exec);

} // namespace prompt::prompts
