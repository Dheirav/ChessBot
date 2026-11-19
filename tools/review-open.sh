#!/usr/bin/env bash
# Review a game and open the report in a browser.
#
#   ./tools/review-open.sh <game.pgn> [extra review args...]
#   ./tools/review-open.sh --latest            the most recent archived game
#   ./tools/review-open.sh <chess.com link>    fetch the game, then review it
#
# Reports land in ~/reviews rather than the repo: they are generated output, one
# file per game, and nothing here should have to gitignore a growing directory.
#
# It does not build. `make review` relinks binaries, which is unsafe while a
# gate or a rated game is in flight -- see CLAUDE.md.
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARCHIVE="${ARCHIVE:-/home/dheirav/Code/lichess-bot/game_records}"
OUT="${REVIEW_OUT:-$HOME/reviews}"

[ -x "$REPO/tools/review" ] || {
    echo "tools/review is not built. Run 'make review' when no gate or rated game is running." >&2
    exit 1
}

if [ "${1:-}" = "--latest" ]; then
    shift
    PGN=$(ls -t "$ARCHIVE"/*.pgn 2>/dev/null | head -1)
    [ -n "$PGN" ] || { echo "no games in $ARCHIVE" >&2; exit 1; }
else
    PGN="${1:?usage: review-open.sh <game.pgn> | <chess.com link> | --latest}"
    shift
fi

mkdir -p "$OUT"

# A Chess.com link is turned into a PGN first, kept under ~/reviews/chesscom so
# a game can be re-reviewed (deeper, or with --explain) without fetching again.
# The id is the filename, which is also what the report is named after.
case "$PGN" in
    http://*|https://*|[0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]*)
        mkdir -p "$OUT/chesscom"
        # The Lichess bot's name is the wrong default for a Chess.com game; the
        # owner's Chess.com account is. BOT= still overrides it.
        : "${BOT:=CrimsyCane}"
        ID=$(python3 "$REPO/tools/chesscom-pgn.py" --id "$PGN") || exit 1
        FETCHED="$OUT/chesscom/$ID.pgn"
        python3 "$REPO/tools/chesscom-pgn.py" "$PGN" -o "$FETCHED" || exit 1
        PGN="$FETCHED"
        ;;
esac

NAME=$(basename "$PGN" .pgn)
# Lichess names carry the game id after the last " - "; use it when present, so
# the file is findable by the id in the URL.
ID="${NAME##* - }"
HTML="$OUT/${ID:-$NAME}.html"

# Orient every game to the side being studied -- a review of your own game shown
# from the opponent's side is one you have to read upside down. Set BOT to your
# own name when reviewing a game exported from Chess.com or Lichess:
#   BOT=your_username ./tools/review-open.sh game.pgn
BOT="${BOT:-Crimsy_Bot}"

# A review of one game is a minute or more at depth 16, so it leaves a start
# time and its own "analysing i/N" counter where tools/review-progress.sh can
# read them. Without these there is nothing to derive a rate from.
STEM="${ID:-$NAME}"
date +%s > "$OUT/$STEM.start"
echo "reviewing $NAME   (watch: ./tools/review-progress.sh $STEM --watch)"
nice -n 10 "$REPO/tools/review" "$PGN" --html "$HTML" --me "$BOT" "$@" \
    2> >(tee "$OUT/$STEM.log" >&2) || exit 1

# Opening a browser is best-effort: the report is the deliverable and it is
# already written by this point, so failing to launch one is not an error.
if command -v wslpath >/dev/null 2>&1 && [ -x "/mnt/c/Program Files/Google/Chrome/Application/chrome.exe" ]; then
    "/mnt/c/Program Files/Google/Chrome/Application/chrome.exe" "$(wslpath -w "$HTML")" >/dev/null 2>&1 &
elif command -v xdg-open >/dev/null 2>&1; then
    xdg-open "$HTML" >/dev/null 2>&1 &
else
    echo "open it yourself: $HTML"
fi
echo "$HTML"
