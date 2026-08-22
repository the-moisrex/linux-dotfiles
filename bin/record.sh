#!/bin/sh
# FFmpeg Linux screen recorder

if [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
    echo "Usage: record.sh [output_file]"
    echo
    echo "Record screen using FFmpeg with ALSA audio capture."
    echo
    echo "Options:"
    echo "  -h, --help    Show this help message"
    echo
    echo "If no output file is specified, outputs to 'output.mkv'."
    exit 0
fi

REC_iface=$(pactl list sources short | awk '{print$2}' | grep 'monitor')
SCREEN_res=$(xrandr -q --current | grep '*' | awk '{print$1}')

output="${1:-output.mkv}"

ffmpeg -f alsa -thread_queue_size 1024 -i hw:0,0 -c:a aac -f x11grab -r 25 -s $SCREEN_res -i $DISPLAY -c:v libx264 -crf 18 -preset superfast "$output"
