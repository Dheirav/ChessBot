# Search heuristics against Stockfish: extensions, reductions, re-search, iterative deepening

Written 2026-09-11. Code reading only, no measurement: a timed gate was running
on this machine, and every number quoted below is either from this repository's
own records or from source on both sides. Nothing here was benchmarked for this
document, and where I say "expected" I mean predicted, not measured. That
distinction matters here more than usual, because `BUGS.md` 21 is the record of
what happens when a prediction gets quoted as a measurement.

Stockfish side: `src/search.cpp` at commit `e52ea9ac8d4f` (master, 2026-09-09).
Line numbers below are that file. Our side: `src/engine/search.cpp`,
`src/engine/search.hpp`, `src/engine/search_tuning.hpp` at the working tree of
2026-09-11.

## The short version

The largest gap is not in the reduction formula, the extensions, or the
aspiration window. It is that **this engine does not do principal variation
search at interior nodes**. PVS exists at the root
(`search.cpp:1310-1318`) and inside the LMR probe (`search.cpp:950-959`), and
nowhere else. Every interior move that is not reduced is searched with the full
parent window (`search.cpp:961-962`).

Two consequences follow, and both of them cost tree.

1. `isPV` is defined as `beta - alpha > 1` (`search.cpp:666`), so it is true for
   every node reached from the root through non-reduced moves. Razoring,
   reverse futility, late move pruning and move futility are all gated on
   `!isPV` (`search.cpp:701`, `search.cpp:902`, `search.cpp:913`). They are
   therefore inert over that whole region. LMP alone cuts 45.5 percent of nodes
   where it is allowed to run (`search.hpp:222-223`).
2. Null-window searches cut sooner than wide-window ones, because a child only
   has to beat `alpha + 1` rather than a distant `beta`, and because a
   transposition entry that is a bound can only produce a cutoff when the bound
   clears the window (`transposition_table.cpp:43-55`). A wide window suppresses
   both.

Stockfish's structure is the opposite. Node type is a template parameter plus an
explicit `cutNode` argument (`sf:721-726`), it asserts that a non-PV node always
has a null window (`sf:745`), and it searches every move after the first with a
null window whether or not it reduced it (`sf:1403-1412`), reserving the wide
window for the first move and for a move that has already beaten alpha
(`sf:1417-1431`).

Everything else in this document is smaller than that.

## 1. Late move reductions

### What each side computes

Ours, `search_tuning.hpp:93-117`:

```
R = LMR_BASE + ln(depth) * ln(moveIndex) / LMR_DIVISOR      (0.77, 2.36)
R += 1 if !improving          (improving is off: search.hpp:687)
R -= history * 2 / HISTORY_MAX  (histReduction is off and cannot work: search.hpp:643-659)
R clamped to [1, depth - 2]
```

Applied at `search.cpp:941-959`, and only when `depth >= 3 && moveIndex > 3 &&
!inCheck && move.flag == NORMAL`.

Stockfish's base, `sf:1893-1895` with the table at `sf:712-713`:

```
reductions[i] = 2872/128 * ln(i)
r = reductions[depth] * reductions[moveCount]
    - delta * 577 / rootDelta
    + !improving * reductionScale * 197 / 512
    + 982
```

in units of 1/1024 ply. The `ln(d) * ln(m)` coefficient works out at about 0.49
plies against our 0.42, so **the shape and scale of the base formula are close
to ours and are not where the difference lives**. All of the difference is in
the adjustments applied at the call site, `sf:1165-1170` and `sf:1323-1367`.

### Every input Stockfish's reduction depends on that ours does not

| input | Stockfish | size | reads the eval? | cheap here? |
|---|---|---|---|---|
| `cutNode` | `sf:1335-1336` | +3.9 plies, +4.8 with no TT move | no | needs the node-type parameter, then trivial |
| `allNode` scaling | `sf:1366-1367` | multiplies r by up to 1.08 at low depth | no | same prerequisite |
| `ttPv` | `sf:1169-1170`, `sf:1324-1326` | +0.9, then up to -4.7 | no | one spare TT bit, see below |
| `ttData.move` absent | `sf:1406-1407` | +1.1 | no | trivial, we already have `ttMove` |
| `ttCapture` | `sf:1339-1340` | +1.05 | no | trivial, `Move` carries `capturedPiece` |
| `(ss+1)->cutoffCnt` | `sf:1343-1344` | +0.26 to +2.3 | no | one small per-ply counter in `SearchContext` |
| `move == ttData.move` | `sf:1347-1348` | -2.1 | no | irrelevant here, we always search the TT move first (`search.cpp:783-788`) |
| `delta / rootDelta` | `sf:1165`, `sf:1894` | up to -0.56 at a PV node | no | needs a `rootDelta`, trivial, but meaningless until PVS exists |
| `moveCount` linear term | `sf:1331` | -0.06 per move, so -1.9 at move 30 | no | trivial |
| `statScore` from main, continuation and capture history | `sf:1350-1360` | +/- about 2 plies | no, but it needs a two-sided history | `histMalus` is the prerequisite and is built, ungated (`search.hpp:611-623`) |
| `alpha - eval` | `sf:1362-1363` | up to +0.28 | **yes** | cheap, and in the class that has failed four times here |
| `correctionValue` | `sf:1332` | small | **yes** | the corrhist family is closed (`search.hpp:439`) |
| `improving` | `sf:1894` | +38 percent of the scale | **yes** | built, gated -10.6 (`search.hpp:674-687`) |
| post-hoc `doDeeperSearch` / `doShallowerSearch` | `sf:1389-1392` | +/- 1 ply | no, it reads search values | cheap, see section 5 |

The pattern is worth stating because it lines up exactly with this project's own
finding in `docs/RESEARCH-SPEED-AND-SEARCH.md`: the large adjustments in
Stockfish, `cutNode` at about 3.9 plies and `ttPv` at up to 4.7, **do not read
the evaluation at all**. They are structural, like the LMR table that gated
+26.4. The eval-dependent adjustments in that list are worth a fraction of a ply
each. We have been trying the small half of Stockfish's reduction logic and
missing the large half.

The single largest number in that table is `cutNode`. Stockfish reduces roughly
four plies more at a node it expects to fail high than at an otherwise identical
node it does not, and it can only do that because it knows which is which. We
reduce identically everywhere. That is section 3.

### One thing we do that Stockfish does not

`lmrReduction` clamps to `depth - 2` (`search_tuning.hpp:115`), so the reduced
search always keeps at least one real ply. Stockfish clamps to `max(1, min(newDepth
- r/1024, newDepth + 2)) + PvNode` (`sf:1377`), which permits a *negative*
reduction, that is, a limited extension for a move whose adjustments made r
negative. Not worth copying on its own, because the adjustments that make r
negative there are the ones we do not have.

## 2. Extensions

Ours, in full: a check extension applied unconditionally at any node in check
below ply 64 (`search.cpp:598-600`), and a singular extension that is off
(`search.hpp:299`) with a probe at `search.cpp:806-826`.

Stockfish's, in full: singular, double and triple extensions (`sf:1273-1276`),
multi-cut (`sf:1285-1298`) and negative extensions (`sf:1309-1310`). That is the
whole of `grep -n extension` on their file outside of comments and the LMR block.
**Stockfish has no check extension at all.**

That reverses the framing of the question. The EBF consequence of having almost
no extensions is that our tree is *smaller* than it would be with them, not
larger. Our one extension costs 9.2 percent of nodes at bench 6 and bought
+23.0 Elo (`search.hpp:54-62`), which is the cleanest statement in this repo
that node count is not the objective. Extensions buy accuracy per node and spend
tree; they are not a branching-factor mechanism and closing this gap in the
obvious direction would make the EBF worse.

Two parts of Stockfish's extension block are tree *reducers*, and both are
attached to the singular probe:

- **Multi-cut** (`sf:1285-1298`): when the excluded search fails high over the
  real beta, the whole node returns immediately. This is a large saving and it
  is free once the probe has run.
- **Negative extensions** (`sf:1309-1310`): `extension = -3` when the TT move is
  expected to fail high or the node is a cut node. Three plies off the TT move
  is a bigger lever than the +1 the singular case grants.

Both are unavailable to us for the same reason: they ride on the probe, and the
probe is closed by cost (`search.hpp:288-298`, `GATES.md` "Singular extensions,
closed by cost"). Nothing here reopens that, and I would not reopen it. The
probe needs `depth >= 10` (`search_tuning.hpp:134`) to be affordable at all,
which is deeper than a 10+0.1 gate reaches, and that is a fact about the harness
rather than about the feature.

The one extension change worth considering is in the restricting direction:
capping consecutive check extensions along a path, or refusing to extend a check
whose SEE is negative. That is eval-free and would cut tree. It is also a direct
trade against a +23.0 Elo measured result, which is the shape of bet
`razortight` lost at -1.0. Low priority, real risk, listed at the bottom of the
ranking.

## 3. Node types, which is the actual finding

Stockfish carries three kinds of node and treats them differently everywhere:

- The type is a template parameter, `PV`, `NonPV`, `Root`, plus an explicit
  `cutNode` argument, with `allNode = !(PvNode || cutNode)` (`sf:721-726`).
- It asserts the invariant that a non-PV node has a null window (`sf:745`),
  which is precisely the invariant we do not maintain.
- The child's type is chosen at every call site: the null-move search passes
  `false` (`sf:1022`), the LMR probe passes `true` because a reduced scout is
  expected to fail high (`sf:1380`), the full-depth re-search passes `!cutNode`
  (`sf:1395`, `sf:1411`), and the PV re-search passes `false` (`sf:1430`).
- Type drives: TT cutoffs (only at non-PV, `sf:875`), null move (only at cut
  nodes, `sf:1012`), IIR (not at all nodes, `sf:1059`), reductions (`sf:1335`,
  `sf:1366`), singular margins (`sf:1258`, `sf:1268-1271`), and the re-search
  ladder (`sf:1403`, `sf:1417`).

We have one derived boolean, `isPV = (beta - alpha > 1)` (`search.cpp:666`,
recomputed as `isPv` inside the move loop at `search.cpp:891`), no cut-node or
all-node concept, and no node-type argument.

The reason this is structural rather than cosmetic is the interaction with the
missing interior PVS. Walk the tree from the root:

- Root move 1 is searched on the aspiration window, width 100 by default
  (`search.cpp:1237`, `ASP_BASE_DELTA = 50` at `search_tuning.hpp:157`), so its
  child sees `beta - alpha == 100`.
- At that child, moves 1 to 3 are never reduced (`search.cpp:941` requires
  `moveIndex > 3`), and neither are captures, promotions or any move while in
  check. Each of those is searched with `-beta, -alpha` (`search.cpp:961`), so
  each of their subtrees is entered with a wide window as well.
- Below remaining depth 3, `reduce` is false for *every* move, because LMR
  requires `depth >= 3`. So the last two plies of that whole region are entered
  wide, unconditionally.

So the set of nodes where `isPV` is true is not the principal variation. It is
everything reachable from the root without passing through an LMR probe or a
null move. Inside that region:

- `lateMovePruning` never fires (`search.cpp:913`), and it is the biggest
  measured tree cutter we have at 45.5 percent of nodes (`search.hpp:222-223`).
- `razoring` never fires (`search.cpp:701`), worth 21 percent of nodes at bench
  6 (`search.hpp:198-200`).
- `revFutility` and `moveFutility` never fire, same gates.
- `wantStatic` is false (`search.cpp:675-680`), so those nodes do not even
  compute a static score, which is a saving that exists only because the
  pruning it would feed is switched off.
- TT cutoffs need an exact entry, or a bound clearing a window 100 wide, rather
  than a bound clearing a null window (`transposition_table.cpp:46-50`).

I could not measure what fraction of the tree that region is, because of the
running gate. It is one counter to find out, and it should be the first thing
measured when the machine is free: increment two counters in `minimaxWithTT`
keyed on `isPV` and print the ratio at the end of a `go depth 11`. My prediction
is that it is most of the tree at depth 11, and I would rather it were checked
than believed.

Note one place where we are already more aggressive than Stockfish: we take TT
cutoffs at PV nodes (`search.cpp:611-613`) where they refuse to (`sf:875`). That
truncates the PV but shrinks the tree, so it is not a problem to fix. It becomes
almost irrelevant once PV nodes are rare.

## 4. Internal iterative reduction against our IID

Ours is IID and it is off: search the position two plies shallower when there is
no TT move, then read the move back out of the table (`search.cpp:767-777`).
Gated 2026-08-18 at -0.1 Elo, interval [-4.9, +4.7], which the comment at
`search.hpp:120-139` correctly calls the tightest null this project has
produced. The mechanism given there is right: iterative deepening means a node
at remaining depth 5 or more was usually visited by the previous iteration, so
the trigger condition is rare. It never fires at bench 6 and moves the tree 0.19
percent at depth 7.

Stockfish's IIR is three lines (`sf:1056-1060`):

```cpp
if (!ss->followPV && !allNode && depth >= 6 && !ttData.move)
    depth--;
```

The difference is the direction of the cost. IID *adds* a search and hopes the
ordering pays for it; IIR *removes* a ply and accepts worse ordering in exchange.
IIR cannot cost nodes, which means it cannot lose the way IID did.

But the honest reading of our IID number applies to IIR too: both key on "no TT
move at depth", and our measurement says that condition is rare at bench depths.
A technique that never fires cannot help. Two things could change that, and
neither is established:

- The default table is 64MB, which is 4.19M entries at 16 bytes per entry
  (`transposition_table.hpp:130-131`). A depth-12 search here costs 11.1M nodes,
  so the table is overwritten roughly three times during a single move. The
  bench-6 observation was taken on a search that fits comfortably in the table
  and does not describe the real-depth regime.
- With interior PVS in place, most nodes become null-window nodes storing
  bounds, and the TT-move hit rate at a given depth changes in ways the current
  measurement cannot predict.

So: IIR is a real gap, it is three lines, and it is worth trying *after* PVS
lands, at which point its fire rate should be re-measured rather than assumed.
It is not worth trying before, because we already have a measurement saying the
trigger is rare in the current structure.

Note the `!ss->followPV` condition (`sf:1059`, set at `sf:772-775` from
`lastIterationIdxPV`). Stockfish refuses to reduce on the previous iteration's
PV path. We have no equivalent of that path and would have to keep one, or
approximate it with the exact-bound TT entries along the PV.

## 5. Re-search policy

### Ours

Two places only.

Root, `search.cpp:1310-1318`: null-window probe, then a full re-search when
`eval > alpha && eval < beta`. This is textbook and correct.

LMR, `search.cpp:950-959`: reduced null-window probe at `depth - 1 - R`, then on
`eval > alpha` a re-search at `depth - 1` with the **full** window `-beta, -alpha`.
At a null-window parent that is the same window, so nothing is lost. At a
wide-window parent it means a move that merely bounced off alpha in the probe
gets its entire subtree searched wide.

Everything else is a single full-window search (`search.cpp:961-962`).

### Stockfish

Three stages, `sf:1370-1431`:

1. Reduced probe with a null window at `d`, cut node (`sf:1380`).
2. On a fail high, adjust the target depth by the *size* of the fail high,
   `doDeeperSearch` when `value > bestValue + 53` and `doShallowerSearch` when
   `value < bestValue + 8` (`sf:1389-1392`), then re-search at full depth still
   with a **null window** (`sf:1395`).
3. Only then, and only at a PV node whose move is the first or has beaten alpha,
   the wide-window search (`sf:1417-1430`).

And for moves that skipped LMR entirely, step 19 still uses a null window and
may shave a ply when the computed reduction was large (`sf:1410-1411`).

The gap that matters is stage 2. Ours goes straight from a reduced null-window
probe to a full-depth wide-window search, skipping the full-depth null-window
verification. Every LMR move that fails the probe but would have failed low at
full depth pays for a wide-window subtree to discover it. Adding the intermediate
null-window re-search is about five lines and is a strict subset of the PVS work.

`doDeeperSearch` and `doShallowerSearch` read search values, not the static
evaluation, so they are not in the class of technique that has failed here. They
are worth a look after PVS, not before.

## 6. Aspiration windows and the iterative deepening loop

### Aspiration

| | ours | Stockfish |
|---|---|---|
| starts at | depth 3 (`search.cpp:1226`) | depth 1, the loop always aspirates (`sf:376-379`) |
| centred on | the previous iteration's score (`search.cpp:1242-1243`) | a running average of the root move's scores, `averageScore` (`sf:377`) |
| initial width | fixed 50, `ASP_BASE_DELTA` (`search_tuning.hpp:157`) | `5 + threadIdx % 8 + abs(meanSquaredScore) / 10193` (`sf:376`) |
| growth | `delta *= 4` (`search.cpp:1340`, `search.cpp:1346`) | `delta += 47 * delta / 128`, about 1.37x (`sf:438`) |
| on fail low | widen alpha only | drop beta to alpha as well (`sf:422-423`) |
| on fail high | widen beta only | raise alpha to `beta - delta` as well (`sf:431-432`) |
| depth on retry | unchanged | `rootDepth - failedHighCnt - 3*(searchAgainCounter+1)/4` (`sf:392-393`) |

Three real differences, in descending order of value:

1. **Depth reduction after repeated fail highs** (`sf:392-393`). A fail high at
   depth d currently costs us the whole of depth d again at the same depth, and
   again if it fails high again. Stockfish searches the retry one ply shallower
   each time. This is cheap and eval-free.
2. **Both bounds move.** On a fail high we keep `windowLo` at `bestScore - 50`
   while pushing `windowHi` out by 4x (`search.cpp:1345-1348`), so the retry
   window is 50 wide on one side and 200, then 800, on the other. Stockfish
   keeps the window narrow on both sides. A window that is 800 wide on the high
   side is barely a window, and under the current structure it also widens the
   PV cone described in section 3.
3. **Centring on an average rather than the last score.** Cheap, and it is
   reading search output rather than static evaluation, so it is not in the
   failed class.

`aspAdaptive` (`search.hpp:358`) is a fourth idea and is off. Its comment is
right that the fixed window is what blocked the tuned evaluations, and the four
fixed configurations that were swept found the shipped one best. I would not
prioritise it: it is a root-only effect, and the gates here resolve about plus
or minus 8 to 10 Elo on 3,360 games, so a root-only window change is almost
certainly under the resolution of the instrument and has to be argued on tree
size with `tools/treecost` instead.

### The iterative deepening loop

Missing here, from `sf:332-441`, setting aside MultiPV:

- **Root moves are not sorted by their previous scores.** Stockfish
  `stable_sort`s the root move list after every search (`sf:403`) and saves
  `previousScore` per move at the top of each iteration (`sf:347-352`). We
  re-order root moves each iteration with the same `orderMoves` used at interior
  nodes plus a TT-move swap (`search.cpp:1200-1210`), so the ranking of root
  moves 2 to N is history-driven rather than score-driven. The effect is smaller
  than it looks because those moves are null-window scouted at the root anyway
  (`search.cpp:1313`), and it only shows up when the best move changes.
- **Per-root-move effort accounting** (`sf:1449-1470`), which feeds time
  management rather than tree size. Out of scope for EBF and out of scope for
  gates, because `--tc` gates here cost about twenty unshardable hours
  (`search.hpp:157-166`).
- **History decay between searches** (`sf:326-330`, `mainHistory` scaled by
  729/1024 at the start of each search, `lowPlyHistory` refilled). We clear the
  orderer outright each search (`search.cpp:1143`), which throws away more than
  it needs to. Cheap, and it is ordering rather than reduction, and ordering
  measured null here twice (`GATES.md`, the history family). Low priority.
- **A stored PV path from the previous iteration** (`sf:370`, `sf:772-775`),
  used to protect the PV from IIR. Only needed if IIR lands.

One thing we have that Stockfish does not: the node-budget prediction in
`budgetSpent` (`search.cpp:1055-1066`), which refuses to begin an iteration that
cannot finish inside the node budget. That exists so both arms of a gate spend
the budget the same way, and it is correct for that purpose. Note that it
hardcodes `BRANCHING = 2.3` (`search.cpp:1053`), which was true before the LMR
table shipped and is about 1.87 now. That is not an Elo bug, but it makes the
predictor pessimistic by 20 percent and so ends node-limited searches one
iteration earlier than needed in some positions. Changing it changes every gate
number, so it should be changed deliberately or not at all.

## Ranked changes

Expected EBF effect is a prediction unless it says measured. Difficulty counts
both cores: `bb_search.cpp` mirrors `search.cpp` node for node
(`bb_search.cpp:843-865` against `search.cpp:941-963`) and
`docs/BITBOARD-REPLACEMENT.md` requires them to search identical trees, so every
search change is two edits and a `tests/bbequiv` check.

**1. Interior principal variation search.** Search moves after the first with
`-alpha-1, -alpha` and re-search on `alpha < value < beta`, exactly as the root
already does at `search.cpp:1313-1318`.
Expected EBF effect: large, and the largest available. It narrows the window
across the tree, which cuts sooner and makes TT bound entries usable, and it
turns the region described in section 3 into non-PV nodes where razoring, LMP
and reverse futility all begin to fire. Those three were measured at +39.1,
+13.1 and +18.4 Elo on the part of the tree where they already run.
Difficulty: moderate. The window change itself is ten lines per core. The work
is in the fallout: bench signature moves, `evalref` is untouched, `bbequiv` has
to be re-established.
Risk: medium. The failure mode is re-search cost from bad move ordering, and our
ordering is TT move first, then SEE-split captures, killers and history, which
gated well enough that better ordering bought nothing further (`contHist`,
+6.8 null). That is evidence the ordering is good enough to carry PVS.
Measure first with `./tools/treecost 8 200 pvs` and `./tools/treecost 11 200
pvs`, then gate. Do not trust `tests/bench` for this, per `BUGS.md` 21.

**2. Explicit PV-line flag, as a separable first half of change 1.** Replace
`isPV = (beta - alpha > 1)` (`search.cpp:666`) with a `pvNode` parameter that is
true only for the first move's chain from the root, and key the pruning gates on
it. No windows change.
Expected EBF effect: large for LMP specifically, which is the eval-free 45.5
percent cutter and whose firing condition (`moveIndex > 3 + depth*depth`,
`search.cpp:924`) does not involve the window at all. Smaller for razoring and
reverse futility, and this is worth being honest about: their conditions
(`staticEval + 500 <= alpha`, `staticEval - 300*depth >= beta`) are *harder* to
satisfy at a wide window, so part of the `!isPV` guard is belt and braces and
removing it will not release as much as the LMP half.
Difficulty: low. One parameter, four call sites.
Risk: medium. It lets LMP prune inside what is currently protected, though the
true PV line stays exempt by construction.
Why it is worth doing separately: it isolates "pruning enablement" from "window
narrowing", which are two mechanisms that change 1 bundles, and this repo's
one-variable rule for gates is what makes a bundled result uninterpretable. The
corrhist v2 entry (`search.hpp:428-438`) is the local precedent for paying an
extra gate to find out which half did the damage.

**3. Node types: `cutNode` plumbed through, and the reductions that key on it.**
After 1 and 2. Pass a `cutNode` boolean, set it as Stockfish does at each call
site (`sf:1022`, `sf:1380`, `sf:1395`, `sf:1430`), then add the cut-node
reduction increase (`sf:1335-1336`), the all-node scaling (`sf:1366-1367`), and
restrict null-move pruning to cut nodes (`sf:1012`), which we currently run at
every node including PV nodes (`search.cpp:731`).
Expected EBF effect: large. This is Stockfish's single biggest reduction
adjustment at about 3.9 plies, and it is eval-free, which is the class that has
worked here.
Difficulty: low once 1 and 2 exist, and near-impossible to reason about before
they do, because `cutNode` is meaningless without the null-window invariant.
Risk: low to medium.

**4. The missing full-depth null-window rung in the LMR re-search.** On a probe
fail high, re-search at `depth - 1` with `-alpha-1, -alpha` first, and only take
the wide window if that also beats alpha (`sf:1394-1395` against
`search.cpp:956-959`).
Expected EBF effect: moderate, and it comes free with change 1 in practice.
Difficulty: low, about five lines per core.
Risk: low. It is a strict narrowing and cannot change the returned score.

**5. Reduction inputs that are eval-free and already available.** `+r` when the
TT move is absent (`sf:1406-1407`), `+r` when the TT move is a capture
(`sf:1339-1340`), the linear `-moveCount` term (`sf:1331`), and a `cutoffCnt`
per ply (`sf:810`, `sf:1343-1344`, `sf:1537`).
Expected EBF effect: small individually, a few percent of tree each, and they
compound.
Difficulty: low. `cutoffCnt` needs one array in `SearchContext`.
Risk: low.
Caveat: each is worth well under 5 Elo on its own, which `GATES.md` says a
3,360-game gate cannot resolve. These have to be justified on `tools/treecost`
distributions and shipped as a block with one gate on the block, or not shipped.

**6. `ttPv` in the transposition table.** One bit, and it is free: `packData`
uses 56 of 64 bits and the spare byte is documented as headroom for exactly this
(`transposition_table.hpp:86-87`), so `TTEntry` stays 16 bytes and
`ENTRIES_PER_MB` does not move. Reduce less at nodes that were once part of a PV
(`sf:1169-1170`, `sf:1324-1326`).
Expected EBF effect: small to moderate. Stockfish's `ttPv` term is its second
largest, but most of the magnitude is the decrease at `sf:1324-1326`, which
protects accuracy rather than cutting tree.
Difficulty: low.
Risk: low.

**7. Aspiration: reduce the retry depth after repeated fail highs, and move both
bounds.** `sf:392-393`, `sf:422-423`, `sf:431-432` against `search.cpp:1339-1350`.
Expected EBF effect: small, root only.
Difficulty: low.
Risk: low.
**Say plainly:** this gap is real and is not worth a gate on its own. It is a
root-level effect and the instrument resolves about plus or minus 8 to 10 Elo.
Fold it into another change or justify it on tree size alone.

**8. Internal iterative reduction.** `sf:1056-1060`.
Expected EBF effect: unknown, and possibly zero, for the reason our IID gate
gave. Re-measure the fire rate after change 1 before writing it.
Difficulty: trivial, three lines.
Risk: low.

**9. Root move ordering by previous score.** `sf:347-352`, `sf:403` against
`search.cpp:1200-1210`.
Expected EBF effect: small. Root moves 2 to N are null-window scouted anyway, so
better ordering among them only pays when the best move changes.
Difficulty: low.
Risk: low.
**Say plainly:** real gap, low value, do it only while touching the root for
something else.

**10. Restricting the check extension.** Cap consecutive extensions on a path,
or refuse to extend a check with negative SEE. `search.cpp:598-600`, against
Stockfish having no check extension at all.
Expected EBF effect: moderate reduction in tree, since ours costs 9.2 percent of
nodes at bench 6.
Difficulty: low.
Risk: **high.** It trades directly against a measured +23.0 Elo [+13.3, +32.7],
and this is the shape of bet `razortight` lost. Do not do this until 1 through 5
are settled, and only then with a gate of its own.

**Not recommended.** Reopening singular extensions, in any form including
multi-cut and negative extensions: the probe needs depth 10 to be affordable
(`search_tuning.hpp:124-134`) and a gate that reaches depth 10 costs about 100
hours (`search.hpp:293-298`). Reopening `improving`, correction history, or
history-based reductions in their current form: all three read the evaluation or
a one-sided table, and the record is four consecutive nulls or losses. The
history route reopens only through `histMalus`, which is built, ungated, and is
the prerequisite for a two-sided `statScore` reduction (`sf:1350-1360`).

## Method note

Everything above is code reading against Stockfish master at `e52ea9ac8d4f`,
fetched as raw source rather than read as prose, which is what makes the line
citations exact on both sides. Two claims in this document are predictions and
are labelled as such: the fraction of our tree where `isPV` is true, and the
expected effect of change 1. Both are one `tools/treecost` run away from being
facts, and neither should be quoted until they are.
