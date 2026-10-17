#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

static void execute_gtest(prompt_context& ctx) {
    auto head_lines = head_lines_or_throw(ctx);

    std::string output;
    output += "Write comprehensive unit tests for the following C++ code using Google Test (gtest).\nThis code is part "
              "of a C++ web framework named web++.\nFocus on:\n- Clear and descriptive test names.\n- Proper "
              "Setup/Teardown (using TEST_F and fixtures if necessary).\n";
    output += "- Edge cases and typical web framework scenarios (e.g., malformed inputs, "
              "boundaries).\n";
    output += "- Standard gtest macros (EXPECT_EQ, ASSERT_TRUE, EXPECT_THROW, etc.).\nProvide the complete test code "
              "implementation.\n\n";

    std::string error;
    auto sources = collect_file_args(ctx);
    for (auto const& file : sources.files) {
        output += std::format("File: {}\n\n", file.filename().string());
        output += std::format("```{}\n", infer_lang(file));
        output += trim_context_nl(read_file(file), head_lines);
        output += "\n```\n";
    }
    for (auto arg : sources.missing) {
        error += std::format("Warning: File not found or not a regular file: {}\n", std::string(arg));
    }

    ctx.out = std::move(output);
    ctx.err = std::move(error);
}

static void render_help_gtest(std::ostream& os) {
    os << R"EOF(Usage: prompt gtest [--head N] [FILE]...
       some-command | prompt gtest [--head N] [FILE]...

Ask the AI to write Google Test (gtest) unit tests for the provided code.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered =
    prompt::register_prompt("gtest", "Ask the AI to write Google Test (gtest) unit tests for the provided code.",
                            &execute_gtest, &render_help_gtest);

} // namespace prompt::prompts
