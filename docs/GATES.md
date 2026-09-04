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
