#include "prompt/prompts/tweets_prompt.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <string>

namespace prompt::prompts {

prompt_result execute_tweets(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }

    std::string output;

    // bash's `embed_stdin || true` prints a blank block for an empty (but
    // still piped) stdin too — the separator tweets.sh relies on.
    if (ctx.stdin_consumed) {
        output += embed_stdin(ctx.stdin_content, head_lines);
    }

    output += "Based on the input content, generate several high-quality tweet ideas.\n";
    output += "Create a markdown table with columns: Tweet | Why this tweet | Expected impact\n";
    output += "Write clean, natural tweets only \xE2\x80\x94 no hashtags and no weird emojis.\n";
    output += "Focus on clear, engaging language that fits the source material.\n";
    output += "Prefer 3\xE2\x80\x93"
              "5 strong, ready-to-post ideas. Keep them concise.\n";
    output += "\n";

    return {std::move(output), 0, false, std::string{}, ctx.stdin_consumed};
}

void render_help_tweets(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt tweets [--head N] [FILE...]
       some-command | prompt tweets [--head N] [FILE...]

Generate tweet ideas from the provided content.
Produce a table showing each tweet, why it works, and its expected impact.
No hashtags or weird emojis.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
