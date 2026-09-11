#!/usr/bin/env bash
# Progress for a set of tools/treecost runs started in parallel, one log each.
# Rate comes from each run's own PROGRESS line, never from an estimate.
for f in "$@"; do
  log=/tmp/tc-$f.log
  [ -f "$log" ] || { printf '  %-12s not started\n' "$f"; continue; }
  if grep -q "### $f DONE" "$log"; then
    printf '  %-12s done\n' "$f"
  else
    stage=$(grep -c '^###' "$log")
    printf '  %-12s running (stage %s/2)\n' "$f" "${stage:-1}"
  fi
done
