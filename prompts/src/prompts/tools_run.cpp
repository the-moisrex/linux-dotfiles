#include "../sdk/agent.hpp"
#include "../sdk/errors.hpp"
#include "../sdk/policy.hpp"
#include "../sdk/prompt_registry.hpp"
#include "../sdk/sink.hpp"
#include "../sdk/stage.hpp"
#include "../sdk/tool_call.hpp"
#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace prompt::prompts {

namespace {

struct run_options {
    bool print_only = false;
    bool list_only = false;
    bool json = false;
    bool yes = false;
    bool abort_on_error = false;
};

run_options parse_run_args(view const& v) {
    run_options opts;
    for (std::size_t i = 0; i < v.args.size(); ++i) {
        std::string_view const arg = v.args[i];
        if (arg == "--print") {
            opts.print_only = true;
        } else if (arg == "--list") {
            opts.list_only = true;
        } else if (arg == "--json") {
            opts.json = true;
        } else if (arg == "--yes") {
            opts.yes = true;
        } else if (arg == "--on-call-error") {
            if (++i >= v.args.size()) throw usage_error{"tools.run: --on-call-error needs abort or continue\n"};
            std::string_view const value = v.args[i];
            if (value == "abort") {
                opts.abort_on_error = true;
            } else if (value == "continue") {
                opts.abort_on_error = false;
            } else {
                throw usage_error{"tools.run: --on-call-error needs abort or continue\n"};
            }
        } else if (arg == "--on-call-error=continue") {
            opts.abort_on_error = false;
        } else if (arg == "--on-call-error=abort") {
            opts.abort_on_error = true;
        } else {
            throw usage_error{"tools.run: unknown option " + std::string(arg) + "\n"};
        }
    }
    return opts;
}

std::vector<std::string_view> views_of(std::vector<std::string> const& values) {
    std::vector<std::string_view> out;
    out.reserve(values.size());
    for (auto const& value : values) out.push_back(value);
    return out;
}

std::string body_of(result const& res) {
    std::string body = res.out;
    if (!res.err.empty()) {
        if (!body.empty() && !body.ends_with('\n')) body += '\n';
        body += res.err;
    }
    return body;
}

std::string body_of(prompt_error const& err) {
    std::string body = err.partial_output();
    std::string_view const message = err.what();
    if (!message.empty()) {
        if (!body.empty() && !body.ends_with('\n')) body += '\n';
        body += message;
    }
    return body;
}

} // namespace

static void execute_tools_run(view const& v, result& r) {
    run_options const opts = parse_run_args(v);

    auto const calls = tool_call::extract(v.text);
    if (calls.empty()) {
        r.err = "prompt tools.run: no tool calls in the context\n";
        return;
    }

    if (opts.list_only) {
        for (auto const& call : calls) {
            r.err += "tool: " + call.name + " " + call.input.dump() + "\n";
        }
        return;
    }

    if (opts.print_only) {
        for (auto const& call : calls) {
            std::vector<std::string> argv = tool_call::to_argv(call);
            std::vector<std::string> chain{"prompt", call.name};
            chain.insert(chain.end(), argv.begin(), argv.end());
            r.out += agent::join_argv(chain);
            r.out += '\n';
        }
        return;
    }

    policy::engine policy_engine(v.env);
    policy_engine.set_yes(opts.yes);

    nlohmann::ordered_json report = nlohmann::ordered_json::array();
    std::string out;

    for (auto const& call : calls) {
        std::vector<std::string> argv;
        try {
            argv = tool_call::to_argv(call);
        } catch (prompt_error const& err) {
            out += tool_call::format_result(call, body_of(err), err.exit_code(), false);
            report.push_back(nlohmann::ordered_json{
                {"tool", call.name},
                {"input", call.input},
                {"ok", false},
                {"exit", err.exit_code()},
                {"output", body_of(err)},
            });
            if (opts.abort_on_error) throw prompt_error{err.exit_code(), err.what(), out};
            continue;
        }

        std::vector<std::string> chain{call.name};
        chain.insert(chain.end(), argv.begin(), argv.end());
        // A policy deny or a cancelled approval is the user saying no: it always
        // aborts, whatever --on-call-error says.
        policy_engine.require_chain(chain);

        try {
            auto const res = run_sub_prompt(v, call.name, views_of(argv));
            std::string const body = body_of(res);
            out += tool_call::format_result(call, body, 0, true);
            report.push_back(nlohmann::ordered_json{
                {"tool", call.name},
                {"input", call.input},
                {"ok", true},
                {"exit", 0},
                {"output", body},
            });
        } catch (prompt_error const& err) {
            std::string const body = body_of(err);
            out += tool_call::format_result(call, body, err.exit_code(), false);
            report.push_back(nlohmann::ordered_json{
                {"tool", call.name},
                {"input", call.input},
                {"ok", false},
                {"exit", err.exit_code()},
                {"output", body},
            });
            if (opts.abort_on_error) throw prompt_error{err.exit_code(), err.what(), out};
        }
    }

    if (opts.json) {
        r.out = report.dump(2);
        r.out += '\n';
    } else {
        r.out = std::move(out);
    }
}

static void render_help_tools_run(std::ostream& os) {
    os << R"EOF(Usage: prompt <prompts...> .tools.run [--print] [--list] [--json]
                                          [--on-call-error abort|continue] [--yes]

Run the tool calls in the accumulated context and replace it with their results.
A call is a fenced ```json block emitted by an AI answer:

  {"tool": "grep", "input": {"pattern": "TODO", "path": "src"}}

Each call is resolved to a registered prompt that declares an input schema, its
input is parsed into argv, the command is checked by the policy (the same
`prompt ...` chain approval `.exec` uses), and the prompt runs in-process. The
result is fed back as a `### Tool: <name>` block, so a following `.ai.reply`
continues the conversation with the results.

No calls in the context is not an error: the sink leaves the context unchanged
and notes it on stderr, so a plain answer still flows to the next segment.

)EOF";
    render_sink_help(os);
    os << R"EOF(
Tool options:
  --print                  Print the resolved `prompt ...` commands without running
  --list                   List the calls on stderr and keep the context
  --json                   Emit a JSON array of {tool, input, ok, exit, output}
  --on-call-error MODE     abort or continue after a failed call (default: continue)
  --yes                    Turn policy asks into allows (never overrides a deny)

Examples:
  prompt .note "find the TODOs" .tools --for chat --category core .ai.dseek .tools.run
  prompt .note "read main.cpp" .tools --for chat .ai.gap .tools.run --print
  prompt .note "..." .tools --for chat .ai.dseek .tools.run .ai.reply

See also: `.tools` (the catalog), `.exec` (run an argv command), `.extract`.

)EOF";
}

bool const registered = prompt::register_sink(
    "tools.run", "Run the tool calls in the chain context (from an AI answer) and continue with their results.",
    &execute_tools_run, &render_help_tools_run, nullptr, true, nullptr, nullptr, false, true);

} // namespace prompt::prompts
