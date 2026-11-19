#!/usr/bin/env bash
# Progress of a running tools/review-open.sh.
#
#   ./tools/review-progress.sh                 newest review, one snapshot
#   ./tools/review-progress.sh <id> --watch    refresh until it finishes
#
# Reads the "analysing i/N" counter that tools/review writes to stderr and the
# start time review-open.sh recorded. The ETA is positions left divided by the
# rate measured so far, nothing assumed in advance: positions are not equally
# expensive (a crowded middlegame costs more than an opening), so this drifts
# and says so by being a rate, not a promise.
set -u
OUT="${REVIEW_OUT:-$HOME/reviews}"

WATCH=0; ID=""
for a in "$@"; do
    case "$a" in
        --watch) WATCH=1 ;;
        *) ID="$a" ;;
    esac
done
[ -n "$ID" ] || ID=$(ls -t "$OUT"/*.start 2>/dev/null | head -1 | xargs -r basename | sed 's/\.start$//')
[ -n "$ID" ] || { echo "no review has been started from review-open.sh"; exit 1; }

snap() {
    local start now el cur tot line rate eta w=30 f
    start=$(cat "$OUT/$ID.start" 2>/dev/null) || { echo "no start time for $ID"; return 1; }
    now=$(date +%s); el=$((now - start))
    line=$(tr '\r' '\n' < "$OUT/$ID.log" 2>/dev/null | grep -o 'analysing [0-9]*/[0-9]*' | tail -1)
    if [ -z "$line" ]; then
        printf '%s: no position finished yet, %ds elapsed (fetching or starting Stockfish)\n' "$ID" "$el"
        return 0
    fi
    cur=${line#analysing }; tot=${cur#*/}; cur=${cur%/*}
    f=$((cur * w / tot)); local bar; bar=$(printf '%*s' "$f" '' | tr ' ' '#')$(printf '%*s' $((w - f)) '' | tr ' ' '-')
    if [ "$cur" -ge "$tot" ]; then
        printf '%s [%s] %d/%d  %ds elapsed  (analysis done, writing report)\n' "$ID" "$bar" "$cur" "$tot" "$el"
    elif [ "$el" -gt 0 ] && [ "$cur" -gt 0 ]; then
        eta=$(( (tot - cur) * el / cur ))
        printf '%s [%s] %d/%d  %ds elapsed  ETA %ds at the measured %.2f positions/s\n' \
            "$ID" "$bar" "$cur" "$tot" "$el" "$eta" "$(echo "$cur $el" | awk '{print $1/$2}')"
    fi
}

if [ "$WATCH" = 1 ]; then
    while :; do
        snap
        pgrep -f "tools/review .*$ID" >/dev/null || break
        sleep 3
    done
    [ -f "$OUT/$ID.html" ] && echo "done: $OUT/$ID.html"
else
    snap
fi
