#!/bin/sh
# Cost report: CPU time and peak memory of the encoder per quality level, to
# see what the psychoacoustic model (doc/psy/) costs. Not run in CI: the
# figures depend on the machine, so compare runs from the same one.
#
# tools/cost-report.sh [-b "64 128"] [-q "0 1"] [-t seconds] item.wav...
#
#   -b  bitrates in kbps (default: 64 128 320)
#   -q  quality levels (default: 0 1)
#   -t  audio encoded per measure, the item looped (default: 300)
#
# Items are any files FFmpeg reads. SHINEENC is the encoder to measure,
# ./shineenc by default; a libtool wrapper is timed with its shell, so give
# the real binary. Columns: CPU seconds (user + system, best of 3), the
# speed as a multiple of real time, the peak resident memory, and both
# against the first quality level.

bitrates="64 128 320"
levels="0 1"
seconds=300
while getopts b:q:t: option; do
  case $option in
  b) bitrates=$OPTARG ;;
  q) levels=$OPTARG ;;
  t) seconds=$OPTARG ;;
  *) exit 2 ;;
  esac
done
shift $((OPTIND - 1))
[ $# -gt 0 ] || { echo "usage: $0 [-b bitrates] [-q levels] [-t seconds] item.wav..." >&2; exit 2; }

encoder=${SHINEENC:-./shineenc}
work=$(mktemp -d) || exit 1
trap 'rm -rf "$work"' EXIT

# CPU seconds and peak resident bytes of a command, on stdout.
measure() {
  if /usr/bin/time -l true 2>/dev/null; then
    /usr/bin/time -l "$@" 2>&1 >/dev/null |
      awk '/ user /{cpu = $3 + $5} /maximum resident set size/{rss = $1}
           END {print cpu, rss}'
  else
    /usr/bin/time -v "$@" 2>&1 >/dev/null |
      awk -F': ' '/User time/{cpu += $2} /System time/{cpu += $2}
                  /Maximum resident set size/{rss = $2 * 1024}
                  END {print cpu, rss}'
  fi
}

printf '%-28s %5s %1s %5s %2s %8s %9s %8s %6s %6s\n' \
  item rate c kbps q cpu-s realtime mem-MB cpu-x mem-x
status=1
for item; do
  input=$work/input.wav
  ffmpeg -nostdin -loglevel error -y -stream_loop -1 -i "$item" -t "$seconds" \
    -c:a pcm_s16le "$input" || continue
  rate=$(ffprobe -v error -select_streams a:0 -show_entries stream=sample_rate \
    -of default=nw=1:nk=1 "$input")
  channels=$(ffprobe -v error -select_streams a:0 -show_entries stream=channels \
    -of default=nw=1:nk=1 "$input")
  for kbps in $bitrates; do
    first_cpu= first_rss=
    for level in $levels; do
      best= rss=
      for run in 1 2 3; do
        set -- $(measure "$encoder" -q -Q "$level" -b "$kbps" "$input" "$work/out.mp3")
        [ -s "$work/out.mp3" ] || continue 2
        if [ -z "$best" ] || [ "$(echo "$1 < $best" | bc)" = 1 ]; then best=$1; fi
        rss=$2
      done
      [ "$(echo "$best > 0" | bc)" = 1 ] || continue
      : "${first_cpu:=$best}" "${first_rss:=$rss}"
      printf '%-28s %5s %1s %5s %2s %8.2f %8.0fx %8.2f %6.1f %6.2f\n' \
        "$(basename "$item")" "$rate" "$channels" "$kbps" "$level" "$best" \
        "$(echo "$seconds / $best" | bc -l)" "$(echo "$rss / 1048576" | bc -l)" \
        "$(echo "$best / $first_cpu" | bc -l)" "$(echo "$rss / $first_rss" | bc -l)"
      status=0
    done
  done
done
# Fails if nothing was measured.
exit $status
