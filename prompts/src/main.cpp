#include "core/clipboard.hpp"
#include "core/sanitize.hpp"
#include "environment.hpp"
#include "sdk/chain_state.hpp"
#include "sdk/chaining.hpp"
#include "sdk/completion.hpp"
#include "sdk/prompt_registry.hpp"
#include "sdk/sink.hpp"
#include "sdk/stage.hpp"
#include "sdk/wire.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <iostream>
#include <optional>
#include <print>
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

std::string_view trim_trailing_newlines(std::string_view text) {
    while (!text.empty() && text.back() == '\n') text.remove_suffix(1);
    return text;
}

std::string_view trim_block(std::string_view text) {
    while (!text.empty() && text.front() == '\n') text.remove_prefix(1);
    while (!text.empty() && text.back() == '\n') text.remove_suffix(1);
    return text;
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

// The historical no-sink byte rule, kept so the output of a plain
// `prompt .note x .note y` chain is unchanged: each segment's output is
// trimmed of trailing newlines and re-terminated with exactly one, and a
// separator is printed even when a segment produced nothing (the old bash
// implementation captured each prompt with `$(...)` and printed with
// `printf '%s\n'`).
struct output_accumulator {
    std::string text;
    std::string clipboard;
    bool first_prompt = true;
    bool first_clipboard = true;

    void add(std::string_view output, bool no_clipboard) {
        std::string_view const trimmed = trim_trailing_newlines(output);
        if (!first_prompt) text += '\n';
        first_prompt = false;
        if (trimmed.empty()) return;
        text.append(trimmed);
        text += '\n';

        if (no_clipboard) return;
        if (!first_clipboard) clipboard += '\n';
        clipboard.append(trimmed);
        clipboard += '\n';
        first_clipboard = false;
    }
};

// Incremental stdout/clipboard emitter for sink chains. Streamed sink output
// has already reached the terminal; it only needs to enter the clipboard and
// affect the separator state.
class output_emitter {
  public:
    void emit(std::string_view output, bool no_clipboard, bool redacted) {
        output = trim_trailing_newlines(output);
        if (output.empty()) return;
        if (!first_stdout_) std::print(std::cout, "\n");
        first_stdout_ = false;
        if (redacted && !stdout_legend_) {
            std::println(std::cout, "{}", prompt::sanitize::kRedactionLegend);
            stdout_legend_ = true;
        }
        std::println(std::cout, "{}", output);
        std::cout.flush();
        add_clipboard(output, no_clipboard, redacted);
    }

    void streamed(std::string_view output, bool no_clipboard, bool redacted) {
        if (!output.empty() && output.back() != '\n') std::print(std::cout, "\n");
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
            std::print(std::cout, "\n");
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

prompt::segment_outcome run_prompt(prompt::view const& in, prompt::prompt_descriptor const& desc) {
    prompt::result out;
    prompt::segment_outcome outcome;
    try {
        desc.execute_fn(in, out);
        auto clean = prompt::sanitize::sanitize(out.out);
        outcome.out = std::move(clean.text);
        outcome.redacted = clean.redacted;
        outcome.no_clipboard = out.no_clipboard;
        outcome.clear = out.clear;
        outcome.err = std::move(out.err);
        outcome.stderr_buffer = std::move(out.stderr_buffer);
        outcome.published = std::move(out.published);
        outcome.stdout_streamed = out.stdout_streamed;
        outcome.stderr_streamed = out.stderr_streamed;
    } catch (prompt::prompt_error const& err) {
        auto clean = prompt::sanitize::sanitize(err.partial_output());
        outcome.out = std::move(clean.text);
        outcome.redacted = clean.redacted;
        outcome.no_clipboard = err.no_clipboard();
        outcome.clear = out.clear;
        outcome.published = std::move(out.published);
        outcome.err = err.what();
        outcome.stderr_buffer = std::move(out.stderr_buffer);
        outcome.stdout_streamed = out.stdout_streamed;
        outcome.stderr_streamed = out.stderr_streamed;
        outcome.failed = true;
        outcome.exit_code = err.exit_code();
    }
    // Piped stdin is spent by the first executed segment, exactly as before:
    // the pipe is shared, and a segment that has already inherited it is the
    // only one that can read it.
    outcome.stdin_consumed = in.in.piped();
    return outcome;
}

bool wants_help(std::span<std::string_view const> args) {
    return std::ranges::any_of(args, [](std::string_view arg) { return arg == "--help" || arg == "-h"; });
}

bool invocation_wants_help(prompt::prompt_invocation const& inv) { return wants_help(inv.args); }

prompt::segment_outcome render_help_result(prompt::prompt_descriptor const& desc) {
    prompt::segment_outcome outcome;
    std::ostringstream help;
    if (desc.render_help_fn != nullptr) desc.render_help_fn(help);
    auto clean = prompt::sanitize::sanitize(help.str());
    outcome.out = std::move(clean.text);
    outcome.redacted = clean.redacted;
    return outcome;
}

prompt::segment_outcome failure_result(std::string message) {
    prompt::segment_outcome outcome;
    outcome.err = std::move(message);
    outcome.failed = true;
    outcome.exit_code = 1;
    return outcome;
}

// What the dispatcher knows about one chain position: its descriptor and the
// arguments the prompt actually sees (a sink's shared flags are stripped).
struct segment {
    prompt::prompt_descriptor const* desc = nullptr;
    std::string_view name;
    std::vector<std::string_view> args;
};

void write_segment(int fd, prompt::segment_outcome const& outcome) {
    std::string const blob = prompt::encode_outcome(outcome);
    std::size_t offset = 0;
    while (offset < blob.size()) {
        auto const n = ::write(fd, blob.data() + offset, blob.size() - offset);
        if (n < 0) {
            if (errno == EINTR) continue;
            return;
        }
        if (n == 0) return;
        offset += static_cast<std::size_t>(n);
    }
}

// Name how a child ended when it died before writing its blob, so a crash is
// not reported as a generic "no result".
std::string child_status_note(int status) {
    if (WIFSIGNALED(status)) return " (terminated by signal " + std::to_string(WTERMSIG(status)) + ")";
    if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
        return " (exit status " + std::to_string(WEXITSTATUS(status)) + ")";
    return {};
}

// Run every segment of one batch concurrently: one forked child per segment,
// each writing its encoded `segment_outcome` back over a pipe. `make_view` runs
// in the child, so it may only read state that already exists in the parent.
// `--help` requests are answered in the parent and keep their argv position.
// Outcomes come back in argv order regardless of which child finishes first.
std::vector<prompt::segment_outcome> run_parallel(std::span<segment const> segs,
                                                  std::function<prompt::view(segment const&)> const& make_view) {
    std::size_t const size = segs.size();
    std::vector<prompt::segment_outcome> outcomes(size);
    std::vector<int> read_fds(size, -1);
    std::vector<pid_t> children(size, -1);

    // Fork every segment before reading anything: a prompt must never fork from
    // a multi-threaded parent, and forking up front keeps each child
    // single-threaded for the `process::command` calls it makes.
    for (std::size_t i = 0; i < size; ++i) {
        if (wants_help(segs[i].args)) {
            outcomes[i] = render_help_result(*segs[i].desc);
            continue;
        }

        std::array<int, 2> fds{};
        if (::pipe2(fds.data(), O_CLOEXEC) != 0) {
            outcomes[i] = failure_result("prompt: pipe failed: " + std::string(std::strerror(errno)) + "\n");
            continue;
        }
        pid_t const pid = ::fork();
        if (pid == 0) {
            ::close(fds[0]);
            auto outcome = run_prompt(make_view(segs[i]), *segs[i].desc);
            write_segment(fds[1], outcome);
            ::close(fds[1]);
            ::_exit(0);
        }
        if (pid < 0) {
            ::close(fds[0]);
            ::close(fds[1]);
            outcomes[i] = failure_result("prompt: fork failed: " + std::string(std::strerror(errno)) + "\n");
            continue;
        }
        ::close(fds[1]);
        read_fds[i] = fds[0];
        children[i] = pid;
    }

    auto const blobs = prompt::drain_all(read_fds);
    for (std::size_t i = 0; i < size; ++i) {
        if (children[i] < 0) continue;
        int status = 0;
        ::waitpid(children[i], &status, 0);
        if (auto decoded = prompt::decode_outcome(blobs[i])) {
            outcomes[i] = std::move(*decoded);
        } else {
            outcomes[i] = failure_result("prompt " + std::string(segs[i].name) + ": parallel child produced no result" +
                                         child_status_note(status) + "\n");
        }
    }
    return outcomes;
}

} // namespace

// std::print/println may throw std::format_error or ios_base::failure; the
// format strings are literals and main has no recovery path.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argc, char** argv) {
    // Handle --help and --version early (only as first argument)
    if (argc > 1) {
        std::string_view arg(argv[1]);
        if (arg == "--help" || arg == "-h") {
            // The dispatcher's usage text: it ships a fixed prompt set and
            // never scans for prompt files.
            std::print(std::cout, "{}", R"(Usage:
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
  - Sinks (.ai.chatgpt/.ai.dseek/.ai.gap/.ai.grok/.ai.kimi) wait for the
    preceding chain, send its text to the site, and continue with the answer.
    They share --with-context, --show-context, --on-error and --head.
  - 'prompt <name> --complete <prefix> [args...]' prints shell-completion
    candidates for the prompt (used by shell/completions.fish), one
    value<TAB>description per line, without running it.
  - 'prompt list' lists available prompts with the first paragraph of each
    prompt's help as the description (--names for bare names only).
  - 'prompt list-prompts' prints the full help text of every available prompt.
)");
            return 0;
        }
        if (arg == "--version" || arg == "-v") {
            std::println(std::cout, "prompt {}", PROMPT_VERSION);
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
                std::print(std::cout, "{}", out.str());
            }
            return 0;
        }
    }

    // Initialize clipboard
    prompt::clipboard::init();

    // The process-wide environment: built once, never written again.
    prompt::environment env = prompt::make_environment();

    // Read stdin if available: anything that is not a terminal counts as a
    // pipe, so an empty pipe still yields (empty) piped stdin.
    prompt::stdin_source stdin;
    if (isatty(STDIN_FILENO) == 0) {
        std::string content;
        std::string line;
        while (std::getline(std::cin, line)) {
            content += line;
            content += '\n';
        }
        stdin.load(std::move(content));
    }

    // Parse chained prompts
    std::span<char const* const> argv_span(argv, static_cast<std::size_t>(argc));
    auto chain = prompt::parse_chain(argv_span, prompt::get_all_prompts());

    // If no prompts found, default to "auto"
    if (chain.invocations.empty()) {
        chain.invocations.push_back(prompt::prompt_invocation{.name = "auto", .args = {}});
    }

    std::size_t const count = chain.invocations.size();
    std::vector<segment> segments(count);

    // The dispatcher owns the common sink flags: parse them once, before any
    // batch runs, and hand the sink only the remaining arguments. This also
    // makes --show-context known before the first segment is emitted.
    std::vector<std::optional<prompt::sink_args>> sink_args(count);
    bool has_sink = false;
    bool show_context = false;
    for (std::size_t i = 0; i < count; ++i) {
        auto const& inv = chain.invocations[i];
        auto const* desc = prompt::find_prompt(inv.name);
        if (desc == nullptr) {
            std::println(std::cerr, "prompt: prompt '{}' not found", inv.name);
            return 3;
        }
        segments[i].desc = desc;
        segments[i].name = inv.name;
        segments[i].args = inv.args;

        if (desc->role != prompt::chain_role::sink) continue;
        has_sink = true;
        auto parsed = prompt::parse_sink_args(inv.args);
        if (!parsed.error.empty()) {
            std::print(std::cerr, "prompt {}: {}", inv.name, parsed.error);
            return 2;
        }
        show_context = show_context || parsed.options.show_context;
        segments[i].args = parsed.rest;
        sink_args[i] = std::move(parsed);
    }

    auto const plan = prompt::plan_chain(chain, prompt::get_all_prompts());
    if (!plan.error.empty()) {
        std::println(std::cerr, "prompt: {}", plan.error);
        return 2;
    }

    // A sink asking for help is a terminal help request, not an AI call with
    // the help text as context.
    for (std::size_t i = 0; i < count; ++i) {
        if (segments[i].desc->role == prompt::chain_role::sink && invocation_wants_help(chain.invocations[i])) {
            auto outcome = render_help_result(*segments[i].desc);
            std::print(std::cout, "{}", outcome.out);
            if (!outcome.out.empty()) prompt::clipboard::copy(outcome.out);
            return 0;
        }
    }

    // Cheap declarations run in this (parent) process before any batch forks,
    // so parallel siblings can agree on argv-derived identity. A failing
    // declaration is a hint, not an error: the segment falls back to its own
    // arguments at execute time.
    prompt::facts declared;
    for (std::size_t i = 0; i < count; ++i) {
        if (segments[i].desc->declare == nullptr || invocation_wants_help(chain.invocations[i])) continue;
        prompt::facts local;
        try {
            prompt::declaration declaration{
                .args = segments[i].args,
                .env = env,
                .prior = declared,
                .declared = local,
            };
            segments[i].desc->declare(declaration);
        } catch (prompt::prompt_error const&) { // NOLINT(bugprone-empty-catch) — best effort
        }
        declared.merge(local, false);
    }

    auto merge_prior = [&](prompt::facts const& artifacts) {
        prompt::facts prior = artifacts;
        prior.merge(declared, false);
        return prior;
    };
    auto prior_for_batch = [&] { return merge_prior({}); };

    // One helper builds the read-only view, so preflights, parallel children and
    // sequential segments all see exactly the same shape.
    auto make_view = [&](std::string_view name, std::span<std::string_view const> args, prompt::facts const& prior) {
        return prompt::view{.name = name, .args = args, .text = {}, .prior = prior, .env = env, .in = stdin};
    };

    // Descriptor preflights run after chain/sink parsing but before any prompt
    // executes. A sink such as `.save` uses this to create and check its
    // destination directory without doing the expensive work first.
    for (std::size_t i = 0; i < count; ++i) {
        if (segments[i].desc->preflight == nullptr || invocation_wants_help(chain.invocations[i])) continue;

        auto prior = prior_for_batch();
        try {
            segments[i].desc->preflight(make_view(chain.invocations[i].name, segments[i].args, prior));
        } catch (prompt::prompt_error const& err) {
            if (!err.partial_output().empty()) std::print(std::cout, "{}", err.partial_output());
            std::print(std::cerr, "{}", err.what());
            return err.exit_code();
        }
    }

    if (!has_sink) {
        std::vector<prompt::segment_outcome> outcomes(count);

        // Piped stdin is shared with a spend-once rule, so keep that case
        // sequential. Without piped stdin the segments are independent and run
        // in parallel.
        bool const parallel = count > 1 && !stdin.available();
        if (parallel) {
            auto declarations = prior_for_batch();
            outcomes =
                run_parallel(segments, [&](segment const& seg) { return make_view(seg.name, seg.args, declarations); });
        } else {
            // Sequential: each segment sees the facts the finished ones
            // published (last writer wins), exactly like the sink path.
            prompt::facts artifacts;
            for (std::size_t i = 0; i < count; ++i) {
                auto const& inv = chain.invocations[i];
                if (invocation_wants_help(inv)) {
                    outcomes[i] = render_help_result(*segments[i].desc);
                    continue;
                }
                auto prior = merge_prior(artifacts);
                outcomes[i] = run_prompt(make_view(inv.name, segments[i].args, prior), *segments[i].desc);
                if (!outcomes[i].failed) artifacts.merge(outcomes[i].published, true);
                if (outcomes[i].stdin_consumed) stdin.spend();
            }
        }

        // Accumulate in command-line order regardless of which parallel
        // segment finished first.
        output_accumulator accumulator;
        int exit_code = 0;
        bool redacted = false;
        for (auto const& outcome : outcomes) {
            redacted = redacted || outcome.redacted;
            accumulator.add(outcome.out, outcome.no_clipboard);
            if (!outcome.err.empty()) std::print(std::cerr, "{}", outcome.err);
            if (!outcome.stderr_streamed && !outcome.stderr_buffer.empty())
                std::print(std::cerr, "{}", outcome.stderr_buffer);
            if (outcome.failed && exit_code == 0) exit_code = outcome.exit_code;
        }

        std::string final_output = std::move(accumulator.text);
        if (redacted && !final_output.empty()) {
            final_output = std::string(prompt::sanitize::kRedactionLegend) + "\n" + final_output;
        }
        std::print(std::cout, "{}", final_output);

        std::string clip = std::move(accumulator.clipboard);
        if (redacted && !clip.empty()) {
            clip = std::string(prompt::sanitize::kRedactionLegend) + "\n" + clip;
        }
        if (!clip.empty()) prompt::clipboard::copy(clip);

        return exit_code;
    }

    // Sink chain: ordinary prompts run in batches; a sink is a barrier whose
    // answer becomes the context later prompts and sinks see.
    prompt::chain_state state;
    bool stdin_pending = stdin.piped();
    int exit_code = 0;
    output_emitter emitter;

    auto report_result = [&](prompt::segment_outcome const& outcome) {
        if (!outcome.err.empty()) std::print(std::cerr, "{}", outcome.err);
        if (!outcome.stderr_streamed && !outcome.stderr_buffer.empty())
            std::print(std::cerr, "{}", outcome.stderr_buffer);
    };

    auto batch_view = [&](segment const& seg, std::string_view text, prompt::facts const& prior,
                          prompt::stream_config stream) {
        return prompt::view{
            .name = seg.name,
            .args = seg.args,
            .text = text,
            .prior = prior,
            .env = env,
            .in = stdin,
            .stream = stream,
        };
    };

    auto run_normal_batch = [&](std::size_t run_index, prompt::chain_run const& run) {
        std::string batch_context = state.text;
        if (state.redacted && !batch_context.empty()) {
            batch_context = prepend_block(prompt::sanitize::kRedactionLegend, batch_context);
        }
        auto declarations = merge_prior(state.known);

        std::size_t const size = run.end - run.begin;
        std::vector<prompt::segment_outcome> batch(size);
        bool const parallel = size > 1 && !stdin.available();
        if (parallel) {
            auto const run_segments = std::span<segment const>(segments).subspan(run.begin, size);
            batch = run_parallel(run_segments,
                                 [&](segment const& seg) { return batch_view(seg, batch_context, declarations, {}); });
        } else {
            for (std::size_t k = 0; k < size; ++k) {
                std::size_t const i = run.begin + k;
                auto const& inv = chain.invocations[i];
                if (invocation_wants_help(inv)) {
                    batch[k] = render_help_result(*segments[i].desc);
                    continue;
                }
                auto in = batch_view(segments[i], batch_context, declarations, {});
                batch[k] = run_prompt(in, *segments[i].desc);
                if (batch[k].stdin_consumed) {
                    stdin_pending = false;
                    stdin.spend();
                }
            }
        }

        bool const emit_sources = show_context || run_index + 1 == plan.runs.size();
        for (auto& outcome : batch) {
            report_result(outcome);
            if (!outcome.failed) state.known.merge(outcome.published, true);
            if (outcome.failed) {
                state.incomplete = true;
                if (exit_code == 0) exit_code = outcome.exit_code;
            }
            if (outcome.stdin_consumed) {
                stdin_pending = false;
                stdin.spend();
            }
            state.append(outcome.out, outcome.redacted);
            if (emit_sources) emitter.emit(outcome.out, outcome.no_clipboard, outcome.redacted);
        }
    };

    auto current_input = [&](std::size_t i) {
        std::string input = state.text;
        if (stdin_pending) {
            std::string with_stdin = prepend_block(stdin.text(), input);
            // A silent sink is a passthrough tap: it must not spend the pipe.
            // Keep the stdin in the accumulated context so a later sink still
            // receives it (the sink sees its own --head/legend-trimmed copy).
            if (!segments[i].desc->stream_output) state.text = with_stdin;
            input = std::move(with_stdin);
            stdin_pending = false;
            stdin.spend();
        }
        auto const& opts = sink_args[i]->options;
        if (opts.head_lines) input = std::string(prompt::trim_context_lines(input, *opts.head_lines));
        if (state.redacted && !input.empty()) {
            std::string with_legend = std::string(prompt::sanitize::kRedactionLegend);
            with_legend += "\n\n";
            with_legend += input;
            input = std::move(with_legend);
        }
        return input;
    };

    auto run_sink = [&](std::size_t i, std::string_view input, bool stream) {
        auto declarations = merge_prior(state.known);
        auto in = batch_view(segments[i], input, declarations,
                             prompt::stream_config{
                                 .stdout_writer =
                                     [](std::string_view chunk) {
                                         std::cout.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
                                         std::cout.flush();
                                     },
                                 .stderr_writer =
                                     [](std::string_view chunk) {
                                         std::cerr.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
                                         std::cerr.flush();
                                     },
                                 .stream_stdout = stream,
                                 .stream_stderr = stream,
                             });
        return run_prompt(in, *segments[i].desc);
    };

    auto emit_sink = [&](prompt::segment_outcome const& outcome) {
        if (outcome.stdout_streamed) {
            emitter.streamed(outcome.out, outcome.no_clipboard, outcome.redacted);
        } else {
            emitter.emit(outcome.out, outcome.no_clipboard, outcome.redacted);
        }
    };

    auto update_from_sink = [&](std::size_t i, prompt::segment_outcome const& outcome) {
        auto const& opts = sink_args[i]->options;
        bool const has_output = !trim_block(outcome.out).empty();
        if (outcome.failed) {
            if (opts.failure == prompt::sink_failure::abort) return true;
            if (opts.failure == prompt::sink_failure::skip) return false;
            if (has_output) {
                if (opts.with_context) {
                    state.append(outcome.out, outcome.redacted);
                } else {
                    state.text = std::string(trim_block(outcome.out));
                    if (!state.text.empty()) state.text += '\n';
                    state.redacted = outcome.redacted;
                }
            }
            state.incomplete = true;
            return false;
        }
        // A sink can ask for a fresh context (`.clear`, and `.flush` once its
        // file is written): drop everything a later section would inherit — the
        // accumulated text and facts, redaction flag and failure taint. Already
        // emitted output, the clipboard and the exit status are unaffected.
        if (outcome.clear) {
            state.clear();
            return false;
        }
        state.known.merge(outcome.published, true);
        // A silent sink (`.save`) is a tap: it observes the context without
        // producing an answer. Keep both the context and any existing failure
        // taint so later sinks still apply their own --on-error policy.
        if (!has_output) return false;
        if (opts.with_context) {
            state.append(outcome.out, outcome.redacted);
        } else {
            state.text = std::string(trim_block(outcome.out));
            if (!state.text.empty()) state.text += '\n';
            state.redacted = outcome.redacted;
        }
        state.incomplete = false;
        return false;
    };

    // A sink that is never invoked cannot explain itself: when the source batch
    // failed and the chain policy stops it, say which sink did not run — so
    // `.save` (and every other sink) never "does nothing" silently.
    auto note_skipped_sink = [&](std::size_t i, prompt::sink_failure policy) {
        std::string_view name = segments[i].desc->name;
        if (policy == prompt::sink_failure::abort) {
            std::println(std::cerr,
                         "prompt: .{} not run: an earlier prompt failed; pass --on-error continue to run it anyway",
                         name);
        } else {
            std::println(std::cerr, "prompt: .{} skipped: an earlier prompt failed (--on-error skip)", name);
        }
    };

    bool chain_aborted = false;
    for (std::size_t run_index = 0; run_index < plan.runs.size() && !chain_aborted; ++run_index) {
        auto const& run = plan.runs[run_index];
        if (!run.sink) {
            run_normal_batch(run_index, run);
            continue;
        }

        for (std::size_t i = run.begin; i < run.end; ++i) {
            auto const& opts = sink_args[i]->options;
            if (state.incomplete) {
                if (opts.failure == prompt::sink_failure::abort) {
                    note_skipped_sink(i, opts.failure);
                    chain_aborted = true;
                    break;
                }
                if (opts.failure == prompt::sink_failure::skip) {
                    note_skipped_sink(i, opts.failure);
                    continue;
                }
            }
            std::string const input = current_input(i);
            bool const stream = segments[i].desc->stream_output;
            if (stream) emitter.begin_stream();
            auto outcome = run_sink(i, input, stream);
            report_result(outcome);
            emit_sink(outcome);
            if (outcome.failed && exit_code == 0) exit_code = outcome.exit_code;
            if (update_from_sink(i, outcome)) {
                chain_aborted = true;
                break;
            }
        }
    }

    auto clip = emitter.clipboard();
    if (!clip.empty()) prompt::clipboard::copy(clip);
    return exit_code;
}
