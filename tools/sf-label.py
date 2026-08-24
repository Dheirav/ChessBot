#!/usr/bin/env python3
"""Relabel a Texel corpus with Stockfish evaluations instead of game results.

    tools/sf-label.py [in.epd] [out.epd] [--depth 12] [--nice 19]

Why relabel at all
------------------
Game outcomes make a corpus of 267 games carry **267 independent labels**,
because a result is one observation however many positions it is spread over.
Tuning eighteen weights against that memorised the archive outright: training
error fell 4.41% while held-out error rose 2.11%. The measurement is in the
commit that built `tools/tune`, and it is the reason this file exists.

A Stockfish evaluation is a label *per position*. The same 14 924 positions
become 14 924 independent observations rather than 267, which is roughly a
fifty-fold increase in what the tuner has to learn from, for the same games.

What is given up, and it is not nothing
---------------------------------------
The objective stops being "predict the result" and becomes "agree with
Stockfish". Those differ: Stockfish's evaluation encodes structure this term
set cannot represent, so parts of the target are unreachable by construction,
and the tune will spend effort approximating them with the terms it has. That
is a real cost and it is why `tests/evalerror` stays the independent check --
it is scored on positions from real games and is not this objective.

**Resumable on purpose.** This is a ~100-minute job at one thread and the
machine it runs on has four CPUs shared with a bot playing rated games and
whatever else. It writes as it goes and skips positions already labelled, so
Ctrl-C costs only the position in flight. Run it when the machine is free --
`nice` schedules CPU and does nothing about memory bandwidth or cache, so a
Stockfish sweep is not as polite as its priority suggests.
"""

import os
import re
import subprocess
import sys
import time

STOCKFISH = os.environ.get("STOCKFISH", "/usr/games/stockfish")
SCORE = re.compile(r"score (cp|mate) (-?\d+)")
MATE_CP = 20000          # a mate is clamped, not infinite: the sigmoid needs a number


def arg(flag, default):
    if flag in sys.argv:
        return sys.argv[sys.argv.index(flag) + 1]
    return default


def evaluate(engine, fen, depth):
    """Stockfish's score for one position, in centipawns, White's point of view."""
    engine.stdin.write(f"position fen {fen}\ngo depth {depth}\n")
    engine.stdin.flush()
    score = None
    while True:
        line = engine.stdout.readline()
        if not line:
            return None
        m = SCORE.search(line)
        if m:
            kind, raw = m.group(1), int(m.group(2))
            score = MATE_CP * (1 if raw > 0 else -1) if kind == "mate" else raw
        if line.startswith("bestmove"):
            break
    if score is None:
        return None
    # UCI scores are from the side to move; the corpus is White-relative.
    return score if fen.split()[1] == "w" else -score


def main():
    src = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith("-") else "tests/data/texel.epd"
    dst = sys.argv[2] if len(sys.argv) > 2 and not sys.argv[2].startswith("-") else src.replace(".epd", ".sf.epd")
    depth = int(arg("--depth", 12))
    niceness = int(arg("--nice", 19))

    fens = []
    with open(src) as fh:
        for line in fh:
            tag = line.find(" c9 ")
            if tag > 0:
                fens.append(line[:tag])
    if not fens:
        sys.exit(f"no positions in {src}")

    done = {}
    if os.path.exists(dst):
        with open(dst) as fh:
            for line in fh:
                tag = line.find(" c9 ")
                if tag > 0:
                    done[line[:tag]] = True
        print(f"resuming: {len(done):,} of {len(fens):,} already labelled")

    todo = [f for f in fens if f not in done]
    if not todo:
        print("nothing to do")
        return

    engine = subprocess.Popen(
        ["nice", "-n", str(niceness), STOCKFISH],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
    assert engine.stdin and engine.stdout, "stockfish pipes did not open"
    engine.stdin.write("uci\nsetoption name Hash value 64\nisready\n")
    engine.stdin.flush()
    while "readyok" not in engine.stdout.readline():
        pass

    start = time.time()
    written = 0
    try:
        with open(dst, "a") as out:
            for i, fen in enumerate(todo, 1):
                cp = evaluate(engine, fen, depth)
                if cp is None:
                    continue
                out.write(f"{fen} c9 \"{cp}\";\n")
                written += 1
                if i % 250 == 0:
                    rate = i / (time.time() - start)
                    left = (len(todo) - i) / rate / 60
                    out.flush()
                    print(f"  {i:,}/{len(todo):,}  {rate:.1f}/s  ~{left:.0f} min left", flush=True)
    except KeyboardInterrupt:
        print("\ninterrupted -- progress is on disk, rerun to resume")
    finally:
        try:
            engine.stdin.write("quit\n")
            engine.stdin.flush()
        except Exception:
            pass
        engine.wait(timeout=5)

    print(f"wrote {written:,} labels to {dst} at depth {depth}")


if __name__ == "__main__":
    main()
