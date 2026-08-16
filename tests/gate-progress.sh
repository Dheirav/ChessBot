#!/usr/bin/env bash
# Live progress for a running (or finished) sharded gate.
#
#   ./tests/gate-progress.sh                 newest shard-* directory
#   ./tests/gate-progress.sh <dir>           a particular one
#   ./tests/gate-progress.sh <dir> --once    print one frame and exit
#
# A gate is hours of wall clock that prints nothing until it is over. That is
# not merely uncomfortable: it is how a gate that died in its first minute --
# a mistyped option name, an engine that would not start, a machine that ran
# out of memory (BUGS.md 8 took WSL down twice this way) -- goes unnoticed until
# the evening it was supposed to have produced a number.
#
# This reads the shard logs the match harness is already writing, so it costs
# the gate nothing and needs no cooperation from it. It reports the running
# score too, which is deliberately *not* a result: a partial match is a biased
# sample of a match, the pooled interval is the only thing that answers the
# question, and pool-shards.sh is what produces it.
set -u

DIR=""
ONCE=0
for a in "$@"; do
    case "$a" in
        --once) ONCE=1 ;;
        *) DIR="$a" ;;
    esac
done
if [ -z "$DIR" ]; then
    DIR=$(ls -1dt shard-*/ 2>/dev/null | head -1)
    [ -z "$DIR" ] && { echo "no shard-* directory here; pass one" >&2; exit 1; }
fi
DIR=${DIR%/}

# A single match log, rather than a directory of shards.
#
# `--tc` gates can never be sharded — wall-clock shards compete for the CPU and
# each one then plays a weaker engine than it would alone — so a time-management
# gate is always one long sequential run, and is exactly the kind that most
# needs watching. Treated as a one-shard directory so the rest of this script
# does not care which it is.
SINGLE=""
if [ -f "$DIR" ]; then
    SINGLE=$DIR
elif [ ! -d "$DIR" ]; then
    echo "no such directory or file: $DIR" >&2; exit 1
fi

BAR_W=44
started=$(date +%s)

# When the gate started, so attaching part-way through still estimates from the
# gate's own start rather than from this script's.
#
# A shard *directory* is created once and not touched again, so its mtime is its
# creation. A single log is appended to continuously, so its mtime is "a moment
# ago" and using it reported 25 minutes remaining on a three-hour run. Birth time
# is the right field for a file; it is only meaningful on filesystems that record
# it, so a zero falls back to mtime and an ETA that is at least not confidently
# wrong for long.
if [ -n "$SINGLE" ]; then
    birth=$(stat -c %W "$SINGLE" 2>/dev/null || echo 0)
    if [ "${birth:-0}" -gt 0 ]; then
        started=$birth
    else
        started=$(stat -c %Y "$SINGLE" 2>/dev/null || date +%s)
    fi
elif dir_epoch=$(stat -c %Y "$DIR" 2>/dev/null); then
    started=$dir_epoch
fi

bar() {   # bar <done> <total> <width>
    local d=$1 t=$2 w=$3 filled i out=""
    [ "$t" -le 0 ] && t=1
    filled=$(( d * w / t ))
    [ "$filled" -gt "$w" ] && filled=$w
    for ((i = 0; i < filled; i++)); do out+="█"; done
    for ((i = filled; i < w; i++)); do out+="░"; done
    printf '%s' "$out"
}

hms() {   # hms <seconds>
    local s=$1
    printf '%dh%02dm%02ds' $((s / 3600)) $(((s % 3600) / 60)) $((s % 60))
}

frame() {
    local total_done=0 total_target=0 live=0 shards=0
    local W=0 D=0 L=0
    local lines=""

    for log in ${SINGLE:-"$DIR"/shard-*.log}; do
        [ -e "$log" ] || continue
        shards=$((shards + 1))
        local name target done_pairs wdl
        name=$(basename "$log" .log)
        # "60 game pairs (up to 120 games) | seed 20261000".
        #
        # Counted in *games*, not pairs, because that is what the harness
        # reports progress in: it prints one line per game ("pair 1 A=white",
        # then "pair 1 A=black"), so counting those lines against a pair target
        # reads half done when it is done.
        target=$(grep -m1 -oE '^[0-9]+ game pairs' "$log" 2>/dev/null | grep -oE '^[0-9]+')
        target=$(( ${target:-0} * 2 ))
        # No `|| echo 0`: grep -c already prints 0 on no match, and the fallback
        # would append a second line, giving "0\n0" to the arithmetic below.
        done_pairs=$(grep -c '^  pair ' "$log" 2>/dev/null)
        done_pairs=${done_pairs:-0}
        # The harness prints a running (W-D-L a-b-c) on every game line.
        wdl=$(grep -oE '\(W-D-L [0-9]+-[0-9]+-[0-9]+\)' "$log" 2>/dev/null | tail -1)
        if [ -n "$wdl" ]; then
            wdl=${wdl#*L }; wdl=${wdl%)}
            W=$((W + ${wdl%%-*}))
            local rest=${wdl#*-}
            D=$((D + ${rest%%-*}))
            L=$((L + ${rest#*-}))
        fi
        total_done=$((total_done + done_pairs))
        total_target=$((total_target + target))
        # A shard is finished when it has printed its result block.
        if grep -q '^=== result' "$log" 2>/dev/null; then
            lines+=$(printf '  %-9s %s %3d/%-3d done\n' \
                     "$name" "$(bar "$done_pairs" "$target" 18)" "$done_pairs" "$target")
        else
            live=$((live + 1))
            lines+=$(printf '  %-9s %s %3d/%-3d\n' \
                     "$name" "$(bar "$done_pairs" "$target" 18)" "$done_pairs" "$target")
        fi
        lines+=$'\n'
    done

    local elapsed=$(( $(date +%s) - started ))
    local games=$((W + D + L))
    local pct=0
    [ "$total_target" -gt 0 ] && pct=$((total_done * 100 / total_target))

    printf '\n  gate: %s\n' "$DIR"
    printf '  args: %s\n\n' "$(grep -m1 -h 'vs' ${SINGLE:-"$DIR"/shard-1.log} 2>/dev/null | head -c 120)"
    printf '  %s  %d%%   %d/%d games\n' \
           "$(bar "$total_done" "$total_target" $BAR_W)" "$pct" "$total_done" "$total_target"
    if [ -n "$SINGLE" ]; then
        printf '  single unsharded run, %s   elapsed %s' \
               "$([ "$live" -gt 0 ] && echo running || echo finished)" "$(hms "$elapsed")"
    else
        printf '  %d shards, %d still running   elapsed %s' "$shards" "$live" "$(hms "$elapsed")"
    fi
    if [ "$total_done" -gt 0 ] && [ "$live" -gt 0 ]; then
        local rate remain eta
        # Integer arithmetic throughout: this runs every few seconds beside a
        # match that wants the CPU.
        rate=$(( elapsed * 1000 / (total_done > 0 ? total_done : 1) ))   # ms per game
        remain=$(( (total_target - total_done) * rate / 1000 ))
        eta=$remain
        printf '   eta ~%s' "$(hms "$eta")"
    fi
    printf '\n\n%s' "$lines"
    if [ "$games" -gt 0 ]; then
        # Percent to one decimal without bc: score is (W + D/2) / games.
        local tenths=$(( (2 * W + D) * 1000 / (2 * games) ))
        printf '\n  running: %d games  W %d / D %d / L %d   %d.%d%%\n' \
               "$games" "$W" "$D" "$L" "$((tenths / 10))" "$((tenths % 10))"
        printf '  (a partial match is a biased sample; the number that answers\n'
        if [ -n "$SINGLE" ]; then
            printf '   the question is the result block this run prints at the end)\n'
        else
            printf '   the question is ./tests/pool-shards.sh %s)\n' "$DIR"
        fi
    fi
    [ "$live" -eq 0 ] && return 1
    return 0
}

if [ "$ONCE" -eq 1 ]; then
    frame || true
    exit 0
fi

trap 'printf "\n"; exit 0' INT
while true; do
    printf '\033[2J\033[H'
    if ! frame; then
        if [ -n "$SINGLE" ]; then
            printf '\n  finished:\n'
            sed -n '/^=== result/,$p' "$SINGLE"
        else
            printf '\n  all shards finished. Pooling:\n'
            "$(dirname "$0")/pool-shards.sh" "$DIR"
        fi
        exit 0
    fi
    sleep 5
done
