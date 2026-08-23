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
| accuracy **96.6% → 93.2%** as opponents strengthen | a small uniform quality gap, not rare catastrophes — 9 blunders in 2 575 moves |
| `threats` swung **±830 at p90**, more than material | it double-counted material; **deleting it was worth +155.0 Elo** (6.2) |

~~review says "no term accounts for it" → there are positions this evaluation
cannot see at all~~ — **withdrawn 2026-08-15.** It was one example, and the
whole-archive sweep put it at 3%. Struck rather than deleted because it drove
the ordering of this file for a day, and a reader who remembers it should find
out what happened to it. See 6.1.

Phase 4 made the evaluation *correct* — mirror-symmetric, no colour-blind
constants, honest "defended". Correct is not good. It remains hand-written
piece-square tables with weights nobody has ever tuned against evidence.

Search work from here buys depth, and depth against a weak evaluation has
diminishing returns. That is the argument for Phase 6 below being first.

---

## Phase 6 — Evaluation quality *(new, and the recommended next work)*

Not in `PLAN.md`. The instrument for it did not exist until this week.

**6.1 Find what the evaluation cannot see** — **DONE 2026-08-15, and the
hypothesis did not survive.**

`./tools/review --explain` over all 62 games, Stockfish 16 at depth 14, 2 575 of
the bot's own moves. The prediction was a list of positions this evaluation is
blind to. There is no such list:

| | |
|---|---|
| criticised moves, the bot's own | 158 — 9 blunders, 37 mistakes, 112 inaccuracies |
| **no term accounts for the loss** | **4 (3%)** |
| played move *was* the engine's choice (search noise) | 5 (3%) |

The terms move on **97%** of this engine's own errors. The premise came from a
single example — `13.Qa8+` in CTGzqoeY — and that was `BUGS.md` 1, the
repetition blindness, since fixed. One sweep retired a phase.

Two things had to be repaired before the numbers meant anything, and both are
the same lesson: **the instrument was wrong in the direction that flattered the
conclusion.** `BUGS.md` 10 (`mate 0` scored backwards) inflated cp loss most in
exactly the games the bot won, which made accuracy look flat across opponent
strength. Corrected, it declines monotonically:

| opponent band | games | score | accuracy | avg cp loss |
|---|---|---|---|---|
| under 1500 | 17 | 97% | 96.6% | 14.2 |
| 1500-1900 | 18 | 86% | 95.4% | 19.3 |
| 1900-2100 | 11 | 73% | 94.6% | 24.3 |
| 2100-2300 | 13 | 23% | 93.9% | 22.0 |
| 2300+ | 3 | 0% | 93.2% | 29.2 |

Overall **94.9%, 20.8 cp**. Note the shape: 3.4 accuracy points separate the
band the bot beats 97% of the time from the one it has never scored in. The
deficit is not rare catastrophes — 9 blunders in 2 575 moves — it is a small,
uniform quality gap that compounds. **That is an argument for tuning weights,
not for hunting blunders.**

**6.2 Retune piece values and term weights** — *now first. The first piece of it
is done and is the largest gain this project has measured.*

### The hanging-piece term is gone, and removing it is worth **+155.0 Elo**

Final: **+155.0 Elo, 95% CI [+144.3, +166.0]**, 3 360 games at `-N 100000`, two
binaries over UCI, 14 shards pooled, measured against the SEE rebuild that
shipped earlier the same day.

It took three steps to get here and the middle one was wrong in an instructive
way, so all three are recorded.

**1. The term was broken.** `hangingPiecePenalty` charged the *full piece value*
of anything attacked and not defended. That is material counted twice, since the
material term was still counting the piece, and it made `threats` the largest
term in the evaluation — p90 950 centipawns, max 2 755, both larger than
material's. It also asked the wrong question: "undefended" is not "does the
exchange win material", so it fired on threats worth nothing and stayed silent
on a defended queen attacked by a pawn.

**2. Rebuilding it on `see()` was worth +121.2 Elo** [+110.8, +131.9], charging
half of what the exchange actually wins. That looked like the answer.

**3. It was not.** Gating the divisor — the constant picked by argument rather
than evidence — found the score still climbing as the charge shrank:

| divisor | Elo vs the shipped 2 |
|---|---|
| 1 (charge the whole exchange) | **−177.7** [−189.1, −166.7] |
| 2 (shipped) | 0 — reference |
| 3 | +66.8 [+57.0, +76.7] |
| 4 | +98.1 [+80.9, +115.7] |
| 6 | +105.2 [+87.4, +123.5] |
| **none** | **+152.0** [+133.1, +171.8] → confirmed **+155.0** at full size |

Monotone, with no peak between 6 and infinity, so no larger divisor could beat
deleting it. **The gain never came from pricing threats accurately. It came from
this term saying less.**

That is the finding worth carrying: a static score cannot know whether a
threatened piece will be saved. The search settles it a ply later, for real, and
the guess was noise laid over an answer that was already coming. A 178 Elo swing
between divisor 1 and divisor 2 says the same thing from the other end — the
term's *magnitude* mattered enormously and its *accuracy* barely at all, which
is not what a term that measures something true looks like.

**Deleting it is also cheaper.** No `see()` per attacked piece, no
cheapest-attacker tracking. Bench 1 599 675 → 1 086 693 across the day (−32.1%),
and the wall clock 21.6% faster than the divisor-2 build measured interleaved.
Simpler, stronger and quicker together, which is rare enough to be suspicious —
hence the controls below.

### Why these numbers are believed

A +121 followed by a +155 in one afternoon is a reason to doubt the instrument,
not to celebrate.

- **The harness has no side bias.** The baseline was run against *itself* from
  two paths so the "A and B differ" guard still passed: 1 120 games, **50.00%**,
  pentanomial `0-0-560-0-0`. Every pair a perfect mirror.
- **The binaries were confirmed distinct** by node count at depth 6, every time.
- **The shipped build was confirmed identical to the one gated** — the deletion
  and the huge-divisor build both give 27 458 nodes and the same move on
  startpos, so the measured result transfers to the code that ships.
- **The cheap scan agreed with the full gate**: +152.0 over 1 120 games against
  +155.0 over 3 360. Ranking candidates at ±17 Elo and confirming the winner at
  ±10 is sound, and three times faster than gating every candidate at full size.
- **`evalref` was audited before regenerating**, twice: exactly two columns moved
  each time, and `total`'s delta equalled `threats`' delta on every changed row.
  Mirror symmetry held throughout, and it cannot be regenerated into agreement.

**What it is still not: Lichess Elo.** Both sides share every remaining blind
spot and differ in exactly the thing being measured, which is the arrangement
that most flatters a change. `HANDOFF.md` says do not translate self-play into
pool Elo, and a day that produced +121 and +155 is precisely when that rule is
hardest to keep and most necessary.

### It survived contact with the pool — 2026-08-16

The rule above is still the rule, and the reason to keep it is visible in the
result: the *direction* transferred and the *magnitude* did not. 31 rated games
on the shipped build went **31-0-0**, average centipawn loss fell 20.9 → 16.3,
and **10 blunders in 2 730 moves became 0 in 1 358** — five expected at the old
rate, P(0) = 0.7%. Rating moved 2102 → 2200, roughly +97 where self-play had
said +276.

The useful detail is *where* it moved. Within each opponent band, the gain
concentrates in **2100-2300** (accuracy 93.8 → 95.2, cp 22.0 → 15.7) and is flat
or marginally worse below 1500. That is this section's own argument showing up
in real games: a term that shouted about phantom threats costs most against
opponents who can punish a wasted tempo. It is also the reading that survives the
obvious objection, since the after-cut field was *easier* on average and a
band-by-band cut divides that out.

`HANDOFF.md`'s third measurement has the full tables and the caveats — chiefly
that no 2300+ opponent has been met since, so the ceiling is untested.

*Still open in 6.2:* the general Texel-style tune, now over an evaluation whose
largest term has stopped shouting.

### The original target, for the record

The values in `evaluation.cpp` are conventional guesses. Tuning them against
real game outcomes (Texel-style: pick the weights that best predict results over
a large position set) is the standard method, and the position set already
exists — the game archive plus the 23 603 positions `evalref` generates.

6.1 named where to start. Across the 154 attributed errors, `threats` is the
largest-magnitude term in the evaluation — larger than material:

| term | n | median \|Δ\| | p90 | max |
|---|---|---|---|---|
| **threats** | 128 | **148** | **830** | **1555** |
| material | 81 | 125 | 550 | 1060 |
| piece placement | 83 | 45 | 90 | 140 |
| mobility | 47 | 30 | 82 | 108 |

It leads 80 of the 154 explanations, and **49 of those 80 involve no material
change at all**. A positional term that routinely swings more than a queen is
not measuring position. Reading `evaluation.cpp:503-553` says why:

- `hangingPiecePenalty` subtracts the **full piece value** for any piece
  attacked and not defended — regardless of whose move it is, whether the
  attacker survives the recapture, or whether the piece can simply step away. A
  queen merely *en prise* reads −900, as though already lost. That is material
  counted twice.
- the threat bonuses accumulate per *(attacker, target)* pair, uncapped: three
  attackers on one piece score three times.
- `see.cpp` exists, is unit-tested, and gated at +25.6 Elo for move ordering.
  The evaluation does not consult it.
- a piece attacked and undefended is *also* charged by the separate `undefended`
  term.

The comment above it reads "much more aggressive evaluation", which describes
what it does rather than what it should do. This is a magnitude problem, not a
correctness one — `evalref`'s mirror-symmetry check passes.

*Verify:* rebuilding the hanging term on SEE is an evaluation change, so it is a
full A/B gated on nodes through the two-binary path (`BUGS.md` 8), not a toggle.
`evalref` will diff and `bench` will move; read both before regenerating either.
Do this **before** the general tune — tuning weights over a term that
double-counts material would only find weights that paper over it.

**The divisor was the most exposed decision in 6.2, and gating it overturned
the change it belonged to.** It was set to 2 by argument — only one side's
threat can be executed next, so the other has a move in which to save the piece
— and the argument was half right: the *direction* was correct and the stopping
point was not. Charging less kept helping all the way to charging nothing.

The precedent held exactly. Delta pruning swung **−50.0 to +7.1 on a single
constant**; this one swung **−177.7 to +155.0** across its range, on a change
whose headline number was +121.2. **A gated feature carrying an ungated constant
is not a gated feature.** The constant was worth more than the feature.

Two habits paid for themselves here and are worth reusing:

- **Sample the curve, do not test one alternative.** Had only divisor 3 been
  tried it would have read +66.8, looked like a win, and shipped — leaving 88
  Elo on the table and the wrong conclusion in the file. The shape is the
  finding; a single comparison cannot show one.
- **Scan cheap, confirm dear.** 1 120-game gates (±17 Elo) ranked five
  candidates in the time two full gates would have taken, and the winner's
  full-size confirmation landed within 3 Elo of its scan. Match the precision to
  the effect: chasing ±10 on a 178-Elo difference is buying nothing at four
  times the price.

**6.3 Add the terms a blind-spot list demands** — **deferred; there is no such
list.** 6.1 was supposed to produce it and returned four moves, one of which is
search noise. Do not add terms speculatively — that is how the current
evaluation acquired 25 of them, several of which were wrong for months. Re-run
6.1 after 6.2 lands; a corrected `threats` may expose blind spots it was
previously masking.

---

## 6.4 — King safety: a real defect that fixing does not fix *(closed 2026-08-16, negative)*

**The engine cannot see an attack on its own king, and giving it that ability
does not win games.** Both halves of that sentence are measured. Read the second
before proposing this again.

### How it was found

Not from theory. The 2300+ band was the one this engine had never scored in —
0-0-3 — so those three games were reviewed. What came back was not what the
"ceiling" framing predicted:

| opponent | rating | accuracy | blunders | mistakes |
|---|---|---|---|---|
| CookieCompote28 | 3042 | 93.6% | 0 | 0 |
| zero3-noW | 2876 | 93.7% | 0 | 0 |
| KirinChessBot | 2567 | 92.1% | 0 | 1 |

**Zero blunders across three losses**, at accuracy barely below this engine's
own average, against opponents playing at 98-99%. It was not punished for an
error; it was outplayed. And in all three it was *checkmated*, having walked its
king across the board — e1→f1→e2→f2→g2→g3→h2 in the first.

The cause is in `evaluation.cpp` and was unchanged since the base bot. King
safety is `-4 * distance from centre` — a centralisation term, applied in every
phase, which *pays* the king to walk up the board and pulls against
`PST_KING_MG` — plus a pawn shield capped at 24 centipawns that switches off the
moment the king leaves the back rank. **Nothing counts attackers.** A queen,
rook and knight swarming the king score exactly what an empty board does.

`tests/evaltrace` was built to confirm it rather than argue it, and did: king
safety read **+4** at the point the engine was being mated in nine while its
total evaluation read +222, and **−16** while its king sat uncastled on e1 for
twenty-seven moves in the AshNostromo loss.

### What was built, and what it measured

A conventional king-danger term: attackers into a nine-square king zone,
weighted by piece (N/B 20, R 40, Q 80), charged on a saturating curve in the
number of *distinct* attackers, faded by game phase. Mirror-symmetric, −1.4%
nodes, and on the losing position it re-scored the attack by −135 where the
shipped evaluation read +4. Separately, the legacy centralisation term was
faded out with game phase so it stops fighting the king PST.

Four gates, 10 080 games, `-N 100000`, two binaries over UCI:

| arm | Elo | 95% CI | games |
|---|---|---|---|
| null control, same build both sides | 0.0 | *(zero variance, `0-0-480-0-0`)* | 960 |
| king danger on | +1.3 | [−7.9, +10.6] | 3 360 |
| legacy centralisation off | +2.2 | [−6.8, +11.1] | 3 360 |
| both together | **−11.0** | **[−20.4, −1.6]** | 3 360 |
| king danger at 8× | **−216.9** | **[−241.9, −193.8]** | 960 |

**The only interval clear of zero is negative, twice.** The magnitude scan is
6.2's threat-term scan in mirror image — monotone, no peak above zero — and
licenses the same conclusion: the best charge is none. The combination is worse
than either half, most likely because `PST_KING_MG` already prices middlegame
king placement and a danger term charges a second time for the same exposure.

It was also **not free when off**: the king-zone test ran per attacked square in
the evaluation's hottest loop, and bench 6 went 2 007 → 2 114 ms, 5% slower for
a term contributing nothing. That is what settled deleting it rather than
keeping it behind a toggle the way `seepruning` and `deltapruning` are kept —
those cost nothing when off.

### The caveat, stated so it is not used as an escape

**Self-play may be structurally unable to see this.** A king-safety term pays
against opponents who attack kings, and in self-play both sides share this
engine's disinclination to. The games that prompted the work were against
engines rated 2567-3042.

That is a real limitation and it is *testable*: play the variant against
Stockfish held near 2300 and compare scores with and without. It needs a
gauntlet mode `tests/match` does not have — a common opponent, two challengers,
scores compared — which is a new instrument, not another arm of this
experiment. **Do not reopen 6.4 without building it first.** Four arms have
already been spent asking self-play a question it may not be able to answer.

### What the same investigation says is worth doing instead

Reviewing the most recent loss (AshNostromo, 2147, 2026-08-16) found the engine
finishing with **535 seconds of its 900+10 clock unused** against an opponent
who spent down to 84. That is `BUGS.md` 11, it is costing rated games now, the
instrument to gate it exists (`--tc`), and it is unblocked. It is a better use
of the next day than a fifth king-safety arm.

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

## Phase 7 — The road to 3000 *(the strength programme, 2026-08-22)*

`Crimsy_Bot` is **2130 Lichess rapid** over 218 rated games (2026-08-23;
`HANDOFF.md` states the current reading, `MEASUREMENTS.md` the history). 3000 is
about **870 Elo away**, which is not a tuning problem: it is the distance between a
competent classical engine and a modern one. Every item below is ordinary
engine work, none of it is exotic, and together they plausibly cover the gap.

**Read the Elo column as a prior, not a measurement.** Those figures come from
general engine-development experience, not from anything gated in this repo.
Every one of them has to earn its number here the same way `checkext` and
`softtime` did. What *is* measured here is the second column — the state of
this engine today.

| lever | measured state today | prior | how it gets gated |
|---|---|---|---|
| **Lazy SMP** | **single-threaded** (`std::thread` appears once, for the UCI search thread) on a **16-core** machine | **+200–280** | node-limited gates say nothing about threads; needs `--tc`, and the comparison is against itself at one thread |
| **NNUE evaluation** | hand-crafted, material-dominated: prices a material edge at −42cp where truth is −586, and ~290cp of that is addressable (`tests/evalerror`) | **+200–400** | self-play gate, plus `tests/evalerror` as the cheap pre-filter |
| **Search pruning suite** | futility, razoring, late-move pruning, singular extensions and probcut are **all absent** — grep finds none of them | **+150–300** | one `shard-gate.sh` each, exactly as PLAN 3.4 already describes |
| **Ponder** | absent; `lichess/config.yml` sets `ponder: false` because the engine has no support | +30–50 | rated games, not a gate — it buys the opponent's clock |
| **Opening book, Syzygy 3-4-5** | neither exists; `config.yml` cannot even advertise `SyzygyPath` | +20–50 | rated games |
| **Raw speed** | ~800 knps. The bitboard module is complete and perft-verified but measured at only **1.02×**, so the win is not there | +50–100 | `-t` gates only — a node budget is blind to speed by construction |

Sum of the priors: **650–1100**. So 3000 is inside the range, and the ordering
below is by Elo per unit of effort rather than by size.

**1. The pruning suite.** Cheapest, most incremental, and the existing harness
already measures it well. PLAN 3.4 is the first one and carries its own warning:
the same bet swung 57 Elo on one constant, so start conservative.

**2. Lazy SMP.** The largest single lever available and it needs no chess
knowledge at all — a thread-safe transposition table, split points, and the
discipline to gate it on a clock rather than on nodes. Sixteen cores are
sitting idle on every move the engine makes, on this machine and on Lichess.

**3. NNUE.** The largest total and the hardest, and the one that ends the line
of work this repo has been losing on. The evidence for that is unusually good:
king safety was gated four times over 10 080 games (+1.3, +2.2, −11.0, −216.9),
rebuilt on 2026-08-21 with two new instruments, and gated again at **−33.1**.
Six negative results, one afternoon each at best. A net learns from games what
six hand-crafted attempts could not state. **The evaluation is the ceiling, and
hand-crafting it is the slow road** — that sentence has now cost this project
more machine time than anything else in the file.

**4. Ponder, book, tablebases.** Afternoon jobs that slot in anywhere and are
worth more on Lichess than their size suggests.

**One caveat on the target.** 3000 *Lichess bot rapid* is not 3000 CCRL — the
bot pool is its own scale, and this account's rating moves with the opponents
matchmaking finds. `opponent_max_rating` was raised to 2500 on 2026-08-21 and
the rating fell from 2190 to 2130 over the two days that followed, because the
bot finally started meeting opponents that beat it — it scores 7% against
2300+, and nine of the fourteen games it has ever played there were in those
two days. That is the rating becoming honest, not the
engine getting worse.

---

## Open defects

`BUGS.md` 6 (deterministic play) and 7 (restarting mid-game forfeits) are the
only ones left; 10 was found and fixed by the 6.1 sweep. 6 matters more than it
looks: results against a given opponent are correlated, so the 62-game archive
is worth less than 62 games of evidence, and every accuracy figure derived from
it — including the table in 6.1 — inherits that.

---

## Game review

R0-R4 are done and the tool works. It has now been **used**: 6.1 was its first
job as an instrument, and it paid for itself twice over — once by retiring 6.3,
once by finding `BUGS.md` 10 in its own scoring.

The residue is a 3% phantom-loss floor: five criticised moves where the played
move *was* the engine's choice, worst at 16.7 win%, caused by successive
searches disagreeing across a shared transposition table. Small enough to leave
alone, large enough that single inaccuracies under ~5 win% should not be read as
real. `REVIEW.md`'s archive profile still predates `BUGS.md` 10 and needs
regenerating before it is quoted again.

If it is ever to be shipped to players rather than used as an instrument, the
constraint in `REVIEW.md` still stands: at 2065 this engine is a peer of the
games it reviews, so point `--engine` at Stockfish for anything user-facing.

---

## How to work on this project

**One number, one home.** Write a result once, in the place that cannot drift
from the code, and link to it everywhere else:

| kind of fact | its one home |
|---|---|
| a verdict on a toggle | the toggle itself, in `search.hpp` or `evaluation.cpp` |
| a pooled gate result | `GATES.md`, append-only |
| an external strength reading | `MEASUREMENTS.md`, append-only |
| what is running right now | `HANDOFF.md` |
| what is open and what blocks it | `TODO.md` |

On 2026-08-22 an audit found `seepruning` restated in ten files, four of twelve
rows in the open queue pointing at work already finished, and a stale line in
`HANDOFF.md` that sent that session off to redo a gate five days after it had
run. A number with ten homes has ten chances to go stale, and a stale number is
indistinguishable from a true one to the next reader. The toggle comments in
`search.hpp` were right when three documents were wrong, because they sit next
to the thing they describe.


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
