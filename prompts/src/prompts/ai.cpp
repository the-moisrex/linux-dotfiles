#include "../core/fs.hpp"
#include "../core/process.hpp"
#include "../sdk/completion.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/errors.hpp"
#include "../sdk/facts.hpp"
#include "../sdk/prompt_registry.hpp"
#include "../sdk/sink.hpp"
#include "../sdk/stage.hpp"
#include "../sdk/terminal.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <sys/ioctl.h>
#include <unistd.h>

namespace {

using namespace prompt;

struct ai_provider {
    std::string_view name;
    std::string_view label;
    //: Speaks the OpenAI-compatible HTTP API instead of replaying a web session.
    bool api = false;
    //: A local server the tool discovers on its own (LM Studio).
    bool local = false;
    // GCC's -Wmissing-field-initializers wants a member initializer so the
    // designators above stay warning-free; clang-tidy calls the empty ones
    // redundant. Keep both quiet, as `stage.hpp` does.
    // NOLINTBEGIN(readability-redundant-member-init)
    std::string_view base_url{}; // default API root, empty = discovered
    std::string_view base_url_env{};
    std::string_view key_env{};
    std::string_view model_env{};
    std::string_view default_model{}; // used when no -m/env model is set
    // NOLINTEND(readability-redundant-member-init)
};

// The provider writes to a pipe when it runs as a sink, so its stdout is
// display-only: rich Markdown frames (or --json events) must never reach the
// clipboard or the next prompt. `bin/ai --context-file` carries the plain
// answer out of band; the file is removed on every path, success or failure.
class answer_file {
  public:
    answer_file() : path_(make_path()) {}
    ~answer_file() { remove(); }
    answer_file(answer_file const&) = delete;
    answer_file& operator=(answer_file const&) = delete;

    [[nodiscard]] std::filesystem::path const& path() const { return path_; }

    // The answer written so far, or `nullopt` when the tool never created the
    // file (a pre-flight failure, --print-prompt, …). Followed by a best-effort
    // unlink so no answer is left behind.
    [[nodiscard]] std::optional<std::string> take() {
        std::error_code ec;
        bool const present = std::filesystem::is_regular_file(path_, ec);
        std::string text = present ? read_file(path_) : std::string{};
        remove();
        if (!present) return std::nullopt;
        return text;
    }

  private:
    static std::filesystem::path make_path() {
        static std::atomic<unsigned> counter{0};
        std::error_code ec;
        auto dir = std::filesystem::temp_directory_path(ec);
        if (ec) dir = std::filesystem::current_path(ec);
        std::string const name = "prompt-ai-" + std::to_string(static_cast<long long>(::getpid())) + "-" +
                                 std::to_string(counter.fetch_add(1, std::memory_order_relaxed)) + ".txt";
        return dir / name;
    }

    void remove() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    std::filesystem::path path_;
};

// The child's stdout/stderr are pipes, so a renderer cannot measure the
// terminal prompt forwards to. Pass the size read from the real one.
void add_terminal_size(std::vector<std::string>& argv) {
    winsize size{};
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) != 0) return;
    if (size.ws_col != 0) argv.emplace_back("--width=" + std::to_string(size.ws_col));
    if (size.ws_row != 0) argv.emplace_back("--height=" + std::to_string(size.ws_row));
}

// --print-prompt, --print-session and --list answer on stdout without an
// assistant turn, so there is no plain answer to read back.
bool inspect_only(std::span<std::string_view const> args) {
    return std::ranges::any_of(args, [](std::string_view arg) {
        return arg == "--print-prompt" || arg == "--print-session" || arg == "--list" || arg == "--models";
    });
}

void render_help_ai_debate(std::ostream& out) {
    out << "Usage: prompt <other prompts...> .ai.debate PROVIDER PROVIDER [PROVIDER...] [DEBATE-OPTIONS]\n"
        << "       prompt .ai.debate PROVIDER PROVIDER [PROVIDER...] [DEBATE-OPTIONS]\n"
        << "\n"
        << "Ask two or more AIs the same question, then let each verify the previous answer:\n"
        << "the first answer is passed to the next AI as \"Another AI said: …\" with a\n"
        << "verification request, and every later turn carries the newest answer around the\n"
        << "ring. Each AI keeps its own conversation, so later turns only carry the other\n"
        << "AI's latest answer.\n"
        << "\n"
        << "The prompt is a sink: it waits until every prompt before it has run, sends their\n"
        << "combined text to the panel as the question, and (by default) replaces the\n"
        << "running context with the last answer. Sinks may appear anywhere in a chain.\n"
        << "\n"
        << "Providers: chatgpt, claude, grok, deepseek (dseek), gap, kimi, perplexity (pplx),\n"
        << "gemini — plus the API providers lms (lmstudio, a local LM Studio server),\n"
        << "openai (oai, OPENAI_API_KEY) and openrouter (OPENROUTER_API_KEY) — in the\n"
        << "order they should speak.\n"
        << "\n";
    render_sink_help(out);
    out << "\n"
        << "Debate options (passed to ai.debate):\n"
        << "  --rounds N                  Full passes through the providers (default: 2)\n"
        << "  --until-agree               Stop after a pass in which every AI votes AGREE\n"
        << "  --instruction TEXT          Verification request appended to every turn\n"
        << "  --no-instruction            Relay the answer with no verification request\n"
        << "  --context last|all          Last answer (default) or the labelled transcript\n"
        << "  --transcript                Same as --context all\n"
        << "  -c, --continue              Continue each provider's last debate chat\n"
        << "  --new                       Start fresh chats and forget the saved debate\n"
        << "  -m, --model PROVIDER=MODEL  Pick one provider's model (repeatable)\n"
        << "  -s, --system TEXT           System / custom instructions for every provider\n"
        << "  -f, --file PATH             Attach a local file to each first turn (repeatable)\n"
        << "  --prompt-file               Force uploading the prompt as a text file\n"
        << "  --no-prompt-file            Never upload the prompt as a text file\n"
        << "  --prompt-limit BYTES        Override each provider's prompt-file threshold\n"
        << "  --base-url URL              API root for the lms/openai providers\n"
        << "  --api-key KEY               API key for the lms/openai providers\n"
        << "  --param KEY=VALUE           Extra request parameter for them (repeatable)\n"
        << "  --show-thinking             Show the models' reasoning on stderr\n"
        << "  --no-thinking               Hide the models' reasoning\n"
        << "  --json                      Emit one JSON event per line (provider/round tags)\n"
        << "  --raw                       Print only the answers\n"
        << "  --tty                       Render as if stdout/stderr were terminals\n"
        << "  --copy                      Copy the chosen context to the clipboard\n"
        << "  --timeout SECONDS           Per-request timeout (default: 600)\n"
        << "  --print-prompt              Print the first turn and template, then exit\n"
        << "  --print-session             Show each provider's session and exit\n"
        << "  --list                      List the available providers and exit\n"
        << "  -h, --help                  Show this help message\n"
        << "\n"
        << "Notes:\n"
        << "  - Use a preceding .note (or --system) for instructions; positional prompt text\n"
        << "    is rejected because stdin carries the question.\n"
        << "  - Every web provider needs a signed-in browser session; the API providers\n"
        << "    (lms, openai) need their local server / API key instead. Sessions are loaded\n"
        << "    up front, before the first turn.\n"
        << "  - The final answer (or --context all) becomes the chain context and clipboard.\n"
        << "    Chain .save after the sink to keep it, or .ai.kimi to summarise the debate.\n"
        << "  - Every debate saves the panel's chats under $XDG_STATE_HOME/ai/; -c resumes\n"
        << "    the last debate between the same providers and --new discards it.\n";
}

void complete_ai_debate(prompt::completion_request const& request, std::ostream& out) {
    using prompt::completion::item;
    static constexpr std::array<item, 15> kProviders = {
        {
            {.value = "chatgpt", .description = "ChatGPT"},
            {.value = "claude", .description = "Claude"},
            {.value = "grok", .description = "Grok"},
            {.value = "deepseek", .description = "DeepSeek"},
            {.value = "dseek", .description = "DeepSeek (alias)"},
            {.value = "gap", .description = "GapGPT"},
            {.value = "gemini", .description = "Gemini"},
            {.value = "kimi", .description = "Kimi"},
            {.value = "perplexity", .description = "Perplexity"},
            {.value = "pplx", .description = "Perplexity (alias)"},
            {.value = "lms", .description = "LM Studio (local)"},
            {.value = "lmstudio", .description = "LM Studio (alias)"},
            {.value = "openai", .description = "OpenAI API"},
            {.value = "oai", .description = "OpenAI API (alias)"},
            {.value = "openrouter", .description = "OpenRouter"},
        },
    };
    // -m/--model takes PROVIDER=MODEL, so complete the provider half there.
    if (!request.prior_args.empty() && (request.prior_args.back() == "-m" || request.prior_args.back() == "--model")) {
        for (auto const& provider : kProviders) {
            std::string value{provider.value};
            value += '=';
            if (prompt::completion::matches(request.prefix, value))
                prompt::completion::write_item(out, value, provider.description);
        }
        return;
    }
    prompt::completion::write_items(out, request.prefix, kProviders);
}

// The state directory `bin/ai/state.py` uses, so `.ai.reply` finds the same
// per-terminal marker the provider just wrote.
std::filesystem::path ai_state_directory() {
    if (char const* xdg = std::getenv("XDG_STATE_HOME"); xdg != nullptr && *xdg != '\0') {
        return std::filesystem::path(xdg) / "ai";
    }
    if (char const* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".local" / "state" / "ai";
    }
    return {};
}

// The provider that talked most recently (this terminal, or globally).
std::optional<std::string> last_ai_provider(bool global) {
    auto dir = ai_state_directory();
    if (dir.empty()) return std::nullopt;
    if (!global) {
        dir /= "terminals";
        dir /= terminal::scope(terminal::current());
    }
    std::string text = read_file(dir / "last");
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '\t')) {
        text.pop_back();
    }
    if (text.empty()) return std::nullopt;
    return text;
}

constexpr std::array<std::string_view, 11> kReplyProviders{
    "chatgpt", "claude", "dseek", "gap", "gemini", "grok", "kimi", "perplexity", "lms", "openai", "openrouter",
};

std::optional<std::string> canonical_provider(std::string_view name) {
    if (name.starts_with("ai.")) name.remove_prefix(3);
    if (name == "deepseek") return std::string{"dseek"};
    if (name == "lmstudio") return std::string{"lms"};
    if (name == "oai") return std::string{"openai"};
    if (name == "pplx") return std::string{"perplexity"};
    for (std::string_view const provider : kReplyProviders) {
        if (name == provider) return std::string(provider);
    }
    return std::nullopt;
}

void execute_ai_argv(view const& v, result& r, std::string_view provider, std::span<std::string_view const> args,
                     std::string_view context) {
    std::string const tool_name = "ai." + std::string(provider);
    auto const tool = fs::bin_tool(tool_name, v.env.exe_path, v.env.git_root);
    if (tool.empty() || !std::filesystem::exists(tool)) {
        throw soft_error{"ai." + std::string(provider) + ": " + tool_name + " not found\n"};
    }

    std::vector<std::string> argv;
    argv.reserve(args.size() + 5);
    argv.push_back(tool.string());
    // The chain context is stdin. This flag makes bin/ai reject positional
    // prompt text instead of silently ignoring the context.
    argv.emplace_back("--stdin-only");
    // We forward the bytes to the real terminal, so render for one.
    if (::isatty(STDOUT_FILENO) != 0) {
        argv.emplace_back("--tty");
        add_terminal_size(argv);
    }
    bool const chainable = !inspect_only(args);
    answer_file answer;
    if (chainable) argv.emplace_back("--context-file=" + answer.path().string());
    // Mirror every prompt-driven chat under its terminal, so `.ai.reply` can
    // continue exactly the one this terminal used. The debate tool mirrors its
    // participants itself and has no such flag.
    if (provider != "debate") {
        argv.emplace_back("--terminal-scope=" + terminal::scope(terminal::current()));
    }
    for (std::string_view const arg : args) {
        if (arg == "-i" || arg == "--interactive") {
            throw usage_error{"ai." + std::string(provider) +
                              ": -i/--interactive cannot be used in a prompt chain; run it directly\n"};
        }
        argv.emplace_back(arg);
    }

    auto const result = run_sink_tool(v, r, std::move(argv), std::string(context), std::chrono::milliseconds{0});

    if (chainable) {
        // The styled stream already reached the terminal; the chain context and
        // the clipboard take the plain answer instead. An empty answer still
        // replaces the (styled) stream, so no frames leak onward.
        if (auto plain = answer.take()) r.out = std::move(*plain);
    }

    if (result.exit_code != 0 || result.timed_out) {
        int code = 1;
        if (!result.timed_out && result.exit_code > 0) code = result.exit_code;
        std::string message = "ai." + std::string(provider) + ": ";
        if (result.timed_out) {
            message += "timed out\n";
        } else {
            message += "exited with status " + std::to_string(result.exit_code) + "\n";
        }
        throw prompt_error{code, std::move(message), r.out, r.no_clipboard, v.in.piped()};
    }

    if (chainable) {
        // Facts for a later `.ai.reply` in the same chain: which provider just
        // answered, and the answer it already holds.
        r.published.set(keys::ai_provider, provider);
        if (!r.out.empty()) r.published.set(keys::ai_answer, r.out);
    }
}

void execute_ai(view const& v, result& r, std::string_view provider) {
    execute_ai_argv(v, r, provider, v.args, v.text);
}

// `.ai.reply`: continue the chat this terminal talked to last (or the one the
// previous AI sink in this chain started), sending only the newly appended
// text. Provider options are forwarded unchanged; `-c` and `--reply` are ours.
void execute_ai_reply(view const& v, result& r) {
    std::string const prefix = "ai.reply: ";
    std::string provider;
    bool global = false;
    std::vector<std::string_view> forward;

    for (std::size_t i = 0; i < v.args.size(); ++i) {
        std::string_view const arg = v.args[i];
        if (arg == "--provider") {
            if (i + 1 >= v.args.size()) throw usage_error{prefix + "--provider needs a name\n"};
            provider = std::string(v.args[++i]);
            continue;
        }
        if (arg.starts_with("--provider=")) {
            provider = std::string(arg.substr(std::string_view{"--provider="}.size()));
            continue;
        }
        if (arg == "--any" || arg == "--global") {
            global = true;
            continue;
        }
        if (arg == "--here") continue; // default
        forward.push_back(arg);
    }

    // A leading bare provider (`prompt .ai.reply gap`) is friendlier than the
    // explicit flag; anything else stays a provider option.
    if (provider.empty() && !forward.empty()) {
        if (auto const canonical = canonical_provider(forward.front())) {
            provider = *canonical;
            forward.erase(forward.begin());
        }
    }

    if (provider.empty()) {
        if (auto const prior = v.prior.get(keys::ai_provider)) {
            if (*prior == "debate") {
                throw usage_error{prefix + "the last AI here was a debate; name a provider, e.g. .ai.reply grok\n"};
            }
            if (auto const canonical = canonical_provider(*prior)) provider = *canonical;
        }
    }
    if (provider.empty()) {
        if (auto const last = last_ai_provider(global)) {
            if (*last == "debate") {
                throw usage_error{prefix +
                                  "the last AI chat here was a debate; name a provider, e.g. .ai.reply grok\n"};
            }
            if (auto const canonical = canonical_provider(*last)) {
                provider = *canonical;
            } else {
                throw soft_error{prefix + "unknown provider in the saved terminal state: " + *last + "\n"};
            }
        }
    }
    if (provider.empty()) {
        throw soft_error{prefix + "no AI chat found" + (global ? " yet" : " in this terminal yet") +
                         "; start one with .ai.<provider>, or name a provider\n"};
    }

    // The chat already holds the previous answer, so sending it again would
    // duplicate it. Drop it when it is exactly the context prefix (the usual
    // `<answer> + blank line + new instruction` shape) and send only the rest.
    std::string_view context = v.text;
    if (auto const previous = v.prior.get(keys::ai_answer)) {
        std::string_view answer = *previous;
        while (!answer.empty() && (answer.back() == '\n' || answer.back() == '\r')) answer.remove_suffix(1);
        auto const is_space = [](char c) { return c == '\n' || c == '\r' || c == ' ' || c == '\t'; };
        if (!answer.empty() && context.starts_with(answer)) {
            std::string_view rest = context.substr(answer.size());
            if (rest.empty() || is_space(rest.front())) {
                while (!rest.empty() && is_space(rest.front())) rest.remove_prefix(1);
                context = rest;
            }
        }
    }
    if (context.empty()) {
        throw usage_error{prefix + "nothing new to send; put the reply in a .note before .ai.reply\n"};
    }

    std::vector<std::string_view> args;
    args.reserve(forward.size() + 2);
    args.emplace_back("-c");
    // `--any` continues the globally newest chat, which a terminal mirror may
    // not hold; the default continues this terminal's own chat.
    if (!global) args.emplace_back("--reply");
    args.insert(args.end(), forward.begin(), forward.end());
    execute_ai_argv(v, r, provider, args, context);
}

// The API providers speak an OpenAI-compatible HTTP API, so they have no
// browser session, no upload endpoint and no prompt-file fallback: the flags
// are the endpoint, the key and the raw inference parameters.
void render_api_provider_options(std::ostream& out, ai_provider const& provider) {
    out << "Provider options (passed to ai." << provider.name << "):\n"
        << "  -f, --file PATH             Attach a local file to the first message: text is\n"
        << "                              inlined, images become data URLs (repeatable)\n"
        << "  -m, --model NAME            Model id (default: $" << provider.model_env;
    if (!provider.default_model.empty()) out << ", else " << provider.default_model;
    if (provider.local) out << ", else the first chat model the server lists";
    out << ")\n"
        << "  -s, --system TEXT           System / custom instructions\n"
        << "  -c, --continue              Continue the last conversation with this provider\n"
        << "  --new                       Start a fresh conversation\n"
        << "  --base-url URL              API root (default: $" << provider.base_url_env;
    if (provider.local) {
        out << ", then the cached\n"
            << "                              endpoint, then the usual local ports)\n";
    } else {
        out << ", then\n"
            << "                              " << provider.base_url << ")\n";
    }
    out << "  --api-key KEY               API key (default: $" << provider.key_env << ")\n"
        << "  --param KEY=VALUE           Extra request parameter, e.g. temperature=0.2 (repeatable)\n"
        << "  --show-thinking             Show the model's reasoning on stderr\n"
        << "  --no-thinking               Hide the model's reasoning\n"
        << "  --json                      Emit one JSON event per line\n"
        << "  --raw                       Print only the answer text\n"
        << "  --tty                       Render as if stdout/stderr were terminals (pipe-friendly)\n"
        << "  --copy                      Copy the answer to the clipboard\n"
        << "  --timeout SECONDS           Request timeout (default: 600)\n"
        << "  --print-prompt              Print the prompt that would be sent and exit\n"
        << "  --print-session             Show the endpoint, model and reachability, then exit\n"
        << "  --list                      List the available providers and models, then exit\n"
        << "  -h, --help                  Show this help message\n"
        << "\n"
        << "Notes:\n"
        << "  - Use a preceding .note (or --system) for instructions; positional prompt text\n"
        << "    is rejected because stdin carries the chain context.\n"
        << "  - The model's reasoning stays on stderr and is never part of the chain context.\n"
        << "  - There is no browser session and no upload endpoint: -f files are inlined\n"
        << "    into the message, --prompt-file is refused, and an oversized prompt is sent\n"
        << "    inline (raise the model's context length instead).\n";
    if (provider.local) {
        out << "  - LM Studio must be running its local server (`lms server start`, or\n"
            << "    Developer → Local Server). The endpoint comes from --base-url, $" << provider.base_url_env
            << ", the\n"
            << "    cached one, then the usual local ports, then `lms status`.\n";
    } else {
        out << "  - Any OpenAI-compatible endpoint works (OpenRouter, Groq, vLLM): set $" << provider.base_url_env
            << "\n"
            << "    to its API root and $OPENAI_MODEL to its model id.\n";
    }
    out << "  - --show-context prints the source prompts in the final output; --print-prompt\n"
        << "    prints exactly what this sink receives.\n";
}

void render_help_ai_reply(std::ostream& out) {
    out << "Usage: prompt <other prompts...> .ai.reply [PROVIDER] [--provider NAME] [--any] [SINK-OPTIONS] "
           "[PROVIDER-OPTIONS]\n"
        << "       prompt .ai.reply [PROVIDER] [--provider NAME] [--any] [SINK-OPTIONS] [PROVIDER-OPTIONS]\n"
        << "\n"
        << "Reply in the AI chat this terminal talked to last and use its answer as the new\n"
        << "chain context, e.g. after `.ai.gap`:\n"
        << "  prompt .note \"implement it; switch to build mode\" .ai.reply\n"
        << "\n"
        << "The provider is inferred: an explicit PROVIDER/--provider, else the AI sink\n"
        << "earlier in the same chain, else the chat this terminal sent last (read from the\n"
        << "per-terminal state bin/ai keeps). `--any` reads the globally newest chat. Every\n"
        << "provider option is forwarded as `.ai.<provider>` would receive it; the `-c` and\n"
        << "`--reply` state flags are supplied for you.\n"
        << "\n";
    render_sink_help(out);
    out << "\n"
        << "Reply options:\n"
        << "  PROVIDER, --provider NAME  Use this provider: chatgpt, claude, dseek, gap, gemini, grok,\n"
        << "                             kimi, perplexity, lms, openai, openrouter\n"
        << "  --any, --global            Use the globally newest chat instead of this terminal's\n"
        << "  --here                     Require this terminal (the default)\n"
        << "  -h, --help                 Show this help message\n"
        << "\n"
        << "Notes:\n"
        << "  - Chained right after an AI sink, the previous answer is dropped from the\n"
        << "    message: only the text appended after it (usually a `.note`) is sent.\n"
        << "  - All provider options are accepted (`-m`, `-s`, `-f`, `--new`, `--show-thinking`,\n"
        << "    `--print-session`, …); see `.ai.<provider> --help`.\n"
        << "  - This continues the single-provider chats. When the last chat here was a\n"
        << "    debate, name a provider (`.ai.reply grok`) to continue its side.\n"
        << "\n"
        << "Examples:\n"
        << "  prompt .note \"implement it\" .ai.reply\n"
        << "  prompt ... .plan .ai.gap .note \"impl it; switch to build mode\" .ai.reply\n"
        << "  prompt .note \"continue on a smaller model\" .ai.reply --provider dseek -m deepseek-chat\n"
        << "  prompt .last.ai .note \"compare with the above\" .ai.reply\n";
}

void complete_ai_reply(prompt::completion_request const& request, std::ostream& out) {
    using prompt::completion::item;
    static constexpr std::array<item, 15> kProviders = {
        {
            {.value = "chatgpt", .description = "ChatGPT"},
            {.value = "claude", .description = "Claude"},
            {.value = "dseek", .description = "DeepSeek"},
            {.value = "deepseek", .description = "DeepSeek (alias)"},
            {.value = "gap", .description = "GapGPT"},
            {.value = "gemini", .description = "Gemini"},
            {.value = "grok", .description = "Grok"},
            {.value = "kimi", .description = "Kimi"},
            {.value = "perplexity", .description = "Perplexity"},
            {.value = "pplx", .description = "Perplexity (alias)"},
            {.value = "lms", .description = "LM Studio (local)"},
            {.value = "lmstudio", .description = "LM Studio (alias)"},
            {.value = "openai", .description = "OpenAI API"},
            {.value = "oai", .description = "OpenAI API (alias)"},
            {.value = "openrouter", .description = "OpenRouter"},
        },
    };
    if (prompt::completion::write_values_after_flag(out, request, "--provider", kReplyProviders)) return;
    prompt::completion::write_items(out, request.prefix, kProviders);
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
    out << "\n";
    if (provider.api) {
        render_api_provider_options(out, provider);
        return;
    }
    out << "Provider options (passed to ai." << provider.name << "):\n"
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
        << "  --tty                       Render as if stdout/stderr were terminals (pipe-friendly)\n"
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
constexpr ai_provider kClaude{.name = "claude", .label = "Claude"};
constexpr ai_provider kDseek{.name = "dseek", .label = "DeepSeek"};
constexpr ai_provider kGap{.name = "gap", .label = "GapGPT"};
constexpr ai_provider kGemini{.name = "gemini", .label = "Gemini"};
constexpr ai_provider kGrok{.name = "grok", .label = "Grok"};
constexpr ai_provider kKimi{.name = "kimi", .label = "Kimi"};
constexpr ai_provider kPerplexity{.name = "perplexity", .label = "Perplexity"};
constexpr ai_provider kLms{
    .name = "lms",
    .label = "a local LM Studio server",
    .api = true,
    .local = true,
    .base_url_env = "LMS_BASE_URL",
    .key_env = "LMS_API_KEY",
    .model_env = "LMS_MODEL",
};
constexpr ai_provider kOpenai{
    .name = "openai",
    .label = "the OpenAI API",
    .api = true,
    .base_url = "https://api.openai.com/v1",
    .base_url_env = "OPENAI_BASE_URL",
    .key_env = "OPENAI_API_KEY",
    .model_env = "OPENAI_MODEL",
};
constexpr ai_provider kOpenrouter{
    .name = "openrouter",
    .label = "OpenRouter",
    .api = true,
    .base_url = "https://openrouter.ai/api/v1",
    .base_url_env = "OPENROUTER_BASE_URL",
    .key_env = "OPENROUTER_API_KEY",
    .model_env = "OPENROUTER_MODEL",
    .default_model = "openrouter/auto-code",
};
constexpr ai_provider kDebate{.name = "debate", .label = "a panel of AIs"};

void execute_ai_chatgpt(view const& v, result& r) { execute_ai(v, r, kChatgpt.name); }
void execute_ai_claude(view const& v, result& r) { execute_ai(v, r, kClaude.name); }
void execute_ai_dseek(view const& v, result& r) { execute_ai(v, r, kDseek.name); }
void execute_ai_gap(view const& v, result& r) { execute_ai(v, r, kGap.name); }
void execute_ai_gemini(view const& v, result& r) { execute_ai(v, r, kGemini.name); }
void execute_ai_grok(view const& v, result& r) { execute_ai(v, r, kGrok.name); }
void execute_ai_kimi(view const& v, result& r) { execute_ai(v, r, kKimi.name); }
void execute_ai_perplexity(view const& v, result& r) { execute_ai(v, r, kPerplexity.name); }
void execute_ai_lms(view const& v, result& r) { execute_ai(v, r, kLms.name); }
void execute_ai_openai(view const& v, result& r) { execute_ai(v, r, kOpenai.name); }
void execute_ai_openrouter(view const& v, result& r) { execute_ai(v, r, kOpenrouter.name); }
void execute_ai_debate(view const& v, result& r) { execute_ai(v, r, kDebate.name); }

void render_help_ai_chatgpt(std::ostream& out) { render_help_ai(out, kChatgpt); }
void render_help_ai_claude(std::ostream& out) { render_help_ai(out, kClaude); }
void render_help_ai_dseek(std::ostream& out) { render_help_ai(out, kDseek); }
void render_help_ai_gap(std::ostream& out) { render_help_ai(out, kGap); }
void render_help_ai_gemini(std::ostream& out) { render_help_ai(out, kGemini); }
void render_help_ai_grok(std::ostream& out) { render_help_ai(out, kGrok); }
void render_help_ai_kimi(std::ostream& out) { render_help_ai(out, kKimi); }
void render_help_ai_perplexity(std::ostream& out) { render_help_ai(out, kPerplexity); }
void render_help_ai_lms(std::ostream& out) { render_help_ai(out, kLms); }
void render_help_ai_openai(std::ostream& out) { render_help_ai(out, kOpenai); }
void render_help_ai_openrouter(std::ostream& out) { render_help_ai(out, kOpenrouter); }

} // namespace

namespace {

bool const registered_ai_chatgpt = prompt::register_sink(
    "ai.chatgpt", "Send the accumulated chain context to ChatGPT and use its answer as the next context.",
    &execute_ai_chatgpt, &render_help_ai_chatgpt);

bool const registered_ai_claude = prompt::register_sink(
    "ai.claude", "Send the accumulated chain context to Claude and use its answer as the next context.",
    &execute_ai_claude, &render_help_ai_claude);

bool const registered_ai_dseek = prompt::register_sink(
    "ai.dseek", "Send the accumulated chain context to DeepSeek and use its answer as the next context.",
    &execute_ai_dseek, &render_help_ai_dseek);

bool const registered_ai_gap = prompt::register_sink(
    "ai.gap", "Send the accumulated chain context to GapGPT and use its answer as the next context.", &execute_ai_gap,
    &render_help_ai_gap);

bool const registered_ai_gemini = prompt::register_sink(
    "ai.gemini", "Send the accumulated chain context to Gemini and use its answer as the next context.",
    &execute_ai_gemini, &render_help_ai_gemini);

bool const registered_ai_grok = prompt::register_sink(
    "ai.grok", "Send the accumulated chain context to Grok and use its answer as the next context.", &execute_ai_grok,
    &render_help_ai_grok);

bool const registered_ai_kimi = prompt::register_sink(
    "ai.kimi", "Send the accumulated chain context to Kimi and use its answer as the next context.", &execute_ai_kimi,
    &render_help_ai_kimi);

bool const registered_ai_perplexity = prompt::register_sink(
    "ai.perplexity", "Send the accumulated chain context to Perplexity and use its answer as the next context.",
    &execute_ai_perplexity, &render_help_ai_perplexity);

bool const registered_ai_lms = prompt::register_sink(
    "ai.lms", "Send the accumulated chain context to a local LM Studio model and use its answer as the next context.",
    &execute_ai_lms, &render_help_ai_lms);

bool const registered_ai_openai = prompt::register_sink(
    "ai.openai",
    "Send the accumulated chain context to the OpenAI API (or any OpenAI-compatible endpoint) and use its answer as "
    "the next context.",
    &execute_ai_openai, &render_help_ai_openai);

bool const registered_ai_openrouter = prompt::register_sink(
    "ai.openrouter", "Send the accumulated chain context to OpenRouter and use its answer as the next context.",
    &execute_ai_openrouter, &render_help_ai_openrouter);

bool const registered_ai_debate = prompt::register_sink(
    "ai.debate", "Ask two or more AIs the same question and let them verify each other's answers, round after round.",
    &execute_ai_debate, &render_help_ai_debate, &complete_ai_debate);

bool const registered_ai_reply = prompt::register_sink(
    "ai.reply", "Reply in the last AI chat from this terminal and use its answer as the next context.",
    &execute_ai_reply, &render_help_ai_reply, &complete_ai_reply);

} // namespace
