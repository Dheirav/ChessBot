#!/usr/bin/env bash
#
# Play ChessBot on Lichess.
#
# lichess-bot itself is not vendored here: it is AGPL-3.0, it is a separate
# project with its own release cadence, and a copy in this tree could not be
# updated with `git pull`. What lives in this repo is the integration — the
# config, this runner, and the README next to them. The checkout is external
# and this script drives it.
#
#   ./lichess/run.sh            play
#   ./lichess/run.sh -u         upgrade the account to a BOT account (once)
#
# Any arguments are passed through to lichess-bot.py.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# Alongside this repo by default: it is a checkout you maintain, not a system
# package. Override with LICHESS_BOT_DIR if you keep it elsewhere.
BOT_DIR="${LICHESS_BOT_DIR:-$(dirname "$REPO")/lichess-bot}"
CONFIG="$REPO/lichess/config.yml"

die() { echo "error: $*" >&2; exit 1; }

# --- the engine ------------------------------------------------------------
# Built here rather than assumed: an out-of-date binary is a silent way to play
# a hundred rated games with a bug you already fixed.
make -C "$REPO" --no-print-directory
[[ -x "$REPO/chessbot-uci.sh" ]] || die "missing $REPO/chessbot-uci.sh"

# --- the bot ---------------------------------------------------------------
if [[ ! -d "$BOT_DIR" ]]; then
    die "lichess-bot not found at $BOT_DIR
  git clone https://github.com/lichess-bot-devs/lichess-bot.git \"$BOT_DIR\"
  (or set LICHESS_BOT_DIR to an existing checkout)"
fi

if [[ ! -x "$BOT_DIR/venv/bin/python" ]]; then
    echo "Creating virtualenv in $BOT_DIR/venv"
    python3 -m venv "$BOT_DIR/venv"
    "$BOT_DIR/venv/bin/python" -m pip install --quiet -r "$BOT_DIR/requirements.txt"
fi

# --- the token -------------------------------------------------------------
# Environment, not config.yml: this file is in version control, and a token
# committed once is a token to revoke. lichess-bot reads LICHESS_BOT_TOKEN in
# preference to the config value.
if [[ -z "${LICHESS_BOT_TOKEN:-}" ]]; then
    die "LICHESS_BOT_TOKEN is not set. In your shell:
  read -rs LICHESS_BOT_TOKEN && export LICHESS_BOT_TOKEN"
fi

# Run from the bot's own directory: it resolves its logs and game_records
# relative to the working directory, and they do not belong in this repo.
cd "$BOT_DIR"
exec ./venv/bin/python lichess-bot.py --config "$CONFIG" -v "$@"
