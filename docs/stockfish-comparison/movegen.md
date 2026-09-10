# Move generation and move ordering, ours against Stockfish

Written 2026-09-11. Read only, no benchmarks run: a timed gate was live on the
machine, so every quantity below is either already recorded in this repo or
derived from reading both sources.

Stockfish is at commit `59aae690f91d6f69aac194f447d84b4a2c3be778` (master,
fetched 2026-09-11). Line numbers on their side refer to that commit; line
numbers on our side refer to the working tree as of the same date, which
includes the uncommitted bitboard replacement.

The question this answers is narrow. `docs/RESEARCH-SPEED-AND-SEARCH.md` already
established that our problem is tree size and not throughput: 11,129,240 nodes
to reach depth 12 against Stockfish's 28,166 on kiwipete, at 80 percent of their
nps. So the only interesting differences in move generation are the ones that
change how many nodes get visited, or that change what the search is able to
decide before it visits one.

---

## 1. Staged and lazy generation

### What Stockfish does

`MovePicker` is a state machine that hands back one move per call and generates
each class of move only at the moment it is first needed
(`src/movepick.cpp:33-57` for the stage list, `src/movepick.cpp:282-383` for
`next_move()`).

The order of events at an ordinary interior node:

1. `MAIN_TT` returns the transposition-table move having generated nothing at
   all (`src/movepick.cpp:289-294`). Its only cost was `pos.pseudo_legal(ttm)`
   in the constructor (`src/movepick.cpp:176`).
2. `CAPTURE_INIT` generates captures only, `MoveList<CAPTURES>`
   (`src/movepick.cpp:299`), scores them, and partially sorts them
   (`src/movepick.cpp:301-304`).
3. `GOOD_CAPTURE` runs SEE per capture, but only on captures it actually reaches
   (`src/movepick.cpp:310-316`). A capture that fails the SEE test is swapped
   into a bad-capture region and deferred rather than discarded.
4. `QUIET_INIT` generates quiets, and only here (`src/movepick.cpp:321-329`).
   It is skipped entirely when `skipQuiets` is set.
5. `BAD_CAPTURE` and `BAD_QUIET` replay the deferred material
   (`src/movepick.cpp:345-358`).

The consequence that matters: a node whose TT move causes a beta cutoff
generates zero moves, and a node whose first good capture cuts never runs the
quiet generator. Late move pruning is expressed as
`mp.skip_quiet_moves()` at `src/search.cpp:1177-1178`, which does not skip
*searching* quiets, it stops them being generated and scored in the first place.

### What we do

Every main-search node generates the complete legal move list up front, before
anything is known about whether it will be needed:

- `src/engine/search.cpp:741` calls `generateLegalMoves(board, side)`
  unconditionally, immediately after the null-move probe.
- `src/engine/search.cpp:780` then scores and sorts the entire list in
  `MoveOrderer::orderMoves`, which computes a score for every move
  (`src/engine/move_ordering.cpp:81-127`), including a full SEE resolution for
  every capture (`src/engine/move_ordering.cpp:277-281` calling
  `see(board, move)`).
- `src/engine/search.cpp:783-788` then swaps the TT move to the front, after
  the sort has already run.
- Only then does the move loop start at `src/engine/search.cpp:840`.

The bitboard core has exactly the same shape: `src/engine/bb_search.cpp:640`
generates everything, `src/engine/bb_search.cpp:681` sorts everything.

Our late move pruning is a `continue` inside that loop
(`src/engine/search.cpp:913-926`), and so is our move-level futility
(`src/engine/search.cpp:902-911`). Both of them skip the *search* of a move that
has already been generated, legality-checked, scored and sorted.

### What that costs, conceptually

At a typical middlegame node there are around 35 legal moves, of which roughly 3
to 5 are captures. Our per-node fixed cost is therefore:

- one full pseudo-legal generation over all 64 squares
  (`src/engine/movegen.cpp:135-341`),
- one legality pass over all ~35 candidates
  (`src/engine/movegen.cpp:384-441`),
- ~35 score computations, of which 3 to 5 resolve a whole exchange,
- one sort of ~35 elements,
- and, on the mailbox path, a heap allocation, because `MoveList` is
  `std::vector<Move>` (`src/engine/move.hpp:50`) with a 20-byte `Move`
  (`src/engine/move.hpp:15-22`) returned by value from
  `generateLegalMoves` (`src/engine/movegen.cpp:462`).

In a well ordered alpha-beta search roughly half of interior nodes are cut
nodes, and the large majority of those cut on the first move tried, which here
is the TT move. Every one of those nodes pays the whole list above to use one
move. Stockfish pays a `pseudo_legal` check.

This is a cost-per-node gap, not a branching-factor gap, so it does not by
itself explain the 395x. It is worth stating in the same breath that
`lateMovePruning` cuts 45.5 percent of nodes at bench depth 6
(`src/engine/search.hpp:222-223`) and cuts exactly zero percent of the
generation and ordering work at the node where it fires, because it fires after
that work is done. That is the clearest single place where our structure and
Stockfish's diverge.

The one place we already do this properly is quiescence. `stagedGen`
(`src/engine/search.hpp:508`) routes quiescence through
`generateLegalCaptures` (`src/engine/movegen.cpp:455-460`), which builds only
tactical moves via `TacticalFilter` (`src/engine/movegen.cpp:106-118`), and
`maskedGen` (`src/engine/search.hpp:558`) suppresses quiet move construction at
the source. On the bitboard core `BB_GEN_CAPTURES` does it with a target mask
instead (`src/engine/bb_movegen.cpp:228-233`), which is the same thing
Stockfish's `generate<CAPTURES>` does. That change was worth about 4.7 percent
of wall time and, by construction, zero nodes
(`src/engine/search.hpp:501-507`).

---

## 2. Move ordering: what each side scores, and on what

### Our bands

`MoveOrderer::getMoveScore`, `src/engine/move_ordering.cpp:247-306`:

| band | score | source |
|---|---|---|
| TT move | 1,000,000 | `move_ordering.cpp:251` |
| sound capture | 900,000 + MVV-LVA (+ capture history) | `move_ordering.cpp:281` |
| promotion | 800,000 + promoted piece value | `move_ordering.cpp:286` |
| killer 1 / killer 2 | 700,000 / 690,000 | `move_ordering.cpp:293-295` |
| losing capture (SEE < 0) | 600,000 + MVV-LVA | `move_ordering.cpp:279` |
| everything else | butterfly history, 0 to 16,384 | `move_ordering.cpp:305` |

The bitboard core reproduces this exactly, band for band
(`src/engine/bb_move_ordering.cpp:117-141`), which is deliberate because the
acceptance test for the replacement is an exact node count.

The whole list is scored in one pass and sorted
(`src/engine/move_ordering.cpp:98-127`). Quiescence has its own scoring pass
with the same idea (`src/engine/search.cpp:404-451`).

### Stockfish's bands

`MovePicker::score`, `src/movepick.cpp:194-265`. Stockfish does not use bands at
all inside a stage, because the stages already separate the classes. Within a
stage:

- Captures: `captureHistory[pc][to][victim] + 7 * PieceValue[victim]`
  (`src/movepick.cpp:225-226`). That is MVV plus capture history, with no LVA
  term, and the good/bad split is not a band but a SEE test at emission time
  with a threshold scaled by the move's own score,
  `pos.see_ge(*cur, -cur->value / 18)` (`src/movepick.cpp:311`).
- Quiets: a sum of seven separate signals (`src/movepick.cpp:233-253`).
- Evasions: captures by victim value plus a large constant, quiets by main
  history plus one ply of continuation history (`src/movepick.cpp:256-262`).
  We use the same scoring for evasions as for everything else, since our
  in-check path just generates all legal moves
  (`src/engine/search.cpp:741`, `src/engine/bb_search.cpp:640`).

### What Stockfish reads that we do not

Taking the quiet scoring at `src/movepick.cpp:233-251` term by term:

| term | Stockfish | ours |
|---|---|---|
| butterfly history | `2 * mainHistory[us][move]`, `movepick.cpp:233` | `historyTable[from][to]`, `move_ordering.cpp:305`. Shipped, but bonus-only. |
| pawn-structure history | `2 * sharedHistory->pawn_entry(pos)[pc][to]`, `movepick.cpp:234` | absent |
| continuation history at plies 1, 2, 3, 4 and 6 | `movepick.cpp:235-239` | built, one ply only, gated OFF |
| quiet-check bonus | `((pos.check_squares(pt) & to) && pos.see_ge(m, -75)) * 16384`, `movepick.cpp:242` | absent, and not expressible |
| threat term | `PieceValue[pt] * 20 * (threatened(from) - threatened(to))`, `movepick.cpp:244-247` | absent |
| low-ply history | `8 * lowPlyHistory[ply][move] / (1 + ply)`, `movepick.cpp:250-251` | absent |
| capture history on captures | `movepick.cpp:225` | built, gated OFF |
| correction history | `history.h:189-194`, applied to the eval not the order | built, closed |

Two of those are worth separating from the rest, because they are the only two
that read nothing derived from the evaluation: the quiet-check bonus and the
threat term. Everything else in the "absent" column is a learned history table
of some kind, and this project's record on those is unambiguous.

### The verdicts already recorded here

These are on the toggles, which is where this repo keeps the living verdict:

- `contHist`, `src/engine/search.hpp:382-391`: +6.8 Elo [-6.1, +19.8] over 1,680
  games. Null, stays off. The mechanism is understood: it costs +12.2 percent
  nodes (445,492 to 499,901), and at an equal node budget the better ordering
  and the larger tree it orders roughly cancel.
- `captHist`, `src/engine/search.hpp:393-396`: built, off.
- `corrHist` and `corrHistQ`, `src/engine/search.hpp:398-453`: three gates,
  +6.3 null, then -39.7 rejected, then +0.4 null for persistence alone. Family
  closed.
- `improving`, `src/engine/search.hpp:661-687`: -10.6 [-31.9, +10.7]. Null with
  a negative estimate.
- `moveFutility`, `src/engine/search.hpp:510-545`: built, not gated, -1.9
  percent tree with zero best moves changed. Too small to gate.
- `histReduction`, `src/engine/search.hpp:625-659`: cannot work as built,
  because `updateHistory` only ever adds
  (`src/engine/move_ordering.cpp:149-186`), so a reduction reading it can only
  reduce a good move less and never a bad move more. Four divisors all grew the
  tree.
- `histMalus`, `src/engine/search.hpp:611-623`: built, ungated. This is the
  missing precondition for the one above.

The pattern the project extracted from that list is in
`docs/RESEARCH-SPEED-AND-SEARCH.md:101-122`: techniques that consult the
evaluation underperform here because the evaluation's own error has a median of
125cp and a p90 of 407cp, and the one large recent win, the LMR table at +26.4
(`src/engine/search.hpp:455-486`), is the one that never reads it.

I want to add a second axis to that pattern, because it explains the history
nulls better than the evaluation coupling does. `contHist` did not fail because
it read the evaluation, it does not read the evaluation. It failed because it
made ordering better while making the tree 12.2 percent bigger, and it was gated
on nodes. **An ordering change that costs nodes cannot win a node-budgeted
gate.** The quiet-check bonus and the threat term are ordering changes that cost
essentially no nodes, only per-node arithmetic, so they sit in a different
category from the history family and should not inherit its verdict.

---

## 3. Legality

### Stockfish

Stockfish generates pseudo-legal moves and tests legality per move, at the
moment the search reaches it, at `src/search.cpp:1136` in the main search and
`src/search.cpp:1786` in quiescence. `Position::legal`
(`src/position.cpp:662-699`) is three cases: castling walks the king path,
a king move tests `attackers_to_exist(to, pieces() ^ from, ~us)`, and everything
else is `!(blockers_for_king(us) & from) || line_bb(from, to) & pieces(us, KING)`.
En passant is not special-cased there, it is handled inside `pseudo_legal` and
by the generator.

`generate<LEGAL>` exists (`src/movegen.cpp:271-289`) and filters only the moves
that could possibly be illegal, `(pinned & from) || from == ksq || EN_PASSANT`,
but **the search never calls it**. It is used for move validation and asserts.

Generation itself is not pin-aware. `generate_all` masks by target squares and
by evasion resolution, and pins are handled downstream.

### Ours

Two implementations, and they answer this differently.

The mailbox generator is pseudo-legal plus a masked filter,
`filterLegal` at `src/engine/movegen.cpp:384-441`. It computes checkers, the
blockers-for-king set and the check resolution mask once per node
(`movegen.cpp:398-407`), then answers each candidate with a bit test:

- king moves: `attackersTo(st, m.to, occWithoutKing) & enemyOcc`
  (`movegen.cpp:421-424`), which is the same expression as
  `src/position.cpp:694`;
- pinned pieces: `testBit(lineThrough(ksq, m.from), m.to)`
  (`movegen.cpp:430-433`), which is the same rule as `src/position.cpp:698`;
- en passant: deliberately kept on the make-test-unmake path
  (`movegen.cpp:416-420`), which is the one place we are slower than Stockfish
  and the comment there says why.

So our legality *rule* is Stockfish's rule. The difference is when it runs: we
run it over every generated move, Stockfish runs it over every move the search
actually reaches, and at a cut node those are very different counts.

The bitboard generator goes further than either and is **pin-aware at
generation**: `bbGenerate` (`src/engine/bb_movegen.cpp:215-284`) builds the
check mask (`bb_movegen.cpp:257-259`) and the pinned set
(`bb_movegen.cpp:262`) and intersects them into the target mask, so an illegal
move is never constructed. Pinned knights are dropped outright rather than
masked (`bb_movegen.cpp:264`, `bb_movegen.cpp:268-269`), pinned pawns get a
per-pawn pin ray (`bb_movegen.cpp:273-279`), king moves are tested against an
occupancy with the king removed (`bb_movegen.cpp:239-246`), and en passant gets
an explicit rebuilt-occupancy test (`bb_movegen.cpp:53-76`) rather than a
make and unmake.

**This is a real gap that is not worth closing.** Deferring legality to the
search, Stockfish style, would save work only on moves we generate and never
reach, and on the bitboard core those moves cost almost nothing to make legal
because the masks are already computed for the ones we do reach. The saving
would come from not *generating* them at all, which is item 1, not item 3.

---

## 4. What Stockfish's generator enables that ours cannot express

Four things, in descending order of how much they matter.

**a. `check_squares` and `gives_check`.** `Position::gives_check` and
`Position::check_squares` (`src/position.h:141` and the `checkSquares` state)
let Stockfish know, without making the move, whether a move gives check. It uses
that in three places: as a 16,384-point ordering bonus for quiet checks
(`src/movepick.cpp:242`), as an exemption from capture futility pruning
(`src/search.cpp:1183-1196`), and as an exemption from the whole quiescence
pruning ladder (`src/search.cpp:1798`).

We have none of it. `grep` for `givesCheck` across `src/engine/` returns
nothing. Our check extension works by testing `inCheck` at the *child* node
(`src/engine/search.cpp:370` in quiescence and the equivalent in the main
search), which is correct but is only usable after the move has been made. Every
pruning decision that wants "do not prune a checking move" is therefore
unavailable to us, and so is any ordering bonus for quiet checks.

The machinery to build it exists on the bitboard core and is cheap: the check
squares for each piece type are `bishopAttacks(theirKsq, occ)`,
`rookAttacks(theirKsq, occ)`, `knightAttacks(theirKsq)` and
`pawnAttacks(them, theirKsq)`, all already declared in
`src/engine/bitboard_attacks.hpp:37-46` and all already called in
`src/engine/bb_evaluation.cpp:52-60`. Discovered checks need the
blockers-for-their-king set, which is `blockersForKing`
(`bitboard_attacks.hpp:74`) with the colours swapped.

**b. `skip_quiet_moves`.** `src/movepick.cpp:385` plus the `skipQuiets` guards
at `src/movepick.cpp:322` and `335`. This turns late move pruning from a
decision about searching into a decision about generating. We cannot express it
because our quiets are generated and sorted before the loop starts
(`src/engine/search.cpp:741` and `780`).

**c. `attacks_by<PieceType>`.** `src/movepick.cpp:205-209` builds
`threatByLesser` once per node from `pos.attacks_by<PAWN>(~us)`,
`attacks_by<KNIGHT>`, `attacks_by<BISHOP>` and `attacks_by<ROOK>`, and then
scores each quiet by whether it moves a piece off a threatened square or onto
one. We have no aggregate attack sets. The evaluation computes per-piece
mobility (`src/engine/bb_evaluation.cpp:52-60`) and throws the attack bitboards
away, and the mailbox evaluation gets mobility from
`countPseudoLegalMoves` (`src/engine/movegen.cpp:348-352`), which returns a
count and not a set.

**d. A capture generator with a SEE threshold, for ProbCut.**
`src/movepick.cpp:181-189` and `src/search.cpp:1062-1101`. We have SEE
(`src/engine/see.cpp`, and `src/engine/bb_see.cpp` on the new core) so this is
buildable, but `docs/RESEARCH-SPEED-AND-SEARCH.md:79` already flags that ProbCut
hits the same depth wall that closed `singularExt`
(`src/engine/search.hpp:288-298`): it fires at depth >= 3 with a reduced search,
and our gates run where the useful depths are barely reached.

---

## 5. The quiescence base, which is the largest concrete gap

`docs/RESEARCH-SPEED-AND-SEARCH.md:50-60` measured a depth-1 search at 791 nodes
against Stockfish's 185, and attributed it to `generateCaptures` calling
`generateLegalMoves` and discarding quiets. `stagedGen` fixed that part, and it
changed zero nodes by construction (`src/engine/search.hpp:501-507`). So the
4.3x is still there, and the cause has to be somewhere else.

Reading both quiescence loops side by side, it is. Stockfish's quiescence prunes
hard, `src/search.cpp:1794-1829`:

- `moveCount > 2` skips every further move outright
  (`src/search.cpp:1801-1802`), subject to the guards on the line above:
  not a checking move, not a recapture on the square the previous move landed
  on, not a promotion, and the score not already a loss
  (`src/search.cpp:1798-1799`);
- futility on the captured piece's value (`src/search.cpp:1804-1812`);
- `see_ge(move, alpha - futilityBase)` (`src/search.cpp:1815`);
- non-captures dropped (`src/search.cpp:1823-1824`);
- `see_ge(move, -74)` on what remains (`src/search.cpp:1827-1828`).

Ours prunes almost not at all. `src/engine/search.cpp:453-470` walks the whole
sorted tactical list. `seePruning` is off (`src/engine/search.hpp:34`),
`deltaPruning` is off (`src/engine/search.hpp:50`), and there is no move-count
limit of any kind. Every legal capture at every quiescence node gets searched.

Note which of Stockfish's five is the eval-free one. `moveCount > 2` reads the
evaluation only through `is_loss(futilityBase)` and `is_loss(bestValue)`, which
are mate-score guards rather than centipawn judgements. The futility test at
1804 and the SEE-versus-alpha test at 1815 are eval-dependent, and those are the
shape of the thing that cost this project 50 Elo at a 200cp margin
(`docs/GATES.md`, `shard-20260814-153213/`) and closed `deltaPruning` at +0.9
(`shard-20260821-112153/`). Quiescence move-count pruning is a different bet
from the one that failed here, and it has not been made.

There is one smaller and entirely mechanical difference in the same area.
Stockfish's `make_promotions` (`src/movegen.cpp:86-103`) puts the queen
promotion in `CAPTURES` and the rook, bishop and knight underpromotions in
`QUIETS` unless the promotion is also a capture. So their quiescence sees one
move per quiet promotion push. Ours emits all four in the tactical set, on both
cores: `TacticalFilter` keeps every `PROMOTION`
(`src/engine/movegen.cpp:113-114`), and `addPromotions`
(`src/engine/bb_movegen.cpp:44-47`) is called from the `wantCaps` branch
(`src/engine/bb_movegen.cpp:109-110`). We search three quiet underpromotions
per promoting pawn that Stockfish never generates. That only fires with a pawn
on the seventh, so it is narrow, but it is free to fix and it is a strict subset
change.

---

## 6. Ranked changes

Ranked by expected value, which here means expected tree reduction divided by
the chance that it turns into another null.

### 1. Quiescence move-count pruning

**What.** After the first two tactical moves at a quiescence node, skip the
rest, with exemptions for promotions, for a recapture on the square the previous
move landed on, and when a mate score is in play. Mirrors
`src/search.cpp:1798-1802`, minus the `givesCheck` exemption we cannot compute
yet. Fits into the existing loop at `src/engine/search.cpp:453-470` and
`src/engine/bb_search.cpp:290` onwards as a counter and a `continue`, behind a
new toggle.

**Effect.** Attacks the measured 4.3x quiescence base directly. Quiescence is
the base of every iteration, so a constant-factor reduction there multiplies
through the whole tree and is visible at an equal node budget, the same way LMP
at +13.1 was. A node-budgeted gate can rule on it.

**Difficulty.** Low. One counter, three guards, one toggle. The recapture
exemption needs the previous move's destination square plumbed into
`quiescence()`, which currently takes only `ply` and `qDepth`
(`src/engine/search.cpp:350-352`, `src/engine/bb_search.cpp:244-246`).

**Risk.** Medium, and honestly stated. It is a prune, so a wrong guess loses a
move rather than costing a re-search, and without `gives_check` we would prune
capture-checks that Stockfish keeps. That argues for pairing it with change 2
below, or for starting at a looser threshold than 2. It is the one item here
that could plausibly lose Elo outright rather than measuring null.

### 2. `gives_check` and `check_squares` on the bitboard core

**What.** Compute the four check-square masks and the discovered-check blocker
set once per node from the enemy king square, then answer "does this move give
check" with a bit test. Mirrors what `src/movepick.cpp:242` and
`src/search.cpp:1158` consume.

**Effect.** On its own, an ordering change: a bonus for quiet checks, which is
`+16384` in Stockfish's scale (`src/movepick.cpp:242`) and would sit near the
top of our history band. Its larger value is as an enabler: it is the missing
guard for change 1, for capture futility, and for any future pruning rule that
needs to exempt forcing moves.

**Difficulty.** Low to medium on the bitboard core, where every needed function
already exists (`src/engine/bitboard_attacks.hpp:37-46, 74`). Awkward on the
mailbox core, which is another argument for gating `bitboardCore`
(`src/engine/search.hpp:608`) first.

**Risk.** Low as an ordering change, because it costs no nodes, only per-node
arithmetic. Note that this does not inherit the `contHist` null: that measured
null because it grew the tree 12.2 percent while improving the order, and this
grows it not at all.

### 3. SEE pruning of late quiet moves in the main search

**What.** Skip a quiet move whose SEE is worse than a depth-scaled threshold.
Stockfish does this at `src/search.cpp:1237`, `!pos.see_ge(move, -23 * lmrDepth
* lmrDepth)`. The eval-free version drops the history and `improving` terms that
feed their `lmrDepth` and uses remaining depth directly.

**Effect.** A direct branching-factor mechanism, listed as absent and untried in
`docs/RESEARCH-SPEED-AND-SEARCH.md:77` and item 1 of its recommendations. It is
structural: SEE counts material on a square and never consults the evaluation,
which puts it on the same side of the divide as the LMR table.

**Difficulty.** Low. `see()` exists (`src/engine/see.cpp`) and `bbSee()` exists
(`src/engine/bb_see.cpp`), and `seeOrdering` already trusts them enough to have
gated at +25.6 (`src/engine/search.hpp:24`). The cost is that SEE is currently
computed for captures only (`src/engine/move_ordering.cpp:277`), so this adds
exchange resolutions on quiets, bounded by only running on moves that survive
LMP at shallow depth.

**Risk.** Medium. It is a prune. But it is the prune whose signal we have
already measured as trustworthy, unlike every margin sized against a 407cp p90.

### 4. Staged generation in the main search

**What.** Replace `generate everything, sort everything` at
`src/engine/search.cpp:741` and `780` with a picker in Stockfish's stage order:
TT move with no generation at all, then captures generated and scored, then
good captures emitted with SEE deciding good from bad, then quiets generated
and scored, then bad captures, then bad quiets. Mirrors
`src/movepick.cpp:282-383`.

**Effect.** Large reduction in cost per node and none at all in nodes. Given the
project's own finding that we run at 80 percent of Stockfish's nps and the whole
gap is tree size, this is chasing the 20 percent. Its real value is that it
makes LMP stop generating rather than stop searching, which is the structural
precondition for change 1 of the recommendations in
`docs/RESEARCH-SPEED-AND-SEARCH.md`.

**Difficulty.** High. This is the largest change in the list and it touches the
one thing the repo protects hardest, which is the exact node count.

**Risk.** High, and specifically not the risk it looks like. Staging changes the
emitted order, because our bands and Stockfish's stages disagree in one place:
our killers score 700,000 and our losing captures 600,000
(`src/engine/move_ordering.cpp:293` and `279`), so we currently search losing
captures *after* killers but *before* every other quiet, while Stockfish
searches bad captures after all good quiets (`src/movepick.cpp:334-347`).
A staged picker cannot preserve our current order without emitting quiets before
bad captures, which means generating quiets anyway, which defeats the point. So
this is an ordering change wearing a performance change's clothes and needs its
own gate.

### 5. History malus, then history as a reduction and pruning input

**What.** Gate `histMalus` (`src/engine/search.hpp:623`), which is built and
ungated, then use the resulting two-sided table as a reduction input
(`histReduction`, `src/engine/search.hpp:659`) and as a pruning input in
Stockfish's form, `if (history < -4136 * depth) continue`
(`src/search.cpp:1213`).

**Effect.** This is the chain the repo has already identified
(`src/engine/search.hpp:643-658` and
`docs/RESEARCH-SPEED-AND-SEARCH.md:93-99`) and it remains correct: the history
family gated null as *ordering* over five gates and 10,080 games, and its main
job in modern engines is as an input to reduction and pruning. Same tables,
different consumer.

**Difficulty.** Low. Both toggles exist and the code is written.

**Risk.** Medium. `histMalus` changes move ordering, so it has to be gated on
its own before anything is stacked on it, and an ordering change here has a poor
local track record. But the measured reason `histReduction` fails is structural
and specific, that the table is one-sided, and this fixes exactly that.

### 6. Threat-based quiet ordering

**What.** Build `threatByLesser` once per node as at `src/movepick.cpp:204-209`
and apply the penalty and bonus at `src/movepick.cpp:244-247`.

**Effect.** Ordering only, eval-free. Plausibly worth something, and it is one
of the two Stockfish quiet-scoring terms we cannot currently express at all.

**Difficulty.** Medium. Needs aggregate attack sets by piece type, which nothing
currently builds. The pieces exist on the bitboard core but the loops do not.

**Risk.** Medium, mostly cost. This adds real per-node arithmetic to a node that
already sorts 35 moves, and it is an ordering change at equal nodes, which is
the exact profile that returned +6.8 null for `contHist`. Worth building only
after 1 to 3.

### 7. Quiet underpromotions out of the quiescence set

**What.** Emit only the queen promotion for a quiet promotion push in the
tactical set, keeping all four for capture-promotions, matching
`src/movegen.cpp:92-100`. On the bitboard core that is one branch in
`addPromotions` (`src/engine/bb_movegen.cpp:44-47`) keyed on whether it was
called from the capture path; on the mailbox core it is a condition in
`TacticalFilter` (`src/engine/movegen.cpp:113-114`).

**Effect.** Removes three quiescence moves per promoting pawn push. Real but
narrow, since it fires only with a pawn on the seventh.

**Difficulty.** Trivial.

**Risk.** Low but not zero: a quiet knight underpromotion that gives check or
forks is a real tactic, and it would be lost from quiescence. Keeping queen and
knight and dropping rook and bishop costs almost nothing and removes that
concern entirely.

### Gaps that are real and not worth closing

**Deferred legality checking.** Stockfish tests legality per move reached
(`src/search.cpp:1136`), we test it per move generated
(`src/engine/movegen.cpp:384-441`). The rule is identical, and on the bitboard
core legality is folded into the generation masks
(`src/engine/bb_movegen.cpp:257-279`) at close to zero marginal cost. The saving
lives in not generating, which is change 4, not in deferring the test.

**Pin-aware generation.** We already have it and Stockfish does not
(`src/engine/bb_movegen.cpp:262-279` against `src/movegen.cpp:271-289`, which is
a filter and is never called from their search). Nothing to close.

**ProbCut.** Buildable, since we have SEE, but it needs depth to fire and this
project has already closed one feature for exactly that reason
(`src/engine/search.hpp:288-298`). Do not start here.

**Faster move generation.** The bitboard core is 2.16x faster at generation and
1.62x on identical search work (`src/engine/search.hpp:605`). Gate it, because
it is built and it is free speed, but expect nothing from it on branching
factor. `docs/RESEARCH-SPEED-AND-SEARCH.md:23-29` settled that. Its real value
in this document is that it is the substrate on which changes 2 and 6 become
cheap.
