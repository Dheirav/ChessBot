#!/usr/bin/env bash
# Review every archived game into ONE browsable file.
#
#   ./tools/review-archive.sh [extra review args...]
#
# Why one file rather than one per game: a per-game report is ~42 KB, of which
# ~21 KB is the same twelve piece images and ~15 KB the same stylesheet. Across
# 72 games that is 3.3 MB to say what 0.4 MB says, and it leaves you with 72
# loose files and no way in. One document carries the assets once and gains a
# game picker for free.
#
# Per-game records are cached as JSON, so adding a game re-reviews that game
# rather than the archive. Assembling is then a concatenation and costs nothing.
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARCHIVE="${ARCHIVE:-/home/dheirav/Code/lichess-bot/game_records}"
OUT="${REVIEW_OUT:-$HOME/reviews}"
CACHE="$OUT/records"
BOT="${BOT:-Crimsy_Bot}"
JOBS="${JOBS:-3}"

[ -x "$REPO/tools/review" ] || {
    echo "tools/review is not built. Run 'make review' when no gate or rated game is running." >&2
    exit 1
}
mkdir -p "$CACHE"
RECVER=$("$REPO/tools/review" --record-version 2>/dev/null | tr -d '[:space:]')
[ -n "$RECVER" ] || { echo "cannot ask tools/review for its record version" >&2; exit 1; }
export RECVER

one() {
    pgn="$1"; name=$(basename "$pgn" .pgn); id="${name##* - }"
    out="$CACHE/${id:-$name}.json"
    # A cached record from an older build is incomplete rather than wrong, and
    # an archive assembled from a mix of the two says nothing about which games
    # lack what. Re-review anything that is not the current format.
    if [ -s "$out" ] && grep -q "\"v\":$RECVER" "$out"; then return 0; fi
    rm -f "$out"
    # Orient each game to the side being studied; the picker then never shows a
    # game upside down.
    nice -n 10 "$REPO/tools/review" "$pgn" --json "$out" --me "$BOT" "${EXTRA[@]}" >/dev/null 2>&1 \
        || { echo "  failed: $name" >&2; rm -f "$out"; return 1; }
    echo "  reviewed $name"
}
export -f one
export REPO CACHE BOT

EXTRA=("$@")
export EXTRA

mapfile -t PGNS < <(ls -1 "$ARCHIVE"/*.pgn 2>/dev/null)
[ "${#PGNS[@]}" -gt 0 ] || { echo "no games in $ARCHIVE" >&2; exit 1; }
echo "${#PGNS[@]} games; reviewing the ones not already cached"
printf '%s\0' "${PGNS[@]}" | xargs -0 -P "$JOBS" -I{} bash -c 'one "$@"' _ {}

# Newest first: the interesting game is nearly always the most recent one.
mapfile -t RECS < <(ls -1t "$CACHE"/*.json 2>/dev/null)
[ "${#RECS[@]}" -gt 0 ] || { echo "no records to assemble" >&2; exit 1; }
HTML="$OUT/archive.html"
"$REPO/tools/review" --archive "$HTML" "${RECS[@]}" || exit 1
du -h "$HTML"

if command -v wslpath >/dev/null 2>&1 && [ -x "/mnt/c/Program Files/Google/Chrome/Application/chrome.exe" ]; then
    "/mnt/c/Program Files/Google/Chrome/Application/chrome.exe" "$(wslpath -w "$HTML")" >/dev/null 2>&1 &
elif command -v xdg-open >/dev/null 2>&1; then
    xdg-open "$HTML" >/dev/null 2>&1 &
fi
echo "$HTML"
