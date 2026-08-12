#!/usr/bin/env bash
# Run the outstanding Phase 3 gates back to back, unattended.
#
#   ./tests/run-gates.sh [pairs-per-shard] [shards]
#
# Defaults to 120 pairs across 14 shards per gate: 3 360 games each, roughly
# 100 minutes each, so all three fit in a night.
#
# Sequential, not parallel: each gate saturates the machine on its own, and
# running them together would only make each one finish later.
set -u
cd "$(dirname "$0")/.."

PAIRS=${1:-120}
SHARDS=${2:-14}
NODES=100000

make tests/match || exit 1

run_gate() {
    local name=$1 opt=$2
    echo
    echo "=============================================================="
    echo "gate: $name   ($(date))"
    echo "=============================================================="
    ./tests/shard-gate.sh "$SHARDS" "$PAIRS" -N "$NODES" \
        --optA "$opt=on" --optB "$opt=off" 2>&1 | tee "gate-$name.log"
}

# Aging first: it is on by default and shipping ungated, which makes it the one
# result that changes what the engine does today.
run_gate ttaging     ttaging
run_gate seepruning  seepruning
run_gate seeordering seeordering

echo
echo "=============================================================="
echo "all gates finished $(date)"
echo "  gate-ttaging.log  gate-seepruning.log  gate-seeordering.log"
echo "=============================================================="
