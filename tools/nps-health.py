#!/usr/bin/env python3
"""Search speed in real games, per day, from the bot's own logs.

    ./tools/nps-health.py              every retained day
    ./tools/nps-health.py --days 3     just the recent ones
    ./tools/nps-health.py --quiet      one line, exit 1 if the latest day is degraded

Why this exists
---------------
On 2026-08-23 the engine ran at roughly a third of its speed for twenty-one
hours and nothing noticed. `tests/bench` measures speed on demand and passed,
because it is run on a quiet machine when someone thinks to run it; nothing
watched the speed of the games actually being played.

The cost was not the lost Elo, which was temporary. It was that thirty-four
rated games were recorded, analysed, and written up as evidence that `razoring`
had regressed -- a two-sigma alarm and a plausible story about the 500cp margin
being too aggressive, none of which was true. Re-measured on healthy hardware
the same build came back at +0.11 sigma. `BUGS.md` 16.

**A band table does not know how fast the engine was when it played.** That is
the whole argument for this file: results-based evidence is silently invalid
while the machine is sick, and the sickness is invisible in the results.

What it measures
----------------
The median `nps` the engine reported, over searches longer than `--min-ms`.
Short searches are excluded because nps is mostly noise there -- a 30 ms search
is dominated by the fixed cost of starting one.

The baseline is the median of the daily medians, so it adapts as the hardware
changes rather than hard-coding a number that will rot. A day below
`--threshold` of that baseline is flagged. With few days retained the baseline
is weak; the flag is a prompt to look, not a verdict.
"""

import argparse
import glob
import os
import re
import statistics
import sys
from collections import defaultdict

LOGS = os.environ.get(
    "BOT_LOGS", "/home/dheirav/Code/lichess-bot/lichess_bot_auto_logs"
)

SAMPLE = re.compile(r"nps (\d+) time (\d+)")
STAMP = re.compile(r"^(\d{4}-\d\d-\d\d)")


def collect(logdir, min_ms):
    """Median nps per day. Returns {day: (median, count)}."""
    per_day = defaultdict(list)
    paths = glob.glob(os.path.join(logdir, "lichess-bot.log*"))
    if not paths:
        sys.exit(f"no logs under {logdir} -- set BOT_LOGS if it moved")
    for path in paths:
        with open(path, errors="replace") as fh:
            for line in fh:
                if "nps " not in line:
                    continue
                day = STAMP.match(line)
                sample = SAMPLE.search(line)
                if not day or not sample:
                    continue
                nps, ms = int(sample.group(1)), int(sample.group(2))
                if ms < min_ms:
                    continue
                per_day[day.group(1)].append(nps)
    return {d: (statistics.median(v), len(v)) for d, v in sorted(per_day.items())}


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--logs", default=LOGS, help="lichess-bot log directory")
    ap.add_argument("--days", type=int, default=0, help="show only the last N days")
    ap.add_argument("--min-ms", type=int, default=2000,
                    help="ignore searches shorter than this (default 2000)")
    ap.add_argument("--threshold", type=float, default=0.6,
                    help="flag a day below this fraction of baseline (default 0.6)")
    ap.add_argument("--quiet", action="store_true",
                    help="one line for the latest day; exit 1 if degraded")
    args = ap.parse_args()

    days = collect(args.logs, args.min_ms)
    if not days:
        sys.exit(f"no searches over {args.min_ms} ms found -- has the bot played?")

    baseline = statistics.median(m for m, _ in days.values())
    floor = baseline * args.threshold
    latest = max(days)
    degraded = days[latest][0] < floor

    if args.quiet:
        med, n = days[latest]
        state = "DEGRADED" if degraded else "ok"
        print(f"{latest}  {med / 1000:.0f} knps over {n} searches  "
              f"(baseline {baseline / 1000:.0f})  {state}")
        return 1 if degraded else 0

    shown = sorted(days)[-args.days:] if args.days else sorted(days)
    print(f"median nps in live games, searches over {args.min_ms} ms")
    print(f"baseline {baseline / 1000:.0f} knps (median of daily medians), "
          f"flagging below {floor / 1000:.0f}\n")
    for day in shown:
        med, n = days[day]
        mark = "  <-- DEGRADED" if med < floor else ""
        bar = "#" * int(med / 25000)
        print(f"  {day}  n={n:5d}  {med / 1000:7.1f}  {bar}{mark}")

    if degraded:
        print(f"\n{latest} is below {args.threshold:.0%} of baseline. Results from "
              f"this day are not\nsafe evidence -- see BUGS.md 16 before reading "
              f"anything off them.")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
