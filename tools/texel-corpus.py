#!/usr/bin/env python3
"""Build the Texel tuning corpus: quiet positions labelled with the game result.

    tools/texel-corpus.py [out.epd]        (default tests/data/texel.epd)

Texel tuning fits the evaluation's weights to the thing that actually matters --
whether the position was won -- rather than to another engine's opinion. Each
line is a FEN and the result of the game it came from, from White's point of
view: 1.0, 0.5 or 0.0.

Why the filters, each of which throws away real data on purpose
--------------------------------------------------------------
**Quiet only.** A static evaluation cannot be asked to predict the result of a
position where a tactic decides it; scoring it against one measures search
depth wearing an evaluation's name. So: not in check, and the move actually
played was not a capture or a promotion. Same reasoning as `eval-corpus.py`,
which says it at more length.

**No opening.** The first sixteen plies are shared by hundreds of games here
and carry almost no information about the result -- and this bot is
deterministic, so they are literally the same moves over and over.

**Deduplicated by position.** `BUGS.md` 6: play is deterministic and 32% of the
archive is against sixteen opponents met four or more times, so whole games
repeat. Without this the tune would weight those openings by however many times
the same bot was played, which is a property of matchmaking and not of chess.
The dedup key ignores the halfmove and fullmove counters, which do not affect
the evaluation.

**Draws are kept.** They are only 20 of 272 games, but dropping them would
teach the evaluation that every position is decisive.

What this corpus cannot do
--------------------------
272 games is small for Texel -- published tunes use hundreds of thousands of
games. It is enough for the twenty scalar weights in `EvalWeights` (roughly
1 800 positions each) and **not** enough for the 448 piece-square-table
entries, which is why the tune starts with the scalars. Overfitting is the
standing risk: `tests/evalerror` is scored against Stockfish and is not part of
the objective here, which makes it the honest check that a tune improved the
evaluation rather than memorising this archive.
"""

import os
import sys
import glob

sys.path.insert(0, "/home/dheirav/Code/lichess-bot/venv/lib/python3.12/site-packages")

try:
    import chess
    import chess.pgn
except ImportError:
    sys.exit("needs python-chess: run with /home/dheirav/Code/lichess-bot/venv/bin/python")

ARCHIVE = os.environ.get("ARCHIVE", "/home/dheirav/Code/lichess-bot/game_records")
SKIP_PLIES = 16          # openings are shared and uninformative
RESULTS = {"1-0": 1.0, "1/2-1/2": 0.5, "0-1": 0.0}


def positions(pgn_path):
    """Yield (fen, result) for every quiet position in one game."""
    with open(pgn_path, errors="replace") as fh:
        game = chess.pgn.read_game(fh)
    if game is None:
        return
    label = RESULTS.get(game.headers.get("Result", "*"))
    if label is None:
        return
    board = game.board()
    for ply, move in enumerate(game.mainline_moves()):
        # Decide about the position *before* the move, using the move as
        # evidence of whether it was quiet.
        if ply >= SKIP_PLIES and not board.is_check():
            noisy = board.is_capture(move) or move.promotion is not None
            if not noisy:
                yield board.fen(), label
        board.push(move)


def main():
    # --split writes a held-out set, divided BY GAME and not by position.
    #
    # Splitting by position would leak: every position in a game carries that
    # game's result, so the same label would appear on both sides of the split
    # and the held-out error would flatter the tune. Whole games either train
    # or test, never both.
    split = "--split" in sys.argv
    argv = [a for a in sys.argv[1:] if not a.startswith("--")]
    out = argv[0] if argv else "tests/data/texel.epd"
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)

    seen = set()
    kept = 0
    dropped_dup = 0
    games = 0
    tally = {1.0: 0, 0.5: 0, 0.0: 0}

    paths = sorted(glob.glob(os.path.join(ARCHIVE, "*.pgn")))
    if not paths:
        sys.exit(f"no PGNs under {ARCHIVE}")

    test_path = out.replace(".epd", ".test.epd")
    fh_test = open(test_path, "w") if split else None
    test_kept = 0
    test_games = 0

    with open(out, "w") as fh:
        for gi, path in enumerate(paths):
            got = False
            # every fourth game is held out
            held = split and (gi % 4 == 3)
            sink = fh_test if held else fh
            for fen, label in positions(path):
                got = True
                # Ignore the move counters: they do not reach the evaluation.
                key = " ".join(fen.split()[:4])
                if key in seen:
                    dropped_dup += 1
                    continue
                seen.add(key)
                sink.write(f"{fen} c9 \"{label}\";\n")
                if held:
                    test_kept += 1
                else:
                    kept += 1
                    tally[label] += 1
            if got:
                games += 1
                if held:
                    test_games += 1
    if fh_test:
        fh_test.close()

    if split:
        print(f"held out {test_games} games -> {test_kept:,} positions in {test_path}")
    print(f"{games - test_games} training games -> {kept:,} positions  "
          f"({dropped_dup:,} duplicates dropped)")
    print(f"  white wins {tally[1.0]:,}   draws {tally[0.5]:,}   black wins {tally[0.0]:,}")
    print(f"  written to {out}")
    if kept < 5000:
        print("\nWARNING: under 5 000 positions is thin even for twenty weights.")


if __name__ == "__main__":
    main()
