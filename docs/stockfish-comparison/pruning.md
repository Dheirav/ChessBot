# Forward pruning, ours against Stockfish, 2026-09-11

Read-only comparison of every forward-pruning rule in this engine against
Stockfish master, commit `59aae690f91d6f69aac194f447d84b4a2c3be778`
(2026-09-09). Nothing was run to produce this: a timed gate was live on the
machine, so every number below is either read from source or quoted from
`docs/GATES.md`, `docs/BUGS.md` and the two research documents.

Reductions and extensions are out of scope except where a pruning rule reads
them, which in Stockfish is most of them.

## Read this first: the unit conversion

**Stockfish's internal scores are not centipawns.** `types.h:199-203` sets Pawn
208, Knight 781, Bishop 825, Rook 1276, Queen 2538. Our engine and the tables
below use pawn = 100. Every Stockfish margin quoted here is given raw and then
divided by 2.08 to put it on our scale. Comparing a Stockfish constant against
one of ours without that division overstates theirs by more than a factor of
two, and several of the differences below would look twice as bad as they are.

## The one-line summary

Our forward pruning is not missing rules so much as it is confined to the bottom
of the tree. Every rule we have that reads the evaluation fires only at
remaining depth 3 or less, and pruning that only fires at fixed shallow depth
changes the tree by a constant factor while leaving the effective branching
factor exactly where it was. The two mechanisms that act at interior nodes are
null move and late move reductions, and only one of them has been rebuilt.

---

## 1. The rule-by-rule table

Ours on the left, Stockfish on the right. Stockfish margins are given as
`raw (converted)`.

| rule | ours | file:line | Stockfish | file:line |
|---|---|---|---|---|
| **Reverse futility** | `staticEval - 300*depth >= beta`, `depth <= 3`, non-PV, not in check, no mate score. Returns `staticEval - 300*depth`. | `search.cpp:650-651`, `702-709` | `eval - futilityMargin >= beta` where `futilityMargin = min(45+4*depth, 85)*depth - (2789*improving + 335*opponentWorsening)*mult/1024 + abs(corr)/198435`, `depth < 19`, `!ss->ttPv`, `(!ttMove \|\| ttCapture)`. Multiplier drops 20 on a TT miss. Returns the blend `(661*beta + 363*eval)/1024`. | `search.cpp:995-1009` |
| | **300 cp per ply, capped at depth 3** | | **22 to 41 cp per ply, to depth 19.** At depth 3: 171 raw = **82 cp** against our **900 cp** | |
| **Razoring** | `staticEval + 500 <= alpha`, `depth <= 2`, non-PV. Drops to quiescence and **returns only if `qScore <= alpha`**, otherwise falls through and searches. `razorTight` offers 350 and gated −1.0. | `search.cpp:654-655`, `711-719`; toggle `search.hpp:309` | `eval < alpha - 482*depth`, non-PV, `!seekMate`, **no depth cap**. Returns the qsearch result unconditionally. | `search.cpp:990-993` |
| | **500 cp flat, depth <= 2** | | **232 cp per ply, every depth.** depth 2 = 964 raw = **464 cp**, depth 5 = **1159 cp** | |
| **Movecount / LMP** | `moveIndex > 3 + depth*depth`, `depth <= 2` (shipped `lmpShallow`; the ladder offers 1 and 3), non-PV, not in check, `move.flag == NORMAL`, `bestEval > -29000`, `hasNonPawnMaterial`. | `search.cpp:913-921`; `search_tuning.hpp:46,51` | `moveCount >= (3 + depth*depth) / (2 - improving)`, **no depth cap**, `!rootNode`, non-pawn material, `!is_loss(bestValue)`. Switches the move picker to skip quiets rather than `continue`-ing. | `search.cpp:1174-1178` |
| | **same formula, half the aggression, capped at depth 2.** depth 2 → from move 8 | | **depth 2 not improving → from move 3**; depth 8 → move 33 | |
| **Move-level futility (quiets)** | `staticEval + 300 + 150*depth <= alpha`, `depth <= 4`, non-PV, not in check, `NORMAL`, no mate score, `hasNonPawnMaterial`. **Off; measured inert.** | `search.cpp:659-661`, `902-911`; toggle `search.hpp:545` | `staticEval + 119*lmrDepth + 90*(staticEval > alpha) + 164 <= alpha`, `lmrDepth < 12`, where `lmrDepth = newDepth - r/1024 + history/lmrDivisor[d]`. Raises `bestValue` to the futility value instead of only skipping. | `search.cpp:1205-1232` |
| | **300 + 150/ply, depth <= 4.** At depth 4 = **900 cp** | | **79 + 57/ply, to lmrDepth 12.** At lmrDepth 4 = 640 raw = **308 cp**. History shifts the threshold per move | |
| **Move-level futility (captures)** | none  | n/a | `staticEval + 234 + 247*lmrDepth + PieceValue[captured] + 134*captHist/1024 <= alpha`, `lmrDepth < 8`, not giving check | `search.cpp:1188-1196` |
| **SEE pruning, captures** | none in the main search  | n/a | `!see_ge(move, -(177*depth + captHist*34/1024))` → prune. No depth cap. | `search.cpp:1198-1203` |
| | | | **−85 cp per ply.** depth 4 → prunes a capture losing more than 340 cp | |
| **SEE pruning, quiets** | none  | n/a | `!see_ge(move, -23*lmrDepth*lmrDepth)` → prune | `search.cpp:1234-1238` |
| | | | **−11 cp × lmrDepth².** lmrDepth 1 → prunes any quiet move that hangs anything; lmrDepth 5 → −133 cp | |
| **History pruning of quiets** | none  | n/a | `contHist[0] + contHist[1] + pawnHistory < -4136*depth` → prune. No depth cap. | `search.cpp:1207-1214` |
| **Null move** | `depth >= 3`, not in check, `hasNonPawnMaterial`. **`R = 2`, fixed.** No eval condition, no verification search, no consecutive-null guard, runs at PV nodes too. Returns `beta`. | `search.cpp:731-739` | `cutNode` only; entry `staticEval + 50*priorNMPFailHigh >= beta - 13*depth - 47*improving + 365`; `beta >= -2000`; `ply >= nmpMinPly`; non-pawn material. **`R = 7 + depth/3 + max((staticEval-beta)/256, 0)`, uncapped.** Verification search at `depth >= 16`, which sets `nmpMinPly = ply + 3*(depth-R)/4`. | `search.cpp:1011-1052` |
| | **R = 2 at depth 3 and at depth 20 alike** | | **R = 11 at depth 12, R = 13 at depth 18** | |
| **ProbCut** | **none**  | n/a | `probCutBeta = beta + 241 - 64*improving` (241 raw = **116 cp**), `depth >= 3`, skipped when `ttValue < probCutBeta`. Verified by qsearch, then by a search at `depth - (improving ? 5 : 3)`. Move filter is a SEE test at threshold `probCutBeta - staticEval`. Plus a TT-only variant at `beta + 428` (**206 cp**) when a lower-bound entry at `depth-4` already clears it. | `search.cpp:1062-1104`, `1108-1112`; `movepick.cpp:377-378` |
| **Quiescence: stand pat** | `standPat >= beta` → return `beta` (fail hard) | `search.cpp:380-385` | returns the blend `(441*bestValue + 583*beta)/1024`; ttValue replaces the stand-pat score when the bound allows | `search.cpp:1741-1743`, `1752-1762` |
| **Quiescence: delta** | `standPat + victim + 900 <= alpha` → skip. **Off.** 200 cp gated **−50.0**, 900 cp gated +7.1 then +0.9, both null. | `search.cpp:478-483`; `search_tuning.hpp:172`; toggle `search.hpp:50` | `futilityBase = staticEval + 306` (**147 cp**); skip when `futilityBase + PieceValue[captured] <= alpha`; then a dynamic SEE test at threshold `alpha - futilityBase`; then a flat floor `!see_ge(move, -74)` (**−36 cp**) | `search.cpp:1767`, `1794-1828` |
| **Quiescence: movecount** | none  | n/a | **`moveCount > 2` → skip**, for non-checking, non-promotion, non-recapture moves | `search.cpp:1801-1802` |
| **Quiescence: SEE** | `seeScore < 0` → skip. **Off**; +2.2 [−7.2, +11.6] on nodes, +4 [−7, +14] timed, cuts **41.1%** of nodes at bench 6 | `search.cpp:461-463`; toggle `search.hpp:34` | two thresholds as above: dynamic `alpha - futilityBase`, flat `-74` | `search.cpp:1817`, `1827` |
| **Quiescence: check evasions** | in check, generate **all** legal moves, no stand pat, no pruning | `search.cpp:381`, `387` | in check, `bestValue = futilityBase = -VALUE_INFINITE`, which disables the whole pruning block; evasion stages return every evasion | `search.cpp:1723-1724`; `movepick.cpp:362-375` |
| **Quiescence: depth cap** | `qDepth >= 8` → return static eval (`qBound`, on) | `search.cpp:362`; `search_tuning.hpp:40` | **no qsearch depth counter.** Only `ss->ply >= MAX_PLY` | `search.cpp:1702-1703` |
| **Quiescence: TT** | **none.** `quiescence()` does not take the table. | `search.cpp:350-352` | probes and cuts on `ttData.depth >= DEPTH_QS`, and uses ttValue as a better stand pat | `search.cpp:1716-1719`, `1741-1743` |
| **TT cutoff** | any node type including PV; `entry.depth >= depth`; EXACT always, LOWER on `>= beta`, UPPER on `<= alpha` | `search.cpp:611-613`; `transposition_table.cpp:22-58` | `!PvNode` only; `ttData.depth > depth - (ttData.value <= beta)`; bound must match the direction; refused when `cutNode != (ttData.value >= beta)` unless `depth > 4`; suppressed at `rule50_count() >= 96`; at `depth >= 7` confirmed by a one-ply do/probe/undo | `search.cpp:873-921` |
| **TT hit below depth** | **move only.** The score and its bound are discarded; `probe()` returns `false` and never exposes them. | `transposition_table.cpp:22-58` | the score is used everywhere: `ttData.eval` seeds the static eval and `ttData.value` **replaces** `eval` when the bound points the right way, so razoring and reverse futility run on a search result rather than on the evaluation. Also drives IIR, ProbCut's skip test, singular extensions, and four separate LMR terms. | `search.cpp:837-846`, `1059`, `1066`, `1254-1310`, `1325-1336` |

---

## 2. Why our shallow pruning cannot move the branching factor

This is the structural point the rest of the document rests on, and it is
arithmetic rather than opinion.

Write the tree as a product of per-ply effective widths, `N(d) = Π w_i`. Suppose
a pruning rule only fires at remaining depth 3 or less, and reduces each of
those three widths by a factor `f`. Then `N(d) = w^(d-3) · (wf)^3 = f³ · w^d`,
and `N(d+1)/N(d) = w`. **The rule multiplies the tree by a constant and leaves
the branching factor untouched**, however large `f` is.

The engine's own record is the proof. Razoring cut 21% of the tree at bench 6
and was worth +39.1 Elo. LMP cut **45.5%** at bench 6 and 41.4% at depth 8, and
was worth +13.1 then +15.0. Both are real gains and both fire at remaining depth
2 to 3, and across all of them the measured branching factor stayed at 2.3. It
moved only when the LMR table shipped, and the LMR table is the one rule whose
cut *grows with depth*: −2.9% at depth 5, −30.2% at depth 8, **−56.1% at depth
11** (`GATES.md`). That is the signature of an interior-node rule, and it took
the branching factor 2.3 → 2.0.

So there is a test that separates the two kinds of change, and this repo already
has half of it as the "two-depth rule":

> Measure the tree cut at two depths, say 8 and 11, with `tools/treecost`. If
> the fraction **grows** with depth, the change moves the branching factor. If
> it is **flat**, it is a constant factor on tree size and the branching factor
> will not move no matter how large it is.

Both kinds are worth having, because a constant factor of 4x is about two plies
at a branching factor of 1.87. But only the first kind closes a gap that
compounds, and the brief here is the branching factor.

Applying the test to our list: **every forward-pruning rule we have except null
move is a constant-factor rule.** Reverse futility caps at depth 3, razoring at
2, LMP at 2, move futility at 4 and inert, delta pruning and SEE pruning live in
quiescence. Null move and LMR are the only two interior mechanisms, and null
move still uses `R = 2`.

---

## 3. Why our margins are wide, quantified

Our evaluation's measured error against Stockfish at depth 16, over
`tests/evalerror`:

| set | n | median | 75th | 90th | mean abs |
|---|---|---|---|---|---|
| `ctl`, ordinary positions | 688 | **125** | 256 | **407** | 182 |
| `comp`, compensation positions | 363  | n/a | n/a  | n/a | **544** |
| all | 1 051  | n/a | n/a  | n/a | **307** |

`docs/RESEARCH-EVAL-GAP.md` has the split and the cause: 61% of the total error
mass is compensation blindness, from `KING_DANGER_SCALE = 0`.

Now put the two engines' margins side by side on our scale, per ply:

| rule | ours, cp/ply | Stockfish, cp/ply | ratio |
|---|---|---|---|
| reverse futility | 300 | 22 to 41 | **7 to 13x** |
| move-level futility | 150 (+300 base) | 57 (+79 base) | **2.6x** |
| razoring | 500 flat | 232 | **2.2x at depth 1, 0.5x at depth 5** |
| quiescence delta | 900 flat | 147 flat | **6x** |

The ratios are not arbitrary. The engine's own gate history is a direct
measurement of what happens when a margin is set inside the evaluation's noise:

- Delta pruning at **200 cp**, which is roughly textbook and roughly 1.6x the
  median error: **−50.0 Elo [−60.3, −39.7]**.
- The same rule at **900 cp**, outside the 90th percentile: +7.1, then +0.9 on
  a rebuild. Null, but not a disaster.
- Move futility at **100 + 100·depth**, roughly textbook: the tree **grew 4.2%**
  and four best moves changed across twelve positions. Over-pruning paid back in
  re-searches.
- The same rule at the shipped **300 + 150·depth**: the tree shrank 1.9% with
  zero best moves changed, which is about 0.03 plies. Inert.
- Razoring tightened from 500 to 350: **−1.0 [−10.0, +7.9]**.

That is a complete cost curve for one family, and it has no useful interior.
Textbook margins lose material amounts of Elo; margins wide enough to be safe
fire so rarely that they do nothing. **Move-level futility is not inert because
of a bad constant. It is inert because there is no constant that works**, and
the reason is the 125 cp median with a 407 cp tail.

The arithmetic that finishes it: if the margin has to be about 3x Stockfish's to
survive our error, then reverse futility at depth 8 needs 2 400 cp against
Stockfish's 296. Nothing fires at 2 400 cp. **That is the mechanism by which the
evaluation error confines the eval-based family to remaining depth 3, and
section 2 is why confinement to depth 3 means no branching-factor effect at
all.** The chain is real: eval error → wide margins → shallow depth caps →
constant factor only.

### The exception that says what to do instead

Razoring is the one member of that family that worked, at **+39.1 Elo**, the
largest accepted gain since the `threats` deletion. It is also the only one that
**verifies**: it does not act on the evaluation's word, it drops to quiescence
and returns only when quiescence agrees (`search.cpp:718`). Stockfish's razoring
does not even do that; ours is strictly safer than theirs.

That distinction generalises. A rule that compares one static evaluation against
a margin inherits the full 125/407 error distribution. A rule that runs a search
and compares *its* result against a margin inherits the error of a subtree
average, which is much smaller because the individual errors partly cancel.
Null move, razoring and ProbCut are all in the second class. Reverse futility,
move futility and delta pruning are all in the first.

**So the answer to a noisy evaluation is not a tighter margin. It is a
verification search.** That single observation reorders the list at the end of
this document.

---

## 4. How much of the branching-factor gap is the evaluation's fault?

This is the question the brief asked to answer explicitly, so here is the
answer with the reasoning and the caveats.

### First, a correction to the target number

`docs/RESEARCH-SPEED-AND-SEARCH.md` states Stockfish's branching factor as
"about 1.4". Stockfish's own wiki defines it as `exp(log(nodes)/rootDepth)` over
a whole iterative deepening run, and by that definition Stockfish 19 from the
start position is **2.38 at depth 10**, 1.90 at depth 20, 1.68 at depth 30 and
1.49 at depth 50. It is depth-dependent, not an engine constant.

The repo's own kiwipete measurement is the right comparison because it is the
same position on the same machine, and it holds up as a *marginal* ratio:
Stockfish goes 16 151 → 28 166 → 31 653 over depths 11 to 13, which is ×1.74
then ×1.12; we go 5 681 796 → 11 129 240 → 20 689 916, which is ×1.96 then
×1.86. So "roughly 1.4 against roughly 1.9" is fair over that depth range, and
should be quoted as a marginal ratio at depth 11 to 13 rather than as
"Stockfish's branching factor".

The decomposition of the 352x node gap at depth 11 then works out as: the
branching-factor ratio contributes `(1.9/1.4)^11 ≈ 50x`, and the remaining
**7x** is the constant factor, of which the repo has already identified 4.3x as
the quiescence base (791 nodes against 185 at depth 1). Both halves matter. The
branching-factor half is the one that gets worse every ply.

### The attribution

Take Stockfish's interior-node width reducers, and classify each by whether it
reads a static evaluation:

| Stockfish mechanism | reads static eval? | our state | blocked by |
|---|---|---|---|
| LMR with history, ttPv and cutNode terms | partly | structural core shipped (+26.4); history terms absent | one-sided history table |
| null move, `R = 7 + depth/3` | **no** (the `depth/3` core) | `R = 2` fixed | **nothing** |
| movecount pruning at all depths | only via `improving` | capped at depth 2 | move ordering quality |
| SEE pruning of quiets and captures | **no**, material only | absent | **nothing** |
| continuation-history pruning | **no** | absent | one-sided history table |
| reverse futility to depth 19 | **yes** | capped at depth 3 | **evaluation error** |
| move-level futility to lmrDepth 12 | **yes** | inert | **evaluation error** |
| ProbCut | barely; search-verified | absent | **nothing** |
| razoring at all depths | yes, but search-verified | capped at depth 2 | partly evaluation error |
| IIR (`depth--` with no TT move) | **no** | absent | **nothing** |

Two of ten are hard-blocked by the evaluation. Two more are partly blocked but
recoverable because they verify. **Six are not blocked by the evaluation at
all**, and of those six, four are blocked by nothing whatsoever and two are
blocked by a history table that only ever adds, which `histMalus` already exists
to fix (`move_ordering.cpp:198-205`, toggle `search.hpp:623`, ungated).

Weighting those ten by my rough sense of how much interior width each removes in
Stockfish, I put the evaluation-attributable share of the branching-factor gap
at **roughly 25 to 35 percent**. That is an estimate from the classification
above, not a measurement, and I have flagged it as one. What is not an estimate
is the direction: **the majority of the gap sits behind mechanisms that read
material, move counts, history and search results, none of which our evaluation
touches.**

### The record already said this and was read one shade too pessimistically

`RESEARCH-SPEED-AND-SEARCH.md` sorts recent results by whether the technique
consults the evaluation and concludes that pruning here is capped by evaluation
quality. Sorted the other way, the same table says something more useful:

| technique | reads the eval? | result |
|---|---|---|
| SEE-split capture ordering | no | **+25.6** |
| check extensions | no | **+23.0** |
| LMR table | no | **+26.4** |
| LMP, then lmpShallow | no | **+13.1, +15.0** |
| razoring (verified) | yes, verified | **+39.1** |
| reverse futility | yes | +18.4 |
| `improving` | yes | −10.6 |
| correction history | yes | closed over three gates |
| `razortight` | yes | −1.0 |
| move futility | yes | inert |
| delta pruning at 200 cp | yes | **−50.0** |

Every unqualified win came from a rule that does not read the evaluation, or
from the one eval-based rule that verifies. That is the finding, and it is
correct. The pessimistic reading is "we are capped by the evaluation". The
accurate reading is **"we are out of eval-based pruning, and we have barely
started on the eval-free kind"**. `TODO.md:141` calls the search pruning suite
"largely done"; on the evidence above it is largely done on exactly the half
that cannot help.

### What the evaluation genuinely blocks

To be fair to the pessimistic reading, these are dead until the evaluation
improves, and no amount of tuning will revive them:

- Move-level futility for quiets and for captures. Measured inert at a safe
  margin and net-negative at a textbook one.
- Delta pruning in quiescence. Measured −50.0 at a textbook margin and null at a
  safe one, twice.
- Reverse futility beyond about remaining depth 4. The margin would have to be
  1 200 cp and up.
- `improving` as an input to anything, and the whole correction-history family.

Between them that is real, and it is roughly the last stretch of the gap, the
part that separates about 1.6 from about 1.4. It is not the part that separates
1.87 from 1.6.

---

## 5. Defects and oddities found while reading

Not recommendations, just things that are wrong or surprising in the code as it
stands.

1. **The null-move comment describes a verification search that does not
   exist.** `search.cpp:725-726` says "the verification search runs at reduced
   depth with a null window, so it is cheap", which describes the null search
   itself. There is no verification of the null result anywhere. That is
   defensible at `R = 2` and would not be at `R = 6`.

2. **Null move has no consecutive-null guard.** The child of a null move may
   null again immediately, which searches the same position two plies deeper for
   nothing. Stockfish blocks this with `nmpMinPly` (`search.cpp:1043`).

3. **Null move runs at PV nodes.** There is no `isPV` guard at
   `search.cpp:731`. The window passed down is null so the search itself is a
   scout, but Stockfish restricts null move to `cutNode` only.

4. **`moveIndex > 0` in the move-futility guard is inert.** `++moveIndex` runs
   at `search.cpp:847` before the test at `search.cpp:905`, so the first move
   has `moveIndex == 1` and passes. The comment at `search.cpp:899-901` claims
   this exempts the first move; it does not. The first move is in fact protected,
   but by `bestEval > -MATE_SCORE + 1000` at `search.cpp:906`, since `bestEval`
   starts at `-INF = -32000` which is below `-29000`. Correct behaviour, wrong
   explanation.

5. **The TT cutoff has no PV guard** (`search.cpp:611`). Stockfish requires
   `!PvNode` (`search.cpp:873`). Ours truncates the principal variation on a
   table hit. This saves nodes rather than costing them, so it is not a
   branching-factor issue, but it is a difference worth knowing about when
   reading PV output.

6. **`probe()` throws away the score on an insufficient-depth hit**
   (`transposition_table.cpp:56-57`). It returns `false` and the caller sees
   only the move. The entry's score and bound are a search result about this
   exact position and are strictly more accurate than `evaluate()`, and they are
   discarded. See recommendation 3.

7. **Quiescence never touches the transposition table** (`search.cpp:350-352`
   takes no `tt` argument). Stockfish cuts in qsearch on `ttData.depth >=
   DEPTH_QS` and uses ttValue as a better stand-pat.

8. **`see()` already works on quiet moves.** `SEE_VALUE[NONE] = 0`
   (`see.cpp:18-25`), so `see(board, quietMove)` returns 0 for a safe move and
   the loss for one that hangs. SEE pruning of quiets needs no new evaluation
   code, only a call site.

---

## 6. Ranked changes

Ranked by expected branching-factor effect per unit of risk. "Blocked" means the
evaluation's error prevents it and no constant fixes that.

### 1. Depth-scale the null-move reduction, and add a verification search

**Not blocked.** `search.cpp:732` is `const int R = 2` and has been since the
search was written. Stockfish uses `R = 7 + depth/3 + max((staticEval-beta)/256,
0)`. At depth 12 that is 11 against our 2.

This is the same defect the LMR table just fixed, in the other interior-node
mechanism. `search.hpp:456-462` states it plainly for LMR: "the thirtieth move at
depth 12 is reduced exactly as much as the fourth at depth 3, and that is the
main reason the effective branching factor sits at 2.3". `R = 2` at depth 12 is
the identical mistake, and it is now the only unfixed one.

Take the `depth/3` core, which is structural and reads nothing, and leave
Stockfish's eval bonus term out of the first version. Something like
`R = 2 + depth/4`, clamped so `depth - 1 - R >= 0`, gives R = 3 at depth 6,
4 at depth 8, 5 at depth 12. That is conservative next to Stockfish and is the
right first step given the zugzwang risk.

- **Expected EBF effect:** large. The null search at depth 12 currently costs a
  depth-9 subtree at every qualifying node; at R = 5 it costs depth-6, which is
  roughly a factor of 4 less at that node. My estimate is a 15 to 30 percent
  tree cut at depth 11, **growing with depth**, which is the signature that
  matters. 1.87 → somewhere around 1.75 to 1.80.
- **Difficulty:** small for the R formula, medium with verification.
- **Risk:** zugzwang, and it is the real risk. A deeper null with no
  verification is the classic way to lose endgames that self-play at gate depth
  will not show. Mitigate with the `hasNonPawnMaterial` guard we already have
  plus a verification search: on a null fail-high at `depth >= 8`, re-search at
  `depth - R` without null and only cut if that agrees.
- **How to measure:** `tools/treecost` at depths 8 and 11 first, to confirm the
  cut fraction grows. Then gate at `-N 1000000`, not `-N 100000`: `GATES.md`
  records the LMR table reading +2.7 [−10.2, +15.6] at the smaller budget
  because depth 5 could not see it, and this feature has the same depth
  sensitivity.

### 2. SEE pruning of quiet moves and losing captures in the main search

**Not blocked.** SEE reads material on one square. It has nothing to do with the
evaluation's 125 cp median error, which is entirely positional.

We have `see()` (`see.cpp:115`), it already runs at every quiescence node for
ordering, and it already handles quiet moves. Stockfish prunes quiets at
`!see_ge(move, -23*lmrDepth²)` and captures at `!see_ge(move, -177*depth)`,
both with no depth cap.

Start with the quiet-move half, guarded exactly like LMP is (non-PV, not in
check, `NORMAL`, `bestEval` not a mate score, `hasNonPawnMaterial`), at a
threshold on our scale of about `-15 * depth²`, which is a little looser than
Stockfish's `-11 * lmrDepth²` because our ordering is worse and our `depth` is
not history-adjusted.

- **Expected EBF effect:** moderate, and it acts at every depth, so it is the
  right kind. Estimate 1.87 → around 1.80 to 1.84, plus a constant-factor cut.
- **Difficulty:** small. One call site, one constant, one toggle.
- **Risk:** moderate. SEE is blind to pins, deflections and in-between moves
  (`see.hpp:21-25`), so a quiet move that "hangs" a piece may be a sound
  sacrifice. The depth-scaled threshold is what bounds that, and it is why the
  threshold must be near zero at depth 1 and loose at depth 6.
- **Cost note:** `see()` is not free and would now run on quiet moves, so
  measure wall time as well as nodes.

### 3. Use the transposition table's score in place of the static evaluation

**This is the one change that attacks the evaluation error without touching the
evaluation.** Stockfish does it at `search.cpp:841-846`: when a TT entry's bound
points the right way, `ttData.value` replaces `eval`, and razoring and reverse
futility then run on a search result instead of on the network's output.

A TT score at depth `k` is worth far more than a static evaluation. Ours is a
depth-16 comparison away from Stockfish by 125 cp median; a depth-4 search
result from our own table is much closer to our own depth-12 answer than our
`evaluate()` is. Every eval-based margin could be narrower on the nodes where an
entry exists.

The obstacle is our API, not our data. `probe()` returns `false` on an
insufficient-depth hit and never exposes the score or the bound
(`transposition_table.cpp:56-57`). `peek()` exists and the singular-extension
code already uses it (`search.cpp:810-815`), so the pattern is there.

- **Expected EBF effect:** indirect but potentially large, because it is what
  makes items 6 and 7 below viable at all.
- **Difficulty:** small. No change to `TTEntry`, so **the 16-byte layout and the
  461,727 bench signature are safe** on the API change itself; the pruning
  decisions it enables will move the signature.
- **Risk:** low. It only ever replaces a noisy number with a less noisy one.
- **Caveat worth measuring first:** how often does a node that reaches reverse
  futility or razoring have a usable entry at all? If the answer is "rarely",
  this buys nothing. That is a cheap instrumented count, not a gate.

### 4. Fix the quiescence base: `seePruning` on, a TT probe, and a movecount cap

**Not blocked**, and this is a constant factor rather than a branching-factor
change, which is why it is fourth rather than first. But the constant is 4.3x
(791 nodes against 185 at depth 1), and 4.3x is about 2.2 plies at a branching
factor of 1.87. That is worth more than most features.

Three parts, and they should be three gates:

- **`seePruning` on.** Already built (`search.cpp:461-463`), cuts **41.1%** of
  nodes at bench 6, and is off for a documented and correct reason: it was gated
  on a node budget, which pays both sides the same nodes and so divides out
  exactly what it buys (`search.hpp:26-32`). The timed gate that was supposed to
  settle it ran at `-t 100` and returned +4 [−7, +14], which is underpowered
  rather than null. A `--tc 10+0.1` gate on the faster harness built for Lazy
  SMP would answer it properly.
- **A TT probe in quiescence.** Stockfish cuts on `ttData.depth >= DEPTH_QS`.
  Ours has no table access at all. Most quiescence nodes are transpositions of
  each other.
- **Stockfish's `moveCount > 2` cap** (`search.cpp:1801-1802`), applied to
  non-checking, non-promotion, non-recapture moves once `bestValue` is not a
  loss. This is eval-free and brutal, and it is a large part of why their
  depth-1 tree is 185 nodes.

- **Difficulty:** small for the first, medium for the other two.
- **Risk:** moderate for the movecount cap, which is the most aggressive rule in
  Stockfish's quiescence. Low for the other two.

### 5. Internal iterative reduction

**Not blocked.** Stockfish, `search.cpp:1059`: `depth--` when `depth >= 6` and
there is no TT move. That is all of it.

We tried IID (`search.hpp:116-140`) and it gated −0.1 [−4.9, +4.7], a genuine
and tight null, for a structural reason: iterative deepening means nodes at
remaining depth >= 5 were already visited and are already in the table, so IID
never fires. **IIR does not share that reason.** IID pays for an extra search to
produce an ordering move; IIR pays nothing and simply reduces a node it cannot
order. The null on IID is not evidence against IIR.

- **Expected EBF effect:** small but at every interior depth, so it is the right
  kind. A few percent.
- **Difficulty:** trivial. One line and a toggle.
- **Risk:** low.

### 6. ProbCut

**Barely blocked, and the depth objection is wrong.** `TODO.md:141` lists
probcut as "untried, hits the depth wall", on the reasoning in `BUGS.md` 19 that
features firing above about remaining depth 8 cannot be gated here. Stockfish's
condition is **`depth >= 3`** (`search.cpp:1064`), not 5 and not 10, so it fires
throughout a `-N 100000` gate's depth 5 to 7 range. The depth wall does not
apply.

More to the point for this engine: ProbCut is the fail-high counterpart of
razoring, and it verifies the same way. Razoring says "so far below alpha that
only tactics could save it, so ask quiescence"; ProbCut says "a capture might be
so far above beta that it cuts, so ask a reduced search". Neither trusts the
static evaluation with the decision. Given section 3's finding that verification
is the answer to a noisy evaluation, ProbCut is a better fit here than in an
engine with a good evaluation, not a worse one.

Size the margin off our error rather than Stockfish's 116 cp: something around
400 to 500 cp over beta, the same reasoning that put razoring at 500.

- **Expected EBF effect:** moderate, at interior depths.
- **Difficulty:** medium. Needs a capture-only move loop at a SEE threshold and
  a nested reduced search, and it must not pollute the table for this node.
- **Risk:** moderate. A wrong probcut returns a fail-high that did not happen.

### 7. Two-sided history, then history-driven pruning and reduction

**Not blocked by the evaluation, blocked by our own history table.**
`updateHistory` is called from one site, the beta cutoff, and only adds
(`move_ordering.cpp:149-160`). Every entry is zero or positive, so a reduction
reading it can reduce a good move less but never a bad move more, and the
savings are entirely in the second half. Measured across four divisors at depth
11: +7.0%, +1.6%, +17.8%, +26.6%. **Every setting grows the tree.**
`histReduction` is not a tuning failure, it is structurally dead.

`histMalus` is already written (`move_ordering.cpp:198-205`) and ungated
(`search.hpp:623`). It changes move ordering, so it needs its own gate before
anything is built on it. Once it lands, two Stockfish rules become available and
neither reads the evaluation:

- History pruning of quiets, `history < -4136*depth` (`search.cpp:1207-1214`).
- History folded into `lmrDepth` before the futility and SEE thresholds
  (`search.cpp:1216-1218`), which is how Stockfish makes one margin behave
  differently for a move with a bad record.

- **Expected EBF effect:** moderate to large, at all depths.
- **Difficulty:** medium, and it is two or three gates rather than one.
- **Risk:** moderate. `conthist` and `capthist` both gated null here as
  *ordering* devices (+6.8 and +2.7), so there is a real chance our history
  signal is simply too weak to prune on. But those measured ordering; this is
  the reduction and pruning consumer, which is a different mechanism and is
  untested here.

### 8. Do not extend movecount pruning deeper yet

Stockfish has no depth cap on movecount pruning and prunes about twice as early
at the same depth. Copying that looks obvious and the local evidence points the
other way: depth 3 → 2 was **+15.0** and depth 2 → 1 was **−31.3**. The measured
optimum for our engine is 2, which is far shallower than Stockfish's unlimited.

The blocker is move ordering, not the evaluation. Movecount pruning is a bet
that the ordering has already put the best move above the cut, and Stockfish can
bet at move 33 at depth 8 because continuation history makes that bet safe.
Ours cannot, and `conthist` gating null (+6.8 [−6.1, +19.8]) as an ordering
device is the measurement that says so. **Revisit after item 7 lands**, and not
before.

### Blocked behind evaluation accuracy, explicitly

These cannot be recovered by tuning and should not be re-attempted until the
evaluation improves. In each case the repo has already paid for the lesson.

| item | evidence |
|---|---|
| Move-level futility at any depth (`moveFutility`) | inert at a safe margin, +4.2% tree at a textbook one |
| Capture futility at the move level | same arithmetic, never built |
| Delta pruning in quiescence (`deltaPruning`) | −50.0 at 200 cp, null at 900 cp, twice |
| Reverse futility beyond remaining depth 4 | the margin would be 1 200 cp and up |
| Tightening razoring below 500 (`razorTight`) | −1.0 [−10.0, +7.9] |
| `improving` as an input to anything | −10.6 [−31.9, +10.7] |
| Correction history (`corrHist`, `corrHistQ`) | closed over three gates; the quiescence half alone cost −39.7 |

The route that unblocks this list is `docs/RESEARCH-EVAL-GAP.md`'s, not a search
change: king safety measured on a gauntlet rather than on self-play, worth 61%
of the error mass, and then tapering, worth the 146 → 230 cp phase drift. Note
that `BUGS.md` 20 is the standing warning about that route: three accurate
evaluation fits all cost more depth than they bought, because a more accurate
evaluation moves its score more between iterations and the aspiration window
misses more often. Any evaluation improvement has to be cheaper per node, not
merely more correct, and it invalidates every margin in section 1 the moment it
lands.

---

## 7. Measurement notes for whoever does this

- **Use `tools/treecost`, not `tests/bench`.** Twelve positions cannot resolve
  anything below about 10 percent: `improving` read +1.7% in aggregate at depth
  11 while eight of the twelve positions shrank, because one position swung
  754 000 nodes (`BUGS.md` 21, `GATES.md`). `treecost` compares pairwise and
  reports a median ratio with quartiles.
- **Measure at two depths.** Section 2's test: a cut fraction that grows with
  depth is a branching-factor change, a flat one is a constant factor. Both are
  worth having and they need different gates.
- **Gate the interior-node items at `-N 1000000`, not `-N 100000`.** The
  standing budget reaches depth 5 to 7 and the LMR table read +2.7 [−10.2,
  +15.6] there against +26.4 at the larger budget, because at depth 5 it
  differed from the shipped code by 2.9% of the tree. Null move R scaling and
  SEE pruning have the same depth sensitivity.
- **`tests/evalref` and `tests/bench` will fail on all of these, by design.**
  Read the diff before `make evalref-regen` / `make bench-regen`. The mirror
  symmetry half of `evalref` has no reference file and regenerating cannot
  silence it.
- **Check `pgrep -f tests/match` before `make tests`.**
