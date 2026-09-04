#!/usr/bin/env bash
# Live progress for a running shard-gate.
#
#   ./tools/gate-progress.sh              newest shard-* dir, one snapshot
#   ./tools/gate-progress.sh --watch      refresh until every shard is done
#   ./tools/gate-progress.sh shard-2026... [--watch]
#
# Why this exists: HANDOFF records gate ETAs being wrong repeatedly -- "20
# minutes" then "10 hours" then 56 minutes -- because tests/match prints per
# pair but nothing aggregates the shards while they run. Guessing at a gate's
# remaining time from the outside is how that happened.
#
# Each pair prints two lines, "A=white" then "A=black", so completed pairs are
# counted off the second of the two. A pair is the unit of observation here for
# the same reason it is in match.cpp: both games share an opening.
set -u

WATCH=0; DIR=""
for a in "$@"; do
    case "$a" in
        --watch) WATCH=1 ;;
        *) DIR="$a" ;;
    esac
done
[ -n "$DIR" ] || DIR=$(ls -dt shard-*/ 2>/dev/null | head -1)
[ -n "$DIR" ] || { echo "no shard-* directory found"; exit 1; }
DIR=${DIR%/}

bar() {  # bar <done> <total> <width>
    local d=$1 t=$2 w=$3 f
    [ "$t" -gt 0 ] || t=1
    f=$(( d * w / t ))
    [ "$f" -gt "$w" ] && f=$w
    # printf with a format and no arguments still emits the format once, so a
    # zero-length run has to be skipped rather than printed.
    printf '['
    [ "$f" -gt 0 ]        && printf '%0.s#' $(seq 1 "$f")
    [ $(( w - f )) -gt 0 ] && printf '%0.s.' $(seq 1 $(( w - f )))
    printf ']'
}

render() {
    local shards done_pairs=0 total_pairs=0 running=0 W=0 D=0 L=0
    shards=$(ls "$DIR"/shard-*.log 2>/dev/null | wc -l)
    [ "$shards" -gt 0 ] || { echo "$DIR: no shard logs yet"; return; }

    # Pairs per shard is on the header line each shard prints.
    local per
    per=$(grep -hoE '^[0-9]+ game pairs' "$DIR"/shard-1.log 2>/dev/null | head -1 | cut -d' ' -f1)
    per=${per:-0}
    total_pairs=$(( per * shards ))

    printf '\n  %s\n' "$DIR"
    grep -hm1 'difference:' "$DIR"/shard-1.log 2>/dev/null | sed 's/^/  /'
    printf '\n'

    local i
    for i in $(seq 1 "$shards"); do
        local f="$DIR/shard-$i.log" n=0 last=""
        [ -f "$f" ] || continue
        n=$(grep -c 'A=black' "$f" 2>/dev/null); n=${n:-0}
        done_pairs=$(( done_pairs + n ))
        last=$(grep -oE '\(W-D-L [0-9]+-[0-9]+-[0-9]+\)' "$f" 2>/dev/null | tail -1 \
               | tr -d '()' | sed 's/W-D-L //')
        if [ -n "$last" ]; then
            W=$(( W + $(echo "$last" | cut -d- -f1) ))
            D=$(( D + $(echo "$last" | cut -d- -f2) ))
            L=$(( L + $(echo "$last" | cut -d- -f3) ))
        fi
        # A shard is still running if its process lives; approximate by "not pooled".
        grep -q '^pairs' "$f" 2>/dev/null || running=$(( running + 1 ))
        printf '  shard %-2d ' "$i"; bar "$n" "$per" 24
        printf ' %3d/%-3d\n' "$n" "$per"
    done

    printf '\n  TOTAL   '; bar "$done_pairs" "$total_pairs" 24
    printf ' %d/%d pairs (%d%%)\n' "$done_pairs" "$total_pairs" \
        $(( total_pairs > 0 ? done_pairs * 100 / total_pairs : 0 ))
    printf '  games   A: %d wins, %d draws, %d losses\n' "$W" "$D" "$L"

    # Rate and ETA from the directory's own mtime, so a resumed watch is honest.
    local t0 now el rate eta
    t0=$(stat -c %Y "$DIR"); now=$(date +%s); el=$(( now - t0 ))
    if [ "$done_pairs" -gt 0 ] && [ "$el" -gt 0 ]; then
        eta=$(( el * (total_pairs - done_pairs) / done_pairs ))
        printf '  elapsed %dh%02dm   ETA ~%dh%02dm   (%.1f pairs/min)\n' \
            $(( el/3600 )) $(( el%3600/60 )) $(( eta/3600 )) $(( eta%3600/60 )) \
            "$(awk -v d="$done_pairs" -v e="$el" 'BEGIN{printf "%.1f", d*60/e}')"
    else
        printf '  elapsed %dh%02dm   ETA --\n' $(( el/3600 )) $(( el%3600/60 ))
    fi
    printf '  shards still running: %d of %d\n\n' "$running" "$shards"
    LAST_RUNNING=$running
}

if [ "$WATCH" -eq 1 ]; then
    LAST_RUNNING=1
    while [ "${LAST_RUNNING:-1}" -gt 0 ]; do
        clear; render; sleep 20
    done
    echo "  all shards finished -- pooled result:"
    ./tests/pool-shards.sh "$DIR"
else
    render
fi
