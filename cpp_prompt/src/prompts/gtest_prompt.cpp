#include "prompt/prompts/gtest_prompt.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

prompt_result execute_gtest(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }

    std::string output;
    output += "Write comprehensive unit tests for the following C++ code using Google Test (gtest).\n";
    output += "This code is part of a C++ web framework named web++.\n";
    output += "Focus on:\n";
    output += "- Clear and descriptive test names.\n";
    output += "- Proper Setup/Teardown (using TEST_F and fixtures if necessary).\n";
    output += "- Edge cases and typical web framework scenarios (e.g., malformed inputs, "
              "boundaries).\n";
    output += "- Standard gtest macros (EXPECT_EQ, ASSERT_TRUE, EXPECT_THROW, etc.).\n";
    output += "Provide the complete test code implementation.\n";
    output += "\n";

    std::string error;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head") {
            ++i;
            continue;
        }
        std::filesystem::path file(ctx.args[i]);
        if (std::filesystem::is_regular_file(file)) {
            output += "File: " + file.filename().string() + "\n\n";
            output += "```" + infer_lang(file) + "\n";
            output += trim_context_nl(read_file(file), head_lines);
            output += "\n```\n";
        } else {
            error += "Warning: File not found or not a regular file: " + std::string(ctx.args[i]) + "\n";
        }
    }

    return {std::move(output), 0, false, std::move(error)};
}

void render_help_gtest(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt gtest [--head N] [FILE]...
       some-command | prompt gtest [--head N] [FILE...]

Ask the AI to write Google Test (gtest) unit tests for the provided code.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
