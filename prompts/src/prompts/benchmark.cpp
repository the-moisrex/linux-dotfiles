#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

static void execute_benchmark(prompt_context& ctx) {
    std::optional<std::size_t> head_lines;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        throw usage_error{std::move(msg)};
    }

    std::string output;
    output += "You are a performance engineering expert.\n";
    output += "Analyze the provided code and design meaningful benchmarks to measure its "
              "performance.\n";
    output += "\nFor each benchmark, provide:\n1. What to measure (latency, throughput, memory, CPU, I/O)\n2. A "
              "concrete benchmark implementation in the appropriate framework\n   - C++: Google Benchmark\n   - "
              "Python: timeit, pytest-benchmark\n   - JavaScript/TypeScript: bench.js or mitata\n   - Go: testing.B\n  "
              " - Rust: criterion\n   - Other: language-appropriate micro-benchmark\n3. What variables to vary (input "
              "sizes, concurrency levels, data distributions)\n4. How to visualize and compare "
              "results\n\nGuidelines:\n- Benchmark the actual hot paths, not toy examples\n- Include warmup "
              "iterations\n- Account for noise (multiple runs, statistical significance)\n- Measure what matters: p50, "
              "p95, p99 latency, not just averages\n- Compare against a baseline when possible\n\n";

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
            // benchmark.sh leaves a blank line before the closing fence.
            output += "\n```\n\n";
        } else {
            warn_missing_file(error, ctx.args[i]);
        }
    }

    ctx.out = std::move(output);
    ctx.err = std::move(error);
}

static void render_help_benchmark(std::ostream& os) {
    os << R"EOF(Usage: prompt benchmark [--head N] [FILE...]
       some-command | prompt benchmark [--head N] [FILE...]

Performance benchmarking prompt.
Analyzes code and suggests concrete benchmarks to measure performance.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

// This file is the prompt: it implements it and registers it at static-init time
// (no central list — see sdk/prompt_registry.hpp).
bool const registered = prompt::register_prompt("benchmark",
                                                "Performance benchmarking prompt. Analyzes code and suggests concrete "
                                                "benchmarks to measure performance.",
                                                &execute_benchmark, &render_help_benchmark);

} // namespace prompt::prompts
