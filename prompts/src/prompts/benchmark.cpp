#include "../sdk/args.hpp"
#include "../sdk/embed.hpp"
#include "../sdk/prompt_registry.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace prompt::prompts {

static void execute_benchmark(prompt_context& ctx) {
    auto head_lines = head_lines_or_throw(ctx);

    std::string output = R"PROMPT(You are a performance engineering expert.
Analyze the provided code and design meaningful benchmarks to measure its performance.

For each benchmark, provide:
1. What to measure (latency, throughput, memory, CPU, I/O)
2. A concrete benchmark implementation in the appropriate framework
   - C++: Google Benchmark
   - Python: timeit, pytest-benchmark
   - JavaScript/TypeScript: bench.js or mitata
   - Go: testing.B
   - Rust: criterion
   - Other: language-appropriate micro-benchmark
3. What variables to vary (input sizes, concurrency levels, data distributions)
4. How to visualize and compare results

Guidelines:
- Benchmark the actual hot paths, not toy examples
- Include warmup iterations
- Account for noise (multiple runs, statistical significance)
- Measure what matters: p50, p95, p99 latency, not just averages
- Compare against a baseline when possible

)PROMPT";

    std::string error;
    auto sources = collect_file_args(ctx);
    for (auto const& file : sources.files) {
        output += std::format("File: {}\n\n```{}\n", file.filename().string(), infer_lang(file));
        output += trim_context_nl(read_file(file), head_lines);
        // benchmark.sh leaves a blank line before the closing fence.
        output += "\n```\n\n";
    }
    for (auto arg : sources.missing) {
        warn_missing_file(error, arg);
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
