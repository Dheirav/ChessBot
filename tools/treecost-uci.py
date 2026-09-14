#!/usr/bin/env python3
"""Paired node-count comparison of two engine binaries at a fixed depth.

tools/treecost does this for a runtime search toggle inside one process. A
compile-time candidate, an evaluation term behind a -D constant, needs two
binaries, and this drives them over UCI with the bot's own option set so the
tree measured is the tree the bot searches.

    ./tools/treecost-uci.py <depth> <N> <engineA> <engineB> [epd]

Positions come from tests/data/evalerr.epd by default: real games, and the
comp-tagged third of them is where a compensation term actually fires, which
random-game positions would rarely reach. Reports the per-position node ratio
B/A as median and quartiles, plus the sign test, and the same over the comp
and ctl tags separately.
"""
import os, subprocess, sys, statistics, time

BOT_OPTIONS = {
    "Hash": 64, "Threads": 1, "RootSeed": 1,
    "BitboardCore": "true", "NullDepthR": "true", "NullVerify": "true",
    "LmpDeep": "true", "InteriorPvs": "true", "QMoveCount": "true",
    "QNoUnderpromo": "true", "QCheckExempt": "true", "CaptHist": "true",
    "QCaptHist": "true",
}

class Engine:
    def __init__(self, path):
        path = os.path.abspath(path)
        self.p = subprocess.Popen([path], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL, text=True, bufsize=1,
                                  cwd=os.path.dirname(os.path.abspath(path)))
        self.send("uci"); self.wait("uciok")
        for k, v in BOT_OPTIONS.items():
            self.send(f"setoption name {k} value {v}")
        self.send("isready"); self.wait("readyok")
    def send(self, s): self.p.stdin.write(s + "\n"); self.p.stdin.flush()
    def wait(self, tok):
        while True:
            line = self.p.stdout.readline()
            if not line: raise RuntimeError("engine died")
            if line.startswith(tok): return line
    def nodes(self, fen, depth):
        # A fresh table per position, so the order of positions cannot leak
        # into the count through what an earlier search left behind.
        self.send("ucinewgame"); self.send("isready"); self.wait("readyok")
        self.send(f"position fen {fen}")
        self.send(f"go depth {depth}")
        nodes = None
        while True:
            line = self.p.stdout.readline()
            if line.startswith("info") and " nodes " in line:
                t = line.split(); nodes = int(t[t.index("nodes") + 1])
            if line.startswith("bestmove"): return nodes
    def quit(self): self.send("quit"); self.p.wait()

def load(path, n):
    rows = []
    for line in open(path):
        if not line.strip() or line.startswith("#"): continue
        parts = [x.strip() for x in line.split(";")]
        tag = next((x[4:] for x in parts if x.startswith("tag ")), "?")
        rows.append((parts[0], tag))
    # Every k-th row rather than the first n, so both tags and all phases show.
    step = max(1, len(rows) // n)
    return rows[::step][:n]

def summarise(name, ratios, signs):
    if not ratios: return
    r = sorted(ratios); q = statistics.quantiles(r, n=4) if len(r) >= 4 else [r[0], r[len(r)//2], r[-1]]
    fewer = sum(1 for s in signs if s < 0); more = sum(1 for s in signs if s > 0)
    print(f"  {name:5s} n={len(r):3d}  median {100*(q[1]-1):+6.1f}%   quartiles {100*(q[0]-1):+6.1f}% .. {100*(q[2]-1):+6.1f}%"
          f"   B fewer on {fewer}, more on {more}")

def main():
    depth, n, a, b = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3], sys.argv[4]
    epd = sys.argv[5] if len(sys.argv) > 5 else "tests/data/evalerr.epd"
    rows = load(epd, n)
    ea, eb = Engine(a), Engine(b)
    ratios, signs, bytag = [], [], {}
    ta = tb = 0
    t0 = time.time()
    for i, (fen, tag) in enumerate(rows):
        na, nb = ea.nodes(fen, depth), eb.nodes(fen, depth)
        if not na or not nb: continue
        ta += na; tb += nb
        ratios.append(nb / na); signs.append(nb - na)
        bytag.setdefault(tag, ([], []))[0].append(nb / na)
        bytag[tag][1].append(nb - na)
        if (i + 1) % 10 == 0:
            el = time.time() - t0; eta = el / (i + 1) * (len(rows) - i - 1)
            print(f"  {i+1}/{len(rows)}  running total B/A {tb/ta:.3f}   elapsed {el/60:.1f} min   eta {eta/60:.1f} min",
                  file=sys.stderr, flush=True)
    ea.quit(); eb.quit()
    print(f"depth {depth}, {len(ratios)} positions, A={a} B={b}")
    print(f"  total nodes  A {ta:,}  B {tb:,}  ratio {tb/ta:.3f}")
    summarise("all", ratios, signs)
    for tag, (r, s) in sorted(bytag.items()): summarise(tag, r, s)

if __name__ == "__main__":
    main()
