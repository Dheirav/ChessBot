# Is NNUE's corpus worth committing to? — 2026-09-04

Written after seven gates in one day returned nothing shippable and closed the
cheap half of the roadmap. `HANDOFF.md` states the consequence: **there is no
more cheap Elo in this search.** This document is the first serious costing of
the expensive item, so the choice is made on numbers rather than on the +200-400
prior that `ROADMAP.md` has carried, unmeasured, since Phase 7 was written.

**Verdict up front: yes on the corpus, no on starting with it.** The corpus is
the cheapest and least risky part and I had it wrong. Do Lazy SMP first anyway.
Reasoning below.

---

## 1. I was wrong about the corpus cost, by roughly an order of magnitude

I have said twice in this project that NNUE needs "weeks of self-play". That was
never calculated. Calculated, at the engine's own measured speed:

| nodes/position | throughput, 7 cores | 50M | 100M | 200M |
|---|---|---|---|---|
| 5 000 | 830 pos/s | 16.7 h | **33.5 h** | 66.9 h |
| 10 000 | 415 pos/s | 33.5 h | 66.9 h | 133.8 h |

At 593 knps — the measured field baseline, not a bench figure — **100M positions
is about a day and a half of background machine time.** Storage is 3.2 GB packed.

That is not a project. It is a long weekend of a machine that is idle whenever
the bot is not thinking, and it can run niced beside the bot at a cost this
session already calibrated (82-89% of baseline under load).

**The corpus was never the blocker. I said it was, three times, without doing
this arithmetic.**

## 2. The real blocker is the accumulator, and it is invasive

NNUE is three separable projects and only one of them is hard here.

| part | difficulty | why |
|---|---|---|
| corpus generation | **low** | a self-play driver around the existing engine; ~1.5 days machine time |
| trainer | **low-medium** | PyTorch, well-documented architecture, public reference trainers exist |
| **inference in the engine** | **high** | the accumulator must update incrementally inside `makeMove`/`unmakeMove`, plus int8/int16 quantisation and AVX2 |

The third is where this repo's specific risk lives. `makeMove` is the hottest
path in the program — the 2026-08-15 profile counted 1.87 *billion* calls to
`Piece::type()` alone — and an accumulator update hangs a 256-wide vector add
and subtract off every piece movement. Get it wrong and the engine is correct
but slower than the hand-crafted evaluation it replaced, which is the exact
trade `BUGS.md` 20 says has already killed three attempts.

There is also a structural hazard this repo has hit before: `evaluate()` is
cached on the zobrist hash (`BUGS.md` 8), and an incrementally-updated
accumulator is a *second* piece of position state that must stay in sync with
the board. A stale accumulator is the same class of bug as the stale-hash
`parseFEN` trap in `CLAUDE.md` — it does not crash, it silently evaluates the
wrong position.

## 3. The bootstrap question, which is the one that decides it

**Can a 2150 engine generate data good enough to beat itself?** Yes, and the
reason is worth stating because it is not obvious: the label is not the static
evaluation. It is the **search result** — what a few thousand nodes of
alpha-beta concluded. Search is strictly better informed than the evaluation it
calls, so the labels encode knowledge the current evaluation does not have, and
a net fitted to them can express what the hand-crafted terms could not.

That is precisely the gap `MEASUREMENTS.md` measures: `material` leads half of
every criticised move at a p90 of 535cp, and `ROADMAP.md` puts ~290cp of static
error as addressable. Six hand-crafted attempts at king safety could not state
it; the argument for a net is that it does not have to be stated.

**The caveat is real but bounded.** Data from this engine encodes this engine's
blindness, so the first net inherits a ceiling. The standard answer is to
iterate — regenerate with the stronger engine, retrain — and each round costs
another day and a half of the machine, not another month.

## 4. What this session's evidence actually says

`corrhist` was built as the cheap test of NNUE's premise and it failed: null at
+6.3, and −39.7 when it actually operated. **That is weak evidence and should
not decide this.** A single scalar keyed on a pawn hash is not a learned
evaluation in any meaningful sense — it has one parameter per pawn structure and
no view of pieces at all. Its failure says the caricature does not work, and
transfers almost nothing to a network with millions of parameters seeing the
whole board.

What the session *does* say, and this is the stronger signal: **six separate
cheap search interventions returned nothing.** The search is not where the
remaining Elo is. That is an argument for the evaluation, and NNUE is the only
route into the evaluation that `BUGS.md` 20 has not already closed.

## 5. The alternative, costed the same way

| | Lazy SMP | NNUE |
|---|---|---|
| prior | +100-140 at 8 threads (half the 16-thread figure) | +200-400 |
| machine time | none | ~1.5 days background |
| engineering | thread-safe TT, split points; `tt-16byte` merged first | corpus driver + trainer + quantised AVX2 inference + accumulator in make/unmake |
| **gateable at equal nodes?** | **no** — needs `--tc` | **yes**, it is an evaluation change |
| risk | moderate, well-understood | high, several novel failure modes |

**The measurement asymmetry is the deciding factor and it points the opposite
way to the Elo.** A `--tc` gate is load-sensitive, so it needs a free machine
*and* the bot down — and it cannot be sharded, so it is ~17 hours of unshardable
wall clock per answer. This session spent seven gates in one day precisely
because `-N` shards run 14-wide and load-independently. Lazy SMP would make
every future measurement of itself expensive.

NNUE, being an evaluation change, gates the ordinary way at `-N 100000`, in an
hour, on a contended machine. **The larger project is the cheaper one to
measure.**

## 6. Recommendation

**Do Lazy SMP first, then NNUE — despite everything above.**

The reason is not Elo per unit of effort, where NNUE wins. It is that Lazy SMP
is the only item whose value *decays*: eight cores are idle on every move the
engine makes, and every day the bot plays without it is throughput permanently
foregone. NNUE's value does not decay, and its corpus can generate in the
background **while** Lazy SMP is built — the machine is the constraint for one
and not the other, so running them in that order costs nothing and overlaps.

Concretely:

1. **Start corpus generation now, niced, in the background.** It is ~1.5 days of
   otherwise-idle CPU and it blocks nothing. Do this first because it is the
   only part that costs wall-clock rather than attention.
2. **Build Lazy SMP while it runs.** `tt-16byte` first. Accept that gating it
   costs an overnight `--tc` run with the bot down.
3. **Then NNUE inference**, against a corpus that already exists.

**One condition, from this session.** Every NNUE step must be checkable before it
is gated — `tests/bench` for node cost, `tests/evalerror` against Stockfish for
accuracy, and a bit-identical signature with the net disabled. The failure mode
this repo keeps hitting is not a wrong idea, it is an instrument that could not
see the thing being measured (`BUGS.md` 17, 18, 19, and `corrhist` today). A net
that cannot be A/B'd against the hand-crafted evaluation in one binary is not
ready to gate, whatever its training loss says.
