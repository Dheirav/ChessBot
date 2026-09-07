#!/usr/bin/env bash
# Run a command once the machine is actually free, and say afterwards whether it
# stayed free.
#
#   tools/when-idle.sh [--cores 1.5] [--samples 3] [--interval 30] -- <command...>
#
# Why the second half matters as much as the first. A timed measurement -- a
# --tc gate, a thread-scaling run -- is only valid on an idle machine, and
# waiting for idle is easy. The trap is contention that arrives *during* the
# run: the numbers come out looking clean and mean nothing, and nothing in them
# says so. So this samples load throughout and reports the peak, and the caller
# can throw the result away on that basis rather than on trust.
#
# Requires the quiet period to be *sustained* (--samples consecutive checks)
# because a multi-core job between work units can dip for a few seconds and is
# not finished.
set -u
CORES=1.5; SAMPLES=3; INTERVAL=30
while [ $# -gt 0 ]; do
  case "$1" in
    --cores)    CORES=$2; shift 2 ;;
    --samples)  SAMPLES=$2; shift 2 ;;
    --interval) INTERVAL=$2; shift 2 ;;
    --) shift; break ;;
    *) break ;;
  esac
done
[ $# -gt 0 ] || { echo "usage: when-idle.sh [opts] -- <command...>" >&2; exit 2; }

# Busy cores *excluding our own process tree*.
#
# The first version summed all of %CPU, which counted the very command being
# measured: a Threads=8 run uses eight cores by design, so the watcher tripped
# its own contention alarm every time it measured threading and reported a
# valid result as invalid. Descendants of this script are therefore excluded,
# and what is left is genuinely somebody else's work.
mine() {   # every pid descended from this script, plus this script
    local out="$$" frontier="$$" next
    while [ -n "$frontier" ]; do
        next=$(ps -eo pid=,ppid= | awk -v p="$(echo $frontier | tr ' ' '|')" \
               '$2 ~ "^("p")$" {print $1}' | tr '\n' ' ')
        [ -z "$next" ] && break
        out="$out $next"; frontier="$next"
    done
    echo "$out"
}
busy_cores() {
    local excl; excl=$(mine | tr ' ' '\n' | grep -v '^$' | paste -sd'|')
    ps -eo pid=,pcpu= | awk -v e="^($excl)$" '$1 !~ e {s+=$2} END {printf "%.2f", s/100}'
}

echo "waiting for < $CORES busy cores, $SAMPLES consecutive checks $INTERVAL s apart"
ok=0
while :; do
  b=$(busy_cores)
  if awk -v b="$b" -v c="$CORES" 'BEGIN{exit !(b<c)}'; then
    ok=$(( ok + 1 ))
    echo "  $(date '+%H:%M:%S')  ${b} cores busy  (quiet $ok/$SAMPLES)"
    [ "$ok" -ge "$SAMPLES" ] && break
  else
    [ "$ok" -gt 0 ] && echo "  $(date '+%H:%M:%S')  ${b} cores busy  (reset)"
    ok=0
  fi
  sleep "$INTERVAL"
done

echo "=== machine is free at $(date '+%H:%M:%S'); starting ==="
PEAK=0
( while :; do
    b=$(busy_cores)
    awk -v b="$b" -v p="$PEAK" 'BEGIN{exit !(b>p)}' && PEAK=$b
    echo "$PEAK" > /tmp/claude-1000/when-idle-peak.$$
    sleep 10
  done ) & SAMPLER=$!

"$@"; rc=$?

kill "$SAMPLER" 2>/dev/null
peak=$(cat /tmp/claude-1000/when-idle-peak.$$ 2>/dev/null || echo "?")
rm -f /tmp/claude-1000/when-idle-peak.$$
echo "=== finished at $(date '+%H:%M:%S'), exit $rc ==="
echo "peak busy cores during the run, excluding our own: $peak"
awk -v p="$peak" 'BEGIN{ if (p+0 > 2.5) print "WARNING: contention arrived mid-run. Treat these numbers as invalid." }'
exit $rc
