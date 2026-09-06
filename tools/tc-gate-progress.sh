#!/usr/bin/env bash
# Progress for a --tc gate, which shard-gate.sh cannot run and
# tools/gate-progress.sh therefore cannot read: one process, one log, no shards.
#
# ETA is derived from this run's own measured pace, never from a prior estimate.
# The conthist gate missed by 13x because its rate was assumed stationary and
# was not; this recomputes from elapsed-so-far on every call.
LOG=${1:-/tmp/claude-1000/-home-dheirav-Code-ChessBot/514fe6b5-51aa-473f-8f02-cfcd855cd27c/scratchpad/gate-threads.log}
[ -f "$LOG" ] || { echo "no log at $LOG"; exit 1; }

total=$(grep -oE '^[0-9]+ game pairs' "$LOG" | head -1 | cut -d' ' -f1)
done=$(grep -c 'A=black' "$LOG")
wdl=$(grep -oE '\(W-D-L [0-9]+-[0-9]+-[0-9]+\)' "$LOG" | tail -1 | tr -d '()' | sed 's/W-D-L //')
# Elapsed comes from the match process itself, not from the log's mtime -- a
# log being written to has its mtime bumped constantly, so that read as "0h00m"
# forever and produced a 0.3 s/pair ETA for a job pacing at about a minute.
pid=$(pgrep -x match | head -1)
if [ -n "$pid" ]; then
    el=$(ps -o etimes= -p "$pid" | tr -d ' ')
else
    # Finished: fall back to the wall time the match itself reported.
    el=$(grep -oE '^wall *: [0-9.]+' "$LOG" | grep -oE '[0-9.]+' | cut -d. -f1)
fi
el=${el:-0}

printf '\n  %s\n' "$(grep -m1 '^difference:' "$LOG")"
w=40; f=0; [ "${total:-0}" -gt 0 ] && f=$(( done * w / total ))
printf '  ['
[ "$f" -gt 0 ] && printf '%0.s#' $(seq 1 "$f")
[ $(( w - f )) -gt 0 ] && printf '%0.s.' $(seq 1 $(( w - f )))
printf ']  %d/%s pairs\n' "$done" "${total:-?}"
[ -n "$wdl" ] && printf '  A: %s (W-D-L)\n' "$wdl"
if [ "$done" -gt 0 ]; then
    eta=$(( el * (total - done) / done ))
    printf '  elapsed %dh%02dm   ETA ~%dh%02dm   %.1f s/pair\n' \
        $(( el/3600 )) $(( el%3600/60 )) $(( eta/3600 )) $(( eta%3600/60 )) \
        "$(awk -v e="$el" -v d="$done" 'BEGIN{printf "%.1f", e/d}')"
else
    printf '  elapsed %dh%02dm   ETA -- (no pair finished yet)\n' $(( el/3600 )) $(( el%3600/60 ))
fi
pgrep -xc match >/dev/null && printf '  running\n\n' || printf '  FINISHED\n\n'
