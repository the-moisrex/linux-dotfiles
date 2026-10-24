#include "../core/fs.hpp"
#include "../core/process.hpp"
#include "../sdk/completion.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/errors.hpp"
#include "../sdk/prompt_registry.hpp"
#include "../sdk/sink.hpp"
#include "../sdk/stage.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <optional>
#include <ostream>
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
bool inspect_only(view const& v) {
    return std::ranges::any_of(v.args, [](std::string_view arg) {
        return arg == "--print-prompt" || arg == "--print-session" || arg == "--list";
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
        << "Providers: chatgpt, grok, deepseek (dseek), gap, kimi — in the order they\n"
        << "should speak.\n"
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
        << "  - Every provider must have a signed-in browser session; sessions are loaded\n"
        << "    up front, before the first turn.\n"
        << "  - The final answer (or --context all) becomes the chain context and clipboard.\n"
        << "    Chain .save after the sink to keep it, or .ai.kimi to summarise the debate.\n"
        << "  - Every debate saves the panel's chats under $XDG_STATE_HOME/ai/; -c resumes\n"
        << "    the last debate between the same providers and --new discards it.\n";
}

void complete_ai_debate(prompt::completion_request const& request, std::ostream& out) {
    using prompt::completion::item;
    static constexpr std::array<item, 6> kProviders = {
        {
            {.value = "chatgpt", .description = "ChatGPT"},
            {.value = "grok", .description = "Grok"},
            {.value = "deepseek", .description = "DeepSeek"},
            {.value = "dseek", .description = "DeepSeek (alias)"},
            {.value = "gap", .description = "GapGPT"},
            {.value = "kimi", .description = "Kimi"},
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

void execute_ai(view const& v, result& r, std::string_view provider) {
    std::string_view const context = v.text;

    std::string const tool_name = "ai." + std::string(provider);
    auto const tool = fs::bin_tool(tool_name, v.env.exe_path, v.env.git_root);
    if (tool.empty() || !std::filesystem::exists(tool)) {
        throw soft_error{"ai." + std::string(provider) + ": " + tool_name + " not found\n"};
    }

    std::vector<std::string> argv;
    argv.reserve(v.args.size() + 4);
    argv.push_back(tool.string());
    // The chain context is stdin. This flag makes bin/ai reject positional
    // prompt text instead of silently ignoring the context.
    argv.emplace_back("--stdin-only");
    // We forward the bytes to the real terminal, so render for one.
    if (::isatty(STDOUT_FILENO) != 0) {
        argv.emplace_back("--tty");
        add_terminal_size(argv);
    }
    bool const chainable = !inspect_only(v);
    answer_file answer;
    if (chainable) argv.emplace_back("--context-file=" + answer.path().string());
    for (std::string_view const arg : v.args) {
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
constexpr ai_provider kDseek{.name = "dseek", .label = "DeepSeek"};
constexpr ai_provider kGap{.name = "gap", .label = "GapGPT"};
constexpr ai_provider kGrok{.name = "grok", .label = "Grok"};
constexpr ai_provider kKimi{.name = "kimi", .label = "Kimi"};
constexpr ai_provider kDebate{.name = "debate", .label = "a panel of AIs"};

void execute_ai_chatgpt(view const& v, result& r) { execute_ai(v, r, kChatgpt.name); }
void execute_ai_dseek(view const& v, result& r) { execute_ai(v, r, kDseek.name); }
void execute_ai_gap(view const& v, result& r) { execute_ai(v, r, kGap.name); }
void execute_ai_grok(view const& v, result& r) { execute_ai(v, r, kGrok.name); }
void execute_ai_kimi(view const& v, result& r) { execute_ai(v, r, kKimi.name); }
void execute_ai_debate(view const& v, result& r) { execute_ai(v, r, kDebate.name); }

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

bool const registered_ai_debate = prompt::register_sink(
    "ai.debate", "Ask two or more AIs the same question and let them verify each other's answers, round after round.",
    &execute_ai_debate, &render_help_ai_debate, &complete_ai_debate);

} // namespace
