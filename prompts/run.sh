#!/usr/bin/env bash
set -u

curdir="$(realpath "$(dirname "$0")/../bin")"
head_lines=""
run_args=()
stdin_piped=false
stdin_content=""

show_help() {
    cat <<'EOF'
Usage: prompt run [--head N] [run-args...]
       run target | prompt run [--head N]

Runs `bin/run` with the provided arguments and builds a debugging prompt
from its output. If stdin is piped in, it debugs the piped run output instead.

Options:
  --head N   Keep only the first N lines of run output
  -h, --help Show this help
EOF
}

if ! [ -t 0 ]; then
    stdin_piped=true
    stdin_content="$(cat)"
fi

while [[ $# -gt 0 ]]; do
    case "$1" in
        --help|-h)
            show_help
            exit 0
        ;;
        --head)
            if [[ $# -lt 2 ]]; then
                echo "Missing value for --head" >&2
                exit 2
            fi
            head_lines="$2"
            shift 2
        ;;
        *)
            run_args+=("$1")
            shift
        ;;
    esac
done

if [[ ${#run_args[@]} -eq 0 ]] && ! $stdin_piped; then
    echo "Debug the output from this run command."
    echo
    echo 'No target was provided to `run`. Ask for the exact target or arguments needed.'
    exit 0
fi

if [[ ${#run_args[@]} -gt 0 ]]; then
    tmp_output="$(mktemp)"
    if "$curdir/run" "${run_args[@]}" >"$tmp_output" 2>&1; then
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

if [[ -n "$head_lines" ]]; then
    run_output="$(printf "%s" "$run_output" | head -n "$head_lines")"
fi

# printf 'The following command was run:\n\n'
# printf '`run'
# for arg in "${run_args[@]}"; do
#   printf ' %q' "$arg"
# done
# printf '`\n\n'

if [[ -n "$stdin_content" && ${#run_args[@]} -gt 0 ]]; then
    printf '%s\n\n' "$stdin_content"
fi

printf 'Please debug the output from %s. Identify the root cause, explain the failure clearly, and suggest the smallest useful fix. If the build succeeded but the program failed at runtime, focus on the runtime issue.\n\n' "$run_description"
printf 'Exit status: %s\n\n' "$run_status"
printf '```text\n%s\n```\n' "$run_output"
