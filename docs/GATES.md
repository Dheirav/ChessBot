# Gates — every match this engine has been measured by

**Append-only.** A row is added when a gate finishes and is never edited
afterwards, because the value of this file is that it records what was measured
at the time rather than what is currently believed.

**This is the only place these results live in full.** Elsewhere — `HANDOFF.md`,
`TODO.md`, `ROADMAP.md` — a verdict is stated once, next to the thing it
decides, and links here for the number. That rule exists because on 2026-08-22
an audit found one verdict restated in ten files and four of twelve queue rows
pointing at work already finished: a number with ten homes has ten chances to
go stale, and a stale number is indistinguishable from a true one to the next
reader.

**The living verdict for a toggle is on the toggle**, in `search.hpp` or
`evaluation.cpp`. That is the one record that cannot drift from the code,
and on 2026-08-22 it was right when three documents were wrong.

Re-pool any of these with `./tests/pool-shards.sh <dir>/`.

## Rules these numbers depend on

- **Gate on nodes, not milliseconds** (`-N 100000` is the standing budget). A
  timed match is not reproducible from its seed and depends on machine load.
  Use `-t` or `--tc` only when the change's value is speed or clock management.
- **Never `--sprt` under sharding** — each shard stops on its own favourable
  noise.
- **A and B differ in exactly one thing**, or the result is not about that
  thing.
- **Pool before believing.** One 240-game shard cannot resolve a 10-Elo effect;
  the `ttaging` gate read +13 with an interval spanning zero on shard 1 and
  +11.5 clear of zero over all fourteen.
- **Do not pool across builds.** The 2026-08-14 `deltapruning` run and the
  2026-08-21 one measure different engines and were deliberately kept apart.
- **Run a null control when a result is surprising.** The +121.2 was five times
  anything previously measured here, so the harness was pointed at the baseline
  from two paths: 1 120 games, exactly 50.00%.

- **Do not let the machine sleep during a `--tc` gate.** The first `softtime`
  attempt was abandoned six games in for that reason. Under WSL2 the process
  clock does not advance while Windows sleeps, so `ps` etime *understates* the
  gap while file timestamps do not, and the only symptom was a progress bar
  reporting an impossible elapsed figure. Wall-clock measurement is precisely
  what a suspend corrupts, and a timed gate is the one built on it. The
  accepted run went 17 hours unbroken.

## Results

| directory | gate | pooled result |
|---|---|---|
| `shard-20260813-000634/` | `ttaging` | +11.5 [+3.2, +19.8] |
| `shard-20260813-015757/` | `seepruning` | +2.2 [−7.2, +11.6] |
| `shard-20260813-034736/` | `seeordering` | +25.6 [+16.1, +35.2] |
| `gate-seepruning-timed.log` | `seepruning`, **timed** `-t 100` | +4 [−7, +14] |
| `shard-20260814-122546/` | Phase 4 evaluation, two binaries | +6.1 [−3.9, +16.1] |
| `shard-20260814-153213/` | `deltapruning`, 200cp margin | **−50.0 [−60.3, −39.7]** |
| `shard-20260814-175417/` | `deltapruning`, 900cp margin | +7.1 [−2.9, +17.2] |
| `shard-20260814-203000/` | `checkext` | **+23.0 [+13.3, +32.7]** — accepted |
| `shard-20260815-112406/` | 6.2 SEE rebuild vs baseline | **+121.2 [+110.8, +131.9]** |
| `shard-20260815-121725/` | **null control**, baseline vs itself | **50.00%** — no side bias |
| `shard-20260815-131111/` | divisor 1 vs 2 | **−177.7 [−189.1, −166.7]** |
| `shard-20260815-142024/` | divisor 3 vs 2 | +66.8 [+57.0, +76.7] |
| `shard-20260815-153101/` | divisor 4 vs 2, scan | +98.1 [+80.9, +115.7] |
| `shard-20260815-155045/` | divisor 6 vs 2, scan | +105.2 [+87.4, +123.5] |
| `shard-20260815-160908/` | no penalty vs 2, scan | +152.0 [+133.1, +171.8] |
| `shard-20260815-163117/` | **no penalty vs 2, full** | **+155.0 [+144.3, +166.0]** — shipped |
| `shard-20260816-114039/` | **null control**, two binaries, same build | **50.00%**, `0-0-480-0-0` |
| `shard-20260816-115624/` | `kingdanger` on vs off | +1.3 [−7.9, +10.6] |
| `shard-20260816-131704/` | `kingcentre` off vs on | +2.2 [−6.8, +11.1] |
| `shard-20260816-140940/` | both king-safety changes | **−11.0 [−20.4, −1.6]** |
| `shard-20260816-150140/` | `kingdanger` at 8× magnitude | **−216.9 [−241.9, −193.8]** |
| `shard-20260821-112153/` | `deltapruning` re-measured on the current build | **+0.9 [−5.8, +7.7]** — closed, stays off |
| `shard-20260821-220901/` | king exposure 100% + king danger 300%, two binaries | **−33.1 [−43.2, −23.0]** — rejected |
| `shard-20260822-025838/` | `revfutility` alone | +12.3 [+1.5, +23.1] |
| `shard-20260822-033651/` | `razoring` alone | **+39.1 [+28.4, +49.9]** — accepted, on by default |
| `shard-20260822-113235/` | `revfutility` **on top of** `razoring` | **+18.4 [+7.8, +29.1]** — accepted, on by default |
| `shard-20260825-123915/` | Texel-tuned eval weights, two binaries, seed 20260825 | +22.8 [+9.6, +36.0] |
| `shard-20260825-161318/` | the same, seed 20260826 | +27.6 [+13.9, +41.3] |
| `shard-pooled-texel/` | **both of the above pooled, 3 360 games** | **+25.2 [+15.7, +34.7]** — accepted per node, **shipped and reverted the same day** (`BUGS.md` 18) |
| `shard-20260826-181028/` | **late move pruning** (`lmp`) | **+13.1 [+3.5, +22.8]** — accepted, on by default |
| `shard-20260827-150404/` | **`lmpshallow`** — LMP at depth 2 instead of 3 | **+15.0 [+5.6, +24.4]** — accepted, on by default |
| `shard-20260827-210628/` | `lmpdepth1` — LMP at depth 1 instead of 2 | **−31.3 [−40.5, −22.1]** — rejected, stays off |
| `shard-20260827-224417/` | `singularext` — **VOID, measured nothing** | −0.6 [−3.4, +2.1] — see below |
| `shard-20260828-002812/` | `razortight` — razoring margin 350cp vs the shipped 500 | **−1.0 [−10.0, +7.9]** — null, 500 stays |
| `shard-20260828-020116/` | `singularext` at `-N 3000000` | **−7.1 [−32.1, +17.8]** — **undecided, not rejected** |
| `shard-20260904-021023/` | **`conthist`** — continuation history | **+6.8 [−6.1, +19.8]** — null, stays off |
| `shard-20260904-094422/` | `corrhist` **v1**, run 1 | +11.2 [−1.0, +23.4] |
| `shard-20260904-110450/` | `corrhist` **v1**, run 2, base 20260904 | +1.4 [−10.8, +13.7] |
| `shard-pooled-corrhist/` | **both v1 runs pooled, 3 360 games** | **+6.3 [−2.3, +15.0]** — null, stays off |
| `shard-20260904-120423/` | `corrhist` **v2** — persistent **and** applied in quiescence | **−39.7 [−53.0, −26.5]** — rejected |
| `shard-20260904-130631/` | `corrhist` **persistence only** (`corrhistq` off both sides) | **+0.4 [−11.8, +12.7]** — null |
| `shard-20260904-142613/` | **`capthist`** — capture history inside the SEE bands | **+2.7 [−9.7, +15.1]** — null, stays off |
| `shard-20260904-214827/` | **`rootrandom`** — seeded tiebreak among near-equal root moves | **−90.7 [−105.0, −76.7]** — rejected; the cost is the root window, not the tiebreak |
| `shard-20260906-002113/` | **`evalnoise`** — ±5cp seeded perturbation of the static score | **−3.9 [−16.9, +9.0]** — **accepted and ON**, see below: a null is the success case here |
| *(unsharded, `--tc 10+0.1`)* | **Lazy SMP** — `Threads=8` against `Threads=1`, 600 games | **+162 [+139, +186]** — **accepted**, shipped at `Threads=6`, see below |
| `shard-20260909-180746/` | `lmrtable` at `-N 100000` | +2.7 [−10.2, +15.6] — **inconclusive by construction**, see below |
| `shard-20260909-194121/` | `lmrtable` at `-N 1000000`, run 1 | +21.1 [−1.5, +43.9] |
| `shard-20260909-233453/` | the same, run 2, base 20260909 | +31.7 [+9.0, +54.7] |
| `shard-pooled-lmrtable/` | **both pooled, 1 120 games** | **+26.4 [+10.4, +42.6]** — **accepted, ON** |

### The LMR reduction table, +26.4, and a gate that measured nothing — 2026-09-10

**+26.4 [+10.4, +42.6]** over 1 120 games, pooled from two runs of 560 at
`-N 1000000` (+21.1 then +31.7) at a total fixed before either was seen.
Pentanomial `41-106-220-113-80`. **On by default.**

The change replaces `const int R = 1` with
`R = 0.77 + ln(depth) * ln(moveCount) / 2.36`. The old constant reduced the
thirtieth move at depth 12 exactly as much as the fourth at depth 3, and that is
the main reason the effective branching factor sat at 2.3 where a strong engine
is 1.7 to 2.0.

**+26.4 is a lower bound, not an estimate.** The tree reduction depends on depth:

| depth | tree vs the fixed R=1 |
|---|---|
| 5 | **−2.9%** |
| 8 | −30.2% |
| 11 | **−56.1%** |

The gate ran at depth 8, so the instrument saw roughly half the feature. At
playing depth it should be worth more, and this is the rare case where the
measured number understates rather than flatters.

### The first gate measured nothing, and that is the lesson

`shard-20260909-180746` ran the same feature at the standing `-N 100000` budget
and returned **+2.7 [−10.2, +15.6]**. That is **not a null on this feature** and
must not be read as one. `-N 100000` reaches **depth 5**, where the table and the
constant differ by 2.9% of the tree: the gate compared the feature against a near
copy of itself.

The pentanomial was spread rather than piled on the centre, so unlike the void
`singularext` gate there is no obvious tell in the output. **A confident null is
what this failure looks like from the outside**, which is what makes it
dangerous.

**This is the fourth instance of one trap**, and it deserves a name rather than
another incident report: `singularext` at a budget three plies too shallow, the
`corrhist` persistence question that bench could not see at all, `probcut` parked
against the same wall, and now this.

> **Any feature whose magnitude scales with depth cannot be gated at a budget
> that does not reach playing depth.** Check the tree at two depths before
> gating: if the effect at gate depth is a small fraction of the effect at
> playing depth, the gate will answer a question about a feature that does not
> play.

Two minutes of `./tests/bench 5` against `./tests/bench 11` would have caught
this before three hours of gate. It was run only after the first gate came back.

### Lazy SMP, +162 — and the first gate here that could not be sharded — 2026-09-07

**+162 Elo [+139, +186]** over 600 games, pentanomial `2-19-90-94-95`, score
71.8%. The largest accepted gain in this file, and the first shipped Elo after
eleven consecutive gates that shipped nothing.

**It could not be a `-N` gate, and that is not a detail.** Every other row here
is node-limited: equal nodes to both sides, deterministic from the seed,
shardable 14 ways, load-independent. Threading buys *more nodes per unit time*,
which a node budget divides out exactly. So this needed `--tc`, two processes,
serial, unshardable, 4h26m on an idle machine — and it is load-sensitive in the
one direction that matters, because contention penalises the eight-thread side
specifically and biases *against* the change.

**`tests/match` could not express it either.** The one-variable check compared
`SearchOptions` and was blind to `--uciA`/`--uciB`; `Threads` is not a
SearchOption, so the whole variable under test was invisible and the harness
refused the gate as a match against itself. Fixed rather than worked around: the
omission would equally have *allowed* a pair whose only difference was a
mistyped `--uciA`, which is the failure that check exists to catch.

### Shipped at 6 threads, gated at 8

The gate ran `Threads=8`. **`lichess/config.yml` sets 6**, and the reason is
measured rather than cautious. Summed depth at a 5s movetime over twelve
positions, two runs each:

| threads | vs 1 thread | within-setting spread |
|---|---|---|
| 1 | — | 0 |
| 2 | +6.6% | 2 |
| 4 | +9.4% | 1 |
| **6** | **+13.1%** | 2 |
| 8 | +13.9% | 0 |

Between-setting gaps of 2-7 against a within-setting spread of 0-2, so 1/2/4/6
are genuinely separated and **6 vs 8 is not** (+0.8, inside the noise). Four
gives up a third of the gain and is not a free choice; six is indistinguishable
from eight and leaves two cores.

That headroom matters more than it would have before. At one thread the engine
needed one core of eight and held 82-89% of baseline under load
(`MEASUREMENTS.md`); at six it wants most of the machine, so every other job now
comes directly out of playing strength.

**So +162 is an upper bound for what actually runs**, on two counts: it was
measured at eight threads, and at 10+0.1 rather than the 900+10 the bot plays —
a 90x longer control. Threading generally holds or improves with more time, but
that is a prior, not a measurement here.

**What it costs permanently: search determinism at `Threads>1`.** TT races make
a threaded search irreproducible by construction. Threads therefore require a
clock — anything bounded by depth or nodes is a measurement and is pinned to one
thread in code, not by memory. Verified with `RootSeed` pinned: a depth-limited
search returns identical nodes and moves at 1, 2, 6 and 8 threads.

### `evalnoise` shipped on a null — 2026-09-06

**−3.9 [−16.9, +9.0]**, pentanomial `78-203-301-176-82`, properly spread. **On by
default.**

**A null is the success case here, and that inverts how this row should be
read.** The feature is not for Elo. It is the `BUGS.md` 6 fix: play was
deterministic, so 32% of the archive is sixteen opponents met four or more
times, whole games repeat, and every accuracy figure in `MEASUREMENTS.md`
inherits the correlation and is worth less than its game count claims.

**Read the number honestly rather than as a win.** The interval spans zero so no
loss is demonstrated, but the point estimate is negative and −17 is not
excluded. It ships because a cost near zero is what theory predicts — ±5cp is
five times smaller than the evaluation's own median error of 125cp — and because
what it buys compounds: every future gate and every field reading from here is
taken on decorrelated games. That is a deliberate trade of possibly-real Elo for
measurement validity, and it should be re-examined if the engine ever sits near
a rating boundary that matters.

**This is the second design for `BUGS.md` 6.** The first, `rootrandom`, was
rejected at −90.7 — and the cost there was never the tiebreak, it was searching
every root move on a fixed window to get exact scores, which disabled root alpha
cutoffs and tripled the tree. `evalnoise` never touches the root window, which
is where all three failures of that feature lived. One hash multiply on a path
that already runs `evaluate()`.

**Placed in `scoreForSideToMove()` rather than inside `evaluate()`**, and that is
a gateability decision, not an aesthetic one: `g_evalCache` is keyed on position
alone, so an evaluation toggle cannot be A/B'd with `--optA/--optB` in one
process (`BUGS.md` 8). On the search side of that boundary it gates the ordinary
way — which is why this row exists at all.

**The seed advances per game, not per process**, announced each time, and is
pinned when `RootSeed` is set explicitly so a logged game still replays.
lichess-bot keeps one engine process across every game it plays, so a
process-lifetime seed would perturb every game identically and decorrelate
nothing.

Bench moved 445 492 → **461 693**, and exactly one best move changed:
`startpos` `g1f3` → `e2e4`. Two near-equal openings swapping is the feature
working, not a regression — and eleven of twelve positions kept their move.

### `rootrandom` rejected, and the comment that hid why — 2026-09-04

**−90.7 [−105.0, −76.7].** Far too large for a 10-centipawn tiebreak among
near-equal moves, and the bench says why in seconds:

    rootrandom off      445 492 nodes
    rootrandom on     1 489 613 nodes      +234%, the tree more than triples

The feature searches **every root move against a fixed window with alpha never
rising**, because PVS returns bounds rather than scores and a tiebreak needs
real ones — two earlier versions of this were wrong for exactly that reason.
The cost is that alpha cutoffs at the root are disabled entirely. At a fixed
100 000-node budget side A therefore searches most of a ply shallower than B.
**The −90.7 is the depth loss, not the randomisation.**

**The comment on that code said the opposite, and it is the lesson here:**

> *"The price is no alpha cutoffs at the root. It is confined to the root ply
> and this path is off for gates and bench, so nothing measured pays for it."*

Being off for gates and bench did not make the cost free. It made the cost
**unmeasured** — and it would have been paid in full in real games, which run on
a clock where a 3.3x tree buys the same lost ply. A feature excluded from the
instruments is not cheap; it is untested. That sentence is the reason this sat
unqueried since 2026-09-01.

**The goal survives the design.** `BUGS.md` 6 is real: deterministic play means
32% of the archive is sixteen opponents met four or more times, whole games
repeat, and every accuracy figure in `MEASUREMENTS.md` inherits the correlation.
Decorrelation is worth more than its Elo because it buys validity for every
future field measurement.

**The cheap route is a seeded perturbation of the evaluation** — a few
centipawns keyed on position hash and a per-game seed. No search cost, since
`evaluate()` already runs; complete decorrelation; and ±5cp sits far inside the
evaluation's own measured error, median 125cp. It never touches the root window,
which is where all three failures of this feature have lived. Not yet built.

### The history family, all null — 2026-09-04

Five gates, **10 080 games, nothing shipped.**

| gate | result |
|---|---|
| `conthist` — continuation history | +6.8 [−6.1, +19.8] null |
| `capthist` — capture history | +2.7 [−9.7, +15.1] null |
| `corrhist` — three gates, closed below | null, then −39.7 |

These went to the head of the queue on 2026-09-01 as "the cheapest unsampled
Elo left in the search", with priors of +20-40, +10-20 and +15-30 taken from
general engine practice. **This engine's answer is no.** The priors were not
measured here and did not survive being measured here, which is the same
warning `ROADMAP.md` Phase 7 attaches to its own Elo column.

**They fired.** Every pentanomial is properly spread — `capthist` at
`82-144-364-179-71`, 43% level — so unlike the void `singularext` gate these
features changed the games and simply did not help. That is a result, not a
missed measurement.

**The regime caveat, logged and deliberately not acted on.** `-N 100000` reaches
depth 5-7; the bot plays at 10-12. History tables accumulate over a search, so
they plausibly pay more at depth than these gates can see — `BUGS.md` 19 again.
But 19 was a feature that *provably never fired*, and these fired. Re-gating at
`-N 3000000` costs 30x per game, and "measure it again at a bigger budget" is
how a losing bet gets tuned indefinitely. Open question, not a queued task.

**What this closes, and it is the useful part.** The pruning suite shipped, the
history family is null, hand-crafted evaluation tuning is closed (`BUGS.md` 20)
and correction history is closed below. **There is no more cheap Elo in this
search.** What remains — NNUE, Lazy SMP, deep-node gates for probcut and
singular extensions — is expensive in a way none of this year's work has been.

### Correction history, closed over three gates — 2026-09-04

The idea: the search already measures this evaluation's error for free at every
node — the gap between the static score and what the search returned — and threw
it away. Keep a running average of it keyed on pawn structure and apply it as an
offset. It attacks the same ~290cp of addressable static error NNUE targets, for
one array read and an add. `BUGS.md` 20 is why that mattered: it closed
hand-crafted evaluation tuning because three accurate fits each cost more depth
than they bought, and a *learned* correction is the cheapest possible test of
whether a learned evaluation escapes that trade.

**It does not. Three gates, and the family is closed.**

| version | what it did | result |
|---|---|---|
| v1 | table cleared every search, main search only | **+6.3 [−2.3, +15.0]** null |
| persistence only | cleared per *game*, main search only | **+0.4 [−11.8, +12.7]** null |
| v2 | persistent **and** applied at quiescence stand-pat | **−39.7 [−53.0, −26.5]** rejected |

**Persistence does nothing. Quiescence application costs about 40 Elo.** That is
the attributable finding, and it took a third gate to get because v2 changed two
things at once — the same one-variable rule this file enforces on gates, not
applied to the implementation. Three gates produced one citable conclusion where
they should have produced two.

**Why quiescence is where it goes wrong, as far as the evidence supports.** A
stand-pat score is compared straight against beta, so an offset there does not
merely reorder moves — it changes which subtrees are entered at all, and the
same for razoring, reverse futility and delta pruning. `CORR_CAP` is ±96cp.
v1's per-search clearing is probably the only reason entries never grew near it;
persistence lets them, and quiescence is where that magnitude lands. Offered as
the best available reading, not as a measured fact — one hypothesis has already
been falsified in this family (see the note on bench below).

**Bench attributed the tree change and was structurally blind to the rest.**
`corrhist` alone reads 446 010 against v1's 446 009 — one node — while
`corrhist`+`corrhistq` reads 456 284, +2.4%, with two best moves changed. So the
quiescence half owns the entire visible effect. But bench runs **one search per
position**, and persistence is about carrying the table *across moves*: within a
single search, "persist" and "clear per search" are the same thing. Bench could
not have answered the persistence question at any node count. `BUGS.md` 17, 18
and 19 are the same shape — instrument and subject in different regimes — and
this is the first time that was recognised *before* a gate was wasted on it
rather than after.

**What this does and does not say about NNUE.** It is not evidence against NNUE:
a single scalar keyed on a pawn hash is the crudest caricature of a learned
evaluation, and its failure does not transfer to a network with millions of
parameters seeing the whole board. What it does say is that this mechanism, at
this granularity, is inert at best and harmful when it actually operates — which
is not the cheap green light the roadmap was hoping for before committing weeks
to corpus generation. The NNUE question stays open and stays expensive.

**Not reopened by tuning.** `CORR_CAP`, the key, the update weight and the
application sites are all knobs, and turning them is exactly what six negative
king-safety gates and three cancelled evaluation tunes look like from the
inside.

### Continuation history, null — 2026-09-04

**+6.8 [−6.1, +19.8]** over 1 680 games. The interval spans zero, so `conthist`
stays off.

**This one measured something, unlike `singularext`.** The pentanomial is
`78-170-325-175-92` — 325 level pairs, 39%, properly spread. Compare the void
gate above at 91% level. The two sides played genuinely different games and the
answer is "not distinguishable from nothing", which is a result rather than an
absence of one.

**The bench predicted it and could have been read harder.** `conthist` cost
**+12.2% nodes** at bench 6 (445 492 → 499 901), concentrated in two of twelve
positions, with midgame-2 changing its move. This is an equal-*node* gate, so
that cost is paid directly in depth: A reaches slightly less than B on the same
100 000-node budget. The ordering is better and the tree it orders is bigger,
and the two roughly cancel.

That is the `BUGS.md` 20 shape for the fourth time — an improvement that works
and costs more than it buys. Worth noting because 20 argued the *evaluation* was
where that trap lived, and this says the search has its own version of it.

**Resolving +6.8 would take about four times the games**, which at the
contention this ran under is over thirty hours, for a point estimate inside the
noise. Not worth it. If it is ever reopened, the thing to change first is the
node cost, not the gate size: a version that orders without growing the tree is
a different feature, not a longer measurement of this one.

**On the machine it ran on.** Load averaged well over 20 for most of the run and
the gate took 7h32m against a predicted 35 minutes. The *result* is unaffected —
a `-N` gate is deterministic from its seed and node budget, which is the whole
reason `shard-gate.sh` refuses to shard a timed one. What contention broke was
the ETA, not the measurement. See `MEASUREMENTS.md`, contention calibration.

### A gate that ran a feature that never fired — 2026-08-27

`-N 100000` reaches **depth 5-7**. `SINGULAR_MIN_DEPTH` is **10**. The singular
probe did not fire once in 3 360 games, so this is not a verdict on singular
extensions; it is an accidental null control between two identical engines.

**The pentanomial said so before the Elo did.** `5-74-1529-66-6` — 1 529 of
1 680 pairs scored dead level, 91% of them. Compare `lmpshallow`'s
`157-332-601-389-201`, or the deliberate null control at
`shard-20260816-114039/`, which collapsed the same way. A distribution piled
onto the centre means the two sides played the same games.

**Read the pentanomial before the Elo.** A ±2.8 interval on 3 360 games is not
precision, it is two engines agreeing with each other.

This is `BUGS.md` 17 and 18 for the third time: the instrument and the thing
measured were in different regimes. There a gate held 32 MB while the bot plays
256 MB, and bench read depth 6 while games run at 10-12. Here the gate searched
depth 7 against a feature that needs depth 10. The commit that added the feature
even said *"bench cannot see this — the tree check has to be a real search at
depth 11 or more"*, and the gate was then run at a budget reaching depth 7
anyway. Writing the constraint down is not the same as applying it.

Re-gated at `-N 3000000`, which reaches depth 10.

### The razoring margin, closed — 2026-08-28

**500 stays.** 350 against it is **−1.0 [−10.0, +7.9]** over 3 360 games, and
the pentanomial `168-323-700-329-160` is properly spread, so this is a real
measurement rather than another accidental null control.

`TODO.md` has carried this since razoring shipped, on the strength of 3.1's
finding that the bet swings **57 Elo on the constant alone**. It does not
reproduce here. 3.1 lost 50 Elo at **200cp**, inside the evaluation's own
measured error; 350 is apparently still outside it, and the curve between 350
and 500 is flat. The first guess was a good one.

### Singular extensions are undecided, and that is a statement about the harness

**−7.1 [−32.1, +17.8]** over 392 games at `-N 3000000`. The pentanomial
`17-44-77-46-12` is spread, so the feature *did* fire this time — but ±25 Elo
cannot separate +18 from −32, and five hours bought that.

**Do not read this as a rejection.** The reason it is undecided is structural
rather than about the feature:

| | depth |
|---|---|
| gate at the usual `-N 100000` | **5-7** |
| this bot in real games | **10-12** |
| `SINGULAR_MIN_DEPTH` | **10** |

A node budget deep enough to fire the probe costs **30x** per game, so the games
affordable in a night fall from 3 360 to 392 and the interval widens past
usefulness. The alternatives are a `--tc` gate, which cannot be sharded and took
17 hours for 200 games and ±36 last time, or lowering the threshold until the
gate can see it — which measures a different feature than the one that would
play. `BUGS.md` 19.

### Singular extensions, closed by cost — 2026-09-07

**Not rejected. Closed because the only measurement that could resolve it is not
worth its price**, and that should be stated once rather than rediscovered every
few weeks.

Lazy SMP looked like it might unblock this: threading buys about a ply at any
given clock, and a `--tc` gate now costs 4h26m for 600 games rather than the 17
hours the last one took. Measured, it does not:

| `Threads=6`, per move | depth reached |
|---|---|
| 250 ms — what `--tc 10+0.1` gives | **7.0** |
| 500 ms | 8.3 |
| 1000 ms | 8.7 |
| `SINGULAR_MIN_DEPTH` | **10** |

At the control that made the Lazy SMP gate affordable **the probe never fires**.
Reaching depth 10 needs roughly 5 s per move, so ~600 s per game, so 600 games
is about **100 hours**. Threading bought a ply; the gap is three.

**Lowering the threshold is not the escape either.** The constant was chosen by
measuring the probe's price, and the numbers are on it in `search.cpp`: at
`MIN_DEPTH 8` the tree grows **+49.8%**, against +11.1% at 10. On an equal-node
gate that is paid in depth — the mechanism that made `conthist` null at only
+12.2%. A shallower variant is not a cheaper way to ask the question; it is a
different and much worse feature.

**So the only real option is more games at `-N 3000000`**, where the feature does
fire (`17-44-77-46-12`, properly spread). Narrowing ±25 to about ±12 needs four
times the 392 games already played: **~1 570 games, ~20 hours unshardable.**

Three reasons that is not worth spending, in order of weight:

1. **The point estimate is already negative** (−7.1). Twenty hours would most
   likely confirm a small loss or another null.
2. **General-practice priors have stopped predicting anything here.** The history
   family carried +20-40, +10-20 and +15-30 and returned +6.8, +2.7 and a closed
   family over five gates and 10 080 games. Singular extensions carry the same
   kind of prior from the same kind of source.
3. It would be the most expensive gate this project has ever run, for a feature
   whose own measurement does not suggest it is helping.

The code stays, off, with its verdict on the toggle. Reopen only if the hardware
changes enough to move the depth table above.

### The LMP depth curve has a peak, and it is 2

| setting | result |
|---|---|
| depth 3 (as first shipped) | baseline |
| **depth 2** | **+15.0 [+5.6, +24.4]** |
| depth 1 | **−31.3 [−40.5, −22.1]** |

Two is an optimum rather than a point on a trend, so the question closes here —
including in the other direction, since a curve with a peak at 2 gives no reason
to try 4.

**The tree check predicted the sign, for a fraction of the cost.** `lmpdepth1`
raised bench 6 from 445 492 to 735 879, **+65%**, which is most of what late
move pruning buys handed straight back; the gate then priced that at −31. Where
`lmpshallow` cost only +2.1% and won, this cost 65% and lost by roughly twice
as much. `CLAUDE.md` already says to check the tree before gating; this is the
first time in this file that the check would have called the result on its own.

Worth keeping for a second reason. It was proposed **with no theory attached**,
deliberately: the explanation offered for `lmpshallow`'s +15 — that pruning less
hands back judgement — had not survived measurement, since the disagreement rate
against a depth-11 referee barely moved (18 against 20). Running the next rung
was the honest way to find out whether the trend was real. It was not.

### The Texel tune, 2026-08-25 — and why the two halves are poolable

Split into two 1 680-game runs because the machine was needed at 15:00, not
because anything about the experiment changed. Both ran the **identical pair of
binaries**, md5 `f8f163a5cd` (tuned) against `4486e4225e` (shipped), verified
before the second started. Different `SEED_BASE` means different opening lines,
which is more games of one experiment rather than two experiments.

That is the distinction this file cares about. `deltapruning`'s 2026-08-14 +7.1
was **not** pooled with its 2026-08-21 re-measurement because a `threats`
deletion and two clock fixes sat between them — different engines, so pooling
would have averaged two different questions. Here nothing moved between the
halves.

The halves agree: +22.8 [+9.6, +36.0] and +27.6 [+13.9, +41.3], intervals
overlapping across almost their whole width. Two independent samples landing on
each other is what makes this believable; the pooled interval merely makes it
precise.

**What this number is not, and it cost a revert.** It is quality *per node*. The
gate pays both sides 100 000 nodes a move and therefore divides out the node
price entirely.

That price was read off bench at depth 6 (+15.1%) and reasoned about as roughly
−14 Elo. Measured at the depth the bot actually plays it is **+59% at depth
10**, worth perhaps −47 — which could make the change a net loss on a clock.
`BUGS.md` 18 has the full curve. The weights were merged, deployed, and reverted
the same day; they live on branch `eval-texel-tune`.

**So this row is a true statement about quality per node and not a claim that
the engine got stronger.** Phase 4's evaluation fixes carry the same caveat at
+20.1% nodes at depth 6, and nobody has measured *their* cost at depth 10
either. Only a `--tc` gate closes it, and it is now worth its cost: the question
is a possible forty-Elo swing rather than the eleven that ±36 could not
resolve.
