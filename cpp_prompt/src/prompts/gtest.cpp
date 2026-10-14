#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "prompts.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

prompt_output execute_gtest(prompt_context&& ctx) {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        throw usage_error{std::move(msg)};
    }

    std::string output;
    output += "Write comprehensive unit tests for the following C++ code using Google Test (gtest).\nThis code is part "
              "of a C++ web framework named web++.\nFocus on:\n- Clear and descriptive test names.\n- Proper "
              "Setup/Teardown (using TEST_F and fixtures if necessary).\n";
    output += "- Edge cases and typical web framework scenarios (e.g., malformed inputs, "
              "boundaries).\n";
    output += "- Standard gtest macros (EXPECT_EQ, ASSERT_TRUE, EXPECT_THROW, etc.).\nProvide the complete test code "
              "implementation.\n\n";

    std::string error;
    for (std::size_t i = 0; i < ctx.args_count; ++i) {
        if (ctx.args[i] == "--head") {
            ++i;
            continue;
        }
        std::filesystem::path file(ctx.args[i]);
        if (std::filesystem::is_regular_file(file)) {
            output += std::format("File: {}\n\n", file.filename().string());
            output += std::format("```{}\n", infer_lang(file));
            output += trim_context_nl(read_file(file), head_lines);
            output += "\n```\n";
        } else {
            error += std::format("Warning: File not found or not a regular file: {}\n", std::string(ctx.args[i]));
        }
    }

    return {.text = std::move(output), .error = std::move(error), .no_clipboard = false, .stdin_consumed = false};
}

void render_help_gtest(std::ostream& os) {
    os << R"EOF(Usage: prompt gtest [--head N] [FILE]...
       some-command | prompt gtest [--head N] [FILE]...

Ask the AI to write Google Test (gtest) unit tests for the provided code.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
