#!/usr/bin/env bash
set -euo pipefail

show_help() {
  cat <<'EOF'
Usage: prompt benchmark [--head N] [FILE...]
       some-command | prompt benchmark [--head N] [FILE...]

Performance benchmarking prompt.
Analyzes code and suggests concrete benchmarks to measure performance.

Options:
  --head N   Keep only the first N lines of the embedded context
EOF
}

source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"

echo "You are a performance engineering expert."
echo "Analyze the provided code and design meaningful benchmarks to measure its performance."
echo
echo "For each benchmark, provide:"
echo "1. What to measure (latency, throughput, memory, CPU, I/O)"
echo "2. A concrete benchmark implementation in the appropriate framework"
echo "   - C++: Google Benchmark"
echo "   - Python: timeit, pytest-benchmark"
echo "   - JavaScript/TypeScript: bench.js or mitata"
echo "   - Go: testing.B"
echo "   - Rust: criterion"
echo "   - Other: language-appropriate micro-benchmark"
echo "3. What variables to vary (input sizes, concurrency levels, data distributions)"
echo "4. How to visualize and compare results"
echo
echo "Guidelines:"
echo "- Benchmark the actual hot paths, not toy examples"
echo "- Include warmup iterations"
echo "- Account for noise (multiple runs, statistical significance)"
echo "- Measure what matters: p50, p95, p99 latency, not just averages"
echo "- Compare against a baseline when possible"
echo

for file in "$@"; do
    if [[ -f "$file" ]]; then
        file_name="$(basename "$file")"
        echo "File: $file_name"
        echo
        echo "\`\`\`$(infer_lang "$file_name")"
        trim_context "$(cat -- "$file")"
        echo
        echo '```'
        echo
    else
        echo "Warning: File '$file' not found." >&2
    fi
done
