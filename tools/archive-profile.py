#!/usr/bin/env python3
"""Profile the bot's real games: accuracy by opponent strength, and blind spots.

    ./tools/archive-profile.py                          the whole archive
    ./tools/archive-profile.py --since 2026.08.15-17:30 games after a build
    ./tools/archive-profile.py --compare 2026.08.15-17:30   both, side by side

This exists because the numbers it produces are quoted as fact in `REVIEW.md`
and `ROADMAP.md` 6.1, and for one day they were reproducible only by whoever
had written the throwaway scripts. A documented number with no way to
regenerate it is the same defect as a documented command nobody has run.

What it measures, and what it cannot
------------------------------------
It reviews each game with a *stronger* engine (Stockfish by default) and scores
every move the bot played in win probability, not centipawns -- `REVIEW.md` R2
explains why centipawns invert. Accuracy is a function of the analysing engine,
its depth and the curve, so these numbers compare this engine against *itself
over time* and against nothing else.

`--compare` is the intended use after a change ships: the same instrument on
games before and after, which is the only way this project can ask whether a
self-play gate meant anything against a real field.

Two cautions carried from `ROADMAP.md`:
  - Play is deterministic (`BUGS.md` 6), so results against a given opponent
    are correlated. N games are worth less than N games of evidence.
  - There is a ~3% phantom-loss floor from successive searches disagreeing, so
    a lone inaccuracy under ~5 win% is not evidence of anything.
"""
import argparse
import collections
import concurrent.futures
import os
import re
import statistics
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_ARCHIVE = "/home/dheirav/Code/lichess-bot/game_records"

# Matches the criticised-move lines tools/review prints, e.g.
#   " 23...Ra4      Blunder     -26.0 win%  (-291 cp, best d3, eval +97)"
MOVE_RE = re.compile(
    r"^\s*(\d+)\.(\.\.|\s)(\S+)\s+(Blunder|Mistake|Inaccuracy)\s+"
    r"-\s*([\d.]+) win%\s+\(-(\d+) cp, best (\S+), eval ([+-]?\d+)\)")
# The per-side summary table: "White    94.4       23.3      43"
SUMMARY_RE = re.compile(r"^(White|Black)\s+([\d.]+)\s+([\d.]+)\s+(\d+)$", re.M)
TERM_RE = re.compile(r"([a-z][a-z' ]*?) ([+-]\d+)")

Game = collections.namedtuple(
    "Game", "id colour result opponent_elo accuracy cp moves stamp criticised blind")


def tag(text, name):
    m = re.search(r'\[%s "([^"]*)"\]' % name, text)
    return m.group(1) if m else ""


def stamp_of(text):
    """UTC date+time as a sortable string, or "" when the tags are absent."""
    d, t = tag(text, "UTCDate"), tag(text, "UTCTime")
    return (d + "-" + t) if d else ""


def review_one(args):
    pgn, work, engine, engine_args, depth, hash_mb, nice = args
    out = os.path.join(work, os.path.basename(pgn)[:-4] + ".txt")
    if not os.path.exists(out) or os.path.getsize(out) == 0:
        cmd = (["nice", "-n", str(nice)] if nice else []) + [
            os.path.join(REPO, "tools", "review"), pgn,
            "--engine", engine, "--depth", str(depth), "--hash", str(hash_mb),
            "--explain"]
        for a in engine_args:
            cmd += ["--engine-arg", a]
        with open(out, "w") as f:
            subprocess.run(cmd, stdout=f, stderr=subprocess.DEVNULL, cwd=REPO)
    return out


def parse(pgn, out, bot):
    text = open(pgn, errors="ignore").read()
    white, black = tag(text, "White"), tag(text, "Black")
    if bot not in (white, black):
        return None
    colour = "W" if white == bot else "B"
    result = tag(text, "Result")
    elo = tag(text, "BlackElo" if colour == "W" else "WhiteElo")
    body = open(out, errors="ignore").read()

    rows = {m.group(1): m.groups() for m in SUMMARY_RE.finditer(body)}
    key = "White" if colour == "W" else "Black"
    if key not in rows:
        return None
    _, acc, cp, n = rows[key]

    criticised, blind = [], []
    lines = body.splitlines()
    for i, ln in enumerate(lines):
        m = MOVE_RE.match(ln)
        if not m:
            continue
        num, dots, san, label, wp, cploss, best, ev = m.groups()
        if ("B" if dots == ".." else "W") != colour:
            continue           # only the bot's own moves
        expl = lines[i + 1].strip() if i + 1 < len(lines) else ""
        rec = dict(mv="%s%s%s" % (num, "..." if colour == "B" else ".", san),
                   label=label, wp=float(wp), cp=int(cploss), best=best,
                   eval=int(ev), expl=expl, game=os.path.basename(pgn)[:-4])
        criticised.append(rec)
        if "no term accounts" in expl:
            blind.append(rec)

    score = (1.0 if result in ("1-0", "0-1") and (result == "1-0") == (colour == "W")
             else 0.5 if result == "1/2-1/2" else 0.0)
    return Game(id=os.path.basename(pgn)[:-4], colour=colour, result=score,
                opponent_elo=int(elo) if elo.isdigit() else 0,
                accuracy=float(acc), cp=float(cp), moves=int(n),
                stamp=stamp_of(text), criticised=criticised, blind=blind)


BANDS = [(0, 1500, "under 1500"), (1500, 1900, "1500-1900"),
         (1900, 2100, "1900-2100"), (2100, 2300, "2100-2300"),
         (2300, 9999, "2300+")]


def report(games, title):
    if not games:
        print("\n%s: no games" % title)
        return
    tot = sum(g.moves for g in games)
    wavg = lambda f: sum(f(g) * g.moves for g in games) / tot
    print("\n=== %s ===" % title)
    print("%d games, %d of the bot's moves" % (len(games), tot))
    print("accuracy %.1f%%   avg cp loss %.1f   score %.0f%%"
          % (wavg(lambda g: g.accuracy), wavg(lambda g: g.cp),
             100 * sum(g.result for g in games) / len(games)))

    print("\n%-14s%6s%8s%10s%9s" % ("opponent", "games", "score", "accuracy", "avg cp"))
    for lo, hi, label in BANDS:
        sel = [g for g in games if lo <= g.opponent_elo < hi]
        if not sel:
            continue
        n = sum(g.moves for g in sel)
        print("%-14s%6d%7.0f%%%9.1f%%%9.1f"
              % (label, len(sel), 100 * sum(g.result for g in sel) / len(sel),
                 sum(g.accuracy * g.moves for g in sel) / n,
                 sum(g.cp * g.moves for g in sel) / n))

    crit = [c for g in games for c in g.criticised]
    blind = [b for g in games for b in g.blind]
    counts = collections.Counter(c["label"] for c in crit)
    print("\ncriticised moves: %d  (%d blunder, %d mistake, %d inaccuracy)"
          % (len(crit), counts["Blunder"], counts["Mistake"], counts["Inaccuracy"]))
    if crit:
        print("no evaluation term accounts for: %d (%.0f%%)"
              % (len(blind), 100.0 * len(blind) / len(crit)))

    mag = collections.defaultdict(list)
    lead = collections.Counter()
    for c in crit:
        terms = TERM_RE.findall(c["expl"])
        if terms:
            lead[terms[0][0].strip()] += 1
        for name, d in terms:
            mag[name.strip()].append(abs(int(d)))
    if mag:
        print("\n%-22s%6s%8s%7s   %s" % ("term", "n", "median", "p90", "leads"))
        for name, vals in sorted(mag.items(), key=lambda kv: -statistics.median(kv[1])):
            if len(vals) < 5:
                continue
            vals.sort()
            print("%-22s%6d%8.0f%7d   %d"
                  % (name, len(vals), statistics.median(vals),
                     vals[int(0.9 * len(vals))], lead[name]))


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--archive", default=DEFAULT_ARCHIVE)
    p.add_argument("--bot", default="Crimsy_Bot")
    p.add_argument("--engine", default="/usr/games/stockfish")
    p.add_argument("--engine-arg", action="append", default=[],
                   help="repeatable; ChessBot needs --engine-arg --uci")
    p.add_argument("--depth", type=int, default=14,
                   help="analysis depth (default 14, which the archive baseline used)")
    p.add_argument("--hash", type=int, default=64, dest="hash_mb")
    p.add_argument("--jobs", type=int, default=4,
                   help="parallel reviews. Keep this well under the core count "
                        "while the bot is playing: its games are on a real clock "
                        "and starving them corrupts the very evidence being collected")
    p.add_argument("--nice", type=int, default=10)
    p.add_argument("--work", default=None,
                   help="cache directory for per-game reviews; reused across runs, "
                        "so adding games only reviews the new ones")
    p.add_argument("--since", default=None, metavar="UTCDATE-TIME",
                   help='only games at or after this stamp, e.g. "2026.08.15-17:30:00"')
    p.add_argument("--until", default=None)
    p.add_argument("--compare", default=None, metavar="UTCDATE-TIME",
                   help="report games before and after this stamp separately")
    a = p.parse_args()

    if not os.path.exists(os.path.join(REPO, "tools", "review")):
        sys.exit("tools/review is not built. Run `make review` first.\n"
                 "(Not run automatically: building relinks binaries, which is "
                 "unsafe while a gate or a rated game is in flight.)")

    pgns = sorted(f for f in
                  (os.path.join(a.archive, x) for x in os.listdir(a.archive))
                  if f.endswith(".pgn"))
    if not pgns:
        sys.exit("no PGNs in " + a.archive)

    work = a.work or tempfile.mkdtemp(prefix="archive-profile-")
    os.makedirs(work, exist_ok=True)
    print("reviewing %d games with %s at depth %d (%d jobs, cache %s)"
          % (len(pgns), a.engine, a.depth, a.jobs, work))

    todo = [(f, work, a.engine, a.engine_arg, a.depth, a.hash_mb, a.nice) for f in pgns]
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as ex:
        outs = list(ex.map(review_one, todo))

    games = [g for g in (parse(f, o, a.bot) for f, o in zip(pgns, outs)) if g]
    if not games:
        sys.exit("no games featuring %s could be parsed" % a.bot)

    if a.since:
        games = [g for g in games if g.stamp and g.stamp >= a.since]
    if a.until:
        games = [g for g in games if g.stamp and g.stamp <= a.until]

    if a.compare:
        before = [g for g in games if g.stamp and g.stamp < a.compare]
        after = [g for g in games if g.stamp and g.stamp >= a.compare]
        report(before, "before " + a.compare)
        report(after, "after " + a.compare)
        print("\nAccuracy is not comparable across analysing engines or depths, "
              "only across runs of this same command.\nAnd note BUGS.md 6: play "
              "is deterministic, so games against a repeated opponent are "
              "correlated\nand these samples are worth less than their game "
              "counts suggest.")
    else:
        report(games, "whole archive" if not (a.since or a.until) else "selected games")


if __name__ == "__main__":
    main()
