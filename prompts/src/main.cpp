#include "core/clipboard.hpp"
#include "core/fs.hpp"
#include "core/process.hpp"
#include "core/sanitize.hpp"
#include "sdk/chaining.hpp"
#include "sdk/completion.hpp"
#include "sdk/pipeline.hpp"
#include "sdk/prompt_context.hpp"
#include "sdk/prompt_registry.hpp"
#include "sdk/sink.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <optional>
#include <regex>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

// The prompts shell out to the repository's tools (bin/tse, jq, …) — there is
// no in-process interpreter here, so no ASan suppressions are needed.

namespace {

// A prompt's result, detached from its context so chain segments can be
// executed in any order (or in separate processes) and emitted in argv order.
struct segment_result {
    std::string output;
    std::string error;
    std::string stderr_buffer;
    bool no_clipboard = false;
    bool stdin_consumed = false;
    bool redacted = false;
    bool failed = false;
    bool stdout_streamed = false;
    bool stderr_streamed = false;
    int exit_code = 0;
};

// Parallel segments run in forked children (not threads): `process::command`
// forks, and forking from a multi-threaded parent is not safe. The child sends
// this header + stdout + stderr through a pipe.
struct segment_wire {
    std::uint64_t output_size = 0;
    std::uint64_t error_size = 0;
    std::int32_t exit_code = 0;
    std::uint32_t flags = 0;
};
static_assert(sizeof(segment_wire) == 24);

constexpr std::uint32_t wire_no_clipboard = 1U << 0U;
constexpr std::uint32_t wire_stdin_consumed = 1U << 1U;
constexpr std::uint32_t wire_redacted = 1U << 2U;
constexpr std::uint32_t wire_failed = 1U << 3U;
constexpr std::uint32_t wire_stdout_streamed = 1U << 4U;
constexpr std::uint32_t wire_stderr_streamed = 1U << 5U;

bool write_all(int fd, std::string_view data) {
    std::size_t offset = 0;
    while (offset < data.size()) {
        auto n = ::write(fd, data.data() + offset, data.size() - offset);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        offset += static_cast<std::size_t>(n);
    }
    return true;
}

std::string read_all(int fd) {
    std::string data;
    std::array<char, 8192> buffer{};
    for (;;) {
        auto n = ::read(fd, buffer.data(), buffer.size());
        if (n > 0) {
            data.append(buffer.data(), static_cast<std::size_t>(n));
        } else if (n == 0 || errno != EINTR) {
            break;
        }
    }
    return data;
}

void write_segment(int fd, segment_result const& result) {
    segment_wire wire{
        .output_size = result.output.size(),
        .error_size = result.error.size(),
        .exit_code = result.exit_code,
        .flags = (result.no_clipboard ? wire_no_clipboard : 0U) | (result.stdin_consumed ? wire_stdin_consumed : 0U) |
                 (result.redacted ? wire_redacted : 0U) | (result.failed ? wire_failed : 0U) |
                 (result.stdout_streamed ? wire_stdout_streamed : 0U) |
                 (result.stderr_streamed ? wire_stderr_streamed : 0U),
    };
    if (!write_all(fd, std::string_view(reinterpret_cast<char const*>(&wire), sizeof wire))) return;
    if (!write_all(fd, result.output)) return;
    write_all(fd, result.error);
}

std::optional<segment_result> read_segment(std::string_view blob) {
    if (blob.size() < sizeof(segment_wire)) return std::nullopt;
    segment_wire wire{};
    std::memcpy(&wire, blob.data(), sizeof wire);
    std::size_t const header = sizeof(segment_wire);
    if (wire.output_size > blob.size() - header) return std::nullopt;
    std::size_t const remaining = blob.size() - header - static_cast<std::size_t>(wire.output_size);
    if (wire.error_size > remaining) return std::nullopt;

    segment_result result;
    result.output.assign(blob.data() + header, static_cast<std::size_t>(wire.output_size));
    result.error.assign(blob.data() + header + static_cast<std::size_t>(wire.output_size),
                        static_cast<std::size_t>(wire.error_size));
    result.exit_code = wire.exit_code;
    result.no_clipboard = (wire.flags & wire_no_clipboard) != 0;
    result.stdin_consumed = (wire.flags & wire_stdin_consumed) != 0;
    result.redacted = (wire.flags & wire_redacted) != 0;
    result.failed = (wire.flags & wire_failed) != 0;
    result.stdout_streamed = (wire.flags & wire_stdout_streamed) != 0;
    result.stderr_streamed = (wire.flags & wire_stderr_streamed) != 0;
    return result;
}

segment_result failure_result(std::string message) {
    segment_result result;
    result.error = std::move(message);
    result.failed = true;
    result.exit_code = 1;
    return result;
}

bool invocation_wants_help(prompt::prompt_invocation const& inv) {
    for (std::size_t j = 0; j < inv.args_count; ++j) {
        if (inv.args[j] == "--help" || inv.args[j] == "-h") return true;
    }
    return false;
}

segment_result render_help_result(prompt::prompt_descriptor const& desc) {
    segment_result result;
    std::ostringstream help;
    if (desc.render_help_fn != nullptr) desc.render_help_fn(help);
    auto clean = prompt::sanitize::sanitize(help.str());
    result.output = std::move(clean.text);
    result.redacted = clean.redacted;
    return result;
}

prompt::prompt_context invocation_context(prompt::prompt_context const& base, prompt::prompt_invocation const& inv,
                                          std::size_t index, std::span<std::string_view const> args) {
    auto ctx = base;
    ctx.name = inv.name;
    ctx.args_count = std::min(args.size(), ctx.args.size());
    for (std::size_t j = 0; j < ctx.args_count; ++j) {
        ctx.args[j] = args[j];
    }
    ctx.chain_index = index;
    return ctx;
}

prompt::prompt_context invocation_context(prompt::prompt_context const& base, prompt::prompt_invocation const& inv,
                                          std::size_t index) {
    return invocation_context(base, inv, index, std::span<std::string_view const>(inv.args.data(), inv.args_count));
}

std::string_view trim_trailing_newlines(std::string_view text) {
    while (!text.empty() && text.back() == '\n') text.remove_suffix(1);
    return text;
}

std::string_view trim_block(std::string_view text) {
    while (!text.empty() && text.front() == '\n') text.remove_prefix(1);
    while (!text.empty() && text.back() == '\n') text.remove_suffix(1);
    return text;
}

void append_block(std::string& target, std::string_view block) {
    block = trim_block(block);
    if (block.empty()) return;
    if (!target.empty()) {
        while (!target.empty() && target.back() == '\n') target.pop_back();
        target += "\n\n";
    }
    target.append(block);
    target += '\n';
}

std::string prepend_block(std::string_view prefix, std::string_view body) {
    prefix = trim_trailing_newlines(prefix);
    body = trim_trailing_newlines(body);
    if (prefix.empty()) return std::string(body);
    if (body.empty()) return std::string(prefix);
    std::string out(prefix);
    out += "\n\n";
    out.append(body);
    return out;
}

// Incremental stdout/clipboard emitter for sink chains. Streamed sink output
// has already reached the terminal; it only needs to enter the clipboard and
// affect the separator state.
class output_emitter {
  public:
    void emit(std::string_view output, bool no_clipboard, bool redacted) {
        output = trim_trailing_newlines(output);
        if (output.empty()) return;
        if (!first_stdout_) std::cout << '\n';
        first_stdout_ = false;
        if (redacted && !stdout_legend_) {
            std::cout << prompt::sanitize::kRedactionLegend << '\n';
            stdout_legend_ = true;
        }
        std::cout << output << '\n';
        std::cout.flush();
        add_clipboard(output, no_clipboard, redacted);
    }

    void streamed(std::string_view output, bool no_clipboard, bool redacted) {
        if (!output.empty() && output.back() != '\n') std::cout << '\n';
        std::cout.flush();
        output = trim_trailing_newlines(output);
        if (output.empty()) return;
        first_stdout_ = false;
        add_clipboard(output, no_clipboard, redacted);
    }

    // Called before a streamed sink starts writing, so its live output is
    // separated from the previous segment like a captured result would be.
    void begin_stream() const {
        if (!first_stdout_) {
            std::cout << '\n';
            std::cout.flush();
        }
    }

    [[nodiscard]] std::string clipboard() const { return clipboard_; }

  private:
    void add_clipboard(std::string_view output, bool no_clipboard, bool redacted) {
        if (no_clipboard) return;
        if (!first_clipboard_) clipboard_ += '\n';
        first_clipboard_ = false;
        if (output.empty()) return;
        if (redacted && !clipboard_legend_) {
            clipboard_ = std::string(prompt::sanitize::kRedactionLegend) + "\n" + clipboard_;
            clipboard_legend_ = true;
        }
        clipboard_.append(output);
        clipboard_ += '\n';
    }

    bool first_stdout_ = true;
    bool first_clipboard_ = true;
    bool stdout_legend_ = false;
    bool clipboard_legend_ = false;
    std::string clipboard_;
};

segment_result run_prompt(prompt::prompt_context& ctx, prompt::prompt_descriptor const& desc) {
    segment_result result;
    try {
        prompt::clear_output(ctx);
        desc.execute_fn(ctx);
        auto clean = prompt::sanitize::sanitize(ctx.out);
        result.output = std::move(clean.text);
        result.redacted = clean.redacted;
        result.no_clipboard = ctx.no_clipboard;
        result.stdin_consumed = ctx.stdin_consumed;
        result.error = std::move(ctx.err);
        result.stderr_buffer = std::move(ctx.stderr_buffer);
        result.stdout_streamed = ctx.stdout_streamed;
        result.stderr_streamed = ctx.stderr_streamed;
    } catch (prompt::prompt_error const& err) {
        auto clean = prompt::sanitize::sanitize(err.partial_output());
        result.output = std::move(clean.text);
        result.redacted = clean.redacted;
        result.no_clipboard = err.no_clipboard();
        result.stdin_consumed = err.stdin_consumed();
        result.error = err.what();
        result.stderr_buffer = std::move(ctx.stderr_buffer);
        result.stdout_streamed = ctx.stdout_streamed;
        result.stderr_streamed = ctx.stderr_streamed;
        result.failed = true;
        result.exit_code = err.exit_code();
    }
    return result;
}

} // namespace

int main(int argc, char** argv) {
    // Every prompt registers itself before main (see sdk/prompt_registry.hpp);
    // this only fills in the sort keys list/list-prompts order by.
    prompt::init_prompt_registry();

    // Handle --help and --version early (only as first argument)
    if (argc > 1) {
        std::string_view arg(argv[1]);
        if (arg == "--help" || arg == "-h") {
            // Mirrors the bash dispatcher's usage(); the Notes differ because
            // the native dispatcher ships a fixed prompt set instead of
            // scanning for prompt files.
            std::cout << R"(Usage:
  prompt --help
  prompt list
  prompt list-prompts
  prompt <name> [args...]                # run a single prompt
  prompt <name> [-- arg1 arg2 ...]       # explicit end of prompt flags

Shorthand (chain multiple prompts with args):
  prompt .note "Some details" .cli "pwd" .files src/main.cpp
  prompt .agents .git-dirty .commit

  Tokens starting with '.' or '-' that match a known prompt name are treated
  as prompt invocations. Bare words between them become that prompt's arguments.
  The first argument is also checked as a prompt name when no prompts have been
  matched yet.

Examples:
  cat file.cpp | prompt cpp-debug
  prompt cpp-debug --flag value
  prompt .note "WIP: fixing parser bug" .files parser.h parser.cpp .cli "make test"
  prompt .agents .files one two three .note "Some details" .cli "pwd"
  prompt .note "review this" .files src/main.cpp .ai.dseek

Notes:
  - Ships a fixed set of built-in prompts; it does not scan for prompt files.
    (The bash reference under prompts-shell/ searches $XDG_CONFIG_DIRS/prompts
    and its own directory and runs .sh/.txt/.md prompts.)
  - Sinks (.ai.chatgpt/.ai.dseek/.ai.gap/.ai.grok/.ai.kimi) wait for the
    preceding chain, send its text to the site, and continue with the answer.
    They share --with-context, --show-context, --on-error and --head.
  - 'prompt <name> --complete <prefix> [args...]' prints shell-completion
    candidates for the prompt (used by shell/completions.fish), one
    value<TAB>description per line, without running it.
  - 'prompt list' lists available prompts with the first paragraph of each
    prompt's help as the description (--names for bare names only).
  - 'prompt list-prompts' prints the full help text of every available prompt.
)";
            return 0;
        }
        if (arg == "--version" || arg == "-v") {
            std::cout << "prompt " << PROMPT_VERSION << "\n";
            return 0;
        }
    }

    // Shell completion: `prompt <name> --complete <prefix> [prior args...]`
    // (alias: `prompt --complete <name> [prefix] ...`). Answered here, before
    // the clipboard is initialized and before stdin is read: completion must
    // not consume the pipe, copy anything to the clipboard, or run the prompt.
    // Flags come from each prompt's own help; positional/value candidates come
    // from its optional complete_fn.
    {
        auto arg = [&](int i) { return std::string_view(argv[static_cast<std::size_t>(i)]); };
        std::string_view complete_name;
        std::string_view prefix;
        std::vector<std::string_view> prior;
        bool wants_complete = false;
        if (argc > 1 && arg(1) == "--complete") {
            wants_complete = true;
            if (argc > 2) complete_name = arg(2);
            if (argc > 3) prefix = arg(3);
            for (int i = 4; i < argc; ++i) prior.push_back(arg(i));
        } else if (argc > 2) {
            for (int i = 2; i < argc; ++i) {
                if (arg(i) != "--complete") continue;
                wants_complete = true;
                complete_name = arg(1);
                if (i + 1 < argc) prefix = arg(i + 1);
                for (int j = i + 2; j < argc; ++j) prior.push_back(arg(j));
                break;
            }
        }
        if (wants_complete) {
            // Leading `.`/`-` prefixes are shorthands for the same prompt name.
            while (!complete_name.empty() && (complete_name.front() == '.' || complete_name.front() == '-')) {
                complete_name.remove_prefix(1);
            }
            if (auto const* desc = prompt::find_prompt(complete_name); desc != nullptr) {
                std::ostringstream out;
                if (desc->render_help_fn != nullptr) {
                    std::ostringstream help;
                    desc->render_help_fn(help);
                    prompt::completion::write_help_flags(out, prefix, help.str());
                }
                if (desc->complete_fn != nullptr) {
                    desc->complete_fn(prompt::completion_request{.prefix = prefix, .prior_args = prior}, out);
                }
                std::cout << out.str();
            }
            return 0;
        }
    }

    // Initialize clipboard
    prompt::clipboard::init();

    // Prepare base context
    auto base_ctx = prompt::make_base_context();

    // Sinks stream through these writers. Normal/parallel prompts leave
    // stream_stdout/stream_stderr false; write_stdout() still accumulates.
    base_ctx.stdout_writer = [](std::string_view chunk) {
        std::cout.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        std::cout.flush();
    };
    base_ctx.stderr_writer = [](std::string_view chunk) {
        std::cerr.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        std::cerr.flush();
    };

    // Wire up services
    base_ctx.clipboard_content_fn = &prompt::clipboard::paste;
    base_ctx.clipboard_identifier_fn = [] -> std::optional<std::string> {
        auto content = prompt::clipboard::paste();
        if (content.empty()) return std::nullopt;

        std::string text = content;
        text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
        text.erase(std::remove(text.begin(), text.end(), '\n'), text.end());

        static const std::regex isin_re(R"(IR[A-Za-z0-9]{10})");
        static const std::regex inscode_re(R"([0-9]{15,})");
        static const std::regex easytrader_re(
            R"(https?://d\.easytrader\.ir/(easy-chart|stock-details)/[A-Za-z0-9]{12})");
        static const std::regex tsetmc_re(R"(https?://(www\.)?tsetmc\.com/instInfo/[0-9]{15,})");
        static const std::regex codal_re(
            R"(https?://(www\.)?codal\.ir/ReportList\.aspx\?([^&]*&)?Symbol=[^&[:space:]]+)");

        std::smatch match;
        if (std::regex_search(text, match, easytrader_re) || std::regex_search(text, match, tsetmc_re) ||
            std::regex_search(text, match, codal_re) || std::regex_search(text, match, isin_re) ||
            std::regex_search(text, match, inscode_re)) {
            return match[0];
        }

        if (!text.empty() && text.size() <= 64) {
            bool has_latin = false;
            for (char c : text) {
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
                    has_latin = true;
                    break;
                }
            }
            if (!has_latin) return text;
        }

        return std::nullopt;
    };

    base_ctx.select_files_fn = [] -> std::vector<std::filesystem::path> {
        auto result = prompt::process::run_command(std::vector<std::string>{"fzf", "-m"});
        std::vector<std::filesystem::path> files;
        std::string line;
        for (char c : result.stdout_data) {
            if (c == '\n') {
                if (!line.empty()) {
                    files.emplace_back(line);
                    line.clear();
                }
            } else {
                line += c;
            }
        }
        if (!line.empty()) files.emplace_back(line);
        return files;
    };

    base_ctx.resolve_input_file_fn = [](std::string_view name) -> std::optional<std::filesystem::path> {
        std::filesystem::path p(name);
        if (std::filesystem::is_regular_file(p)) return p;

        auto git_root = prompt::fs::find_git_root();
        if (git_root) {
            auto full = *git_root / name;
            if (std::filesystem::is_regular_file(full)) return full;
        }

        // _common.sh resolve_input_file: filter the repo's files with fzf.
        // stderr is silenced (fzf complains without a tty) because most
        // callers capture the function through a command substitution.
        std::string name_str(name);
        std::vector<std::string> storage = {"bash", "-c"};
        if (git_root) {
            storage.emplace_back("cd \"$0\" && git ls-files --cached --others --exclude-standard 2>/dev/null | "
                                 "fzf -f \"$1\" 2>/dev/null | head -n 1");
            storage.push_back(git_root->string());
        } else {
            storage.emplace_back("rg --files 2>/dev/null || find . -type f | fzf -f \"$0\" 2>/dev/null | head -n 1");
        }
        storage.push_back(name_str);

        auto result = prompt::process::run_command(storage, "");
        if (!result.stdout_data.empty()) {
            std::string line = result.stdout_data.substr(0, result.stdout_data.find('\n'));
            std::filesystem::path selected(line);
            if (std::filesystem::is_regular_file(selected)) return selected;
        }

        return std::nullopt;
    };

    // Read stdin if available. The bash dispatcher only tests for a pipe
    // ([ -t 0 ]), so an empty pipe still counts as piped stdin.
    if (isatty(STDIN_FILENO) == 0) {
        std::string stdin_content;
        std::string line;
        while (std::getline(std::cin, line)) {
            stdin_content += line;
            stdin_content += '\n';
        }
        base_ctx.stdin_content = std::move(stdin_content);
        base_ctx.stdin_consumed = true;
    }

    // Parse chained prompts
    std::span<char const* const> argv_span(argv, static_cast<std::size_t>(argc));
    auto chain = prompt::parse_chain(argv_span, prompt::get_all_prompts());

    // If no prompts found, default to "auto"
    if (chain.count == 0) {
        chain.invocations[0].name = "auto";
        chain.count = 1;
    }

    // Expose the whole chain to the prompts, so related prompts can borrow a
    // sibling's arguments (e.g. `.tse foolad .tse.codal .tse.intraday`).
    base_ctx.chain.clear();
    base_ctx.chain.reserve(chain.count);
    std::vector<prompt::prompt_descriptor const*> descriptors(chain.count, nullptr);
    for (std::size_t i = 0; i < chain.count; ++i) {
        auto const& inv = chain.invocations[i];
        prompt::chain_segment segment;
        segment.name = inv.name;
        for (std::size_t j = 0; j < inv.args_count; ++j) segment.args.push_back(inv.args[j]);
        base_ctx.chain.push_back(std::move(segment));

        descriptors[i] = prompt::find_prompt(inv.name);
        if (descriptors[i] == nullptr) {
            std::cerr << "prompt: prompt '" << inv.name << "' not found\n";
            return 3;
        }
    }

    // The dispatcher owns the common sink flags: parse them once, before any
    // batch runs, and hand the sink only the remaining arguments. This also
    // makes --show-context known before the first segment is emitted.
    std::vector<std::optional<prompt::sink_args>> sink_args(chain.count);
    bool has_sink = false;
    bool show_context = false;
    for (std::size_t i = 0; i < chain.count; ++i) {
        auto const& inv = chain.invocations[i];
        if (descriptors[i]->role != prompt::chain_role::sink) continue;
        has_sink = true;
        auto parsed = prompt::parse_sink_args(std::span<std::string_view const>(inv.args.data(), inv.args_count));
        if (!parsed.error.empty()) {
            std::cerr << "prompt " << inv.name << ": " << parsed.error;
            return 2;
        }
        show_context = show_context || parsed.options.show_context;
        base_ctx.chain[i].args.assign(parsed.rest.begin(), parsed.rest.end());
        sink_args[i] = std::move(parsed);
    }

    auto const plan = prompt::plan_chain(chain, prompt::get_all_prompts());
    if (!plan.error.empty()) {
        std::cerr << "prompt: " << plan.error << "\n";
        return 2;
    }

    // A sink asking for help is a terminal help request, not an AI call with
    // the help text as context.
    for (std::size_t i = 0; i < chain.count; ++i) {
        if (descriptors[i]->role == prompt::chain_role::sink && invocation_wants_help(chain.invocations[i])) {
            auto result = render_help_result(*descriptors[i]);
            std::cout << result.output;
            if (!result.output.empty()) prompt::clipboard::copy(result.output);
            return 0;
        }
    }

    if (!has_sink) {
        std::vector<segment_result> results(chain.count);

        // Piped stdin is shared with a spend-once rule: a prompt that reads it
        // spends the pipe for the prompts after it. Running in parallel would let
        // every segment see the same pipe, so keep that case sequential. Without
        // piped stdin the segments are independent and run in parallel.
        bool const parallel = chain.count > 1 && !base_ctx.stdin_consumed;
        if (parallel) {
            std::vector<int> read_fds(chain.count, -1);
            std::vector<pid_t> children(chain.count, -1);

            // Fork every segment before reading anything: a prompt must never
            // fork from a multi-threaded parent, and forking up front keeps each
            // child single-threaded for the `process::command` calls it makes.
            for (std::size_t i = 0; i < chain.count; ++i) {
                auto const& inv = chain.invocations[i];
                if (invocation_wants_help(inv)) {
                    results[i] = render_help_result(*descriptors[i]);
                    continue;
                }

                std::array<int, 2> fds{};
                if (::pipe2(fds.data(), O_CLOEXEC) != 0) {
                    results[i] = failure_result("prompt: pipe failed: " + std::string(std::strerror(errno)) + "\n");
                    continue;
                }
                pid_t const pid = ::fork();
                if (pid == 0) {
                    ::close(fds[0]);
                    auto ctx = invocation_context(base_ctx, inv, i);
                    auto result = run_prompt(ctx, *descriptors[i]);
                    write_segment(fds[1], result);
                    ::close(fds[1]);
                    ::_exit(0);
                }
                if (pid < 0) {
                    ::close(fds[0]);
                    ::close(fds[1]);
                    results[i] = failure_result("prompt: fork failed: " + std::string(std::strerror(errno)) + "\n");
                    continue;
                }
                ::close(fds[1]);
                read_fds[i] = fds[0];
                children[i] = pid;
            }

            for (std::size_t i = 0; i < chain.count; ++i) {
                if (read_fds[i] < 0) continue;
                std::string blob = read_all(read_fds[i]);
                ::close(read_fds[i]);
                int status = 0;
                ::waitpid(children[i], &status, 0);
                if (auto decoded = read_segment(blob)) {
                    results[i] = std::move(*decoded);
                } else {
                    results[i] = failure_result("prompt " + std::string(chain.invocations[i].name) +
                                                ": parallel child produced no result\n");
                }
            }
        } else {
            for (std::size_t i = 0; i < chain.count; ++i) {
                auto const& inv = chain.invocations[i];
                if (invocation_wants_help(inv)) {
                    results[i] = render_help_result(*descriptors[i]);
                    continue;
                }
                auto ctx = invocation_context(base_ctx, inv, i);
                results[i] = run_prompt(ctx, *descriptors[i]);
                if (results[i].stdin_consumed) base_ctx.stdin_content.clear();
            }
        }

        // Pipeline for output accumulation, in command-line order regardless of
        // which parallel segment finished first.
        prompt::pipeline pipeline;
        int exit_code = 0;
        // Whether clean_privacy rewrote anything in any segment; if so the
        // dispatcher prepends the redaction legend once, before the whole output.
        bool redacted = false;
        for (auto const& result : results) {
            redacted = redacted || result.redacted;
            pipeline.add_output(result.output, result.no_clipboard);
            if (!result.error.empty()) std::cerr << result.error;
            if (result.failed && exit_code == 0) exit_code = result.exit_code;
        }

        // Finalize and output
        std::string final_output = pipeline.finalize();
        if (redacted && !final_output.empty()) {
            final_output = std::string(prompt::sanitize::kRedactionLegend) + "\n" + final_output;
        }
        std::cout << final_output;

        // Copy to clipboard (respecting per-prompt no-clipboard opt-outs)
        auto clip = pipeline.for_clipboard();
        if (redacted && !clip.empty()) {
            clip = std::string(prompt::sanitize::kRedactionLegend) + "\n" + clip;
        }
        if (!clip.empty()) {
            prompt::clipboard::copy(clip);
        }

        return exit_code;
    }

    // Sink chain: ordinary prompts run in batches; a sink is a barrier whose
    // answer becomes the context later prompts and sinks see. The dispatcher
    // owns the barrier, so sink children never try to synchronise with siblings.
    std::string accumulated;
    std::string accumulated_stderr;
    bool stdin_pending = base_ctx.stdin_consumed;
    bool context_redacted = false;
    bool incomplete = false;
    int exit_code = 0;
    output_emitter emitter;

    auto report_result = [&](segment_result const& result) {
        if (!result.error.empty()) std::cerr << result.error;
        if (!result.stderr_streamed && !result.stderr_buffer.empty()) std::cerr << result.stderr_buffer;
        if (!result.stderr_buffer.empty()) append_block(accumulated_stderr, result.stderr_buffer);
    };

    auto run_normal_batch = [&](std::size_t run_index, prompt::chain_run const& run) {
        std::string batch_context = accumulated;
        if (context_redacted && !batch_context.empty()) {
            batch_context = prepend_block(prompt::sanitize::kRedactionLegend, batch_context);
        }
        base_ctx.chain_context = batch_context;
        base_ctx.chain_stderr = accumulated_stderr;

        std::vector<segment_result> batch(run.end - run.begin);
        bool const parallel = batch.size() > 1 && !stdin_pending;
        if (parallel) {
            std::vector<int> read_fds(batch.size(), -1);
            std::vector<pid_t> children(batch.size(), -1);
            for (std::size_t k = 0; k < batch.size(); ++k) {
                std::size_t const i = run.begin + k;
                auto const& inv = chain.invocations[i];
                if (invocation_wants_help(inv)) {
                    batch[k] = render_help_result(*descriptors[i]);
                    continue;
                }
                std::array<int, 2> fds{};
                if (::pipe2(fds.data(), O_CLOEXEC) != 0) {
                    batch[k] = failure_result("prompt: pipe failed: " + std::string(std::strerror(errno)) + "\n");
                    continue;
                }
                pid_t const pid = ::fork();
                if (pid == 0) {
                    ::close(fds[0]);
                    auto ctx = invocation_context(base_ctx, inv, i);
                    auto result = run_prompt(ctx, *descriptors[i]);
                    write_segment(fds[1], result);
                    ::close(fds[1]);
                    ::_exit(0);
                }
                if (pid < 0) {
                    ::close(fds[0]);
                    ::close(fds[1]);
                    batch[k] = failure_result("prompt: fork failed: " + std::string(std::strerror(errno)) + "\n");
                    continue;
                }
                ::close(fds[1]);
                read_fds[k] = fds[0];
                children[k] = pid;
            }
            for (std::size_t k = 0; k < batch.size(); ++k) {
                if (read_fds[k] < 0) continue;
                std::string blob = read_all(read_fds[k]);
                ::close(read_fds[k]);
                int status = 0;
                ::waitpid(children[k], &status, 0);
                if (auto decoded = read_segment(blob)) {
                    batch[k] = std::move(*decoded);
                } else {
                    batch[k] = failure_result("prompt " + std::string(chain.invocations[run.begin + k].name) +
                                              ": parallel child produced no result\n");
                }
            }
        } else {
            for (std::size_t k = 0; k < batch.size(); ++k) {
                std::size_t const i = run.begin + k;
                auto const& inv = chain.invocations[i];
                if (invocation_wants_help(inv)) {
                    batch[k] = render_help_result(*descriptors[i]);
                    continue;
                }
                auto ctx = invocation_context(base_ctx, inv, i);
                batch[k] = run_prompt(ctx, *descriptors[i]);
                if (batch[k].stdin_consumed) {
                    stdin_pending = false;
                    base_ctx.stdin_content.clear();
                }
            }
        }

        bool const emit_sources = show_context || run_index + 1 == plan.runs.size();
        for (auto& result : batch) {
            report_result(result);
            if (result.failed) {
                incomplete = true;
                if (exit_code == 0) exit_code = result.exit_code;
            }
            if (result.stdin_consumed) {
                stdin_pending = false;
                base_ctx.stdin_content.clear();
            }
            append_block(accumulated, result.output);
            context_redacted = context_redacted || result.redacted;
            if (emit_sources) emitter.emit(result.output, result.no_clipboard, result.redacted);
        }
    };

    auto current_input = [&](std::size_t i) {
        std::string input = accumulated;
        if (stdin_pending) {
            input = prepend_block(base_ctx.stdin_content, input);
            stdin_pending = false;
            base_ctx.stdin_content.clear();
            base_ctx.stdin_consumed = true;
        }
        auto const& opts = sink_args[i]->options;
        if (opts.head_lines) input = std::string(prompt::trim_context_lines(input, *opts.head_lines));
        if (context_redacted && !input.empty()) {
            std::string with_legend = std::string(prompt::sanitize::kRedactionLegend);
            with_legend += "\n\n";
            with_legend += input;
            input = std::move(with_legend);
        }
        return input;
    };

    auto run_sink = [&](std::size_t i, std::string_view input, bool stream) {
        auto const& inv = chain.invocations[i];
        auto const& parsed = *sink_args[i];
        base_ctx.chain_context = input;
        base_ctx.chain_stderr = accumulated_stderr;
        base_ctx.sink = parsed.options;
        base_ctx.stream_stdout = stream;
        base_ctx.stream_stderr = stream;
        auto ctx = invocation_context(base_ctx, inv, i,
                                      std::span<std::string_view const>(parsed.rest.data(), parsed.rest.size()));
        auto result = run_prompt(ctx, *descriptors[i]);
        base_ctx.stream_stdout = false;
        base_ctx.stream_stderr = false;
        return result;
    };

    auto emit_sink = [&](segment_result const& result) {
        if (result.stdout_streamed) {
            emitter.streamed(result.output, result.no_clipboard, result.redacted);
        } else {
            emitter.emit(result.output, result.no_clipboard, result.redacted);
        }
    };

    auto update_from_sink = [&](std::size_t i, segment_result const& result) {
        auto const& opts = sink_args[i]->options;
        if (result.failed) {
            if (opts.failure == prompt::sink_failure::abort) return true;
            if (opts.failure == prompt::sink_failure::skip) return false;
            if (!result.output.empty()) {
                if (opts.with_context) {
                    append_block(accumulated, result.output);
                    context_redacted = context_redacted || result.redacted;
                } else {
                    accumulated.assign(trim_block(result.output));
                    if (!accumulated.empty()) accumulated += '\n';
                    context_redacted = result.redacted;
                }
            }
            incomplete = true;
            return false;
        }
        if (opts.with_context) {
            append_block(accumulated, result.output);
            context_redacted = context_redacted || result.redacted;
        } else {
            accumulated.assign(trim_block(result.output));
            if (!accumulated.empty()) accumulated += '\n';
            context_redacted = result.redacted;
        }
        incomplete = false;
        return false;
    };

    bool chain_aborted = false;
    for (std::size_t run_index = 0; run_index < plan.runs.size() && !chain_aborted; ++run_index) {
        auto const& run = plan.runs[run_index];
        if (!run.sink) {
            run_normal_batch(run_index, run);
            continue;
        }

        if (run.policy == prompt::sink_policy::fanout) {
            std::string input;
            bool input_ready = false;
            auto ensure_input = [&] {
                if (!input_ready) {
                    input = current_input(run.begin);
                    input_ready = true;
                }
            };
            std::string const prior = accumulated;
            bool const prior_redacted = context_redacted;
            bool const was_incomplete = incomplete;
            bool fanout_abort = false;
            std::vector<std::pair<std::size_t, segment_result>> answers;
            answers.reserve(run.end - run.begin);
            for (std::size_t i = run.begin; i < run.end; ++i) {
                auto const& opts = sink_args[i]->options;
                if (incomplete && opts.failure == prompt::sink_failure::abort) {
                    fanout_abort = true;
                    break;
                }
                if (incomplete && opts.failure == prompt::sink_failure::skip) continue;
                ensure_input();
                auto result = run_sink(i, input, false);
                report_result(result);
                emit_sink(result);
                if (result.failed && exit_code == 0) exit_code = result.exit_code;
                answers.emplace_back(i, std::move(result));
            }
            accumulated = prior;
            context_redacted = prior_redacted;
            bool any_failure = false;
            bool any_answer = false;
            for (auto const& [i, result] : answers) {
                auto const& opts = sink_args[i]->options;
                if (result.failed) {
                    any_failure = true;
                    if (opts.failure == prompt::sink_failure::abort) fanout_abort = true;
                    if (opts.failure == prompt::sink_failure::skip) continue;
                } else {
                    any_answer = true;
                }
                append_block(accumulated, result.output);
                context_redacted = context_redacted || result.redacted;
            }
            incomplete = any_failure || (was_incomplete && !any_answer);
            if (fanout_abort) chain_aborted = true;
            continue;
        }

        for (std::size_t i = run.begin; i < run.end; ++i) {
            auto const& opts = sink_args[i]->options;
            if (incomplete) {
                if (opts.failure == prompt::sink_failure::abort) {
                    chain_aborted = true;
                    break;
                }
                if (opts.failure == prompt::sink_failure::skip) continue;
            }
            std::string const input = current_input(i);
            emitter.begin_stream();
            auto result = run_sink(i, input, true);
            report_result(result);
            emit_sink(result);
            if (result.failed && exit_code == 0) exit_code = result.exit_code;
            if (update_from_sink(i, result)) {
                chain_aborted = true;
                break;
            }
        }
    }

    auto clip = emitter.clipboard();
    if (!clip.empty()) prompt::clipboard::copy(clip);
    return exit_code;
}
