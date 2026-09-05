#!/bin/bash
# FFmpeg Linux screen recorder

if [[ "$1" == "--help" || "$1" == "-h" ]]; then
    cat <<'EOF'
Usage: record.sh [output_file]

Record screen using FFmpeg with ALSA audio capture.

Options:
  -h, --help    Show this help message

If no output file is specified, outputs to 'output.mkv'.
EOF
    exit 0
fi

if ! command -v ffmpeg >/dev/null 2>&1; then
    echo "Error: ffmpeg is not installed." >&2
    exit 1
fi

REC_iface="$(pactl list sources short 2>/dev/null | awk '/monitor/{print $2}')"
SCREEN_res="$(xrandr -q --current 2>/dev/null | awk '/\*/{print $1}')"

if [[ -z "$SCREEN_res" ]]; then
    echo "Error: Could not detect screen resolution." >&2
    exit 1
fi

output="${1:-output.mkv}"

ffmpeg -f alsa -thread_queue_size 1024 -i hw:0,0 -c:a aac -f x11grab -r 25 -s "$SCREEN_res" -i "$DISPLAY" -c:v libx264 -crf 18 -preset superfast "$output"
