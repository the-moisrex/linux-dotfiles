#include "../core/clipboard.hpp"
#include "../core/strings.hpp"
#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/errors.hpp"
#include "../sdk/prompt_registry.hpp"
#include "../sdk/sink.hpp"
#include "../sdk/stage.hpp"
#include <cstddef>
#include <format>
#include <ostream>
#include <string>
#include <string_view>

namespace prompt::prompts {

static void render_help_clipboard(std::ostream& os);
static void render_help_cp(std::ostream& os);
static void render_help_cc(std::ostream& os);

namespace {

// Paste is a source: read the clipboard and embed it as context. `clipboard`
// and its `.c.p` alias share this path; only `v.name` and the help differ.
void run_paste(view const& v, result& r, void (*render_help)(std::ostream&)) {
    auto head_lines = head_lines_or_throw(v);

    // An optional `paste` subcommand mirrors `.c.p`/`.c.c`. `copy` cannot run
    // here: a source prompt never receives the accumulated chain context, so
    // copying is the sink `.c.c`.
    for (std::size_t i = 0; i < v.args.size(); ++i) {
        std::string_view const arg = v.args[i];
        if (arg == "--head") {
            ++i; // value validated by head_lines_or_throw
            continue;
        }
        if (arg == "paste") continue;
        if (arg == "copy") {
            throw usage_error{
                std::format("prompt {}: `copy` writes the chain context and runs as a sink; use `.c.c` instead\n{}",
                            v.name, help_text(render_help))};
        }
        throw usage_error{std::format("prompt {}: unexpected argument: {}\n{}", v.name, arg, help_text(render_help))};
    }

    // bash: content="$(clipboard_content)" — the command substitution strips
    // trailing newlines, so an all-whitespace clipboard reads as empty. Trim
    // both sides here too, so `--head N` counts real content lines rather than
    // the blank edges a terminal copy carries (the dispatcher trims the emitted
    // block the same way).
    std::string content(prompt::text::strip_newlines(clipboard_content(v.env)));
    if (content.empty()) {
        throw soft_error{std::format("prompt {}: Clipboard is empty or unavailable.\n", v.name)};
    }

    r.out = trim_context_nl(content, head_lines);
}

} // namespace

static void execute_clipboard(view const& v, result& r) { run_paste(v, r, &render_help_clipboard); }
static void execute_cp(view const& v, result& r) { run_paste(v, r, &render_help_cp); }

// `.c.c` is a passthrough sink: it writes the accumulated chain context to the
// system clipboard and leaves the context unchanged, so it can sit before
// another sink such as `.ai.grok`.
static void execute_cc(view const& v, result& r) {
    if (!v.args.empty()) {
        throw usage_error{std::format("prompt c.c: does not take arguments\n{}", help_text(render_help_cc))};
    }

    // --head (and the other sink flags) are dispatcher-owned: the context this
    // sink receives is already trimmed. The bytes are copied verbatim.
    std::string const content(v.text);
    if (content.empty()) {
        throw soft_error{"prompt c.c: no chain context to copy\n"};
    }
    if (!prompt::clipboard::copy(content)) {
        throw soft_error{"prompt c.c: could not copy to the clipboard\n"};
    }

    // No stdout, so the dispatcher treats it as a passthrough tap and keeps the
    // context (and any failure taint) for later segments.
    r.no_clipboard = true;
}

static void render_help_clipboard(std::ostream& os) {
    os << R"EOF(Usage: prompt clipboard [paste] [--head N]
       prompt .clipboard paste [--head N]

Read from the clipboard and embed the content as context for the AI. The
optional `paste` subcommand is accepted for symmetry with `.c.p`.

To copy the accumulated chain context to the clipboard, use the sink `.c.c`.

Options:
  --head N   Keep only the first N lines of the embedded content

See also: `.c.p` pastes, and `.c.c` copies the chain context.
)EOF";
}

static void render_help_cp(std::ostream& os) {
    os << R"EOF(Usage: prompt .c.p [paste] [--head N]

Paste alias for `.clipboard`: read the clipboard and embed the content as
context for the AI.

Options:
  --head N   Keep only the first N lines of the embedded content

See also: `.c.c` copies the accumulated chain context.
)EOF";
}

static void render_help_cc(std::ostream& os) {
    os << R"EOF(Usage: prompt <other prompts...> .c.c [--head N]

Copy the accumulated chain context to the system clipboard. The sink is a
passthrough: it writes the clipboard and leaves the chain context unchanged, so
it can sit before another sink such as `.ai.grok`.

The copied text is the same sanitized context an AI sink would receive,
including --head trimming and the redaction legend when applicable.

Examples:
  prompt .note "remember this" .c.c
  prompt .tse khodro .tse.analyze .c.c
  prompt .files a.cpp .extract .c.c

)EOF";
    render_sink_help(os);
    os << "\nSee also: `.c.p` / `.clipboard` paste from the clipboard.\n";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered =
    prompt::register_prompt("clipboard", "Read from the clipboard and embed the content as context for the AI.",
                            &execute_clipboard, &render_help_clipboard);

// `.c.p` is the short paste alias; it reuses the same executor.
bool const registered_cp =
    prompt::register_prompt("c.p", "Paste alias for `.clipboard`: read the clipboard and embed the content as context.",
                            &execute_cp, &render_help_cp);

// `.c.c` is the copy sink: the chain context goes to the clipboard unchanged.
bool const registered_cc = prompt::register_sink(
    "c.c", "Copy the accumulated chain context to the system clipboard, then pass the context through unchanged.",
    &execute_cc, &render_help_cc, nullptr, false);

} // namespace prompt::prompts
