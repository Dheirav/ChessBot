# Why the branching factor is 1.9, and what would move it

Four comparisons against Stockfish, written 2026-09-11: `movegen.md`,
`search-heuristics.md`, `pruning.md`, `evaluation.md`. This is the synthesis.
Read this first and the others for the evidence.

## The question

Reaching depth 12 costs this engine 11,129,240 nodes against Stockfish's 28,166
on the same position, while running at 80 percent of its nps. The gap is what
gets searched, not how fast. Each extra ply costs about 1.87x here against about
1.4x there, measured as a marginal ratio over depths 11 to 13. Nine more plies
is therefore 280x of work against 21x.

Stockfish's figure is not a constant, incidentally: their own wiki gives 2.38 at
depth 10 falling to 1.49 at depth 50, so this comparison is only meaningful at a
stated depth.

## The finding that reorganises everything else

**A pruning rule that only fires near the leaves cannot change the branching
factor.** Write the tree as a product of per-ply widths. A rule that cuts only
the bottom three plies gives `N(d) = f^3 * w^d`, so `N(d+1)/N(d) = w` no matter
how large `f` is. It buys a one-off constant factor, which is worth Elo, and
leaves the slope alone.

This project has already run the experiment without noticing. Razoring cut 21
percent of the tree. Late move pruning cut 45.5 percent. Both won real Elo. The
branching factor stayed at 2.3. It moved once, when the LMR table shipped, and
that is the one rule whose cut grows with depth: -2.9 percent at depth 5 against
-56.1 percent at depth 11.

So the question is not which pruning rules are missing. Most of the missing ones
are capped at depth 3 and would buy Elo without touching the branching factor.
The question is which mechanisms cut proportionally harder as depth grows.

There is an operational test, and it should become the habit: measure a
candidate's tree cut at two depths with `tools/treecost`. Growing means it is a
branching-factor change. Flat means it is a constant factor. Both are worth
having; only one answers this question.

## Does the evaluation block it? No, and this is the surprise

The evaluation is genuinely bad: median absolute error 125 centipawns, p90 407,
and 61 percent of the error mass is compensation blindness. It genuinely does
force wide margins. Three independent confirmations: `search.cpp:641` derives
the margins from the measured error and says so; delta pruning at the textbook
200 centipawns cost -50.0 Elo; and move futility is inert at 300+150 per ply but
over-prunes at 100+100, with nothing in between. That empty window is eval error
killing a technique outright.

But the margins are not what sets the branching factor, and there is a measured
proof already in `GATES.md`: `razortight` narrowed the razoring margin from 500
to 350 and measured **-1.0 [-10.0, +7.9]** over 3,360 games. Tightening the
margin bought nothing, because razoring fires only at depth 2 or less.

Branching factor is set at interior nodes, by two constants:

- null move reduction, `search.cpp:732`, `const int R = 2`, fixed, which is
  below Stockfish 11's *minimum* of 3 and far below master's roughly 11 at
  depth 12
- late move pruning, capped at depth 3, keeping 4, 7 and 12 quiet moves at
  depths 1 to 3, where Stockfish keeps about 2, 3 and 6 at *every* depth

Neither would be relaxed by any evaluation improvement whatsoever. Both are the
same shape as `const int R = 1` in LMR, the defect this project already
diagnosed, already fixed, and was already paid +26.4 Elo for. `search.hpp:456`
states it outright: a constant reduction "is the main reason the effective
branching factor sits at 2.3".

The honest restatement is not "we are capped by the evaluation" but **"we are
out of eval-based pruning and have barely started on the eval-free kind."**

## What the evaluation error does block

It blocks the shallow-pruning block, which Stockfish prices at roughly 200 Elo
against roughly 1 for razoring. So it is worth a great deal, just not here and
not for this question.

Reaching textbook margins needs the p90 of `ctl` error down from 407 to about
150, a 2.7x reduction. Whether that is even possible is **unmeasured**:
`tests/README.md` records a 282 centipawn irreducible dynamics floor on `comp`,
and the same number for `ctl` has never been computed. `tools/sf-label.py
--depth 2` over the 688 corpus positions answers it in minutes, and the answer
decides whether any evaluation work in this area can ever pay. Do that before
committing to an evaluation project.

## Ranked

### 1. Depth-scale the null-move reduction

`R = 2 + depth/4` clamped, leaving Stockfish's eval-dependent bonus out of the
first version. Add the verification search the comment at `search.cpp:725`
already claims exists but does not, because zugzwang is the real risk above
about R = 4. Also missing beside it: a consecutive-null guard, and null move
currently runs at PV nodes.

Estimated 15 to 30 percent cut at depth 11, growing with depth. Branching factor
perhaps 1.87 down to 1.75 to 1.80. Reads nothing from the evaluation.

Gate at `-N 1000000`, not `-N 100000`: `GATES.md` records the LMR table reading
+2.7 [-10.2, +15.6] at the lower budget because depth 5 could not see it.

### 2. Uncap late move pruning

Currently depth 3 and below, keeping 4, 7, 12. Stockfish keeps about 2, 3, 6 at
every depth. Removing the cap converts a constant factor into a slope change.
Eval-free apart from the mate guard.

### 3. Principal variation search at interior nodes

There is none: PVS exists at the root and inside the LMR probe and nowhere else,
so `isPV = beta - alpha > 1` is true across most of the tree, and razoring,
reverse futility, LMP and move futility are all gated on `!isPV` and therefore
inert exactly where the nodes are. Fix in three steps: an explicit PV-line flag
rather than deriving it from the window, then the null-window scouts, then
`cutNode`, which adds about 3.9 plies of reduction in Stockfish and is
meaningless until the null-window invariant actually holds.

Both cores need the edit: `bb_search.cpp:843` mirrors `search.cpp:941` and
`bbequiv` requires node-identical trees.

### 4. Quiescence move-count pruning

Quiescence is a 4.3x multiplier on the whole tree and currently walks the entire
sorted tactical list. Stockfish stops after `moveCount > 2`. Exempt promotions,
recaptures on the previous move's destination, and mate scores. Nearly
eval-free. This is the one item that could lose Elo outright rather than measure
null, because without `gives_check` it would prune capture-checks Stockfish
keeps.

### 5. Do not generate what pruning will discard

LMP is a `continue` at `search.cpp:913`, so the node has already generated and
sorted every move, including a full SEE resolution per capture. Stockfish's
`skip_quiet_moves()` stops the quiets being generated. Pure speed, no tree
change, no risk.

### 6. `gives_check`

Absent entirely. Everything needed is already in `bitboard_attacks.hpp`. It
unlocks the check exemptions that make items 4 and several Stockfish pruning
rules safe, and is worth a large ordering bonus in its own right.

### 7. Raise `CORR_CAP` and read it as confidence

`CORR_CAP` is +/-96 centipawns, which is *below* the 125 centipawn median error
it exists to correct. Raise it, then use `|corrHist|` as a per-position
confidence signal to widen the futility margin and shrink the LMR reduction,
as master does in three places. This is the only mechanism that escapes the
empty-window trap, because a per-position margin is not obliged to be right for
`comp` and `ctl` at once.

### 8. Three terms that do not do what they are named

Not branching factor, but they are defects. `drawish` is unreachable, because
`whiteMaterial` includes the king's 20000, and it is excluded from the total
anyway, so there is no scale factor of any kind. `trapped` charges any rook or
bishop on any edge square. `outpost` has no outpost condition. Passed pawns are
flat 20 where Stockfish 11 pays up to 130 by rank plus 319 for a safe path.

### 9. Tapering

`gamePhaseFactor` is pinned at exactly 1.0 on 69.9 percent of `ctl` and 89.3
percent of `comp` positions, it uses total material including pawns where
Stockfish uses non-pawn material, and `kingActivityBonus` is a hard step at 2000
creating a 30 centipawn discontinuity that both `improving` and the aspiration
window difference across.

## NNUE

The failed net was three trainer bugs, not the blocker. The blocker is that a
5,000-node self-play label is partly made of the evaluation it is meant to
replace, so the 0.945 correlation is circular rather than damning. Do not
retrain on the existing corpus. The choice is external labels or nothing.

`ROADMAP.md`'s unmeasured +200 to +400 prior should be **+90 or less**: that is
what Stockfish measured for the same substitution.

## What to do first

Items 1, 2 and 3 are the branching factor. All three are eval-free, which is the
axis every unqualified win here has come from. Item 1 is a handful of lines and
is the same defect already fixed once in LMR for +26.4 Elo.

Before any evaluation project, run `tools/sf-label.py --depth 2` and find out
whether `ctl` error can fall at all.
