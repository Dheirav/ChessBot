# Evaluation against Stockfish, and whether it is what caps the pruning

2026-09-11. Read-only study: a timed match was running on the machine
(`tests/match --tc 10+0.1`, the `BitboardCore` toggle from 6ce151b), so nothing
here comes from a build, a bench, a gate or a run of `tests/evalerror`. Every
number is either read out of the source, quoted from a recorded measurement in
this repo, or computed from `tests/data/evalerr.epd` by counting FEN characters.
Where a number is derived rather than measured I say so.

---

## 1. The answer to the central question

**Does evaluation error force the search into wide pruning margins? Yes, and it
is measured three separate times. But the wide margins are not what holds the
branching factor at 1.87, and that distinction is the whole finding.**

The chain has two links and only the first one holds.

**Link one, eval error sets the margins: confirmed, three times.**
`search.cpp:641-655` does not merely use wide margins, it derives them from the
measured error and says so in the code. Three margins have been probed against
that derivation:

| margin | textbook | here | what happened when it was set to the textbook value |
|---|---|---|---|
| delta pruning (quiescence) | ~200 | **900** (`search_tuning.hpp:172`) | **−50.0 Elo [−60.3, −39.7]**, 3 360 games, all twelve shards negative (`search.cpp:35-39`) |
| razoring | 100 to 150 | **500** (`search.cpp:654`) | 350 measured **−1.0 [−10.0, +7.9]** over 3 360 games (`GATES.md:504-514`) |
| move-level futility | 100 + 100·d | **300 + 150·d** (`search.cpp:659-660`) | at 100 + 100·d the tree **grew 4.2%** and four best moves changed (`search.hpp:536-538`) |

The third row is the cleanest statement of the problem and it is the one worth
carrying. Move-level futility at this engine's margin is **inert**: −1.9% of the
tree at depth 11, zero best moves changed across twelve positions, about 0.03
plies (`search.hpp:528-531`). Tighten it to the standard value and it
**over-prunes**, growing the tree by 4.2% because the re-searches cost more than
the skipped nodes saved. There is no setting in between that both fires often
enough to matter and is safe. **The window is empty**, and it is empty because
the evaluation's p90 error of 407cp is wider than the whole useful range of the
margin. That is evaluation error blocking a pruning technique outright, measured
directly rather than inferred.

**Link two, wide margins hold the branching factor high: not supported.** The
razoring margin has 150cp of slack that is worth nothing in either direction:
tightening it from 500 to 350 prunes considerably more at depth ≤ 2 and measures
−1.0 with a properly spread pentanomial, which is a real null rather than a gate
that failed to fire. If the wide margin were costing branching factor, closing
30% of it would show something.

The reason is structural. Razoring fires only at `depth <= 2` and reverse
futility only at `depth <= 3` (`search.cpp:651`, `655`). Those are leaf-adjacent
nodes. Branching factor is set at **interior** nodes, by null move, LMR and late
move pruning, and of those three the only one that consults a static evaluation
anywhere in a strong engine is null move, which here does not consult it at all:

    if (g_searchOptions.nullMove && depth >= 3 && ...) {
        const int R = 2;                        // search.cpp:732

A fixed reduction, no depth term, no eval term. And late move pruning is capped
at `LMP_MAX_DEPTH = 3` (`search_tuning.hpp:46`), so it never fires above depth 3
at all.

Those are the same shape of defect as the one that was already fixed and paid.
LMR was `const int R = 1` until the log table shipped, and that change was worth
**+26.4 Elo [+10.4, +42.6]** and took the branching factor from 2.3 to 2.0
(`HANDOFF.md:431-434`). It is also the one recent large win that never reads the
evaluation, which is the pattern `RESEARCH-SPEED-AND-SEARCH.md:100-115` already
noticed. Two constants of exactly that shape are still standing, in the two
remaining interior-node pruners, and no evaluation improvement would relax
either of them.

**And the coupling that is measured runs the other way.** `BUGS.md` 20 records
three independent attempts to make this evaluation *more accurate*, each of
which succeeded at accuracy and was cancelled by node cost: +59%, +32.5% and
+22.7% nodes. The mechanism is measured too: the tuned build missed the ±50
aspiration window on 22% of iterations against the shipped build's 14%, and each
miss is a re-search. So in this engine accuracy is currently a **node cost**
through the aspiration window, not a pruning enabler through the margins.

Put together: improve the evaluation because the moves it picks are wrong, not
because tighter margins are waiting behind it. The branching factor is held by
two fixed constants that have nothing to do with the evaluation.

*A note on the 1.87 figure.* The number this study was handed is 1.87 against
Stockfish's 1.4. The most recent branching factor recorded in this repo is
**2.0**, measured on kiwipete after the LMR table shipped, down from 2.3 before
it (`RESEARCH-SPEED-AND-SEARCH.md:37-38`), so 1.87 is either newer or measured
differently and I have not reproduced it. The arithmetic is worth having either
way: at twelve plies, `(1.87/1.4)^12` is about **32x** the nodes and
`(2.0/1.4)^12` is about **73x**, against a directly measured node ratio of 352x
to 654x at depths 11 to 13. The remainder is the quiescence base, which is 4.3x
larger than Stockfish's at depth 1 because `generateCaptures` calls
`generateLegalMoves` and discards the quiet moves. So the gap decomposes as
roughly branching factor times base, and neither factor is the evaluation.

### 1.1 What error level would permit materially tighter margins

The code states the relation it used: the margin is set at roughly the p90 of
the evaluation's error, 300 against a measured 407, and 500 at what it calls the
p95 (`search.cpp:650-654`). Inverting that gives the target directly.

To use the standard margins, which is what `moveFutility` needs at 100 + 100·d
and what would make it a real technique rather than an inert one:

| statistic of `ctl` error | now | needed for textbook margins | factor |
|---|---|---|---|
| median | 125 | ~45 | 2.8x |
| p90 | **407** | **~150** | **2.7x** |
| mean absolute | 181.9 | ~70 | 2.6x |

**So the answer is roughly a 2.7x reduction in the p90 of ordinary-position
error.** Note that it is the tail and not the mean that sets a margin: the
distribution is heavily skewed, with a mean of 182 against a median of 125 and a
p90 of 407, so halving the mean would not halve the margin. A change that
removes a few large errors is worth more here than one that shaves every score.

### 1.2 The measurement that decides whether any of this is reachable, and which has never been run

The 125 / 407 figures are the error of the **static** evaluation against
**Stockfish at depth 16**. Razoring replaces a depth-2 search and move futility a
depth-4 one. The quantity that actually governs those decisions is
`|static − truth(depth 2)|`, and it is strictly smaller than
`|static − truth(depth 16)|`, because part of the depth-16 number is tactics that
no static evaluation of any quality can contain.

That gap has been measured on one subset and it is enormous.
`tests/README.md:133-140` records that **Stockfish's own depth-1 evaluation sits
282cp from its depth-16 evaluation** over the `comp` positions, against our 572.
More than half of `comp` error is dynamics, and no evaluation change can remove
it.

**The same number for `ctl` has never been measured**, and it is the number that
decides whether the 2.7x above is reachable or impossible. If the `ctl` floor is
of the same order, then a p90 of 150 is below the floor, the standard margins can
never be safe in this engine whatever is done to the evaluation, and this whole
line of work closes. If the floor is small on quiet ordinary positions, the 2.7x
is a real target.

`tools/sf-label.py` already takes a `--depth` argument. Running it at depth 1 and
depth 2 over the 688 `ctl` FENs is minutes of Stockfish at trivial depth, not the
~100-minute depth-12 sweep its docstring describes. **This is the cheapest
high-value measurement available anywhere in this area and it should precede
every item in section 7.**

### 1.3 What Stockfish's own margins say about the same question

Section 5.1 has the full comparison with units and citations. Two rows from it
belong here because they settle both links.

**On link one.** Modern Stockfish's reverse-futility margin is **15 to 28
centipawns per ply** (`master/src/search.cpp:1001-1003`, 45 to 85 internal units
divided by 3.04), and Stockfish 11's was **102 cp per ply**. This engine's is
**300 cp per ply**. Stockfish's modern margin is about a fifth of this engine's
*median* evaluation error and a twentieth of its p90. That is what an evaluation
worth pruning on buys, stated as a number, and it confirms the first link about
as directly as it can be confirmed.

**On link two.** Stockfish's move-count pruning keeps **2, 3 and 6** quiet moves
at depths 1, 2 and 3 and applies the rule at **every depth**, in both SF11 and
master. This engine keeps **5, 8 and 13** and stops at depth 3. Roughly twice as
permissive where it fires, and absent entirely above depth 3. Stockfish's own
in-source Elo annotations rank its shallow-pruning block at **~200 Elo**, its
reverse futility at **~50** and its razoring at **~1** (`sf_11/src/search.cpp`,
step headers). Neither the move-count schedule nor its depth coverage has
anything to do with evaluation accuracy, and by Stockfish's own ranking that is
where the strength is.

One more piece of Stockfish's history bears on link two directly. Razoring was
**deleted** from Stockfish on 2020-12-31, four months after NNUE landed, with the
entire commit message being "has become ineffective now" (`8ec97d161e`). A
dramatically better evaluation made the razoring bet worthless rather than
tighter. That is a specific warning against expecting a better evaluation here to
convert through the razoring margin, and it agrees with `razortight`'s null.

---

## 2. What this evaluation is, read out of the source

`src/engine/evaluation.cpp` is 926 lines and sums exactly 21 terms into
`e.total` at `evaluation.cpp:842`: material, mobility, king safety, centre
control, bishop pair, doubled, isolated, passed, backward, connected and chained
pawns, rook on open file, rook on semi-open file, rook on the seventh, PST,
outpost, trapped pieces, king activity, threats, undefended pieces and space.
`tools/tune` reaches 476 parameters across them (`tools/tune.cpp:66`), of which
432 are PST entries.

Several of those terms are weaker than their names suggest, and three of them are
not doing what the name says at all.

**`drawish` is dead code, twice over.** It is computed at
`evaluation.cpp:817-821` under the condition
`whiteMaterial == 0 && blackMaterial == 0`, but `whiteMaterial` accumulates
`pieceValues[p.type()]` for every piece including the king
(`evaluation.cpp:529`), and `pieceValues[KING]` is 20000
(`evaluation.cpp:15-17`), so the condition is unreachable in any legal position.
It is then excluded from `e.total` anyway, deliberately, as a diagnostic
(`evaluation.cpp:864-865`). **There is no drawishness detection and no scale
factor of any kind in this evaluation.** That matters for section 5.

**`trapped` charges a flat 5 for any rook or bishop on any edge square**
(`evaluation.cpp:691-697`). A rook on an open a-file, a rook on the eighth rank
and a fianchettoed bishop on g2 are all "trapped". It measures being on the rim,
not being trapped, and the two are close to opposite for a rook.

**`outpost` has no outpost condition** (`evaluation.cpp:673-688`). It pays 10 for
any knight or bishop past the halfway line that has a friendly pawn behind it on
a diagonal. The defining property of an outpost, that no enemy pawn can ever
challenge the square, is not tested.

**`space` has no weight and counts pieces, not squares**
(`evaluation.cpp:807-815`). `spaceScore = whiteSpace - blackSpace`, one
centipawn per piece standing in the enemy half. It is the only term in the file
with no named weight.

**`centreControl` is occupancy, not control** (`evaluation.cpp:654-662`). Five
centipawns for standing on d4/e4/d5/e5, with no distinction between a pawn and a
queen and no credit for attacking the square from a distance.

**`backward` flags most pawns.** The test is
`(span3(ownPawnFile, file) & ranksBehind(rank)) == 0` at
`evaluation.cpp:540`, which is "no friendly pawn on this or an adjacent file
behind me". In the starting position that is true of all sixteen pawns, so both
sides carry −64 and the term nets zero. In general it fires on the rearmost pawn
of every pawn island rather than on backward pawns, and backwardness proper needs
the square in front to be controlled by an enemy pawn, which is not tested.

**Mobility counts pawn pushes and promotions.** `countMobility` calls
`countPseudoLegalMoves` over the whole side (`evaluation.cpp:138`), and that
generator emits pawn moves including one Move per promotion piece
(`movegen.cpp:181-187`). A pawn on the seventh with two capturable pieces beside
it therefore scores 3 squares × 4 promotion pieces = 12 "mobility", 24
centipawns, for one pawn. Mobility is also a flat 2 per move for every piece
type, over unrestricted squares, so a queen sitting on a square attacked by a
pawn is credited for every square it will have to abandon.

**`threats` accumulates per (attacker, target) pair with no cap and no SEE**
(`evaluation.cpp:756-784`), plus a `captureIncentive` of 10% of the value gap.
Three attackers on one knight score three times. `see()` exists, is unit-tested
and gated at +25.6 Elo for move ordering, and the evaluation does not call it
(`ROADMAP.md:299-300`).

**The evaluation is blind to the side to move.** `evaluate_details()` reads
`board.squares[]` and nothing else, which is stated as the correctness argument
for the eval cache at `evaluation.cpp:877-882`. The same position with White to
move and with Black to move gets the same score. There is no tempo term; one was
removed on 2026-08-14 because it was a constant rather than a bonus to the mover
and it was a `float` that truncated the whole sum (`evaluation.cpp:834-838`).
Stockfish 11 carries a tempo bonus of `Value(28)` internal units, which is 13cp
on its scale (`sf_11/src/evaluate.h:32`), added after the side-to-move flip so
that its evaluation is exactly antisymmetric apart from it.

For what it is worth, the port at `src/engine/bb_evaluation.cpp` computes
bit-identical integers 2.17x faster (0.52 M/s to 1.13 M/s,
`docs/BITBOARD-REPLACEMENT.md:87`). It changes none of the above: it is the same
21 terms with the same constants.

---

## 3. Tapering

**Question three has a sharper answer than "only the king".**

`gamePhaseFactor` is computed once, at `evaluation.cpp:707`:

    int totalMaterial = whiteMaterial + blackMaterial - pieceValues[KING]*2;
    gamePhaseFactor = std::min(1.0f, totalMaterial / 3200.0f);

and used in exactly three places: king danger (`evaluation.cpp:726`), king
exposure (`evaluation.cpp:734`) and the king PST blend
(`evaluation.cpp:737-744`). The first two are multiplied by
`KING_DANGER_SCALE = 0` and `KING_EXPOSURE_SCALE = 0`, so they contribute
nothing. **Of 21 summed terms, exactly one is interpolated, and it is the king's
contribution to the PST.**

That is the known part. Three things about it are not recorded anywhere and are
worse than the headline.

**The taper is switched off on most of the positions the engine sees.** Starting
material by this file's own piece values is 8 240, and the factor is clamped to
1.0 whenever `totalMaterial >= 3200`, which is until 61% of the material has left
the board. Counted over `tests/data/evalerr.epd`:

| | positions | phase factor pinned at exactly 1.0 |
|---|---|---|
| `ctl` (ordinary) | 688 | **481, 69.9%** |
| `comp` | 363 | **324, 89.3%** |

So on seven ordinary positions in ten, the single tapered term in the evaluation
is inert and the evaluation is untapered in the strict sense. Stockfish
interpolates across essentially the whole game because its phase runs between
`EndgameLimit` and `MidgameLimit` on non-pawn material, which is close to the
starting value.

**The phase measure includes pawns.** Stockfish uses non-pawn material
deliberately, because pawns persist into the endgame and their count says nothing
about phase. Here eight pawns are 800 of each side's 4 120, so trading pawns
moves this engine toward "endgame" and trading queens for pawns can move it back.
On this corpus the practical damage is small, 26 positions of 1 051, but the
direction of the error is the opposite of what you want: a pawn-heavy rook ending
reads as more of a middlegame than a pawnless one.

**The one tapered term has the wrong sign for a large part of the endgame.** Take
a king on d4, index 35. `PST_KING_MG[35] = -40` and `PST_KING_EG[35] = +40`
(`eval_weights.hpp:223`, `eval_weights.hpp:235`), so the blended value is
`40 - 80f`. In a pure
king-and-pawn ending with all sixteen pawns, `totalMaterial` is 1 600 and `f` is
0.5, so a centralised king scores **0** where a correctly tapered evaluation
gives it **+40**. In a rook ending with all pawns, `f` is 0.83 and the same king
scores **−26**, so the evaluation is actively pushing its king toward the corner
in a position where centralisation is most of the technique. Both are inside the
band where `ctl` error is measured at its worst, 230cp against 146 in the opening
(`RESEARCH-EVAL-GAP.md`).

**And the endgame king term that would compensate is a step function.**
`kingActivityBonus` fires only when `totalMaterial < 2000`
(`evaluation.cpp:700-704`), which on this corpus is 17.4% of `ctl` positions and
3.3% of `comp` ones. Above that threshold it is exactly zero; below it, up to
±15. So the evaluation has a **discontinuity of up to 30 centipawns at
`totalMaterial == 2000`**, crossed by any capture of a knight or a bishop in that
region. `gamePhaseFactor`'s clamp at 3200 is a second, gentler discontinuity in
the derivative.

That is worth connecting to the search. `improving` compares the static
evaluation at ply *p* with the one at ply *p−2* (`search.cpp:697`), and the
aspiration window compares the score at depth *d* with depth *d−1*. Both are
differencing operators on a function with step discontinuities in it. `improving`
gated **−10.6 [−31.9, +10.7]** and stays off (`search.hpp:674-676`), and
`BUGS.md` 20's whole mechanism is score volatility between iterations. A step in
the evaluation is a source of exactly that volatility, and it is free to remove.

**Cost of not tapering, stated honestly.** The recorded phase split of `ctl`
error is 146.0 in the opening and middlegame, 205.8 in the middlegame band and
230.0 in the endgame, a 58% rise (`RESEARCH-EVAL-GAP.md`). That drift is what
tapering is for, and it is 84cp of it. `EVAL-VS-ETHEREAL.md` already argued that
tapering would be a larger change to the error distribution than any of the three
tunes that `BUGS.md` 20 killed, so the margins would need re-deriving in the same
piece of work. That argument stands. What it does not cover is the two items
above, the 3200 clamp and the 2000 step, which are not tapering *depth* but
tapering *coverage*, and which can be fixed without moving any weight.

---

## 4. Compensation blindness: which terms are actually responsible

The recorded decomposition is 543.7 mean absolute error over 363 `comp`
positions with 210 sign flips (57.9%), against 181.9 and 1.5% on 688 `ctl`
positions. `comp` is 35% of the corpus and 61% of the error mass. It is flat
across game phase, 536 / 546 / 588, so it is not the tapering defect.

The corpus's own decomposition of the 544 is the most useful thing in the file
and is recorded at `HANDOFF.md:887-894`: over the 363 positions the side ahead in
material is ahead by **+195cp**, this engine prices the position at **+153**, and
the truth is **−391**. We discount a material edge by 42 centipawns where
Stockfish discounts it by 586. And 282 of the 544 is dynamics, so **the
addressable part is about 262cp**.

Attributing that 262 to terms, in what I judge to be descending order:

**1. King safety, and it is not going to be rescued by another attempt at the
same term.** This is the largest single item and it has been rejected seven
times: four self-play arms (+1.3, +2.2 and −11.0 over 3 360 games each, and
−216.9 over 960 at 8x magnitude, `eval_weights.hpp:39-42`), a fifth self-play
gate at **−33.1 [−43.2, −23.0]** over 3 360 games, an 80-game gauntlet that
resolved nothing at ±80 Elo, and a **1 680-game gauntlet against a fixed external
opponent at 62.11% baseline against 59.79% with the term** (`KING-SAFETY.md`). That last
one removes the standing excuse, which was that self-play cannot price a term
both sides get.

`KING-SAFETY.md` diagnoses it correctly: the term counts proximity, not danger.
It is quadratic in a weighted count of attacked zone squares starting from zero,
so a lone queen touching four squares near a king costs 160cp at scale 500 and a
rook touching three costs 50, in ordinary positions where nothing is happening.
The measured consequence is `comp` 543.7 → 504.5 while `ctl` 181.9 → **189.3**:
it improves the rare case by degrading the common one, and the common one is
nearly everything the search visits.

Ethereal's defensive components have since been built, and all of them together
are worse than the crude version at matched `ctl` damage: at `ctl` 189,
proximity-only gives `comp` 504.5 while the full Ethereal-shaped term gives
539.6. Eight configurations, one curve, no separation.

The important fact for this document is the one `HANDOFF.md:896-903` records
about the flagship position, `r2r4/pN3pkp/Qb6/3qn1p1/3Pn3/4BP2/PP2P1PP/R3KB1R w
KQ - 3 20`, where the engine reads +381 static against a truth of −149. **The
danger term scores it 4cp**, because exactly one black piece currently attacks the
king zone: the queen, the second knight and the bishop are all aimed at it and
blocked. The danger is latent. A static count of current attacks cannot see
latent danger, and neither can this engine's own search at depth 16, which still
says +444. That is the shape of the defect and no weighting of a
current-attack count addresses it.

**2. There is no scale factor, so every drawn-with-compensation position is
scored at full material.** Section 2 established that the drawishness term is
unreachable and excluded anyway. Stockfish applies a `ScaleFactor` to the
**endgame component only**, and the case it exists for is precisely
compensation: opposite-coloured bishops, where a pawn or two of material is
routinely worth nothing. This engine has no mechanism at all for saying "this
material edge does not convert", which is a literal statement of what `comp`
positions are. It is also the cheapest of the ideas in this section, because a
scale factor multiplies an existing score rather than adding a new term, so it
cannot manufacture error in positions where it does not apply.

**3. Passed pawns are flat at 20 centipawns regardless of rank**
(`eval_weights.hpp:111`). A passer on the seventh and a passer on the third score
identically, and there is no blockade test, no king-proximity test, no
free-advance test, no connected-passer bonus and no rook-behind-passer bonus. In
Stockfish a passer on the seventh is worth well over 200. **This is the single
largest arithmetic misprice in the file**, it costs on both `comp` and `ctl`, and
"a pawn about to queen against a piece" is one of the most common compensation
patterns there is.

**4. The threat term is uncapped and SEE-blind**, per section 2. On `comp`
positions, which by construction have pieces bearing on things, an uncapped
per-pair sum is exactly where a large spurious swing comes from. The recorded p90
for `threats` is 138 to 225 after `ROADMAP.md` 6.2 fixed the worst of it
(`TODO.md:122`), which was ±830 before. It is no longer the largest term but it
is still uncapped.

**5. The mobility term charges the wrong way in a compensation position.** A side
that has sacrificed material for activity has fewer pieces, so it usually has
*less* raw pseudo-legal mobility, while what it actually has is more *useful*
mobility on safe squares. A flat count over unrestricted squares, including pawn
pushes, cannot express that and will often price the sacrifice negatively twice.

**What I would not attribute it to.** Not tapering, on the recorded evidence:
`comp` error is flat across phase. Not piece values: the Texel tune transferred
100% to held-out data and cost 32.5% nodes for it (`BUGS.md` 20), so the values
are approximately right and moving them is a solved and closed question.

---

## 5. What Stockfish provides that this does not

Sources, all fetched 2026-09-11 from `raw.githubusercontent.com`:
`official-stockfish/Stockfish` at tag `sf_11` (`src/evaluate.cpp`, `pawns.cpp`,
`material.cpp`, `types.h`, `search.cpp`), at tag `sf_17.1`, and at `master`.

**A units warning that has to come first, because every comparison below depends
on it.** Stockfish does not work in centipawns internally. SF11 uses
`PawnValueEg = 213` as its unit and `uci.cpp:264` converts with
`v * 100 / PawnValueEg`, so **divide any SF11 number by 2.13**. Modern master
normalises through a cubic in the material count anchored at `a = 304.15`
(`master/src/uci.cpp:537-553`), so **divide master numbers by 3.04**. `sf_17.1`
divides by 3.774. Every "cp" figure below has been converted; the raw values are
given beside them so they can be checked.

### 5.1 The margins, which is where the central question is decided

| | SF11 (2020) | SF 17.1 (2025) | master (2026) | here |
|---|---|---|---|---|
| razoring margin | 531 = **249 cp**, `depth < 2` | 461 + 315·d² = **206 cp** at d=1 | 482·d = **158 cp per ply**, no depth cap | **500 cp**, `depth <= 2` |
| reverse futility | 217/ply = **102 cp per ply**, `depth < 6` | 110/ply = **29 cp per ply**, `depth < 14` | 45 to 85/ply = **15 to 28 cp per ply**, `depth < 19` | **300 cp per ply**, `depth <= 3` |
| largest RFP margin ever applied | 5 × 217 = 509 cp | 13 × 110 = 379 cp | 18 × 85 = 503 cp | 3 × 300 = 900 cp |
| move-count pruning, quiets kept at depth 1/2/3 | **2 / 3 / 6**, no depth cap | 2 / 3 / 6, no depth cap | **2 / 3 / 6**, no depth cap | **5 / 8 / 13**, capped at depth 3 |
| quiet move futility | 110 + 81 cp per ply, `lmrDepth < 6` | 12.7 + 30.7 cp per ply | 54 + 39 cp per ply, `lmrDepth < 12` | 300 + 150 cp per ply, `depth <= 4`, off |
| quiescence delta base | 154 = **72 cp** + victim | 359 = **95 cp** + victim | 306 = **101 cp** + victim | **900 cp** + victim |
| null move reduction | `(854 + 68·d)/258 + min((eval−beta)/192, 3)` | depth and eval scaled | depth and eval scaled | **`const int R = 2`** |

Read the reverse futility row twice. **Modern Stockfish prunes on a margin of 15
to 28 centipawns per ply, and this engine's median evaluation error is 125
centipawns.** Stockfish's margin is roughly a fifth of our median error and a
twentieth of our p90. That is what an evaluation worth pruning on buys, stated as
a number, and it is the strongest available confirmation of link one in section 1.
Stockfish 11 goes further still: `futility_margin(d, improving) = 217 * (d - improving)`,
so **at depth 1 with `improving` true the margin is exactly zero** and it returns
the static eval on the strength of the evaluation alone.

The arc across six years is uniform and it is the shape the central question
predicts: **as the evaluation got more accurate the per-ply margin fell about 3.6x
while the depth it is applied over rose about 3.6x**, from 102 cp per ply capped
at depth 5, to 29 cp per ply capped at 13, to 15 to 28 cp per ply capped at 18.
Razoring went the other way, from a flat shallow shortcut to an unbounded
depth-scaled ramp. This engine sits outside that arc in both directions at once:
a wider margin than any of them, over a shallower depth than any of them.

But read the move-count row too, because it is the one that matters for
branching factor and it has nothing to do with the evaluation. **Stockfish keeps
2, 3 and 6 quiet moves at depths 1, 2 and 3 and applies the rule at every depth;
this engine keeps 5, 8 and 13 and stops at depth 3.** Roughly twice as permissive
where it fires, and it does not fire at all above depth 3. Stockfish's own comment
prices its shallow-pruning block at **~200 Elo** (`sf_11/src/search.cpp`, step 13
header), against **~1 Elo** for razoring and **~50 Elo** for reverse futility.
The engine's authors rank these in exactly the opposite order to where this
project has spent its effort.

The null move row is the other half of the same point. SF11 computes
`R = (854 + 68 * depth) / 258 + std::min(int(eval - beta) / 192, 3)`
(`sf_11/src/search.cpp:852`), which at depth 8 is 5 plus up to 3 more, against a
flat 2 here. The depth term is pure structure and is the larger half.

**Two pieces of Stockfish's own history are worth carrying**, because they say
something about the direction of the coupling. Razoring was **deleted** on
2020-12-31, four months after NNUE landed, with the entire commit message being
"has become ineffective now" (`8ec97d161e`, 63 448 games at STC). It was
**reintroduced** on 2022-02-05 in the form this engine uses, verify with
quiescence and fail low only if it agrees (`4d3950c6eb`). So a stronger
evaluation made the classical razoring bet *worthless* rather than *tighter*: the
technique earns its place by catching cases the evaluation is confident about,
and NNUE made it redundant rather than more aggressive. That is a caution against
expecting a better evaluation here to convert through razoring, and it agrees
with `razortight`'s null.

### 5.2 Stockfish 11, the hand-crafted reference: what is structurally absent here

The concept list is close, as `EVAL-VS-ETHEREAL.md` already established. What
follows is only the structure, not the list.

**Every constant is a packed `(mg, eg)` pair.** `types.h:257-273` defines
`Score` as one 32-bit int holding both halves via
`make_score(mg,eg) = Score((int)((unsigned)eg << 16) + mg)`, so both phases
accumulate in a single add and tapering costs nothing at runtime. Counted from
the source, SF11 carries **about 800 tuned scalars, of which 642 arrive as 321
(mg, eg) pairs**: 107 pairs in `evaluate.cpp` (66 of them mobility), 208 in
`psqt.cpp`, 6 in `pawns.cpp`, plus 56 shelter and storm values, the 42-integer
imbalance polynomial in `material.cpp`, and roughly 60 bare coefficients in
`king()`, `passed()`, `space()`, `initiative()` and `scale_factor()`. This engine
has 476 parameters, all single values, and one tapered table.

**Mobility is a table per piece type over a restricted area, not a multiplier.**
`evaluate.cpp:91-107` has `MobilityBonus[4][32]` with 9 entries for knights, 14
for bishops, 15 for rooks and 28 for queens, each an `(mg, eg)` pair. The rook row
runs `S(-58,-76)` to `S(58,171)`: nearly flat in the middlegame and steep in the
endgame, which a single scalar cannot express in either direction. And the area
is restricted (`evaluate.cpp:225-230`):

    Bitboard b = pos.pieces(Us, PAWN) & (shift<Down>(pos.pieces()) | LowRanks);
    mobilityArea[Us] = ~(b | pos.pieces(Us, KING, QUEEN)
                           | pos.blockers_for_king(Us) | pe->pawn_attacks(Them));

Own blocked pawns and pawns on the first two ranks, own king and queen, own
pieces pinned to the king, and every square attacked by an enemy pawn are all
excluded. **This engine excludes none of them and additionally counts pawn moves
and four separate promotion moves per promotion square.** Attack sets are also
computed through friendly queens and rooks (x-ray), and a pinned piece's set is
masked to the pin line (`evaluate.cpp:269-274`).

**Passed pawns are the single largest arithmetic gap.**

    constexpr Score PassedRank[RANK_NB] = {
      S(0,0), S(10,28), S(17,33), S(15,41), S(62,72), S(168,177), S(276,260) };

In centipawns that is 4.7 mg on the second rank rising to **129.6 mg and 122.1 eg
on the seventh**, and the rank bonus is only the start. For `r > RANK_3` the term
adds king proximity to the block square, endgame only, worth up to 168 eg cp at
rank 6; and a free-advance bonus `k * w` where `k` is 35 for a completely safe
path and `w = 5r - 13`, worth up to **319 cp in each phase** at rank 6. A
supported passer on the sixth with a clear path is worth several hundred
centipawns. **This engine pays a flat 20 in every phase, on every rank, blocked or
free.**

**King safety is a unit accumulator with a quadratic middlegame transform and a
linear endgame one** (`evaluate.cpp:446-461`):

    kingDanger +=        kingAttackersCount[Them] * kingAttackersWeight[Them]
                 + 185 * popcount(kingRing[Us] & weak)
                 + 148 * popcount(unsafeChecks)
                 +  98 * popcount(pos.blockers_for_king(Us))
                 +  69 * kingAttacksCount[Them]
                 +   3 * kingFlankAttack * kingFlankAttack / 8
                 +       mg_value(mobility[Them] - mobility[Us])
                 - 873 * !pos.count<QUEEN>(Them)
                 - 100 * bool(attackedBy[Us][KNIGHT] & attackedBy[Us][KING])
                 -   6 * mg_value(score) / 8
                 -   4 * kingFlankDefense
                 +  37;
    if (kingDanger > 100)
        score -= make_score(kingDanger * kingDanger / 4096, kingDanger / 16);

Four things here that `KING-SAFETY.md` has not recorded, and one of them is the
answer to that document's open question.

- **The quadratic is middlegame only.** The endgame component is linear,
  `kingDanger / 16`. At `kingDanger = 2000` that is 458 cp of middlegame charge
  against 59 cp of endgame, an 8x asymmetry. This engine's term is quadratic in
  both, scaled by a phase factor that section 3 showed is pinned at 1.0 on 89% of
  compensation positions.
- **The threshold is on the accumulated danger, at 100, before the transform.**
  This engine's `KING_DANGER_OFFSET` was tried and produced the same
  one-dimensional trade, but it was applied against a raw proximity count rather
  than against an accumulator that already contains the defensive credits.
- **`blockers_for_king` is in the sum, weighted 98.** That is the pin and x-ray
  query, and **it is the term that would fire on this project's flagship
  position.** In `r2r4/pN3pkp/Qb6/3qn1p1/3Pn3/4BP2/PP2P1PP/R3KB1R w KQ - 3 20`
  the danger term here scores 4cp because only one black piece currently attacks
  the king zone and the queen, second knight and bishop are all aimed and blocked
  (`HANDOFF.md:896-903`). Stockfish charges for exactly those blockers. This is
  the one untried idea in the king-safety family and it is a geometry query, not
  another attack weighting.
- **Losing the attacking queen subtracts a flat 873**, which on its own drops
  most positions below the threshold of 100. `KING-SAFETY.md` identified the
  equivalent Ethereal term and it was tried; the point worth adding is the
  magnitude, which is far larger relative to the rest of the sum than the version
  tried here.

**Threats are indexed by victim and split by whether the victim is defended**
(`evaluate.cpp:116-149`, `489-561`). `ThreatByMinor[]` runs
`S(6,32)` for a pawn to `S(90,119)` for a rook; `ThreatByRook[]` is a separate
table; `Hanging = S(69,36)` is charged only on genuinely weak pieces;
`RestrictedPiece = S(7,7)` prices squares the opponent cannot use. The gate is
`stronglyProtected = attackedBy[Them][PAWN] | (attackedBy2[Them] & ~attackedBy2[Us])`,
so a piece defended by a pawn is not a threat at all. **This engine sums a
per-attacker constant with no victim/attacker interaction, no defence test and no
cap.**

**`initiative()` and `scale_factor()` have no counterpart here at all.**
`initiative()` (`evaluate.cpp:698-737`) is a second-order correction from pawn
count, passers, king outflanking, infiltration and pawns on both flanks, clamped
so it can never flip the sign of either phase and so the middlegame half is only
ever a penalty. `scale_factor()` (`742-761`) multiplies **the endgame component
only**, and its headline case is compensation:

    if (pos.opposite_bishops() && pos.non_pawn_material() == 2 * BishopValueMg)
        sf = 22;
    else
        sf = std::min(sf, 36 + (pos.opposite_bishops() ? 2 : 7) * pos.count<PAWN>(strongSide));
    sf = std::max(0, sf - (pos.rule50_count() - 12) / 4);

With `SCALE_FACTOR_NORMAL = 64`, a pure opposite-coloured-bishop ending is scored
at **34% of its endgame value**, and the 50-move counter drives it to zero. This
engine's only drawishness mechanism is unreachable dead code (section 2).

**Phase, and the tempo.** `material.cpp:130-135`:

    Value npm = clamp(npm_w + npm_b, EndgameLimit, MidgameLimit);
    e->gamePhase = Phase(((npm - EndgameLimit) * PHASE_MIDGAME) / (MidgameLimit - EndgameLimit));

A linear 0 to 128 ramp on **non-pawn material** clamped to [3915, 15258], so pawns
do not affect phase and the ramp is live across essentially the whole game.
Compare `min(1.0f, totalMaterial / 3200.0f)` on total material including pawns,
pinned at 1.0 on 70% of ordinary positions. `Tempo = Value(28)` = **13 cp**,
added after the side-to-move flip so the evaluation is exactly antisymmetric
apart from it (`evaluate.h:32`, `evaluate.cpp:834`).

**How the numbers were obtained, which is the part that cannot be copied.** SF11's
constants come from fishtest SPSA and, for the margins specifically, Bayesian
optimisation: commit `443787b0d1`, "Tuned razor and futility margins", records
"Acquisition function: Expected Improvement / alpha: 0.05 / xi: 1e-4 / TC:
60+0.6 / Number of iterations: 100 / Initial points: 5 / Batch size: 20 games".
Every change was then SPRT-validated on a distributed cluster. **That is the
resource this project does not have and cannot substitute for**, and it is the
real reason `KING-SAFETY.md` concluded that six interacting hand-guessed weights
will not land near an optimum. It is also why item 7 in section 7 is ranked where
it is: doubling the parameter count is only useful alongside a way to fit the new
numbers.

### 5.3 Modern Stockfish NNUE: what a learned evaluation gives

**There is essentially no hand-crafted evaluation left.** `master/src/evaluate.cpp`
is 105 lines and the whole of `Eval::evaluate` is 28 of them: the net output, a
material-count rescaling, a complexity term `|psqt - positional|` that both
amplifies optimism and shrinks the eval, an optimism injection set from the root
move's running average, and a 50-move damping. Six constants.

The classical evaluation was **deleted** on 2023-07-11 (`af110e02ec`, PR #4674),
and the commit message is worth quoting because it is the honest measurement of
what a top hand-crafted evaluation is worth once a net exists:

> this PR removes the classical evaluation from SF. Even though this evaluation
> is probably the best of its class, it has become unimportant for the engine's
> strength [...] roughly 25% of SF [...] This impact on strength is small,
> **roughly 2Elo**.

with STC −2.35 ± 1.1, LTC −1.74 ± 1.0 and VLTC SMP −1.70 ± 0.9 over 100 000 games
each. And when NNUE first landed (`84f3e86790`, 2020-08-06, shipped as SF12), the
measured gain over the identical search with the classical evaluation was
**+92.77 ± 2.1 Elo** at 10+0.1 single-threaded and **+89.47 ± 2.0** at 20+0.2 on
eight threads. Those two numbers bracket the whole question: the best hand-crafted
evaluation ever written is worth about 90 Elo less than a net, and about 2 Elo
once the net is present.

**Architecture, at the last stable release** (`sf_17.1/src/nnue/nnue_architecture.h:38-50`),
which is the citable one because master has since moved to three feature sets:
`HalfKAv2_hm` with 22 528 input features per perspective, into a 3 072-wide
accumulator per perspective, then 15, then 32, then 1, with 8 output buckets and
a 128-wide small net used when `|simple_eval| > 962`. Quantisation is int16
accumulation clipped to u8 in [0, 255], int8 hidden weights into int32
accumulators, int32 PSQT weights. Master is now 86 896 input features into 1 024,
mixing `HalfKAv2_hm` with a `FullThreats` set of 59 808 features and a pawn-pair
set of 4 560.

The relevant structural point for this project is the **king bucketing**:
`half_ka_v2_hm.h:69-78` has 32 distinct king buckets, mirrored so the king is
always on files e to h. Every weight in the net is conditioned on where the king
is. That is what the IJCAI paper `KING-SAFETY.md` already cites means when it says
the hand-crafted king-safety concept "does not have a clear correspondence in the
NNUE network": the network does not have a king-safety term because king position
conditions the entire evaluation rather than contributing one addend to it. A
hand-crafted evaluation cannot express that, and seven failed attempts here are
consistent with it.

### 5.4 The mechanism modern Stockfish uses that this engine has the parts for and does not use

This is the most directly actionable thing in the Stockfish survey, and it is not
a term.

Master keeps **five** correction-history tables, not one
(`master/src/search.cpp:85-101`): pawn structure, minor-piece configuration,
white non-pawn material, black non-pawn material, and continuation correction
from two and four plies back, combined with weights 15341, 10569, 12906 and 8761
and applied as `v + cv / 131072`. The uncorrected value is what goes into the
transposition table.

**And then the magnitude of that correction is used to modulate pruning**, which
is the part this engine has never tried:

- the reverse futility margin **widens** by `|correctionValue| / 198435`
  (`master/src/search.cpp:1005`);
- the singular-extension margins **narrow** by `|correctionValue| / 198368`
  (`:1267-1271`);
- the LMR reduction **shrinks** by `|correctionValue| / 26310` (`:1332`).

In plain terms: where the engine has learned that its static evaluation is
unreliable for this class of position, it prunes less and searches more, and where
the evaluation has proved reliable it prunes harder. **That is a per-position
margin instead of one global constant chosen at the p90 of a whole-corpus error
distribution**, and it is exactly the answer to the problem section 1 describes.

This engine already builds the table. `corrHist` is keyed on the pawn hash,
16 384 entries per side, and it is used in one place only: adding a correction to
the static evaluation, capped at `CORR_CAP = 96` centipawns
(`search.cpp:81-105`). Two observations follow.

**The cap is below the error it was built to correct.** ±96cp against a median
error of 125cp and a p90 of 407cp. By construction the table cannot fix a
median-sized mistake, which is a sufficient explanation for `corrhist` closing
over three gates without anyone needing to conclude that the premise is wrong.

**And the magnitude has never been used as a signal.** `corrHist` here is a
point estimate added to the eval; in master it is *also* a confidence measure
that three separate pruning decisions read. The table is already populated on
every node. Reading `|corrHist[side][slot]|` to widen `REV_FUTILITY_MARGIN` and
shrink the LMR reduction costs one array read on a path that already does one,
needs no corpus, no new term and no evaluation change, and it is gateable at equal
nodes in an hour. It is the cheapest thing in this entire document.

---

## 6. NNUE: an honest verdict

The recorded state, from `docs/NNUE-DECISION.md` and its 2026-09-06 addendum,
plus `HANDOFF.md:531-559`:

- The corpus exists and is sound. 25.2M positions, 99.5% unique, 3.2 GB, labels
  monotonic with results, generated at 5 000 nodes per position in about a day
  and a half of background machine time. It is reusable.
- A first net was trained and **rejected at the kill-check before any accumulator
  code was written**. On 200 000 held-out positions: a constant scores MAE 313.4,
  the hand-crafted evaluation scores **68.7 with correlation 0.945**, and the net
  scored **295.2 with correlation 0.194**. The net was barely better than a
  constant.
- The net failed on three initialisation-class bugs: a summing feature
  transformer initialised like a Linear layer, so 89% of the accumulator
  saturated outside `clamp(0,1)`; an output that could not reach the label range,
  ±52cp against labels spanning ±800; and, after both were fixed, an accumulator
  with too little spread to separate positions. `tools/nnue-train.py` now refuses
  to start on the first two.
- The standing instruction is **do not start the accumulator**, and the named next
  step is to regenerate a small corpus at a much higher node count and re-measure
  the 0.945.

**The three net bugs are not the blocker and should not be read as evidence about
NNUE.** They are ordinary trainer bugs, all three diagnosed, two now guarded in
the trainer. The sequencing that caught them before weeks of accumulator work was
the right call and it worked.

**The blocker is that the corpus label is a function of the thing the net is
meant to replace**, and I think the repo is drawing the wrong conclusion from it.
A 5 000-node self-play search is this engine's own evaluation propagated seven or
eight plies, so a correlation of 0.945 between the evaluation and the label is
not a fact about how much knowledge search adds. It is a fact about the labelling
procedure being circular. The addendum says almost this, and then concludes
"self-play NNUE may be dead for this engine" rather than "the label is wrong".

Two things are being over-read, in opposite directions, and both are checkable
cheaply on the corpus that already exists.

**The 0.945 is inflated by material, and nobody has partialled it out.** A
correlation of 0.945 is r² = 0.893 of the label's variance, but the label's
variance is dominated by material, which every evaluation on earth gets right.
The question that matters is how much of the label's residual *after material* the
evaluation explains, and that number has never been computed. Regress the label
on the material count alone, then ask what the remaining 20 positional terms add.
On the corpus that is already on disk this is one pass with `tools/corpus-stats.py`
sitting next to it, and it would materially change the reading in either
direction.

**The same evaluation scores MAE 68.7 against a 5 000-node self-play label and
181.9 against Stockfish at depth 16 on quiet ordinary positions** (and 543.7 on
compensation ones). That 2.6x gap is not a contradiction, it is the measurement
of the circularity: the self-play label agrees with us because it is partly made
of us. The headroom a net would need to learn is exactly the headroom
`tests/evalerror` measures against an external reference, and the existing corpus
does not contain it.

So the honest position on NNUE is:

1. **Do not train another net on the existing 25.2M-position corpus.** It cannot
   contain what is missing, and regenerating the same corpus at a higher node
   count is a weaker version of the same objection: a deeper search by this engine
   is still this engine.
2. **The decision is a values decision, not a technical one.** `tools/sf-label.py`
   exists and has never been run. External labels would work and would give the
   net something to learn, and they would also make this engine an approximator of
   Stockfish, which is the same category of choice as adopting an external
   opening book. The repo already identifies this as "a measurement-purity
   decision, and a larger one than the opening book". It should be made
   explicitly rather than arrived at by drift.
3. **`BUGS.md` 20's cost argument for NNUE is half wrong and the half that is
   wrong matters.** It says NNUE's accuracy "costs a fixed, small amount per node
   with no magnitude churn to destabilise the search". The cost-per-node half is
   right. The no-churn half is unargued, and the mechanism that entry itself
   measured was that a more accurate evaluation produces scores that move more
   between iterations, missing the aspiration window on 22% of iterations against
   14%. An NNUE evaluation is far more accurate than a Texel tune, so on that
   entry's own mechanism it should churn more, not less. Section 3 above offers a
   competing explanation, that the churn came from the *scale* of the fastest-moving
   terms rather than from accuracy, which would let NNUE off. Neither reading has
   been tested. **Either way the aspiration window is a prerequisite for NNUE and
   not a consequence of it**, and `aspAdaptive` already exists as a toggle and
   already closes a third of the gap.
4. **The cost-per-node worry is weaker here than in a typical engine, and that
   has not been said.** The profile in `docs/BITBOARD-REPLACEMENT.md:30-34` puts
   `evaluate_details` at 29.51%, `countPseudoLegalMoves` at 14.99% (called only
   from the evaluation, for mobility) and `evaluate` at 4.94%: **roughly 49% of
   search time is already the evaluation**, because mobility runs a move generator
   at every leaf. A quantised net with an incremental accumulator is not obviously
   more expensive than that. This is a genuine point in NNUE's favour that the
   existing documents do not make.
5. **The accumulator risk has just changed and nobody has said so.** `NNUE-DECISION`
   §2 ranks inference as the hard part, specifically the accumulator update inside
   `makeMove`/`unmakeMove` on a mailbox board, and the hazard of a second piece of
   position state next to an eval cache keyed on the zobrist hash (`BUGS.md` 8).
   `src/engine/bb_position.cpp` landed in b1f6bae and is now connected behind a
   toggle in 6ce151b. A from-scratch accumulator hangs off a bitboard `Position`
   far more naturally than off the mailbox `Board`, so the single highest-risk item
   in that document just got cheaper. That is a reason to re-cost, not a reason to
   start.
6. **And NNUE is still not the highest-value item available**, for the reason in
   section 1: the branching factor is held by two fixed constants in null move and
   late move pruning that no evaluation change touches.

**The size of the prize, from Stockfish's own fishtest numbers.** When NNUE first
landed against an otherwise identical search (`84f3e86790`, 2020-08-06, shipped as
SF12) it measured **+92.77 ± 2.1 Elo** at 10+0.1 on one thread and
**+89.47 ± 2.0** at 20+0.2 on eight. Three years later, deleting the entire
hand-crafted evaluation, "probably the best of its class" and roughly 25% of the
program, cost **about 2 Elo** (`af110e02ec`, 2023-07-11, three 100 000-game runs
at −2.35, −1.74 and −1.70). Those two numbers bracket the question honestly: a net
is worth about 90 Elo over the best hand-crafted evaluation ever written, and that
evaluation is worth about 2 Elo once the net exists.

`ROADMAP.md` has carried an unmeasured +200 to +400 prior for NNUE since Phase 7.
**Stockfish's own measurement of the same substitution is +90**, at a much
stronger baseline. The prior is roughly three times too large and should be
written down as +90 or less before anything is costed against it.

---

## 7. Ranked changes

Ranked by expected reduction in measured evaluation error per unit of work.
Effect is stated against `tests/evalerror`'s `ctl` and `comp` columns, not Elo,
because king safety is this project's standing proof that Elo is a poor
instrument on evaluation terms: the corpus called it 37cp better and the gate
called it 33 Elo worse.

**Every item below moves the score scale, so `test-evalref` and `test-bench` will
both fail by design. Read the diffs before regenerating.** And every item changes
the error distribution that `RAZOR_MARGIN`, `REV_FUTILITY_MARGIN` and
`QS_DELTA_MARGIN` were derived from, which is `BUGS.md` 20's whole warning.

### 0. Measure the `ctl` dynamics floor: a prerequisite, not a change

Run `tools/sf-label.py --depth 1` and `--depth 2` over the 688 `ctl` FENs and
compare against the existing depth-16 labels, exactly as `tests/README.md:133-140`
did for `comp`.

- **Effect on eval error:** none. It measures the floor.
- **Difficulty:** trivial. Minutes of Stockfish at depth 1 and 2, not the
  100-minute depth-12 sweep the script's docstring describes.
- **Risk:** none.
- **Why it is first:** it decides whether items 2 to 7 have a reachable target.
  If the `ctl` floor is near the `comp` floor of 282cp, then a p90 of 150 is
  unreachable, the textbook margins can never be safe here whatever is done to the
  evaluation, and this entire line of work closes on one measurement.

### 1. Raise `CORR_CAP`, and use the correction's *magnitude* to modulate pruning

This is a search change, so it does not belong in a ranked list of evaluation
terms on a strict reading. It is first anyway, because it attacks the exact
problem section 1 identified, it needs no corpus, no new term and no evaluation
change, and it is the cheapest item in this document. Section 5.4 has the
Stockfish citations.

Two separate changes, and the second is the important one.

**Raise the cap.** `CORR_CAP = 96 * CORR_GRAIN` (`search.cpp:83`) bounds the
correction at ±96 centipawns, against a median evaluation error of 125 and a p90
of 407. **By construction the table cannot correct a median-sized mistake.** That
is on its own a sufficient explanation for `corrhist` closing over three gates,
and it does not require concluding that the premise is wrong. Master's equivalent
clamp is `CORRECTION_HISTORY_LIMIT`, and its per-position correction is a
weighted sum over five tables rather than one.

**Then read the magnitude as a confidence signal.** Master uses
`|correctionValue|` in three places that have nothing to do with adjusting the
eval: it **widens** the reverse-futility margin by `|corr| / 198435`
(`master/src/search.cpp:1005`), **narrows** the singular-extension margins
(`:1267-1271`), and **shrinks** the LMR reduction by `|corr| / 26310` (`:1332`).
Where the engine has learned its static evaluation is unreliable for this class of
position it prunes less; where it has proved reliable it prunes harder.

That is a **per-position margin** instead of one global constant chosen at the p90
of a whole-corpus error distribution, and a per-position margin is the only thing
that can escape the trap section 1.1 describes, where 300 + 150·d is inert and
100 + 100·d over-prunes. There is no single number that is right for both classes
of position, which is why the window is empty. Stockfish does not use a single
number.

- **Effect on eval error:** raising the cap improves the *effective* static
  evaluation the search consumes, which is the quantity the margins are calibrated
  against, though it does not move `tests/evalerror`, which scores the raw
  `evaluate()`. Consider adding a `--corrected` mode to `tests/evalerror` so the
  quantity being improved is the quantity being measured.
- **Difficulty:** low. `ctx.corrHist[side][slot]` is already read on every node
  that computes a static eval (`search.cpp:100`); using its absolute value in two
  more expressions is a handful of lines.
- **Risk:** low, and unusually well-instrumented. It is gateable at equal nodes in
  an hour, it is behind an existing toggle, and it cannot change the evaluation, so
  `test-evalref` stays green and only `test-bench` moves.
- **The caveat:** `corrhist` has three negative gates behind it, and one of them
  was −39.7. Read `search.hpp:430-453` before starting: that verdict attributes the
  whole of it to `corrHistQ`, the quiescence consumer, not to the table itself.
  This proposal touches neither the quiescence path nor the eval adjustment; it
  reads the table for a purpose it has never been read for.

### 2. Passed pawns by rank, with a middlegame and endgame pair

`PASSED_PAWN = 20` flat (`eval_weights.hpp:111`) becomes a table indexed by rank.
The detection already exists and is correct (`evaluation.cpp:536`, `566`); only
the constant is wrong.

- **Effect:** the largest single-term arithmetic correction in the file, and
  section 5.2 now puts a number on it. Stockfish 11's `PassedRank` runs from
  `S(10,28)` on the second rank to `S(276,260)` on the seventh, which is 4.7cp to
  **130cp** in the middlegame and 13cp to **122cp** in the endgame, before a
  free-advance bonus worth up to **319cp in each phase** at rank 6 and an
  endgame-only king-proximity term worth up to **168cp**. A supported passer on the
  sixth with a clear path is several hundred centipawns. This engine pays **20**,
  on every rank, in every phase, blocked or free. It lands on both columns, most
  heavily on the endgame band of `ctl` where error is worst at 230, and on `comp`
  because a passer is one of the commonest forms of compensation for material.
- **Difficulty:** low. One table, two loops, no new geometry.
- **Risk:** low in isolation, moderate downstream: it widens the endgame score
  scale, which is exactly what `BUGS.md` 20 says costs nodes through the
  aspiration window. Gate it with `aspAdaptive` on as well as off.

### 3. Repair the phase measure's coverage

Three changes, all in `evaluation.cpp:700-707`: compute the phase from **non-pawn**
material rather than total; **unclamp it** so it interpolates across the whole
game rather than sitting at exactly 1.0 until 61% of the material is gone; and
replace `kingActivityBonus`'s hard `totalMaterial < 2000` threshold with the phase
factor.

- **Effect:** aimed at the recorded 146 → 230 phase drift in `ctl`, so up to
  ~84cp on the endgame band. Narrower than item 2 in isolation because only one
  term is currently tapered, but this is the prerequisite for item 7 and it is
  what makes item 2's endgame values reachable.
- **Difficulty:** low. Four lines.
- **Risk:** moderate on scores, low on the search, and it is **the one item in
  this list with search upside rather than search cost**: it removes a step
  discontinuity of up to 30 centipawns from the evaluation, and `improving` and the
  aspiration window are both differencing operators that a step is poison for.
  Worth re-gating `improving` (currently −10.6, off) after it lands.

### 4. A scale factor on the endgame component

There is no drawishness mechanism at all: the `drawish` term is unreachable in any
legal position because `whiteMaterial` includes the king's 20000, and it is
excluded from `e.total` anyway (`evaluation.cpp:817-821`, `864-865`). Add a
multiplicative scale on the endgame side, starting with opposite-coloured bishops
and pawnless or single-pawn endings. Stockfish 11's whole rule is nine lines
(`sf_11/src/evaluate.cpp:742-761`) and its headline case is a pure
opposite-coloured-bishop ending scored at `sf = 22` out of
`SCALE_FACTOR_NORMAL = 64`, which is **34% of the endgame value**, plus a 50-move
damping that drives it to zero.

- **Effect:** aimed squarely at `comp`, which is 61% of the error mass. "A
  material edge that does not convert" is a literal description of what a `comp`
  position is, and this is the only structure that can express it.
- **Difficulty:** medium, and it depends on item 3, because a scale factor is
  meaningless without a real endgame component to scale.
- **Risk:** **the lowest risk of any term change here**, and that is the argument
  for it. It is multiplicative and conditional, so in positions where it does not
  apply it is exactly 1 and cannot manufacture error. That is the property every
  king-safety attempt lacked: those all charged something almost everywhere, which
  is why eight configurations improved `comp` by wrecking `ctl`.

### 5. Cap the threat term and gate it on `see()`

`evaluation.cpp:756-784` accumulates `threatBonus[target]` per (attacker, target)
pair with no cap, plus 10% of every value gap, and never asks whether the capture
is sound. `see()` exists, is unit-tested and is gated at +25.6 Elo for ordering.

- **Effect:** removes the remaining source of large spurious swings. Recorded p90
  is 138 to 225 after 6.2, down from ±830 before it.
- **Difficulty:** low to cap, medium to consult SEE (it costs time in the hottest
  loop; 6.2 measured the old SEE call at ~13% per node).
- **Risk:** **read `ROADMAP.md` 6.2 first and take its prior seriously.** That
  section swept the divisor on the hanging-piece penalty and found the score still
  climbing as the charge shrank, all the way to charging nothing: divisor 1 −177.7,
  divisor 6 +105.2, none +152.0. The lesson was that a static score cannot know
  whether a threatened piece will be saved, and the search already does it a ply
  later for real. **So implement only changes that make this term say less**, never
  more. A cap qualifies. A SEE gate qualifies. A new threat category does not.

### 6. Restrict the mobility area and stop counting pawn moves

`countMobility` counts every pseudo-legal move of the whole side including pawn
pushes and four Moves per promotion square (`evaluation.cpp:138`,
`movegen.cpp:181-187`), at a flat 2 per move for every piece type, over
unrestricted squares.

- **Effect:** unclear in sign, which is why it is here rather than higher. It
  removes a real distortion, because a term that counts pawn pushes is partly
  measuring pawn structure and double-counting with the six pawn terms, and a pawn
  on the seventh currently scores up to 24cp of "mobility". But mobility is also
  the term the search is most sensitive to, per item 8.
- **Difficulty:** medium. The generator already has the hooks, but excluding
  squares attacked by enemy pawns needs a pawn attack map, which the bitboard
  `Position` gives cheaply and the mailbox `Board` does not.
- **Risk:** **the highest of the term changes.** Mobility changes with every move,
  so it is the fastest-moving component of the score, and item 8 argues that is
  where `BUGS.md` 20's node cost actually lives. Changing it will move the
  aspiration miss rate.

### 7. Taper everything

Every scalar becomes a middlegame and endgame pair, and the five non-king piece
tables become ten. This is `EVAL-VS-ETHEREAL.md`'s structural finding and it is
correct.

- **Effect:** the largest available reduction in `ctl` error, plausibly most of
  the 146 → 230 drift, and the only change that addresses the defect at its cause
  rather than one term at a time.
- **Difficulty:** high. It roughly doubles the parameter count from 476 and none
  of the new numbers can be guessed, so it is a tune as well as a refactor, and
  `tools/tune` at 476 parameters is already hours of coordinate descent.
- **Risk:** **highest in the list, and `BUGS.md` 20 says it will not convert
  unless the pruning margins are re-derived in the same piece of work.** That is a
  real constraint and it is why this is sixth and not first, despite being the
  largest win on paper. Do items 0 and 3 first: item 0 says whether the margins can
  move at all, and item 3 is the machinery this depends on.

### 8. Re-run the Texel tune with the high-frequency terms pinned

**This is a new finding and it may reopen a closed door.** `BUGS.md` 20 closed
hand-crafted evaluation tuning on three attempts that were each cancelled by node
cost, with the mechanism identified as "a more accurate evaluation produces scores
that move more between iterations". Reading the tuned weights on branch
`eval-texel-tune` (commit c980e23) against the shipped ones suggests a different
mechanism:

| weight | shipped | tuned |
|---|---|---|
| `MOBILITY` | 2 | **4** |
| `UNDEFENDED` | 5 | **8** |
| `ROOK_OPEN_FILE` | 10 | 19 |
| `CENTRE_CONTROL` | 5 | **−11** |
| `KING_CENTRE_DIST` | 4 | **7** |
| `KING_ACTIVITY` | 5 | **−5** |
| `ROOK_ON_7TH` | 10 | 1 |

Two things stand out. **The tuner doubled the weight on `MOBILITY` and raised
`UNDEFENDED` by 60%, and those are the two fastest-moving terms in the
evaluation**: both are counts over the whole side that change with every single
move, so they are the highest-frequency component of the score. Doubling the
highest-frequency term is a very plausible mechanical cause of scores moving more
between iterations, and it is a *scale* effect rather than an *accuracy* effect.
If that is what happened, then "this evaluation cannot afford to be more accurate"
is the wrong conclusion, and the right one is "the tuner is allowed to rescale the
noisiest term and the fixed ±50 aspiration window cannot absorb it".

**And `CENTRE_CONTROL` at −11 and `KING_ACTIVITY` at −5 are chess nonsense**,
which is the signature of a tuner compensating for missing structure rather than
finding truth. The clearest case is the king: the tuner raised `KING_CENTRE_DIST`
from 4 to 7, which rewards centralisation in **every** phase, and simultaneously
drove `KING_ACTIVITY` from +5 to −5, which penalises it in the endgame. Those two
are the same idea in different phase bands, and the tuner pushed them in opposite
directions because it had two untapered scalars and was trying to synthesise a
taper out of them. **That is a direct, readable argument for item 7** and it is
better evidence than the structural comparison in `EVAL-VS-ETHEREAL.md`, because
it is this repo's own fitting procedure telling us what it needed and could not
express.

- **Effect:** unknown on eval error, since the tune already generalised. The
  target is the node cost, not the accuracy.
- **Difficulty:** low. `tools/tune` exists, the corpus exists, and pinning two
  parameters is a command-line change.
- **Risk:** low, and it is cheap enough to be worth doing on the hypothesis alone.
  It is also the only item here that could retire `BUGS.md` 20, which is currently
  the entry blocking every other item in this list.

### 9. A tempo bonus

`evaluate_details()` reads `board.squares[]` and nothing else
(`evaluation.cpp:877-882`), so the same position scores identically with either
side to move. Stockfish 11 carries 13cp for the move (`Value(28)` on a scale
where a pawn is 213).

- **Effect:** small and uniform, plausibly a few centipawns of mean error, but it
  is a systematic bias rather than noise and it costs one line.
- **Difficulty:** trivial. The zobrist hash already includes side to move
  (`zobrist_hash.cpp:27`), so the eval cache stays correct and its hit rate is
  unchanged.
- **Risk:** low, but note the history: the term that was removed on 2026-08-14 was
  a `float 0.01f` that promoted the whole sum to float and truncated every score
  (`evaluation.cpp:834-838`). Keep the sum `int`.

### 10. King safety: do not build another version

Seven rejections across two instruments, the last of them the 1 680-game gauntlet
the documentation itself demanded, at 62.11% against 59.79%. Eight configurations
of the term produce one curve and none beats the crude version.

The only untried idea is the one the flagship position points at, and it is not
an attack count. In `r2r4/pN3pkp/Qb6/3qn1p1/3Pn3/4BP2/PP2P1PP/R3KB1R w KQ - 3 20`
the danger term scores **4cp** because exactly one black piece currently attacks
the king zone; the queen, the second knight and the bishop are all aimed at the
king and blocked (`HANDOFF.md:896-903`). The danger is latent, and a count of
current attacks cannot see it.

**Stockfish charges for exactly this and it has not been noticed here.**
`98 * popcount(pos.blockers_for_king(Us))` is a term in the SF11 `kingDanger` sum
(`sf_11/src/evaluate.cpp:450`), weighted comparably to the 148 for unsafe checks
and 185 for weak zone squares. `blockers_for_king` is the set of pieces standing
between the king and an enemy slider, which is precisely the queen, knight and
bishop that this position's danger term scores at zero. It is a geometry query
rather than another attack weighting, so it is a genuinely different experiment
from the eight that have already been run, and it is also the query that a
king-safety term shares with pin-aware move generation.

Note also that SF11's `kingDanger` transform is **quadratic in the middlegame and
linear in the endgame** (`kingDanger * kingDanger / 4096` against `kingDanger / 16`,
`sf_11/src/evaluate.cpp:461`), an 8x asymmetry at `kingDanger = 2000`. This
engine's is quadratic in both, scaled by a phase factor that section 3 showed is
pinned at 1.0 on 89% of `comp` positions. So the version rejected here charges a
squared penalty in endgames where Stockfish charges a linear one, which is one
more reason the term damaged `ctl` in every phase.

- **Effect:** unknown, and the prior after seven rejections is bad.
- **Difficulty:** medium on `bb_position`, high on `Board`.
- **Risk:** high, and the cost of finding out is a gauntlet, which is the
  expensive instrument. **Do items 0 through 9 first.** If one of them lands, the
  error decomposition changes and this question should be re-asked against the new
  numbers rather than the old ones.

### Not evaluation, but the honest answer to the branching factor

Section 1 concluded that the branching factor is held by two constants that no
evaluation change touches. Leaving them out of a ranked list would misrepresent
where the work is:

- **`const int R = 2` in null move** (`search.cpp:732`). No depth term, no eval
  term. Stockfish 11's is
  `R = (854 + 68 * depth) / 258 + std::min(int(eval - beta) / 192, 3)`
  (`sf_11/src/search.cpp:852`), a range of **3 to 9**: base 3 at depths 1 to 3,
  rising one ply per four plies of depth, plus one more ply per 90cp the static
  eval stands above beta. Master's is `7 + depth/3 + max((staticEval - beta)/256, 0)`
  (`master/src/search.cpp:1019`), so its *base* is 7. **This engine's fixed 2 is
  below even Stockfish 11's minimum.** The depth half is pure structure and is
  the larger half. This is the same shape of defect as `const int R = 1` in LMR,
  which was worth **+26.4 Elo** and 0.3 of branching factor when it was replaced
  by a table.
- **`LMP_MAX_DEPTH = 3`** (`search_tuning.hpp:46`), and a permissive threshold
  underneath it. Late move pruning never fires above depth 3, and where it does
  fire it keeps **5, 8 and 13** quiet moves at depths 1, 2 and 3 against
  Stockfish's **2, 3 and 6** at every depth, in both SF11 and master
  (`sf_11/src/search.cpp:81-83`, `master/src/search.cpp:1177`). So it is roughly
  twice as permissive where it applies and absent entirely where most of the tree
  is. Stockfish's own comment prices its shallow-pruning block at **~200 Elo**
  against **~50** for reverse futility and **~1** for razoring.

Both are pure structure, both are cheap, and both are gateable at equal nodes in
an hour. On the evidence in section 1, and on Stockfish's own ranking of what its
pruning is worth, they are worth more than anything in the evaluation list.
