#!/usr/bin/env python3
"""Ask Stockfish which of two candidate moves was actually better.

tools/configsweep scores a configuration by whether its move matches what this
engine concludes with four times the budget. That reference is the engine's own
deeper opinion, not ground truth, so a disagreement is evidence of a difference
and not of a mistake. A configuration that prunes hard could be finding a better
move, and the agreement column would score it as a miss either way.

This settles it. For each disagreement, Stockfish evaluates the position after
each candidate move, to a fixed depth, from the mover's point of view. Whichever
move leaves the mover better off is the better move, and the loss is how much
the other one gave away.

Reads the TSV configsweep writes: label, fen, reference move, candidate move.

  ./tools/adjudicate.py [tsv] [depth]
"""
import os
import subprocess
import sys
from collections import defaultdict

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


def score_after(engine, fen, move, depth):
    """Centipawns for the side that played `move`, after it is played.

    Stockfish reports from the side to move, which after our move is the
    opponent, so the sign is flipped back. A mate is clamped to a large finite
    value so the arithmetic below stays meaningful.
    """
    engine.stdin.write(f"position fen {fen} moves {move}\ngo depth {depth}\n")
    engine.stdin.flush()
    cp = None
    for line in engine.stdout:
        if line.startswith("info") and " score " in line:
            parts = line.split()
            i = parts.index("score")
            if parts[i + 1] == "cp":
                cp = int(parts[i + 2])
            elif parts[i + 1] == "mate":
                n = int(parts[i + 2])
                cp = 30000 - abs(n) if n > 0 else -(30000 - abs(n))
        if line.startswith("bestmove"):
            break
    return None if cp is None else -cp


def main():
    tsv = sys.argv[1] if len(sys.argv) > 1 else "/tmp/disagreements.tsv"
    depth = int(sys.argv[2]) if len(sys.argv) > 2 else 18
    rows = [l.rstrip("\n").split("\t") for l in open(tsv) if l.strip()]
    print(f"{len(rows)} disagreements, Stockfish depth {depth}\n")

    engine = start()
    per_config = defaultdict(lambda: {"ref": 0, "cand": 0, "same": 0, "loss": 0})
    for n, (label, fen, ref, cand) in enumerate(rows, 1):
        a = score_after(engine, fen, ref, depth)
        b = score_after(engine, fen, cand, depth)
        if a is None or b is None:
            continue
        d = b - a          # positive: the candidate's move was better
        s = per_config[label]
        if abs(d) <= 10:
            s["same"] += 1
            verdict = "equal"
        elif d > 0:
            s["cand"] += 1
            verdict = "CANDIDATE better"
        else:
            s["ref"] += 1
            s["loss"] += -d
            verdict = "reference better"
        print(f"  {label:<20} {ref} vs {cand}  {a:+6d} / {b:+6d}  {d:+6d}  {verdict}")
        print(f"    {fen}")
        sys.stdout.flush()
    engine.stdin.write("quit\n")
    engine.stdin.flush()

    print("\n  summary, by configuration")
    print(f"  {'configuration':<20} {'ref better':>10} {'equal':>7} {'cand better':>12} {'avg loss':>9}")
    for label, s in per_config.items():
        avg = (s["loss"] / s["ref"]) if s["ref"] else 0
        print(f"  {label:<20} {s['ref']:>10} {s['same']:>7} {s['cand']:>12} {avg:>8.0f}cp")
    print("\n  'cand better' and 'equal' are disagreements that were NOT mistakes:")
    print("  the agreement column counted them against the configuration anyway.")


if __name__ == "__main__":
    main()
