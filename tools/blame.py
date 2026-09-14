#!/usr/bin/env python3
"""Find where a recorded game was lost, and ask the evaluation what it thought.

Reads the JSONL that tools/record-match.py writes, scores every position of
every game with Stockfish, and for each game names the first decisive error:
the first move that cost at least BLUNDER centipawns from a position that was
still holdable. Then, for a candidate whose only difference from the shipped
engine is an evaluation term, it evaluates the position after the error and
after Stockfish's move with both binaries' static evaluation, so the term's
own opinion of the error is on the table: did it prefer the mistake?

    ./tools/blame.py <games.jsonl> --eval-a <evaldump> --eval-b <evaldump> [--depth 16] [--jobs 2]

Side A is the candidate; B is the shipped engine. Both sides' errors are
reported, because the shipped engine's blunders are the control: a term that
loses games either creates a new kind of mistake or makes the old kind more
frequent, and only the comparison says which.
"""
import json, os, subprocess, sys
from multiprocessing import Pool

STOCKFISH = os.environ.get("OPP", "/usr/games/stockfish")
BLUNDER = 100        # cp lost on one move
HOLDABLE = -150      # from the mover's view, before the move
FILES = "abcdefgh"


class SF:
    def __init__(self, depth):
        self.depth = depth
        self.p = subprocess.Popen([STOCKFISH], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL, text=True, bufsize=1)
        self.send("uci"); self.wait("uciok")
        self.send("setoption name Threads value 1"); self.send("setoption name Hash value 64")
        self.send("isready"); self.wait("readyok")

    def send(self, s): self.p.stdin.write(s + "\n"); self.p.stdin.flush()

    def wait(self, tok):
        while True:
            line = self.p.stdout.readline()
            if not line: raise RuntimeError("stockfish died")
            if line.startswith(tok): return line

    def fen(self, moves):
        self.send("position startpos moves " + " ".join(moves)); self.send("d")
        fen = None
        while True:
            line = self.p.stdout.readline()
            if line.startswith("Fen:"): fen = line[4:].strip()
            if line.startswith("Checkers:"): return fen

    def score(self, moves):
        """Side-to-move score and best move at the configured depth."""
        self.send("position startpos moves " + " ".join(moves))
        self.send(f"go depth {self.depth}")
        cp = None
        while True:
            line = self.p.stdout.readline()
            if line.startswith("info") and " score " in line:
                t = line.split(); i = t.index("score")
                if t[i + 1] == "cp": cp = int(t[i + 2])
                elif t[i + 1] == "mate":
                    n = int(t[i + 2]); cp = 30000 - abs(n) if n > 0 else -(30000 - abs(n))
            if line.startswith("bestmove"):
                return cp, line.split()[1]


def piece_at(fen, sq):
    rows = fen.split()[0].split("/")
    r = 8 - int(sq[1]); f = FILES.index(sq[0])
    col = 0
    for ch in rows[r]:
        if ch.isdigit():
            col += int(ch)
            if col > f: return None
        else:
            if col == f: return ch
            col += 1
    return None


def king_square(fen, white):
    rows = fen.split()[0].split("/")
    k = "K" if white else "k"
    for r, row in enumerate(rows):
        col = 0
        for ch in row:
            if ch.isdigit(): col += int(ch)
            else:
                if ch == k: return FILES[col] + str(8 - r)
                col += 1
    return None


def dist(a, b):
    return max(abs(FILES.index(a[0]) - FILES.index(b[0])), abs(int(a[1]) - int(b[1])))


def material(fen, white):
    v = {"p": 1, "n": 3, "b": 3, "r": 5, "q": 9}
    tot = 0
    for ch in fen.split()[0]:
        if ch.lower() in v and (ch.isupper() == white): tot += v[ch.lower()]
    return tot


def classify(fen_before, move, mover_white):
    """A few coarse features of the move, from the FEN alone."""
    frm, to = move[:2], move[2:4]
    piece = piece_at(fen_before, frm) or "?"
    capture = piece_at(fen_before, to) is not None or (piece.lower() == "p" and frm[0] != to[0])
    ek = king_square(fen_before, not mover_white)
    ok = king_square(fen_before, mover_white)
    tags = []
    if capture: tags.append("capture")
    if piece.lower() == "k": tags.append("king-move")
    if ek and piece.lower() != "k":
        if dist(to, ek) < dist(frm, ek) and dist(to, ek) <= 2: tags.append("toward-their-king")
        if dist(frm, ek) <= 2 and dist(to, ek) > dist(frm, ek): tags.append("away-from-their-king")
    if ok and piece.lower() != "k":
        if dist(to, ok) <= 1 and dist(frm, ok) > 1: tags.append("to-own-king")
    if piece.lower() == "p": tags.append("pawn")
    return piece, tags


def analyse(args):
    game, depth = args
    sf = SF(depth)
    moves = game["moves"]
    n = len(moves)
    scores, bests, fens = [], [], []
    for i in range(n + 1):
        fens.append(sf.fen(moves[:i]))
        cp, best = sf.score(moves[:i])
        scores.append(cp); bests.append(best)
    errors = []
    for i in range(len(game["opening"]), n):
        if scores[i] is None or scores[i + 1] is None: continue
        loss = scores[i] + scores[i + 1]          # mover's score before, minus after
        if loss < BLUNDER or scores[i] < HOLDABLE: continue
        white = (i % 2 == 0)
        who = "A" if white == game["a_white"] else "B"
        fen_after_best = sf.fen(moves[:i] + [bests[i]])
        piece, tags = classify(fens[i], moves[i], white)
        own = game["scores"][i - len(game["opening"])] if i - len(game["opening"]) < len(game["scores"]) else None
        errors.append({
            "ply": i, "who": who, "white": white, "move": moves[i], "best": bests[i],
            "sf_before": scores[i], "loss": loss, "own_after": own,
            "fen_before": fens[i], "fen_after": fens[i + 1], "fen_after_best": fen_after_best,
            "piece": piece, "tags": tags,
            "mat_change": (material(fens[i + 1], white) - material(fens[i + 1], not white))
                         - (material(fens[i], white) - material(fens[i], not white)),
        })
    sf.send("quit")
    return {"pair": game["pair"], "a_white": game["a_white"], "result": game["result"],
            "how": game["how"], "errors": errors}


def evals(dump, fens):
    out = subprocess.run([os.path.abspath(dump)], input="\n".join(fens) + "\n",
                         capture_output=True, text=True).stdout.split()
    return [int(x) for x in out]


def main():
    path = sys.argv[1]
    def arg(name, default):
        return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default
    depth = int(arg("--depth", 16)); jobs = int(arg("--jobs", 2))
    dumpA, dumpB = arg("--eval-a", None), arg("--eval-b", None)
    games = [json.loads(l) for l in open(path) if l.strip()]
    with Pool(jobs) as pool:
        reports = pool.map(analyse, [(g, depth) for g in games])

    # The term's opinion. eval is White's view; flip to the mover's.
    if dumpA and dumpB:
        fens = []
        for r in reports:
            for e in r["errors"]:
                fens += [e["fen_after"], e["fen_after_best"]]
        ea, eb = evals(dumpA, fens), evals(dumpB, fens)
        k = 0
        for r in reports:
            for e in r["errors"]:
                sign = 1 if e["white"] else -1
                term_played = sign * (ea[k] - eb[k]); term_best = sign * (ea[k + 1] - eb[k + 1])
                e["term_played"] = term_played; e["term_best"] = term_best
                e["shipped_played"] = sign * eb[k]; e["shipped_best"] = sign * eb[k + 1]
                k += 2

    # Per game: who lost, and the first decisive error by the loser.
    print(f"{len(games)} games, Stockfish depth {depth}\n")
    first = {"A": [], "B": []}
    for r in reports:
        if r["result"] == "1/2-1/2": continue
        loser_white = (r["result"] == "0-1")
        loser = "A" if loser_white == r["a_white"] else "B"
        errs = [e for e in r["errors"] if e["who"] == loser]
        if not errs: continue
        e = errs[0]
        first[loser].append(e)
        term = f"  term played {e['term_played']:+5d}  best {e['term_best']:+5d}" if "term_played" in e else ""
        print(f"pair {r['pair']:2d} {'A=w' if r['a_white'] else 'A=b'}  {loser} lost  ply {e['ply']:3d}  "
              f"{e['piece']} {e['move']} (sf {e['best']})  before {e['sf_before']:+5d}  loss {e['loss']:4d}  "
              f"mat {e['mat_change']:+d}  own {e['own_after']}  {' '.join(e['tags'])}{term}")

    print()
    for who in ("A", "B"):
        es = first[who]
        if not es: continue
        n = len(es)
        tagc = {}
        for e in es:
            for t in e["tags"]: tagc[t] = tagc.get(t, 0) + 1
        print(f"{who}: {n} lost games with a decisive error   mean loss {sum(e['loss'] for e in es)/n:.0f}   "
              f"mean sf-before {sum(e['sf_before'] for e in es)/n:+.0f}")
        print("   tags: " + ", ".join(f"{t} {c}" for t, c in sorted(tagc.items(), key=lambda x: -x[1])))
        print(f"   material given on the move: {sum(1 for e in es if e['mat_change'] < 0)} of {n}; "
              f"taken: {sum(1 for e in es if e['mat_change'] > 0)}")
        if all("term_played" in e for e in es):
            fav = sum(1 for e in es if e["term_played"] > e["term_best"])
            print(f"   term preferred the error to Stockfish's move in {fav} of {n}; "
                  f"mean term on played {sum(e['term_played'] for e in es)/n:+.0f}, on best {sum(e['term_best'] for e in es)/n:+.0f}")
            over = [e for e in es if e["own_after"] is not None]
            if over:
                # own_after is White's view in the record; sf_before - loss is the
                # mover's view after the move. Both to the mover's view here.
                own = sum((e['own_after'] if e['white'] else -e['own_after']) for e in over) / len(over)
                sf = sum(e['sf_before'] - e['loss'] for e in over) / len(over)
                print(f"   after the error, mover's view: own score {own:+.0f}, Stockfish {sf:+.0f}")
    # All errors, not only the first, for the frequency question.
    print()
    for who in ("A", "B"):
        es = [e for r in reports for e in r["errors"] if e["who"] == who]
        plies = sum(len(g["moves"]) - len(g["opening"]) for g in games) / 2
        print(f"{who}: {len(es)} errors of {BLUNDER}+ cp over {plies:.0f} moves = {100*len(es)/plies:.1f} per 100 moves"
              + (f";  term preferred the error in {sum(1 for e in es if e['term_played'] > e['term_best'])}" if es and 'term_played' in es[0] else ""))
    out = path.replace(".jsonl", ".blame.json")
    json.dump(reports, open(out, "w"))
    print(f"\ndetail -> {out}")


if __name__ == "__main__":
    main()
