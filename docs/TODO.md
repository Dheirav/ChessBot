# TODO — the work queue

Every open piece of work, with what blocks it and roughly what it costs. Kept
current; the last entry here was checked on **2026-08-16**.

**This file deliberately does not explain anything.** The reasoning lives where
it was earned — `ROADMAP.md` for why the priorities are what they are, `BUGS.md`
for defects and their evidence, `REVIEW.md` for the review tool. Every item
below is a pointer plus a size, and duplicating the argument here is how two
copies of the same knowledge start to drift. Read `HANDOFF.md` first for state.

Sizes are honest rather than encouraging: **S** is under an hour, **M** is an
afternoon, **L** is a day or a night of machine time.

---

## Before touching anything

- **`pgrep -x chessbot`** — a PID means a rated game is live. `pgrep -f` matches
  the shell running the check and has produced false positives repeatedly
  (`BUGS.md` 9).
- **Do not `make` the engine while the bot runs.** It relinks `./chessbot`, and
  the bot spawns a fresh engine per game, so the next rated game silently gets
  an ungated binary. `make review` and `make tests` do not touch it.
- **Stopping the bot needs a gap between games**, not a signal during one
  (`BUGS.md` 7). SIGINT does *not* finish the game in progress.
- **Gate discipline:** `-N 100000`, never `--sprt` under sharding, A and B differ
  in exactly one thing, pool before believing. `tests/README.md` has the rest.

---

## 1. The measurement everything else is waiting on

**Does self-play Elo mean anything here?** — **L** (machine time, not yours)

```bash
./tools/archive-profile.py --compare 2026.08.15-13:27:00
```

The stamp is Lichess **UTC**, and it is when the build with the hanging-piece
term removed started playing. 2026-08-15 produced **+121.2** and then **+155.0**
in self-play; **16 games** had accumulated by 2026-08-16 00:20, which is not
enough. Wants 30+.

This ranks first because its answer changes how every other gate in this project
should be read, including the ones already banked. Watch the 2100-2300 band,
which sat at 25% before, rather than the rating headline. **Write down what it
says even if it says nothing** — a null result here is the more valuable one and
the easiest to quietly not record.

---

## 2. Engine

| item | size | blocked by | note |
|---|---|---|---|
| **Time management** (`BUGS.md` 11) | M | nothing | The fix is deliberately unwritten. The instrument exists (`--tc`), and "spend more clock" is exactly the kind of plausible argument that has been wrong twice here. `tools/review` now charts the clock, so real games can show whether a fix worked, not only a gate. |
| **6.2 remainder: the general tune** | L | nothing | Texel-style, over an evaluation whose largest term has stopped shouting. Position set exists: the game archive plus `evalref`'s 23 603 positions. |
| **Re-run 6.1** | S | 6.2 landing | A corrected `threats` may expose blind spots it was masking. `ROADMAP.md` 6.3. |
| **`deltapruning`** | L | different seeds | One gate short of a verdict at +7.1 [−2.9, +17.2]. Needs twice the games and **different seeds** — `shard-gate.sh` derives them from a fixed base and would replay the same games. |
| **PLAN 3.5 — IID** | M | nothing | The safe search item: pure move ordering, cannot lose a game by discarding a line. |
| **PLAN 3.4 — futility / razoring** | M | ⚠ read 3.1 first | Same bet delta pruning makes. That bet swung 57 Elo on one constant. Start conservative, not textbook. |
| **PLAN 3.6 — retune LMR** | L | 6.2 | Deliberately last, against a search that has stopped changing shape. |
| **PLAN 5.4 — lazy evaluation** | M | nothing | Evaluation is ~33% of search time, but speed-per-node has returned ~zero Elo three times. Do it for the wall clock, not for a gate. |
| **`BUGS.md` 6 — deterministic play** | M | nothing | Results against a repeated opponent are correlated, so the archive is worth less than its game count and every accuracy figure inherits that. A seeded random tiebreak among near-equal root moves; weigh against gate reproducibility before committing. |

---

## 3. Review tool

| item | size | note |
|---|---|---|
| **Archive index view** | M | The biggest gap. 80 games behind a `<select>` is a picker, not an index. Wants a sortable landing table — date, opponent, rating, result, accuracy, blunders — with a game one click away, and `archive-profile.py`'s band table alongside it. |
| **Opening names** | S | `ECO` and `ECOUrl` are already in Chess.com and Lichess exports and currently ignored. Reading the tag costs nothing; computing openings without one needs a book. |
| **Phantom-loss floor** | M | ~3% of criticised moves are moves the engine itself would have played, from successive searches disagreeing across a shared transposition table (`REVIEW.md`). Clearing the table between positions would cost analysis time; measure before choosing. |
| **`Miss` label** | S | Needs mate scores preserved rather than clamped to ±1000. |
| **`Brilliant` label** | M | Needs "material sacrificed and still best". |
| **`Great` label** | M | Needs MultiPV; `UciEngine` does not request it. |
| **`review-archive.sh` default path** | S | Hardcodes the Lichess archive; `ARCHIVE=` overrides. Fine for the bot, awkward for anyone reviewing their own games. |

**Explicitly not doing** (`REVIEW.md`): reimplementing Stockfish's analysis, a
tablebase or cloud-eval integration, an opening book for the `Book` label.

---

## 4. Housekeeping

- **`CLAUDE.md` is gitignored** (`.gitignore:4`), so its bench signature lives
  only on this machine. A fresh clone gets whatever was last committed. Current
  signature: **1,086,693** nodes at depth 6.
- **`PLAN.md` status lines are current, its body is dated.** `BACKLOG.md` is a
  frozen 2026-08-10 archive — §2.1 is wrong, §4 and §7 still good.
- **`~/reviews/` is generated output** and outside the repo. `records/` there is
  the per-game cache; it re-reviews itself when the record format changes.
