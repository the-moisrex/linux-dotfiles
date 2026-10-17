#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <string>

namespace prompt::prompts {

static void execute_tweets(prompt_context& ctx) {
    auto head_lines = head_lines_or_throw(ctx);

    std::string output;

    // bash's `embed_stdin || true` prints a blank block for an empty (but
    // still piped) stdin too — the separator tweets.sh relies on.
    if (ctx.stdin_consumed) {
        output += embed_stdin(ctx.stdin_content, head_lines);
    }

    output += R"PROMPT(Based on the input content, generate several high-quality tweet ideas.
Create a markdown table with columns: Tweet | Why this tweet | Expected impact
Write clean, natural tweets only — no hashtags and no weird emojis.
Focus on clear, engaging language that fits the source material.
Prefer 3–5 strong, ready-to-post ideas. Keep them concise.
)PROMPT";
    output += '\n';

    ctx.out = std::move(output);
}

static void render_help_tweets(std::ostream& os) {
    os << R"EOF(Usage: prompt tweets [--head N] [FILE...]
       some-command | prompt tweets [--head N] [FILE...]

Generate tweet ideas from the provided content.
Produce a table showing each tweet, why it works, and its expected impact.
No hashtags or weird emojis.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered =
    prompt::register_prompt("tweets",
                            "Generate tweet ideas from the provided content. Produce a table showing each tweet, why "
                            "it works, and its expected impact. No hashtags or weird emojis.",
                            &execute_tweets, &render_help_tweets);

} // namespace prompt::prompts
