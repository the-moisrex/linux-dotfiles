#include "../sdk/prompt_registry.hpp"
#include "prompt_entry.hpp"
#include "prompts.hpp"

// Stdin-centric prompts: translation wrappers, raw stdin embedding and the
// URL-fetching (yt / summarize) helpers.
namespace prompt {

void register_batch_stdin() {
    register_prompt({
        .name = "english",
        .help_summary = "Translate to English.",
        .help_full = "",
        .execute_fn = prompts::execute_english,
        .render_help_fn = prompts::render_help_english,
    });

    register_prompt({
        .name = "farsi",
        .help_summary = "Translate to Farsi.",
        .help_full = "",
        .execute_fn = prompts::execute_farsi,
        .render_help_fn = prompts::render_help_farsi,
    });

    register_prompt({
        .name = "stdin",
        .help_summary = "Read from stdin and embed the content as context for the AI. Pipe something into "
                        "this prompt to include it in the AI context.",
        .help_full = "",
        .execute_fn = prompts::execute_stdin,
        .render_help_fn = prompts::render_help_stdin,
    });

    register_prompt({
        .name = "tweets",
        .help_summary = "Generate tweet ideas from the provided content. Produce a table showing each tweet, why "
                        "it works, and its expected impact. No hashtags or weird emojis.",
        .help_full = "",
        .execute_fn = prompts::execute_tweets,
        .render_help_fn = prompts::render_help_tweets,
    });

    register_prompt({
        .name = "symbols",
        .help_summary = "Builds a prompt that reviews symbol names. If file paths are given, it reads those files; "
                        "otherwise it reads stdin. Example: `prompt symbols $(fzf)`",
        .help_full = "",
        .execute_fn = prompts::execute_symbols,
        .render_help_fn = prompts::render_help_symbols,
    });

    register_prompt({
        .name = "summarize",
        .help_summary = "Builds a prompt that summarizes stdin. If stdin is a single URL, it tries to "
                        "fetch subtitles/text first.",
        .help_full = "",
        .execute_fn = prompts::execute_summarize,
        .render_help_fn = prompts::render_help_summarize,
    });

    register_prompt({
        .name = "yt",
        .help_summary = "Builds an article-writing prompt from stdin. If stdin is a single YouTube URL, it "
                        "tries to fetch subtitles/text first.",
        .help_full = "",
        .execute_fn = prompts::execute_yt,
        .render_help_fn = prompts::render_help_yt,
    });
}

} // namespace prompt
