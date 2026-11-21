#!/bin/sh
# Quality report (TODO.md, quality gates): output level, gain-corrected SNR
# and the perceptual score per item, bitrate and quality level, decoded with
# FFmpeg and, when installed, mpg123, with LAME at the same bitrate as
# reference. Not run in CI.
#
# The perceptual score is the ODG of Advanced PEAQ (ITU-R BS.1387), from 0,
# imperceptible, to -4, very annoying. It is computed by the peaq command of
# GstPEAQ (https://github.com/HSU-ANT/gstpeaq), found in PATH or given as
# PEAQ, with its plugin in GST_PLUGIN_PATH; the column is "-" without it.
# GstPEAQ does not meet the standard's conformance tolerances: compare scores
# from the same installation only.
#
# tools/quality-report.sh [-b "64 128"] [-q "0 1"] [-m j|s|d] [-s] [-g]
#                          [-j jobs] [item.wav...]
#
#   -b  bitrates in kbps (default: 64 128 192)
#   -q  quality levels (default: every level)
#   -m  stereo mode for two-channel items: j joint (default), s, d dual
#   -s  add synthetic items made by FFmpeg: a tone, a sweep, pink noise,
#       bursts on one channel against a tone on the other
#   -g  only what tools/quality-gate.sh reads: shine, decoded by FFmpeg
#   -j  items measured in parallel (default: 1); lines then come unordered
#
# Items are any files FFmpeg reads. SHINEENC is the encoder to measure,
# ./shineenc by default: run the script on two builds and compare the
# reports to measure a change.

set -e

bitrates="64 128 192"
qualities=
mode=j
synthetic=
gate=
jobs=1
header=1
while getopts b:q:m:sgj:H option; do
  case $option in
  b) bitrates=$OPTARG ;;
  q) qualities=$OPTARG ;;
  m) mode=$OPTARG ;;
  s) synthetic=1 ;;
  g) gate=-g ;;
  j) jobs=$OPTARG ;;
  H) header= ;; # a parallel child: no header
  *) sed -n '14,27p' "$0" >&2; exit 2 ;;
  esac
done
shift $((OPTIND - 1))

shineenc=${SHINEENC:-./shineenc}
peaq=${PEAQ:-$(command -v peaq || true)}
command -v ffmpeg >/dev/null || { echo "ffmpeg is required" >&2; exit 2; }
[ -x "$shineenc" ] || { echo "$shineenc is not executable" >&2; exit 2; }
if [ -z "$qualities" ]; then
  max=$("$shineenc" -h 2>/dev/null | sed -n 's/.*quality level \[0-\([0-9]*\)\].*/\1/p')
  qualities=$(seq 0 "${max:-0}")
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -O2 -o "$work/compare_pcm" "$(dirname "$0")/compare_pcm.c" -lm

if [ -n "$synthetic" ]; then
  n=0
  for source in "aevalsrc=0.5*sin(2*PI*997*t)+0.01*(2*random(0)-1):d=10" \
                "aevalsrc=0.4*sin(2*PI*(50+1000*t)*t)|0.4*sin(2*PI*(50+1000*t)*t+0.3):d=10" \
                "anoisesrc=c=pink:a=0.3:d=10" \
                "aevalsrc=0.5*sin(2*PI*440*t)*(1-floor(2*t+0.9)+floor(2*t))|0.3*sin(2*PI*660*t):d=10"; do
    n=$((n + 1))
    ffmpeg -nostdin -loglevel error -f lavfi -i "$source" -ar 44100 -c:a pcm_s16le \
      "$work/synthetic-$n.wav"
  done
  set -- "$work"/synthetic-*.wav "$@"
fi
[ $# -gt 0 ] || { echo "no items" >&2; exit 2; }

# Decodes $1 to raw PCM at the item's rate and channel count, in $2.
decode_ffmpeg() {
  ffmpeg -nostdin -loglevel error -y -i "$1" -ar "$rate" -ac "$channels" \
    -f s16le "$2"
}
decode_mpg123() {
  mpg123 -q -w "$work/mpg123.wav" "$1" && decode_ffmpeg "$work/mpg123.wav" "$2"
}

# The ODG of the decoded PCM, delayed by $1 samples. PEAQ does not align
# its inputs: the decoded stream is trimmed to the reference.
odg() {
  [ -n "$peaq" ] || { echo -; return; }
  frames=$(($(wc -c <"$work/reference.raw") / 2 / channels))
  ffmpeg -nostdin -loglevel error -y -f s16le -ar "$rate" -ac "$channels" \
    -i "$work/decoded.raw" \
    -af "atrim=start_sample=$1:end_sample=$(($1 + frames))" "$work/aligned.wav"
  "$peaq" --advanced "$work/item.wav" "$work/aligned.wav" 2>/dev/null |
    sed -n 's/^Objective Difference Grade: //p'
}

# One line: item, bitrate, encoder, decoder, level, SNR, delay, ODG.
measure() {
  for decoder in ffmpeg mpg123; do
    [ $decoder = ffmpeg ] || [ -z "$gate" ] || continue
    [ $decoder = ffmpeg ] || command -v mpg123 >/dev/null || continue
    if decode_$decoder "$work/out.mp3" "$work/decoded.raw" 2>/dev/null &&
       result=$("$work/compare_pcm" "$channels" "$work/reference.raw" \
                "$work/decoded.raw"); then
      printf '%-28s %5s  %-8s %-7s %7s %7s %6s %7s\n' "$item" "$bitrate" "$1" \
        $decoder $result "$(odg "${result##* }")"
    else
      printf '%-28s %5s  %-8s %-7s  failed\n' "$item" "$bitrate" "$1" $decoder
    fi
  done
}

[ -z "$header" ] ||
  printf '%-28s %5s  %-8s %-7s %7s %7s %6s %7s\n' item kbps encoder decoder \
    "level" "SNR" delay ODG
if [ "$jobs" -gt 1 ] && [ $# -gt 1 ]; then
  printf '%s\n' "$@" | SHINEENC=$shineenc PEAQ=$peaq xargs -P "$jobs" -I{} \
    "$0" -H $gate -b "$bitrates" -q "$qualities" -m "$mode" {}
  exit
fi
for path in "$@"; do
  item=$(basename "$path")
  set -- $(ffprobe -v error -select_streams a:0 -show_entries \
    stream=sample_rate,channels -of default=noprint_wrappers=1:nokey=1 "$path")
  rate=$1 channels=$2
  ffmpeg -nostdin -loglevel error -y -i "$path" -c:a pcm_s16le \
    "$work/item.wav"
  ffmpeg -nostdin -loglevel error -y -i "$work/item.wav" -f s16le \
    "$work/reference.raw"
  stereo=
  [ "$channels" = 2 ] && stereo=-$mode

  for bitrate in $bitrates; do
    for quality in $qualities; do
      if "$shineenc" -q $stereo -Q "$quality" -b "$bitrate" "$work/item.wav" \
           "$work/out.mp3" >/dev/null 2>&1; then
        measure "shine-q$quality"
      else
        printf '%-28s %5s  %-8s unsupported\n' "$item" "$bitrate" \
          "shine-q$quality"
      fi
    done
    if [ -z "$gate" ] && command -v lame >/dev/null; then
      lame_mode=$mode
      [ "$channels" = 2 ] || lame_mode=m
      lame --silent -b "$bitrate" -m "$lame_mode" "$work/item.wav" \
        "$work/out.mp3" && measure lame
    fi
  done
done
