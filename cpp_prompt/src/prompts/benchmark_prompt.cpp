#include "prompt/prompts/benchmark_prompt.hpp"
#include "prompt/sdk/args.hpp"
#include "prompt/sdk/embed.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

prompt_result execute_benchmark(prompt_context&& ctx) noexcept {
    std::size_t head_lines = 0;
    if (auto msg = parse_head_option({ctx.args.data(), ctx.args_count}, head_lines); !msg.empty()) {
        return {std::string{}, 2, false, std::move(msg)};
    }

    std::string output;
    output += "You are a performance engineering expert.\n";
    output += "Analyze the provided code and design meaningful benchmarks to measure its "
              "performance.\n";
    output += "\n";
    output += "For each benchmark, provide:\n";
    output += "1. What to measure (latency, throughput, memory, CPU, I/O)\n";
    output += "2. A concrete benchmark implementation in the appropriate framework\n";
    output += "   - C++: Google Benchmark\n";
    output += "   - Python: timeit, pytest-benchmark\n";
    output += "   - JavaScript/TypeScript: bench.js or mitata\n";
    output += "   - Go: testing.B\n";
    output += "   - Rust: criterion\n";
    output += "   - Other: language-appropriate micro-benchmark\n";
    output += "3. What variables to vary (input sizes, concurrency levels, data distributions)\n";
    output += "4. How to visualize and compare results\n";
    output += "\n";
    output += "Guidelines:\n";
    output += "- Benchmark the actual hot paths, not toy examples\n";
    output += "- Include warmup iterations\n";
    output += "- Account for noise (multiple runs, statistical significance)\n";
    output += "- Measure what matters: p50, p95, p99 latency, not just averages\n";
    output += "- Compare against a baseline when possible\n";
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
            // benchmark.sh leaves a blank line before the closing fence.
            output += "\n```\n";
            output += "\n";
        } else {
            error += "Warning: File '" + std::string(ctx.args[i]) + "' not found.\n";
        }
    }

    return {std::move(output), 0, false, std::move(error)};
}

void render_help_benchmark(std::ostream& os) noexcept {
    os << R"EOF(Usage: prompt benchmark [--head N] [FILE...]
       some-command | prompt benchmark [--head N] [FILE...]

Performance benchmarking prompt.
Analyzes code and suggests concrete benchmarks to measure performance.

Options:
  --head N   Keep only the first N lines of the embedded context
)EOF";
}

} // namespace prompt::prompts
