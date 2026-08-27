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

Re-gated at `-N 1000000`, which reaches depth 9-10.

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
