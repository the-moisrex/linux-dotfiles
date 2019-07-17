#!/bin/sh
# FFmppeg Linux screen recorder
#
REC_iface=$(pactl list sources short | awk '{print$2}' | grep 'monitor')
SCREEN_res=$(xrandr -q --current | grep '*' | awk '{print$1}')

# ffmpeg -f alsa -thread_queue_size 1024 -i hw:0 -ac 2 -acodec vorbis -f x11grab -r 25 -s $SCREEN_res -i :0.0 -vcodec libx264 "$1"
# ffmpeg -f alsa -thread_queue_size 1024 -i hw:0 -ac 2 -c:a libmp3lame -f x11grab -r 25 -s $SCREEN_res -i :0.0 -c:v libx264 -crf 18 -preset ultrafast "$1"
#ffmpeg -f pulse -thread_queue_size 1024 -i default -c:a aac -f x11grab -r 25 -s $SCREEN_res -i $DISPLAY -c:v libx264 -crf 18 -preset superfast -qp 0 "$1"
#ffmpeg -f alsa -thread_queue_size 1024 -i front:CARD=MID,DEV=0 -c:a aac -f x11grab -r 25 -s $SCREEN_res -i $DISPLAY -c:v libx264 -crf 18 -preset superfast -qp 0 "$1"
#ffmpeg -f alsa -thread_queue_size 1024 -i hw:0 -c:a aac -f x11grab -r 25 -s $SCREEN_res -i $DISPLAY -c:v libx264 -crf 18 -preset superfast -qp 0 "$1"


ffmpeg -f alsa -thread_queue_size 1024 -i hw:0,0 -c:a aac -f x11grab -r 25 -s $SCREEN_res -i $DISPLAY -c:v libx264 -crf 18 -preset superfast -qp 0 "$1"
