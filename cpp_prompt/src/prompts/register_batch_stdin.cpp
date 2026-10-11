#include "prompt/prompts/english_prompt.hpp"
#include "prompt/prompts/farsi_prompt.hpp"
#include "prompt/prompts/prompt_entry.hpp"
#include "prompt/prompts/stdin_prompt.hpp"
#include "prompt/prompts/summarize_prompt.hpp"
#include "prompt/prompts/symbols_prompt.hpp"
#include "prompt/prompts/tweets_prompt.hpp"
#include "prompt/prompts/yt_prompt.hpp"

// Stdin-centric prompts: translation wrappers, raw stdin embedding and the
// URL-fetching (yt / summarize) helpers.
namespace prompt {

void register_batch_stdin() noexcept {
    register_prompt({"english", "Translate to English.", "", prompts::execute_english, prompts::render_help_english});

    register_prompt({"farsi", "Translate to Farsi.", "", prompts::execute_farsi, prompts::render_help_farsi});

    register_prompt({"stdin",
                     "Read from stdin and embed the content as context for the AI. Pipe something into "
                     "this prompt to include it in the AI context.",
                     "", prompts::execute_stdin, prompts::render_help_stdin});

    register_prompt({"tweets",
                     "Generate tweet ideas from the provided content. Produce a table showing each tweet, why "
                     "it works, and its expected impact. No hashtags or weird emojis.",
                     "", prompts::execute_tweets, prompts::render_help_tweets});

    register_prompt({"symbols",
                     "Builds a prompt that reviews symbol names. If file paths are given, it reads those files; "
                     "otherwise it reads stdin. Example: `prompt symbols $(fzf)`",
                     "", prompts::execute_symbols, prompts::render_help_symbols});

    register_prompt({"summarize",
                     "Builds a prompt that summarizes stdin. If stdin is a single URL, it tries to "
                     "fetch subtitles/text first.",
                     "", prompts::execute_summarize, prompts::render_help_summarize});

    register_prompt({"yt",
                     "Builds an article-writing prompt from stdin. If stdin is a single YouTube URL, it "
                     "tries to fetch subtitles/text first.",
                     "", prompts::execute_yt, prompts::render_help_yt});
}

} // namespace prompt
