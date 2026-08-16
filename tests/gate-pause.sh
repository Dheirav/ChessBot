#!/usr/bin/env bash
# Freeze or thaw a running gate, to get the machine back without losing it.
#
#   ./tests/gate-pause.sh stop     SIGSTOP every gate process
#   ./tests/gate-pause.sh start    SIGCONT them
#   ./tests/gate-pause.sh status   what is running, and whether it is frozen
#
# This is safe for exactly two reasons, both of which would stop being true if
# the gate were run differently.
#
# The budget is *nodes*, not milliseconds, so a search interrupted for an hour
# spends the same budget it would have spent uninterrupted. A `-t` or `--tc`
# match frozen this way would silently become a different experiment, which is
# the same reason shard-gate.sh refuses to shard one.
#
# And UciEngine::waitFor() blocks on the pipe with no deadline -- it fails only
# if the child dies. So a frozen engine is not a slow engine, it is a silent
# one, and the harness simply waits. Add a timeout there and this script becomes
# a way to corrupt a gate rather than to pause one.
set -u

# The engine binaries are matched by their gate names, never by `chessbot`:
# `pgrep -f chessbot` matches the shell running the check and has produced
# false positives repeatedly (BUGS.md 9).
PATTERNS='tests/match|tests/engine|shard-gate.sh'

pids() { pgrep -f "$PATTERNS" | grep -v "^$$\$"; }

case "${1:-status}" in
    stop)
        n=0
        for p in $(pids); do kill -STOP "$p" 2>/dev/null && n=$((n + 1)); done
        echo "froze $n gate processes; ./tests/gate-pause.sh start resumes them"
        ;;
    start)
        n=0
        for p in $(pids); do kill -CONT "$p" 2>/dev/null && n=$((n + 1)); done
        echo "resumed $n gate processes"
        ;;
    status)
        # State T is stopped; R/S are running and sleeping.
        ps -o pid,stat,etime,args -p "$(pids | tr '\n' ',' | sed 's/,$//')" 2>/dev/null \
            || echo "no gate processes running"
        ;;
    *)
        echo "usage: $0 stop|start|status" >&2
        exit 1
        ;;
esac
