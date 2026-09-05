#!/usr/bin/env python3
"""Validate a gendata corpus before anything trains on it.

    tools/corpus-stats.py [dir]        default /home/dheirav/chessbot-data

Checks that cost minutes and can save a training run:

**Score/result agreement.** The two labels are independent signals -- a search
score and a game outcome -- so they should agree without being identical. If
high scores did not predict wins, something is inverted, and a sign error in a
label is invisible in a loss curve: the net simply learns the mirror.

**Distribution shape.** A corpus piled at 0.5 results or at 0cp scores is one
where the adjudication or the filters ate the signal. `gendata`'s first smoke
test wrote 0.5 onto a hundred won positions for exactly that reason.

**Uniqueness, sampled.** 25M hashes will not fit in memory here, so this
estimates from a sample. Positions repeat within a game (dedup handles that)
and across shards only where random openings collided.
"""
import sys, os, glob, random, collections, statistics

d = sys.argv[1] if len(sys.argv) > 1 else "/home/dheirav/chessbot-data"
files = sorted(glob.glob(os.path.join(d, "shard*.txt")))
if not files: sys.exit(f"no shards in {d}")

results = collections.Counter()
scores = []
by_result = collections.defaultdict(list)
bad = 0
total = 0
sample = []
SAMPLE_EVERY = 12          # ~2M of 25M, enough to estimate a duplicate rate

for path in files:
    with open(path, errors="replace") as fh:
        for line in fh:
            total += 1
            parts = line.rstrip("\n").split(";")
            if len(parts) != 3:
                bad += 1; continue
            fen, sc, res = parts
            try:
                sc = int(sc); res = float(res)
            except ValueError:
                bad += 1; continue
            f = fen.split()
            if len(f) != 6 or f[1] not in ("w", "b"):
                bad += 1; continue
            results[res] += 1
            by_result[res].append(sc)
            if total % 7 == 0: scores.append(sc)
            if total % SAMPLE_EVERY == 0:
                sample.append(" ".join(f[:4]))

print(f"positions      {total:,}   malformed {bad:,}")
print(f"files          {len(files)}   {sum(os.path.getsize(p) for p in files)/1e9:.2f} GB\n")

print("result labels")
for r in (1.0, 0.5, 0.0):
    n = results[r]
    print(f"  {r:>4}  {n:>12,}  {n/total*100:5.1f}%")

print("\nscore distribution (centipawns, White's view)")
scores.sort()
def pct(p): return scores[int(len(scores)*p)]
print(f"  min {scores[0]:>7}   p05 {pct(.05):>6}   p25 {pct(.25):>6}   median {pct(.5):>6}"
      f"   p75 {pct(.75):>6}   p95 {pct(.95):>6}   max {scores[-1]:>7}")
print(f"  mean {statistics.mean(scores):>7.1f}   |score|<50: "
      f"{sum(1 for s in scores if abs(s)<50)/len(scores)*100:.1f}%")

print("\nagreement: mean score by game result  (must increase; if not, a label is inverted)")
ok = True
prev = None
for r in (0.0, 0.5, 1.0):
    m = statistics.mean(by_result[r]) if by_result[r] else 0
    print(f"  result {r}  mean score {m:>8.1f}   n={len(by_result[r]):,}")
    if prev is not None and m <= prev: ok = False
    prev = m
print("  ORDERING OK" if ok else "  *** ORDERING WRONG -- do not train on this ***")

u = len(set(sample))
print(f"\nuniqueness (sampled 1 in {SAMPLE_EVERY}): {u:,} unique of {len(sample):,}"
      f"  = {u/len(sample)*100:.1f}%")
