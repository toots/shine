#!/bin/sh
# The metric gate of doc/psy/tests.md §4, on two reports of
# tools/quality-report.sh: the previous commit's and the new one's.
#
# tools/quality-gate.sh previous.txt new.txt
#
# Compares shine's lines decoded by FFmpeg, from the previous report to the
# new one, and from each quality level to the next within the new one:
#
# - no item may lose more than SCORE_TOLERANCE, unless both scores are 0;
# - no configuration (sample rate, channels, bitrate) may lose more than
#   SCORE_MEAN_TOLERANCE on the mean of its items.
#
# The score is the 2f column of the reports. Reports made before it existed
# have none: give the gate two such reports and it falls back to the ODG
# column with the rules it had then (0.2 per item, 0.03 per configuration,
# below -3.0 on both sides not counted).
#
# An item whose score falls by more than the tolerance as the bitrate rises,
# at one quality level of the new report, is one the metric misjudges: it is
# listed, and left out of both rules. Fails too if nothing was compared.

[ $# -eq 2 ] || { sed -n '5p' "$0" >&2; exit 2; }

# The 2f column when both reports have it.
column=13 tolerance=5 mean_tolerance=0.75 floor=0.0001
for report in "$1" "$2"; do
  awk 'NF == 13 && $13 ~ /^[0-9.]+$/ { found = 1; exit } END { exit !found }' \
    "$report" || { column=10 tolerance=0.2 mean_tolerance=0.03 floor=-3.0; }
done

awk -v column=$column -v tolerance=$tolerance -v mean_tolerance=$mean_tolerance \
    -v floor=$floor '
FNR == 1 { pass++ }
$5 ~ /^shine-q[0-9]+$/ && $6 == "ffmpeg" && $column ~ /^-?[0-9.]+$/ {
  quality = substr($5, 8) + 0
  key = $1 SUBSEP $4 SUBSEP quality
  if (pass == 1) { previous[key] = $column; next }
  current[key] = $column
  config[$1] = $2 " Hz " $3 " ch"
  items[$1]; rates[$4 + 0]; if (quality > levels) levels = quality
}
function compare(what, item, kbps, quality, new, old,    group) {
  compared++
  group = what ": " config[item] " " kbps " kbps"
  sum[group] += new - old; count[group]++
  if (new - old < -tolerance && !(new < floor && old < floor)) {
    printf "FAIL %s: %s %s kbps quality %d scores %.3f, was %.3f\n", what, item, kbps, quality, new, old
    failures++
  }
}
END {
  n = 0; for (r in rates) sorted[n++] = r + 0
  for (i = 0; i < n; i++) for (j = i + 1; j < n; j++)
    if (sorted[j] < sorted[i]) { t = sorted[i]; sorted[i] = sorted[j]; sorted[j] = t }
  for (item in items)
    for (q = 0; q <= levels; q++) {
      last = ""
      for (i = 0; i < n; i++) {
        key = item SUBSEP sorted[i] SUBSEP q
        if (!(key in current)) continue
        if (last != "" && current[key] - last < -tolerance && !(item in unreliable)) {
          unreliable[item]
          printf "UNRELIABLE %s: quality %d scores %.3f at %d kbps after %.3f at a lower bitrate\n", item, q, current[key], sorted[i], last
          flagged++
        }
        last = current[key]
      }
    }
  for (key in current) {
    split(key, part, SUBSEP)
    if (part[1] in unreliable) continue
    if (key in previous)
      compare("against the previous report", part[1], part[2], part[3], current[key], previous[key])
    lower = part[1] SUBSEP part[2] SUBSEP (part[3] - 1)
    if (lower in current)
      compare("quality " part[3] " against " (part[3] - 1), part[1], part[2], part[3], current[key], current[lower])
  }
  for (group in sum)
    if (sum[group] / count[group] < -mean_tolerance) {
      printf "FAIL %s: mean of %d items %+.3f\n", group, count[group], sum[group] / count[group]
      failures++
    }
  if (!compared) { print "FAIL: nothing was compared"; exit 1 }
  printf "%d comparisons, %d items the metric misjudges left out, %d failures\n", compared, flagged, failures
  exit failures ? 1 : 0
}' "$1" "$2"
