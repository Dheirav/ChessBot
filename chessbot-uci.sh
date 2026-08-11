#!/usr/bin/env bash
# ChessBot as a plain UCI engine, for tools that expect to exec one binary and
# talk the protocol over stdin/stdout: lichess-bot, cutechess-cli, Arena.
#
# The wrapper exists for two reasons. The flag: `--uci` has to be passed, and
# every tool spells "extra engine arguments" differently. And the directory:
# the move lookup tables are resolved next to the executable, so starting from
# somewhere else makes the engine recompute them on every launch.
cd "$(dirname "$0")" || exit 1
exec ./chessbot --uci "$@"
