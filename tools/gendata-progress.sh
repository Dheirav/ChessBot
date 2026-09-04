#!/usr/bin/env bash
# Combined progress for the gendata shards. Per the same reasoning as
# tools/gate-progress.sh: a long job that reports nothing is a job whose ETA
# gets guessed, and guessed ETAs in this project have been wrong by 13x.
DATA=${1:-/home/dheirav/chessbot-data}
tot=0; run=0
printf '\n  corpus generation - %s\n\n' "$DATA"
for f in "$DATA"/shard*.txt; do
  [ -f "$f" ] || continue
  n=$(wc -l < "$f")
  tot=$(( tot + n ))
  pgrep -f "$(basename "$f")" >/dev/null && { st="running"; run=$((run+1)); } || st="stopped"
  printf '  %-14s %10d positions   %s\n' "$(basename "$f")" "$n" "$st"
done
sz=$(du -sh "$DATA" 2>/dev/null | cut -f1)
printf '\n  TOTAL %d positions   %s on disk   %d/%d shards running\n' "$tot" "$sz" "$run" "$(ls "$DATA"/shard*.txt 2>/dev/null | wc -l)"
line=$(grep -ho 'ETA [0-9]*m' "$DATA"/shard*.progress 2>/dev/null | tail -1)
[ -n "$line" ] && printf '  slowest shard %s\n' "$line"
printf '\n'
