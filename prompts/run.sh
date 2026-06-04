#!/usr/bin/env bash
set -u

curdir="$(realpath "$(dirname "$0")/../bin")"
promptdir="$(realpath "$(dirname "$0")")"
head_lines=""
stdin_piped=false
stdin_content=""
include_gtest_cases=false

show_help() {
    cat <<'EOF'
Usage: prompt run [--head N] [--gtest] [run-args...]
       run target | prompt run [--head N] [--gtest]

Runs `bin/run` with the provided arguments and builds a debugging prompt
from its output. If stdin is piped in, it debugs the piped run output instead.

Options:
  --head N   Keep only the first N lines of run output
  --gtest    Include source for failed Google Test cases
  -h, --help Show this help
EOF
}

source "$(dirname "$0")/_common.sh"
common_behavior
set -- "${ARGS[@]}"

RUN_ARGS=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --gtest)
            include_gtest_cases=true
            shift
        ;;
        *)
            RUN_ARGS+=("$1")
            shift
        ;;
    esac
done
set -- "${RUN_ARGS[@]}"

strip_ansi() {
    sed -E $'s/\x1B\\[[0-9;?]*[ -/]*[@-~]//g'
}

extract_failed_gtests() {
    awk '
        {
            line = $0
            sub(/\r$/, "", line)
            if (match(line, /^\[  FAILED  \] +([[:alnum:]_][^[:space:](),]*)/, found)) {
                test = found[1]
                if (test ~ /\./ && !(test in seen)) {
                    seen[test] = 1
                    print test
                }
            }
        }
    '
}

if [[ $# -gt 0 ]]; then
    tmp_output="$(mktemp)"
    if "$curdir/run" "$@" >"$tmp_output" 2>&1; then
        run_status=0
    else
        run_status=$?
    fi
    run_output="$("$curdir/strip-osc" < "$tmp_output")"
    rm -f "$tmp_output"
    run_description='the `run` command'
else
    run_status="unknown"
    run_output="$(printf "%s" "$stdin_content" | "$curdir/strip-osc")"
    run_description='the piped `run` output'
fi

gtest_search_output="$run_output"

if [[ -n "$head_lines" ]]; then
    run_output="$(printf "%s" "$run_output" | head -n "$head_lines")"
fi

# printf 'The following command was run:\n\n'
# printf '`run'
# for arg in "$@"; do
#   printf ' %q' "$arg"
# done
# printf '`\n\n'

printf 'Debug the output from %s. Identify the root cause, explain the failure clearly, and suggest the smallest useful fix. If the build succeeded but the program failed at runtime, focus on the runtime issue.\n\n' "$run_description"
printf 'Exit status: %s\n\n' "$run_status"
printf '```text\n%s\n```\n' "$run_output"

if $include_gtest_cases; then
    mapfile -t failed_gtests < <(printf '%s\n' "$gtest_search_output" | strip_ansi | extract_failed_gtests)
    if [[ ${#failed_gtests[@]} -gt 0 ]]; then
        printf '\n'
        "$promptdir/gtest-case.sh" --exact "${failed_gtests[@]}"
    else
        printf '\nNo failed Google Test cases were detected in the output.\n'
    fi
fi
