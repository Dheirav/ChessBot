#!/usr/bin/env bash
# Review a game and open the report in a browser.
#
#   ./tools/review-open.sh <game.pgn> [extra review args...]
#   ./tools/review-open.sh --latest            the most recent archived game
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
    PGN="${1:?usage: review-open.sh <game.pgn> | --latest}"
    shift
fi

mkdir -p "$OUT"
NAME=$(basename "$PGN" .pgn)
# Lichess names carry the game id after the last " - "; use it when present, so
# the file is findable by the id in the URL.
ID="${NAME##* - }"
HTML="$OUT/${ID:-$NAME}.html"

echo "reviewing $NAME"
nice -n 10 "$REPO/tools/review" "$PGN" --html "$HTML" "$@" || exit 1

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
