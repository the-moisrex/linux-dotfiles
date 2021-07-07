#!/bin/bash

lang=$1
for file in *.mkv; do
  index=$(ffmpeg -i "$file" 2>&1 | grep $lang | grep Subtitle | head -1 | grep -Eo '[0-9]+:[0-9]+');
  ffmpeg -i "$file" -map $index "$file.$lang.srt";
done
