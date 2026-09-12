#!/usr/bin/env python3
"""How close a *static* evaluation can possibly get, per corpus tag.

tests/README.md records that Stockfish's own depth-1 evaluation sits 282 cp from
its depth-16 verdict on the `comp` positions, against this engine's 544. Half
that gap is therefore dynamics, which no evaluation term can score, and the
addressable part is only the difference between 282 and 544.

The same number has never been computed for `ctl`, where this engine scores 182.
If the floor there is 170 then the control positions are already as good as a
static evaluation gets and no work on them can pay; if it is 60, there is a lot
available. That decides whether an evaluation project is worth starting, so it
is worth the few minutes this takes.

Method: Stockfish at depth 1 is a near-static evaluation, and the corpus already
carries its depth-16 label. The mean absolute difference between them is the
floor.

  ./tools/eval-floor.py [corpus] [--depth 1]
"""
import os
import subprocess
import sys

STOCKFISH = os.environ.get("STOCKFISH", "/usr/games/stockfish")


def start():
    e = subprocess.Popen([STOCKFISH], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         text=True, bufsize=1)
    e.stdin.write("uci\n")
    e.stdin.flush()
    for line in e.stdout:
        if line.startswith("uciok"):
            break
    return e


def evaluate(engine, fen, depth):
    """Centipawns from White's point of view, matching the corpus labels.

    depth 0 uses Stockfish's `eval` command: its static evaluation with no
    search at all, which is the strict definition of the floor. Any depth
    above 0 runs a search to that depth, which sees tactics a static term
    cannot and so reports a lower, flattering floor.
    """
    if depth <= 0:
        engine.stdin.write(f"position fen {fen}\neval\n")
        engine.stdin.flush()
        # `eval` prints a table then "Final evaluation +2.24 (white side)"; it
        # ends with no sentinel, so read until that line.
        for line in engine.stdout:
            if line.startswith("Final evaluation"):
                tok = line.split()
                try:
                    v = float(tok[2])
                except ValueError:
                    return None
                return int(round(v * 100))
        return None
    engine.stdin.write(f"position fen {fen}\ngo depth {depth}\n")
    engine.stdin.flush()
    cp, stm_white = None, fen.split()[1] == "w"
    for line in engine.stdout:
        if line.startswith("info") and " score cp " in line:
            p = line.split()
            cp = int(p[p.index("score") + 2])
        elif line.startswith("info") and " score mate " in line:
            p = line.split()
            n = int(p[p.index("score") + 2])
            cp = 30000 - abs(n) if n > 0 else -(30000 - abs(n))
        if line.startswith("bestmove"):
            break
    if cp is None:
        return None
    return cp if stm_white else -cp


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "tests/data/evalerr.epd"
    depth = 0
    if "--depth" in sys.argv:
        depth = int(sys.argv[sys.argv.index("--depth") + 1])

    rows = []
    for line in open(path):
        if line.startswith("#") or not line.strip():
            continue
        parts = [p.strip() for p in line.split(";")]
        fen = parts[0]
        label = tag = None
        for p in parts[1:]:
            if p.startswith("sf "):
                label = int(p[3:])
            elif p.startswith("tag "):
                tag = p[4:]
        if label is not None and tag:
            rows.append((fen, label, tag))

    print(f"{len(rows)} positions, Stockfish {'static eval' if depth <= 0 else f'depth {depth}'} against the depth-16 label\n")
    engine = start()
    acc = {}
    for i, (fen, label, tag) in enumerate(rows, 1):
        v = evaluate(engine, fen, depth)
        if v is None:
            continue
        s = acc.setdefault(tag, [0, 0])
        s[0] += abs(v - label)
        s[1] += 1
        if i % 100 == 0:
            print(f"\r  {i}/{len(rows)}", end="", flush=True)
    engine.stdin.write("quit\n")
    engine.stdin.flush()
    print("\r                    \r")

    ours = {"comp": 543.7, "ctl": 181.9}
    print(f"  {'tag':<6} {'n':>5} {'floor':>8} {'ours':>8} {'addressable':>12}")
    for tag, (total, n) in sorted(acc.items()):
        floor = total / n
        mine = ours.get(tag)
        room = f"{mine - floor:.0f} cp" if mine else "?"
        print(f"  {tag:<6} {n:>5} {floor:>7.1f} {mine if mine else 0:>8.1f} {room:>12}")
    print("\n  'floor' is how far a near-static view is from the deep verdict, so it")
    print("  is the part no evaluation term can remove. 'addressable' is what is")
    print("  actually left to win.")


if __name__ == "__main__":
    main()
