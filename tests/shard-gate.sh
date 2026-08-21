#!/usr/bin/env bash
# Run one gate as N independent shards in parallel and pool the results.
#
#   ./tests/shard-gate.sh <shards> <pairs-per-shard> [match args...]
#
# e.g.  ./tests/shard-gate.sh 14 60 -N 100000 --optA seepruning=on --optB seepruning=off
#
# tests/match is single-threaded, so one run uses one core of however many this
# machine has. Sharding by opening seed uses the rest.
#
# Shard only a *node*-limited gate (-N). Under a time budget (-t) the shards
# compete for the CPU, so each one plays a weaker engine than it would have
# alone, and the pooled result describes a time control that was never chosen.
# A node budget is spent identically no matter what else is running, which is
# what makes the shards poolable at all.
#
# Do NOT pass --sprt: a stopping rule applied per shard and then pooled is not
# a valid test — each shard stops on its own favourable noise. Fixed N per
# shard, pooled afterwards, is.
set -u

SHARDS=${1:?shards}; shift
PAIRS=${1:?pairs per shard}; shift

for arg in "$@"; do
    if [ "$arg" = "--sprt" ]; then
        echo "refusing: --sprt per shard is not a valid pooled test (see header)" >&2
        exit 1
    fi
done
case " $* " in
    *" -N "*|*" --na "*) ;;
    *) echo "refusing: shard only a node-limited gate (-N); see header" >&2; exit 1 ;;
esac

# Re-running a gate with the same base replays the same games, so pooling the
# two runs double-counts every one of them. A second run that means to *add*
# games must state a different base: SEED_BASE=20260821 ./tests/shard-gate.sh ...
SEED_BASE=${SEED_BASE:-20260810}

DIR="shard-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$DIR"
echo "$SHARDS shards x $PAIRS pairs -> $DIR"
echo "  seed base:  $SEED_BASE"
echo "  match args: $*"

for i in $(seq 1 "$SHARDS"); do
    seed=$((SEED_BASE + i * 1000))
    ./tests/match -n "$PAIRS" -s "$seed" "$@" > "$DIR/shard-$i.log" 2>&1 &
done
wait

echo "$DIR"
"$(dirname "$0")/pool-shards.sh" "$DIR"
