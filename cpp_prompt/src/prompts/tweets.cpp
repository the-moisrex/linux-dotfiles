#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "prompts.hpp"
#include <string>

namespace prompt::prompts {

prompt_output execute_tweets(prompt_context&& ctx) {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        throw usage_error{std::move(msg)};
    }

    std::string output;

    // bash's `embed_stdin || true` prints a blank block for an empty (but
    // still piped) stdin too — the separator tweets.sh relies on.
    if (ctx.stdin_consumed) {
        output += embed_stdin(ctx.stdin_content, head_lines);
    }

    output += "Based on the input content, generate several high-quality tweet ideas.\nCreate a markdown table with "
              "columns: Tweet | Why this tweet | Expected impact\nWrite clean, natural tweets only \xE2\x80\x94 no "
              "hashtags and no weird emojis.\nFocus on clear, engaging language that fits the source material.\n";
    output += "Prefer 3\xE2\x80\x93"
              "5 strong, ready-to-post ideas. Keep them concise.\n";
    output += "\n";

    return {
        .text = std::move(output),
        .error = std::string{},
        .no_clipboard = false,
        .stdin_consumed = ctx.stdin_consumed,
    };
}

void render_help_tweets(std::ostream& os) {
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
