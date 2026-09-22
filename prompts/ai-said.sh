#!/usr/bin/env bash

show_help() {
  cat <<'EOF'
Usage: prompt ai-said [--interval N] [--no-clear] [--head N]

Collect multiple AI chat outputs via the clipboard and combine them
into a single prompt.

Flow:
  1. Run `prompt ai-said` (clears the clipboard by default).
  2. Copy each AI chatbot output from the browser, one after another.
     Each clipboard change is captured, prefixed with
     "The first AI said:", "The second AI said:", ... and the
     combined result is copied back to the clipboard.
  3. Paste the combined result into your AI agent.
  4. Press Ctrl+C to stop; the final combined text is printed to
     stdout (and auto-copied by the `prompt` dispatcher).

Options:
  --interval N   Poll the clipboard every N seconds (default: 0.5)
  --no-clear     Keep the existing clipboard content instead of clearing it
  --head N       Keep only the first N lines of each captured entry
EOF
}

source "$(dirname "$0")/_common.sh"

interval="0.5"
clear_on_start=true

parse_input_arguments() {
    set -- "${ARGS[@]}"
    ARGS=()
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --help|-h)
                show_help
                exit 0
            ;;
            --head)
                if [[ $# -lt 2 ]]; then
                    echo "prompt ai-said: Missing value for --head" >&2
                    exit 2
                fi
                head_lines="$2"
                shift 2
            ;;
            --interval)
                if [[ $# -lt 2 ]]; then
                    echo "prompt ai-said: Missing value for --interval" >&2
                    exit 2
                fi
                interval="$2"
                shift 2
            ;;
            --no-clear)
                clear_on_start=false
                shift
            ;;
            --*)
                echo "prompt ai-said: Unknown option: $1" >&2
                exit 2
            ;;
            *)
                ARGS+=("$1")
                shift
            ;;
        esac
    done
}

parse_input_arguments
set -- "${ARGS[@]}"

if [[ -n "${1:-}" ]]; then
    echo "prompt ai-said: This prompt takes no positional arguments." >&2
    exit 2
fi

if ! [[ "$interval" =~ ^[0-9]+(\.[0-9]+)?$ ]] || [[ "$interval" == "0" ]]; then
    echo "prompt ai-said: --interval must be a positive number of seconds." >&2
    exit 2
fi

script_dir="$(cd "$(dirname "$0")" && pwd)"
CLIPBOARD="${CLIPBOARD_CMD:-$script_dir/../bin/clipboard}"

if [[ ! -x "$CLIPBOARD" ]]; then
    echo "prompt ai-said: Clipboard helper not found: $CLIPBOARD" >&2
    exit 1
fi

# Ordinal words for the first entries; numeric suffixes after that.
ordinal_word() {
    local n="$1"
    case "$n" in
        1) printf 'Another' ;;
        2) printf 'A second' ;;
        3) printf 'A third' ;;
        *)
            local mod100=$(( n % 100 ))
            local mod10=$(( n % 10 ))
            local suffix="th"
            if [[ "$mod100" -lt 11 || "$mod100" -gt 13 ]]; then
                case "$mod10" in
                    1) suffix="st" ;;
                    2) suffix="nd" ;;
                    3) suffix="rd" ;;
                esac
            fi
            printf 'A %s%s' "$n" "$suffix"
        ;;
    esac
}

entry_label() {
    printf '%s AI said:' "$(ordinal_word "$1")"
}

hash_content() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum | cut -d' ' -f1
        elif command -v md5sum >/dev/null 2>&1; then
        md5sum | cut -d' ' -f1
        elif command -v cksum >/dev/null 2>&1; then
        cksum | cut -d' ' -f1
    else
        # Fallback: use the content itself as its own "hash".
        cat
    fi
}

paste_clipboard() {
    "$CLIPBOARD" paste 2>/dev/null
}

copy_clipboard() {
    printf '%s' "$1" | "$CLIPBOARD" copy 2>/dev/null
}

entries=()

build_output() {
    local i content label
    for i in "${!entries[@]}"; do
        content="${entries[$i]}"
        if [[ -n "${head_lines:-}" ]]; then
            content="$(trim_context "$content")"
        fi
        label="$(entry_label "$(( i + 1 ))")"
        printf '%s\n\n%s\n\n' "$label" "$content"
        if [[ "$i" -lt "$(( ${#entries[@]} - 1 ))" ]]; then
            printf '\n\n\n------------------------------------------------------------------\n'
        fi
    done
}

finalized=false

finalize() {
    [[ "$finalized" == "true" ]] && return 0
    finalized=true
    if [[ "${#entries[@]}" -gt 0 ]]; then
        build_output
        printf 'Collected %d %s. Final prompt printed to stdout.\n' \
        "${#entries[@]}" \
        "$([[ "${#entries[@]}" -eq 1 ]] && printf 'entry' || printf 'entries')" >&2
    else
        printf 'No entries collected. Nothing to output.\n' >&2
    fi
}

trap finalize EXIT
trap 'exit 130' INT TERM

if "$clear_on_start"; then
    "$CLIPBOARD" clear 2>/dev/null || true
    # Give the clipboard daemon a moment to settle, then check.
    sleep 0.2
    baseline_raw="$(paste_clipboard || true)"
    if [[ -n "${baseline_raw//[[:space:]]/}" ]]; then
        printf 'Warning: clipboard still holds %d chars after clear; that content will be ignored.\n' "${#baseline_raw}" >&2
    else
        printf 'Clipboard cleared.\n' >&2
    fi
else
    printf 'Keeping existing clipboard content.\n' >&2
    baseline_raw="$(paste_clipboard || true)"
fi

# Baseline so pre-existing/stale content is not captured as entry #1.
# IMPORTANT: hash the command-substituted value (trailing newlines
# stripped), exactly like the poll loop does. Hashing the raw pipe
# directly would hash trailing newlines too, producing a different hash
# for identical content and wrongly capturing it as entry #1.
last_raw_hash="$(printf '%s' "$baseline_raw" | hash_content)"
last_wrote_hash=""

printf 'Watching clipboard every %ss. Copy AI outputs one by one;\n' "$interval" >&2
printf 'each change is collected and re-copied. Press Ctrl+C to finish.\n' >&2

while true; do
    raw="$(paste_clipboard || true)"
    raw_hash="$(printf '%s' "$raw" | hash_content)"
    
    if [[ "$raw_hash" == "$last_raw_hash" ]]; then
        sleep "$interval"
        continue
    fi
    last_raw_hash="$raw_hash"
    
    # Skip empty clipboard and our own combined output (loop guard).
    if [[ -z "${raw//[[:space:]]/}" ]]; then
        sleep "$interval"
        continue
    fi
    if [[ -n "$last_wrote_hash" && "$raw_hash" == "$last_wrote_hash" ]]; then
        sleep "$interval"
        continue
    fi
    
    entries+=("$raw")
    combined="$(build_output)"
    if copy_clipboard "$combined"; then
        last_wrote_hash="$(printf '%s' "$combined" | hash_content)"
    else
        printf 'Warning: failed to copy combined output to clipboard.\n' >&2
    fi
    
    printf '[ai-said] captured #%d (%d chars) — combined output copied to clipboard.\n' \
    "${#entries[@]}" "${#raw}" >&2
    
    sleep "$interval"
done
