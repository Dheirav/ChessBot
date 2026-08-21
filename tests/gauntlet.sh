#!/usr/bin/env bash
# Play ChessBot against a *different* engine, not against itself.
#
#   ./tests/gauntlet.sh <pairs> [--optA <feature>=on] [more match args...]
#
# e.g. ./tests/gauntlet.sh 40 --optA kingdanger=on --optB kingdanger=off
#
# Why this exists. Every gate in this repo is self-play: two configurations of
# ChessBot, one difference between them. That answers "is A better than B at
# being this engine", and for most heuristics it is the right question. For one
# family of changes it is structurally the wrong one.
#
# ROADMAP.md 6.4 closed king safety as negative over four gates and 10 080
# games, and recorded the caveat that makes those gates suspect: in self-play
# **both sides get the term**, and both sides share this engine's disinclination
# to attack. A king-safety term is worth what it saves against an opponent that
# attacks kings. Give it to both players in a match between two engines that do
# not, and it prices at zero — not because it is worthless, but because the
# experiment cannot contain the thing it is measuring.
#
# BUGS.md 13 is the same lesson from the other end: the bot is 0-4 against
# 2200+ opposition on Lichess, making 3.3 errors per 100 moves against them and
# zero in 268 moves below 2000. The opponents that punish it are exactly the
# ones self-play cannot simulate.
#
# So: a fixed external opponent. The score against it is comparable across
# runs, and a change that helps against a real attacker shows up here even when
# self-play calls it nothing.
#
# Calibration. Stockfish at equal nodes wins every game, and a 0% score cannot
# measure anything — every pair scores the same, so there is no variance to
# estimate from and the harness says so rather than inventing an interval.
# Handicap it by nodes until the score is somewhere near even, then keep that
# number fixed forever: the opponent is a ruler, and a ruler that changes
# length between measurements is not one. OPP_NODES below is that number.
#
# Calibrated 2026-08-21 against ChessBot at the standing 100 000-node gate
# budget: Stockfish at 300 nodes scored 18.8%, at 800 nodes 50.0%, at 2 500
# nodes 87.5%, at 8 000 nodes 100%. Eight games each, so those percentages
# carry intervals hundreds of Elo wide — which is fine for choosing where to
# stand the ruler and useless as a measurement of anything. The number to keep
# is 800; the percentages around it are not results.
#
# Not a replacement for shard-gate.sh. This plays one engine against another
# and cannot be sharded into a pooled result the way a self-play gate can, so
# it is the second opinion on a change, not the first.
set -uo pipefail

PAIRS=${1:?usage: gauntlet.sh <pairs> [match args...]}; shift
OPP=${OPP:-/usr/games/stockfish}
OPP_NODES=${OPP_NODES:-800}      # calibrated 2026-08-21; see above before changing
OUR_NODES=${OUR_NODES:-100000}   # the standing gate budget, so results compare
SEED=${SEED:-20260821}

[ -x "$OPP" ] || { echo "no opponent engine at $OPP (set OPP=<path>)" >&2; exit 1; }
[ -x ./tests/match ] || { echo "run 'make tests' first" >&2; exit 1; }

echo "ChessBot @${OUR_NODES}n  vs  $(basename "$OPP") @${OPP_NODES}n   ${PAIRS} pairs, seed ${SEED}"
echo "the opponent is a fixed ruler: change OPP_NODES and past results stop comparing"
exec ./tests/match -n "$PAIRS" -s "$SEED" \
    --na "$OUR_NODES" --nb "$OPP_NODES" \
    --engineA ./chessbot-uci.sh --engineB "$OPP" \
    --argsB "" --foreignB "$@"
