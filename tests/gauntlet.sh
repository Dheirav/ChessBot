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
# budget. The first attempt used eight games per point and put the even mark at
# 800 nodes; re-measured over forty, the same opponent scores:
#
#   Stockfish @400n  ->  we score 48.8%   (40 games)
#   Stockfish @550n  ->  we score 41.2%   (40 games)
#   Stockfish @800n  ->  we score 32.5%   (80 games)
#
# So 800 was wrong by a wide margin and this file said otherwise for an evening.
# Eight games cannot place a ruler: the interval on 8 games is hundreds of Elo,
# which was written in this very comment and then used anyway. The number to
# keep is 400.
#
# On resolution. At 80 games this script could not separate a candidate from
# the baseline (30.6% vs 32.5%) on a change self-play measured cleanly at -33
# Elo over 3 360 games. That is a sample-size problem and not a property of the
# method: the match is node-limited, so it shards exactly like any other -N
# gate and the fix is to run it that way —
#
#   ./tests/shard-gate.sh 14 60 --na 100000 --nb 400 \
#       --engineA tests/engine-<candidate> --engineB /usr/games/stockfish \
#       --argsB "" --foreignB
#
# 1 680 games in about an hour, which is the same order as a self-play gate.
# (An earlier version of this comment claimed a gauntlet cannot be sharded.
# That is true of a *timed* match, where shards compete for the CPU and each
# plays a weaker engine than it would alone. A node budget is spent identically
# whatever else is running, which is the whole reason shard-gate.sh insists on
# one.)
#
# What it is for, and what it is not. Self-play compares this engine to itself,
# so it cannot see a change whose value depends on the opponent doing something
# this engine does not do — attacking, most of all — and it scores 50% for a
# change that makes both sides equally worse. Those are the two jobs here. It
# is not a second opinion on questions self-play answers well: on 2026-08-21 it
# added nothing to a verdict self-play had already delivered cleanly.
#
# Not a replacement for shard-gate.sh. This plays one engine against another
# and cannot be sharded into a pooled result the way a self-play gate can, so
# it is the second opinion on a change, not the first.
set -uo pipefail

PAIRS=${1:?usage: gauntlet.sh <pairs> [match args...]}; shift
OPP=${OPP:-/usr/games/stockfish}
# OUR points at the engine under test. Default is the shipped binary; a gate on
# a candidate points it at that build instead, e.g.
#   OUR=tests/engine-kingsafety ./tests/gauntlet.sh 40
OUR=${OUR:-./chessbot-uci.sh}
OPP_NODES=${OPP_NODES:-400}      # calibrated 2026-08-21; see above before changing
OUR_NODES=${OUR_NODES:-100000}   # the standing gate budget, so results compare
SEED=${SEED:-20260821}

[ -x "$OPP" ] || { echo "no opponent engine at $OPP (set OPP=<path>)" >&2; exit 1; }
[ -x "$OUR" ] || { echo "no engine at $OUR (set OUR=<path>)" >&2; exit 1; }
[ -x ./tests/match ] || { echo "run 'make tests' first" >&2; exit 1; }

echo "$(basename "$OUR") @${OUR_NODES}n  vs  $(basename "$OPP") @${OPP_NODES}n   ${PAIRS} pairs, seed ${SEED}"
echo "the opponent is a fixed ruler: change OPP_NODES and past results stop comparing"
exec ./tests/match -n "$PAIRS" -s "$SEED" \
    --na "$OUR_NODES" --nb "$OPP_NODES" \
    --engineA "$OUR" --engineB "$OPP" \
    --argsB "" --foreignB "$@"
