#!/usr/bin/env python3
"""Play recorded games between two engine binaries, at fixed nodes.

tests/match is the gate and keeps only results. When a gate says a change
loses and the static instruments say it should win, the games themselves are
the only evidence left, so this plays the same shape of match (random six-ply
openings, colours swapped per pair, resign and draw adjudication with the
gate's constants) and writes every move and both engines' scores to a JSONL
file that tools/blame.py reads.

    ./tools/record-match.py <pairs> <engineA> <engineB> <out.jsonl> [--nodes N] [--jobs J] [--seed S]

Stockfish is the referee: legal moves come from `go perft 1`, positions from
`d`, so this file needs no move generator of its own and cannot disagree with
the engines about what is legal. Both engines get the bot's option set.
"""
import json, os, random, subprocess, sys, time
from multiprocessing import Pool

BOT_OPTIONS = {
    "Hash": 32, "Threads": 1,
    "BitboardCore": "true", "NullDepthR": "true", "NullVerify": "true",
    "LmpDeep": "true", "InteriorPvs": "true", "QMoveCount": "true",
    "QNoUnderpromo": "true", "QCheckExempt": "true", "CaptHist": "true",
    "QCaptHist": "true",
}
OPENING_PLIES = 6
RESIGN_SCORE, RESIGN_PLIES = 800, 8
DRAW_SCORE, DRAW_PLIES, DRAW_MIN_PLY = 10, 8, 80
PLY_LIMIT = 400
STOCKFISH = os.environ.get("OPP", "/usr/games/stockfish")


class Uci:
    def __init__(self, path, options=None):
        path = os.path.abspath(path)
        self.p = subprocess.Popen([path], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL, text=True, bufsize=1,
                                  cwd=os.path.dirname(path))
        self.send("uci"); self.wait("uciok")
        for k, v in (options or {}).items():
            self.send(f"setoption name {k} value {v}")
        self.send("isready"); self.wait("readyok")

    def send(self, s):
        self.p.stdin.write(s + "\n"); self.p.stdin.flush()

    def wait(self, tok):
        while True:
            line = self.p.stdout.readline()
            if not line: raise RuntimeError("engine died")
            if line.startswith(tok): return line

    def quit(self):
        try: self.send("quit"); self.p.wait(timeout=5)
        except Exception: self.p.kill()


class Referee(Uci):
    def legal(self, moves):
        self.send("position startpos moves " + " ".join(moves))
        self.send("go perft 1")
        out = []
        while True:
            line = self.p.stdout.readline()
            if line.startswith("Nodes searched"): return out
            if ":" in line and len(line.split(":")[0].strip()) in (4, 5):
                out.append(line.split(":")[0].strip())

    def state(self, moves):
        """FEN and whether the side to move is in check."""
        self.send("position startpos moves " + " ".join(moves))
        self.send("d")
        fen, check = None, False
        while True:
            line = self.p.stdout.readline()
            if line.startswith("Fen:"): fen = line[4:].strip()
            if line.startswith("Checkers:"):
                check = line[9:].strip() != ""
                # "Checkers:" is the last line of `d` in every Stockfish since 12.
                return fen, check


class Player(Uci):
    def move(self, moves, nodes):
        self.send("position startpos moves " + " ".join(moves))
        self.send(f"go nodes {nodes}")
        score = None
        while True:
            line = self.p.stdout.readline()
            if line.startswith("info") and " score " in line:
                t = line.split(); i = t.index("score")
                if t[i + 1] == "cp": score = int(t[i + 2])
                elif t[i + 1] == "mate":
                    n = int(t[i + 2]); score = 30000 - abs(n) if n > 0 else -(30000 - abs(n))
            if line.startswith("bestmove"):
                return line.split()[1], score


def play(ref, white, black, opening, nodes):
    moves = list(opening)
    scores = []          # White's view, from the engine that moved
    seen = {}
    winning = [0, 0]     # consecutive plies white / black at resign score
    level = 0
    while True:
        fen, check = ref.state(moves)
        key = " ".join(fen.split()[:4])
        seen[key] = seen.get(key, 0) + 1
        legal = ref.legal(moves)
        stm_white = fen.split()[1] == "w"
        if not legal:
            if check: return moves, scores, ("0-1" if stm_white else "1-0"), "mate"
            return moves, scores, "1/2-1/2", "stalemate"
        if seen[key] >= 3: return moves, scores, "1/2-1/2", "repetition"
        if int(fen.split()[4]) >= 100: return moves, scores, "1/2-1/2", "50-move"
        if len(moves) >= PLY_LIMIT: return moves, scores, "1/2-1/2", "ply limit"
        eng = white if stm_white else black
        mv, sc = eng.move(moves, nodes)
        if mv not in legal:
            return moves, scores, ("0-1" if stm_white else "1-0"), f"illegal {mv}"
        moves.append(mv)
        wscore = sc if stm_white else (-sc if sc is not None else None)
        scores.append(wscore)
        if wscore is not None:
            winning[0] = winning[0] + 1 if wscore >= RESIGN_SCORE else 0
            winning[1] = winning[1] + 1 if wscore <= -RESIGN_SCORE else 0
            level = level + 1 if abs(wscore) <= DRAW_SCORE else 0
            if winning[0] >= RESIGN_PLIES: return moves, scores, "1-0", "resign"
            if winning[1] >= RESIGN_PLIES: return moves, scores, "0-1", "resign"
            if level >= DRAW_PLIES and len(moves) - len(opening) >= DRAW_MIN_PLY:
                return moves, scores, "1/2-1/2", "agreed"


def worker(args):
    pairs, engA, engB, nodes, seed = args
    ref = Referee(STOCKFISH, {"Threads": 1})
    A, B = Player(engA, BOT_OPTIONS), Player(engB, BOT_OPTIONS)
    out = []
    for pair in pairs:
        prng = random.Random(seed * 1000003 + pair)
        opening = []
        for _ in range(OPENING_PLIES):
            legal = ref.legal(opening)
            if not legal: break
            opening.append(prng.choice(legal))
        for a_white in (True, False):
            for e in (A, B):
                e.send("ucinewgame"); e.send("isready"); e.wait("readyok")
            white, black = (A, B) if a_white else (B, A)
            t0 = time.time()
            moves, scores, result, how = play(ref, white, black, opening, nodes)
            out.append({"pair": pair, "a_white": a_white, "opening": opening,
                        "moves": moves, "scores": scores, "result": result, "how": how,
                        "seconds": round(time.time() - t0, 1)})
            print(f"  pair {pair:3d} {'A=white' if a_white else 'A=black'}: {result:7s} {how:12s} {len(moves)} plies",
                  file=sys.stderr, flush=True)
    for e in (A, B, ref): e.quit()
    return out


def main():
    pairs, engA, engB, outPath = int(sys.argv[1]), sys.argv[2], sys.argv[3], sys.argv[4]
    nodes = int(sys.argv[sys.argv.index("--nodes") + 1]) if "--nodes" in sys.argv else 100000
    jobs = int(sys.argv[sys.argv.index("--jobs") + 1]) if "--jobs" in sys.argv else 4
    seed = int(sys.argv[sys.argv.index("--seed") + 1]) if "--seed" in sys.argv else 20260914
    slices = [list(range(j, pairs, jobs)) for j in range(jobs)]
    t0 = time.time()
    with Pool(jobs) as pool:
        results = pool.map(worker, [(s, engA, engB, nodes, seed) for s in slices if s])
    games = [g for r in results for g in r]
    games.sort(key=lambda g: (g["pair"], not g["a_white"]))
    with open(outPath, "w") as f:
        for g in games:
            g["A"] = engA; g["B"] = engB; g["nodes"] = nodes
            f.write(json.dumps(g) + "\n")
    w = sum(1 for g in games if (g["result"] == "1-0") == g["a_white"] and g["result"] != "1/2-1/2")
    d = sum(1 for g in games if g["result"] == "1/2-1/2")
    l = len(games) - w - d
    print(f"{len(games)} games  A: W {w} / D {d} / L {l}  {100*(w+0.5*d)/len(games):.1f}%   {(time.time()-t0)/60:.1f} min  -> {outPath}")


if __name__ == "__main__":
    main()
