#!/bin/sh
# The metric gate of doc/psy/tests.md §4, on two reports of
# tools/quality-report.sh: the previous commit's and the new one's.
#
# tools/quality-gate.sh previous.txt new.txt
#
# Fails if the ODG of an item falls by more than ODG_TOLERANCE from the
# previous report to the new one, or from a quality level to the next one
# within the new report, unless both scores are below ODG_FLOOR. Compares
# shine's lines decoded by FFmpeg, per item and bitrate. Fails too if
# nothing was compared.

[ $# -eq 2 ] || { sed -n '2,5p' "$0" >&2; exit 2; }

awk -v tolerance=0.05 -v floor=-3.0 '
function compare(what, key, new, old) {
  compared++
  sum[what] += new - old
  count[what]++
  if (new - old < -tolerance && !(new < floor && old < floor)) {
    printf "FAIL %s: %s scores %.3f, was %.3f\n", what, key, new, old
    failures++
  }
}
FNR == 1 { pass++ }
$3 ~ /^shine-q[0-9]+$/ && $4 == "ffmpeg" && $8 ~ /^-?[0-9.]+$/ {
  key = $1 " " $2 " kbps " $3
  if (pass == 1) { previous[key] = $8; next }
  if (key in previous) compare("against the previous report", key, $8, previous[key])
  quality = substr($3, 8) + 0
  current[$1 " " $2 " " quality] = $8
  if (quality > levels) levels = quality
  items[$1 " " $2]
}
END {
  for (item in items)
    for (q = 1; q <= levels; q++)
      if ((item " " q) in current && (item " " (q - 1)) in current)
        compare("quality " q " against " (q - 1), item " kbps",
                current[item " " q], current[item " " (q - 1)])
  for (what in sum)
    printf "%s: %d comparisons, mean %+.3f\n", what, count[what], sum[what] / count[what]
  if (!compared) { print "FAIL: nothing was compared"; exit 1 }
  printf "%d failures\n", failures
  exit failures ? 1 : 0
}' "$1" "$2"
