# Roadmap — what to do next, and how to know it worked

Written 2026-08-15, after a week in which the engine went from Lichess rapid
2198-provisional to **2065 with rd=77**, ran nine gates over ~20 000 games,
fixed six defects, and got 2.5× faster in a day.

`PLAN.md` is the original phased plan and remains the reference for *what each
item is*. This file is about **what to do next and why**, and it exists because
the plan's ordering is now out of date: it was written before any of this was
measured, and measurement has moved the priorities twice.

Read `HANDOFF.md` first for current state. This is the layer above it.

---

## The one finding that should drive everything

**The evaluation is the ceiling, and no phase of the plan addresses it.**

Every measurement this week points at it:

| evidence | what it says |
|---|---|
| strength curve crosses 50% at **2050-2100** | 96% under 1500, 31% at 2100-2300 |
| Phase 4 gate: **+6.1, CI spans zero** | the corrected evaluation is not demonstrably better than the broken one |
| `deltapruning` **−50 → +7.1** on a margin change | pruning that trusts the static score is fragile because the score is |
| `checkext` **+23.0**, costing 9.2% *more* nodes | quality per node wins; speed per node has returned ~zero, three times |
| review says **"no term accounts for it"** | there are positions this evaluation cannot see at all |

Phase 4 made the evaluation *correct* — mirror-symmetric, no colour-blind
constants, honest "defended". Correct is not good. It remains hand-written
piece-square tables with weights nobody has ever tuned against evidence.

Search work from here buys depth, and depth against a weak evaluation has
diminishing returns. That is the argument for Phase 6 below being first.

---

## Phase 6 — Evaluation quality *(new, and the recommended next work)*

Not in `PLAN.md`. The instrument for it did not exist until this week.

**6.1 Find what the evaluation cannot see.** `./tools/review --explain` over the
game archive, collecting every move where the analysing engine reports a large
loss and no term accounts for it. Those are the blind spots, named. This is a
scripted afternoon and needs no engine changes; it produces the list everything
else in this phase works from.

**6.2 Retune piece values and term weights.** The values in `evaluation.cpp` are
conventional guesses. Tuning them against real game outcomes (Texel-style: pick
the weights that best predict results over a large position set) is the standard
method, and the position set already exists — the game archive plus the 23 603
positions `evalref` generates.

*Verify:* each candidate weight set is a full A/B, gated on nodes with the
two-binary path. `evalref` will show a diff; `bench` will move. Expect the tune
to be worth more than everything remaining in Phases 3 and 5 combined.

**6.3 Add the terms the blind-spot list demands**, and only those. Do not add
terms speculatively — that is how the current evaluation acquired 25 of them,
several of which were wrong for months.

---

## Phase 3 remainder — search

**3.4 Futility pruning and razoring.** ⚠️ **Read 3.1's result first.** This makes
the same bet delta pruning does — discarding a subtree on the strength of a
static score. That bet swung **57 Elo on a single constant** this week. Start at
the conservative end of the margin, not the textbook one, and gate before
believing anything.

**3.5 Internal iterative deepening.** The safe one: pure move ordering, cannot
lose a game by discarding a line the way pruning can. Modest and cheap.

**3.6 Retune LMR.** Deliberately last, so it is tuned against a search that has
stopped changing shape. Doing it before 6.2 would tune against an evaluation
about to change underneath it.

---

## Phase 5 remainder — speed

**5.4 Lazy evaluation.** Evaluation is ~33% of real search time, so the premise
holds. But note what three gates have now shown: speed-per-node changes have
returned approximately zero Elo on this engine, every time. Do this for the wall
clock, not because a gate will reward it.

**5.5 is done** (1.28×), and so are two items that were never in the plan:
inlining the `Piece` and `Move` accessors (2.0×) and counting mobility rather
than collecting it (1.04×). All three came from `make profile`, and the largest
was five lines.

**Re-profile before starting anything here.** The 2026-08-10 profile has been
wrong twice about where the time goes, and both corrections came from measuring.

---

## Open defects

`BUGS.md` 6 (deterministic play) and 7 (restarting mid-game forfeits) are the
only ones left. 6 matters more than it looks: results against a given opponent
are correlated, so the 49-game archive is worth less than 49 games of evidence,
and every accuracy figure derived from it inherits that.

---

## Game review

R0-R4 are done and the tool works. What is left is **using** it — 6.1 above is
the highest-value application, and it is engine work rather than a feature.

If it is ever to be shipped to players rather than used as an instrument, the
constraint in `REVIEW.md` still stands: at 2065 this engine is a peer of the
games it reviews, so point `--engine` at Stockfish for anything user-facing.

---

## How to work on this project

These are not principles chosen in advance. Each one cost something this week.

**Measure before choosing what to optimise.** The largest performance win in the
project was five lines, found by `make profile`, and no amount of reasoning
about algorithms would have suggested it. Twice the stale profile pointed at the
wrong thing.

**Match the instrument to what the change is supposed to buy.** A node budget
divides out speed-per-node by construction. `seepruning` cost two gates and a
night to learn that; `checkext` was seen on the first run because it buys
quality and the instrument could see it.

**Interleave timing measurements, or measure nodes.** Absolute wall-clock on a
shared machine is not evidence. A pin-aware move generator was reported as a
regression and was in fact 1.28× faster; the difference was an unloaded run
compared against a loaded one.

**A default of `false` is not a rejection.** It means no gate demonstrated a
gain. Write down which, or the record becomes a verdict nobody made.

**Pool before believing a shard.** One shard is 280 games and no 280-game match
resolves a 10-Elo effect. A single shard read +41 in a gate that pooled to +7.1.

**A behaviour-preserving change has a free proof.** If the bench signature is
byte-identical, the search did the same work. Three of this week's speedups were
verified that way and needed no gate at all.

**A test that has never failed is not known to work.** Every regression test
added this week was checked against the build that has the bug, before the fix.

**A documented command that has never been run is a guess.** `HANDOFF.md` spent
days recommending a gate command that exits immediately. CI ran against a branch
that no longer existed. Both failed silently.

**A harness that sets up differently from production validates nothing.** A
609 115-position differential passed while the real code was broken, because the
harness initialised tables that no real caller did. `perft` caught it in one
run.

**Two copies of the same knowledge will drift.** It has cost this project a gate
already. The move generator is templated on its output sink rather than copied;
the UCI option list is generated from the struct rather than written; `toUciMove`
lives in one place after nearly becoming a third copy.
