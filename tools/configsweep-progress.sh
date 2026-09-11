#!/usr/bin/env bash
# Progress for tools/configsweep. The transposition table prints an allocation
# line per search straight to stdout, which buries everything else; this strips
# it and derives the ETA from the run's own measured pace.
LOG=${1:-/tmp/configsweep.log}
TOTAL=${2:-11}
[ -f "$LOG" ] || { echo "no log at $LOG"; exit 1; }
done_n=$(tr '\r' '\n' < "$LOG" | grep -cE "^  [a-z].*[0-9]+/[0-9]+")
ref=$(tr '\r' '\n' < "$LOG" | grep -oE "reference [0-9]+/[0-9]+" | tail -1)
pid=$(pgrep -x configsweep)
if [ -n "$pid" ]; then
    el=$(ps -o etimes= -p "$pid" | tr -d ' ')
    # The reference pass is a fixed cost; pace the remainder off finished configs.
    if [ "$done_n" -gt 0 ]; then
        per=$(( (el - 480) / done_n ))
        eta=$(( per * (TOTAL - done_n) ))
        printf '  %d/%d configs   elapsed %dm%02ds   ETA ~%dm   (%ds per config)\n' \
            "$done_n" "$TOTAL" $((el/60)) $((el%60)) $((eta/60)) "$per"
    else
        printf '  reference pass: %s   elapsed %dm%02ds\n' "${ref:-starting}" $((el/60)) $((el%60))
    fi
else
    printf '  finished, %d/%d configs\n' "$done_n" "$TOTAL"
fi
echo
tr '\r' '\n' < "$LOG" | grep -E "^  (configuration|[a-z])" | grep -vE "^  reference"
