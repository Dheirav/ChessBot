#!/usr/bin/env bash
# Depth reached at each second, per move, for one lichess game, read from
# lichess-bot's own auto log. Run with no argument for the game in progress or
# the most recent one; pass a game id to pick one.
#
# Depth at the N-second mark is the deepest iteration that had *completed* by
# then, which is what the engine would play if the clock stopped there.
LOG=${LOG:-/home/dheirav/Code/lichess-bot/lichess_bot_auto_logs/lichess-bot.log}
GAME=${1:-$(grep -oE "for game [A-Za-z0-9]+" "$LOG" | tail -1 | awk '{print $3}')}
[ -n "$GAME" ] || { echo "no game found in $LOG"; exit 1; }

python3 - "$LOG" "$GAME" <<'PY'
import re, sys
log, game = sys.argv[1], sys.argv[2]
moves = []          # list of (moveno, clockms, [(depth, ms, nps)])
cur = None
for line in open(log, errors="replace"):
    m = re.search(r"Searching for wtime (\d+) btime (\d+) for game (\w+)", line)
    if m and m.group(3) == game:
        cur = {"clock": min(int(m.group(1)), int(m.group(2))), "iters": []}
        moves.append(cur)
        continue
    if cur is None:
        continue
    m = re.search(r"info depth (\d+) .*?nodes (\d+) nps (\d+) time (\d+)", line)
    if m:
        cur["iters"].append((int(m.group(1)), int(m.group(4)), int(m.group(3))))
    elif "bestmove" in line and cur["iters"]:
        cur = None

if not moves:
    print(f"no searches logged for game {game}"); sys.exit(0)

def depth_at(iters, ms):
    d = 0
    for depth, t, _ in iters:
        if t <= ms: d = max(d, depth)
    return d

print(f"game {game}: {len(moves)} of our moves logged\n")
print(f"  {'move':>4} {'clock':>7} {'1s':>4} {'2s':>4} {'3s':>4} {'5s':>4} {'final':>6} {'in':>7} {'nps':>8}")
tot = {1:0, 2:0, 3:0, 5:0}; n = 0
for i, mv in enumerate(moves, 1):
    it = mv["iters"]
    if not it: continue
    fd, ft, fn = it[-1]
    row = [depth_at(it, s*1000) for s in (1,2,3,5)]
    for s, d in zip((1,2,3,5), row): tot[s] += d
    n += 1
    print(f"  {i:>4} {mv['clock']//1000:>6}s {row[0]:>4} {row[1]:>4} {row[2]:>4} {row[3]:>4} {fd:>6} {ft/1000:>6.1f}s {fn/1000:>7.0f}k")
if n:
    print(f"\n  mean depth:  1s {tot[1]/n:.1f}   2s {tot[2]/n:.1f}   3s {tot[3]/n:.1f}   5s {tot[5]/n:.1f}")
    print("  (a depth of 0 at a mark means the search finished before it; read 'final' and 'in')")
PY
