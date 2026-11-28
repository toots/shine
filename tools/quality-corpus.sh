#!/bin/sh
# Makes the items of a quality corpus from music files, for
# tools/quality-report.sh: 30 s from the middle of each file, named after it,
# in stereo at the three MPEG-1 rates, at 22.05 and 16 kHz (MPEG-2) and
# 11.025 kHz (MPEG-2.5), and in mono at 44.1 kHz.
#
# tools/quality-corpus.sh <directory> <file>...
#
# Low rates matter: the model's tables differ per rate, and a corpus at
# 44.1 kHz only says nothing about them.

set -e
[ $# -ge 2 ] || { sed -n '6p' "$0" >&2; exit 2; }
directory=$1
shift
mkdir -p "$directory"

n=0
for path in "$@"; do
  n=$((n + 1))
  name=$(basename "$path" | sed 's/\.[^.]*$//; s/[^A-Za-z0-9_-]/_/g')
  start=$(ffprobe -v error -show_entries format=duration \
    -of default=noprint_wrappers=1:nokey=1 "$path" |
    awk '{ s = ($1 - 30) / 2; print (s > 0 ? s : 0) }')
  for rate in 48000 44100 32000 22050 16000 11025; do
    ffmpeg -nostdin -loglevel error -y -ss "$start" -t 30 -i "$path" \
      -ar $rate -ac 2 -c:a pcm_s16le "$directory/$name-$rate.wav"
  done
  ffmpeg -nostdin -loglevel error -y -ss "$start" -t 30 -i "$path" -ar 44100 \
    -ac 1 -c:a pcm_s16le "$directory/$name-44100-mono.wav"
done
echo "$n files, $((n * 7)) items in $directory"
