#!/usr/bin/env python3
"""Build the self-play half of the evaluation-error corpus.

tests/data/evalerr.epd is root positions from the bot's rated games, and a
term can be tuned to fit it and still lose (docs/KING-SAFETY.md, 2026-09-14):
the search evaluates leaves it chose itself, and a term that rewards a shape
of position will steer the search into that shape whether or not it is any
good. Real games between real players never reach a queen parked next to a
king for nothing, so the corpus never asks the term about one, and the
tuner never has to keep it honest there.

This takes the positions a candidate's own games actually reached, keeps the
ones where the candidate's evaluation and the shipped evaluation disagree
(that is where the term under test fires), labels them with Stockfish at the
corpus depth under the corpus rules (quiet, inside the +-1000 cap, one row per
position), and writes them with `tag self`. tools/kstune reads this file
beside the main one and holds the tag to its baseline like ctl.

    /home/dheirav/Code/lichess-bot/venv/bin/python tools/self-corpus.py <games.jsonl> \
        --cand build/ks460/evaldump --ship tools/evaldump [--min 30] [--depth 16] [--jobs 4] \
        [--out tests/data/evalerr-self.epd] [--append]

--append keeps the rows already in the file and adds the new ones, so the set
grows with each candidate: the positions the next candidate steers into are
not the positions the last one did, and the tuner has to answer for all of
them.

python-chess lives in lichess-bot's venv; this repo has none of its own.
"""
import chess, json, os, subprocess, sys
from multiprocessing import Pool

STOCKFISH = os.environ.get("OPP", "/usr/games/stockfish")
EVAL_CAP = 1000


def evals(dump, fens):
    out = subprocess.run([os.path.abspath(dump)], input="\n".join(fens) + "\n",
                         capture_output=True, text=True).stdout.split()
    return [int(x) for x in out]


def label(args):
    fens, depth = args
    p = subprocess.Popen([STOCKFISH], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.DEVNULL, text=True, bufsize=1)
    def send(s): p.stdin.write(s + "\n"); p.stdin.flush()
    send("uci")
    while not p.stdout.readline().startswith("uciok"): pass
    send("setoption name Threads value 1"); send("setoption name Hash value 64")
    out = []
    for fen in fens:
        send(f"position fen {fen}"); send(f"go depth {depth}")
        cp, best = None, None
        while True:
            line = p.stdout.readline()
            if line.startswith("info") and " score " in line:
                t = line.split(); i = t.index("score")
                if t[i + 1] == "cp": cp = int(t[i + 2])
                elif t[i + 1] == "mate":
                    n = int(t[i + 2]); cp = 30000 - abs(n) if n > 0 else -(30000 - abs(n))
            if line.startswith("bestmove"):
                best = line.split()[1]; break
        # Stockfish reports from the side to move; the corpus is White's view.
        if cp is not None and fen.split()[1] == "b": cp = -cp
        out.append((fen, cp, best))
    send("quit")
    return out


def main():
    def arg(name, default):
        return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default
    games = [json.loads(l) for l in open(sys.argv[1]) if l.strip()]
    cand, ship = arg("--cand", None), arg("--ship", None)
    minTerm = int(arg("--min", 30)); depth = int(arg("--depth", 16)); jobs = int(arg("--jobs", 4))
    out = arg("--out", "tests/data/evalerr-self.epd")
    append = "--append" in sys.argv

    # Every position after the opening, one row per distinct position.
    rows, seen = [], set()
    old = []
    if append and os.path.exists(out):
        for line in open(out):
            if line.startswith("#") or not line.strip(): continue
            old.append(line.rstrip("\n"))
            seen.add(" ".join(line.split(";")[0].split()[:4]))
        print(f"{len(old)} rows already in {out}")
    for g in games:
        board = chess.Board()
        for i, mv in enumerate(g["moves"]):
            board.push_uci(mv)
            if i < len(g["opening"]): continue
            if board.is_check(): continue
            fen = board.fen()
            key = " ".join(fen.split()[:4])
            if key in seen: continue
            seen.add(key)
            rows.append((fen, f"{g['pair']}{'w' if g['a_white'] else 'b'}:{i}"))
    print(f"{len(rows)} distinct quiet-side positions from {len(games)} games")

    # Keep the ones the term has an opinion about.
    fens = [r[0] for r in rows]
    ec, es = evals(cand, fens), evals(ship, fens)
    keep = [(r, c - s) for r, c, s in zip(rows, ec, es) if abs(c - s) >= minTerm]
    print(f"{len(keep)} where the candidate and shipped evaluations differ by {minTerm}+ cp")

    # Label under the corpus rules.
    chunks = [[k[0][0] for k in keep[j::jobs]] for j in range(jobs)]
    with Pool(jobs) as pool:
        labelled = pool.map(label, [(c, depth) for c in chunks if c])
    byFen = {fen: (cp, best) for chunk in labelled for fen, cp, best in chunk}
    kept, dropped = [], {"cap": 0, "tactic": 0}
    for (fen, gid), term in keep:
        cp, best = byFen[fen]
        if cp is None or abs(cp) > EVAL_CAP: dropped["cap"] += 1; continue
        board = chess.Board(fen)
        bm = chess.Move.from_uci(best) if best and best != "(none)" else None
        if bm is None or board.is_capture(bm) or bm.promotion or board.gives_check(bm):
            dropped["tactic"] += 1; continue
        kept.append((fen, cp, term, gid))
    print(f"{len(kept)} labelled and quiet; dropped {dropped['cap']} past the cap, {dropped['tactic']} tactical")

    tag = os.path.basename(os.path.dirname(os.path.abspath(cand)))
    with open(out, "w") as f:
        f.write("# self-play half of the evaluation-error corpus -- generated by tools/self-corpus.py, do not hand-edit\n")
        f.write("# positions from a candidate's own games where its evaluation and the shipped one differ; labelled like evalerr.epd\n")
        f.write("# <fen> ; sf <centipawns, white POV> ; tag self ; term <candidate minus shipped, white POV> ; id <candidate>:<pair><colour>:<ply>\n")
        for line in old: f.write(line + "\n")
        for fen, cp, term, gid in kept:
            f.write(f"{fen} ; sf {cp} ; tag self ; term {term} ; id {tag}:{gid}\n")
    print(f"-> {out}")


if __name__ == "__main__":
    main()
