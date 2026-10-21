#include "../core/fs.hpp"
#include "../core/process.hpp"
#include "../sdk/errors.hpp"
#include "../sdk/prompt_context.hpp"
#include "../sdk/prompt_registry.hpp"
#include "../sdk/sink.hpp"
#include <chrono>
#include <filesystem>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace prompt;

struct ai_provider {
    std::string_view name;
    std::string_view label;
};

void execute_ai(prompt_context& ctx, std::string_view provider) {
    std::string_view const context = wait_for_prompts(ctx);

    std::string const tool_name = "ai." + std::string(provider);
    auto const tool = fs::bin_tool(tool_name, ctx.exe_path, ctx.git_root);
    if (tool.empty() || !std::filesystem::exists(tool)) {
        throw soft_error{"ai." + std::string(provider) + ": " + tool_name + " not found\n"};
    }

    std::vector<std::string> argv;
    argv.reserve(ctx.args_count + 2);
    argv.push_back(tool.string());
    // The chain context is stdin. This flag makes bin/ai reject positional
    // prompt text instead of silently ignoring the context.
    argv.emplace_back("--stdin-only");
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        std::string_view const arg = ctx.args[i];
        if (arg == "-i" || arg == "--interactive") {
            throw usage_error{"ai." + std::string(provider) +
                              ": -i/--interactive cannot be used in a prompt chain; run it directly\n"};
        }
        argv.emplace_back(arg);
    }

    auto const result = run_sink_tool(ctx, std::move(argv), std::string(context), std::chrono::milliseconds{0});
    ctx.stdin_consumed = ctx.stdin_consumed || !context.empty();

    if (result.exit_code != 0 || result.timed_out) {
        int code = 1;
        if (!result.timed_out && result.exit_code > 0) code = result.exit_code;
        std::string message = "ai." + std::string(provider) + ": ";
        if (result.timed_out) {
            message += "timed out\n";
        } else {
            message += "exited with status " + std::to_string(result.exit_code) + "\n";
        }
        throw prompt_error{code, std::move(message), ctx.out, ctx.no_clipboard, ctx.stdin_consumed};
    }
}

void render_help_ai(std::ostream& out, ai_provider const& provider) {
    out << "Usage: prompt <other prompts...> .ai." << provider.name << " [SINK-OPTIONS] [PROVIDER-OPTIONS]\n"
        << "       prompt .ai." << provider.name << " [SINK-OPTIONS] [PROVIDER-OPTIONS]\n"
        << "\n"
        << "Send the accumulated chain context to " << provider.label
        << " and use the answer as the new chain context.\n"
        << "\n"
        << "The prompt is a sink: it waits until every prompt before it has run, sends their\n"
        << "combined text to " << provider.label << ", and (by default) replaces the running\n"
        << "context with its answer. Sinks may appear anywhere in a chain.\n"
        << "\n";
    render_sink_help(out);
    out << "\n"
        << "Provider options (passed to ai." << provider.name << "):\n"
        << "  -f, --file PATH             Upload and attach a local file (repeatable)\n"
        << "  --prompt-file               Force uploading the prompt as a text file\n"
        << "  --no-prompt-file            Never upload the prompt as a text file\n"
        << "  --prompt-limit BYTES        Override the automatic prompt-file threshold\n"
        << "  -m, --model NAME            Provider model\n"
        << "  -s, --system TEXT           System / custom instructions\n"
        << "  -c, --continue              Continue the last conversation with this provider\n"
        << "  --new                       Start a fresh conversation\n"
        << "  --show-thinking             Show the model's reasoning on stderr\n"
        << "  --no-thinking               Hide the model's reasoning\n"
        << "  --json                      Emit one JSON event per line\n"
        << "  --raw                       Print only the answer text\n"
        << "  --copy                      Copy the answer to the clipboard\n"
        << "  --timeout SECONDS           Request timeout (default: 600)\n"
        << "  --print-prompt              Print the prompt that would be sent and exit\n"
        << "  --list                      List available providers and exit\n"
        << "  --print-session             Show the browser session that would be used\n"
        << "  -h, --help                  Show this help message\n"
        << "\n"
        << "Notes:\n"
        << "  - Use a preceding .note (or --system) for instructions; positional prompt text\n"
        << "    is rejected because stdin carries the chain context.\n"
        << "  - The model's reasoning stays on stderr and is never part of the chain context.\n"
        << "  - --show-context prints the source prompts in the final output; --print-prompt\n"
        << "    prints exactly what this sink receives.\n";
}

constexpr ai_provider kChatgpt{.name = "chatgpt", .label = "ChatGPT"};
constexpr ai_provider kDseek{.name = "dseek", .label = "DeepSeek"};
constexpr ai_provider kGap{.name = "gap", .label = "GapGPT"};
constexpr ai_provider kGrok{.name = "grok", .label = "Grok"};
constexpr ai_provider kKimi{.name = "kimi", .label = "Kimi"};

void execute_ai_chatgpt(prompt_context& ctx) { execute_ai(ctx, kChatgpt.name); }
void execute_ai_dseek(prompt_context& ctx) { execute_ai(ctx, kDseek.name); }
void execute_ai_gap(prompt_context& ctx) { execute_ai(ctx, kGap.name); }
void execute_ai_grok(prompt_context& ctx) { execute_ai(ctx, kGrok.name); }
void execute_ai_kimi(prompt_context& ctx) { execute_ai(ctx, kKimi.name); }

void render_help_ai_chatgpt(std::ostream& out) { render_help_ai(out, kChatgpt); }
void render_help_ai_dseek(std::ostream& out) { render_help_ai(out, kDseek); }
void render_help_ai_gap(std::ostream& out) { render_help_ai(out, kGap); }
void render_help_ai_grok(std::ostream& out) { render_help_ai(out, kGrok); }
void render_help_ai_kimi(std::ostream& out) { render_help_ai(out, kKimi); }

} // namespace

namespace {

bool const registered_ai_chatgpt = prompt::register_sink(
    "ai.chatgpt", "Send the accumulated chain context to ChatGPT and use its answer as the next context.",
    &execute_ai_chatgpt, &render_help_ai_chatgpt);

bool const registered_ai_dseek = prompt::register_sink(
    "ai.dseek", "Send the accumulated chain context to DeepSeek and use its answer as the next context.",
    &execute_ai_dseek, &render_help_ai_dseek);

bool const registered_ai_gap = prompt::register_sink(
    "ai.gap", "Send the accumulated chain context to GapGPT and use its answer as the next context.", &execute_ai_gap,
    &render_help_ai_gap);

bool const registered_ai_grok = prompt::register_sink(
    "ai.grok", "Send the accumulated chain context to Grok and use its answer as the next context.", &execute_ai_grok,
    &render_help_ai_grok);

bool const registered_ai_kimi = prompt::register_sink(
    "ai.kimi", "Send the accumulated chain context to Kimi and use its answer as the next context.", &execute_ai_kimi,
    &render_help_ai_kimi);

} // namespace
