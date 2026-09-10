# What actually makes a chess engine fast, measured against ours, 2026-09-10

Set up to answer two questions: what makes engines fast, and which search
heuristics earn their place. The useful part turned out to be a direct
measurement rather than the literature, because the literature agrees with our
numbers and then the numbers say something surprising.

## The measurement that reframes everything

Stockfish 16 and our engine, **same position** (kiwipete), **same machine**,
**single thread**, hash 64MB:

| depth | Stockfish nodes | our nodes | ratio |
|---|---|---|---|
| 11 | 16 151 | 5 681 796 | **352x** |
| 12 | 28 166 | 11 129 240 | **395x** |
| 13 | 31 653 | 20 689 916 | **654x** |

| | Stockfish | ours |
|---|---|---|
| nodes per second | 613k to 687k | **501k to 572k** |

**We run at about 80% of Stockfish's speed and use 400 to 650 times more nodes
to reach the same depth.**

That settles the question the whole exercise was set up to ask. Our problem is
not speed. It is not close to being speed. Every hour spent on faster move
generation, better data layout or bitboards is chasing the 20% while the 40 000%
sits untouched.

It also retires the target framing. "Depth 20 in 5 seconds" is not about
throughput: at 500k nps and Stockfish's node efficiency, depth 20 would take
well under a second.

## Where the 400x lives

**Branching factor.** Ours is about **2.0** after the LMR table shipped, down
from 2.3. Stockfish on the same position runs at roughly **1.4** on average,
with several iterations under 1.15.

Compounded over twelve plies, `(2.0/1.4)^12` is about **73x**, which is most of
the gap.

The Chess Programming Wiki says "the best engines today have a branch factor of
about 2", which is what made 2.0 look like arrival. Measured directly against a
current engine on the same position, it is not: 2.0 is roughly where a decent
engine sits and 1.4 is where a top one does, and the difference between those two
numbers is two orders of magnitude of work at depth.

**The base is also wrong, and it is the cheaper half.** A depth-1 search:

    ours   791 nodes
    SF     185 nodes

Depth 1 is almost entirely quiescence, so ours is **4.3x** larger before the main
search does anything. That compounds through every iteration on top of the
branching factor. It is also the one number with an obvious cause: our
`generateCaptures` calls `generateLegalMoves` and discards the quiet moves, so
every quiescence node pays full legal generation, including the make and unmake
legality filter, to keep about a tenth of the result.

## What takes a branching factor from 2.0 to 1.4

Everything below is standard, and the ones we have measured are marked.

| technique | in ours | our result |
|---|---|---|
| null move pruning | yes | shipped |
| razoring, reverse futility | yes | +39.1, +18.4 |
| late move pruning | yes | +13.1, then +15.0 |
| LMR, depth and move-count scaled | yes | **+26.4** |
| check extensions | yes | +23.0 |
| SEE-split capture ordering | yes | +25.6 |
| `improving` heuristic | built | **−10.6, null** |
| move-level futility pruning | **no** | untried |
| SEE pruning in the main search | **no** | untried |
| history-based reductions | **no** | untried |
| internal iterative reductions | **no** | untried |
| probcut | **no** | untried, hits the depth wall |
| continuation history | built | +6.8 null, as *ordering* |
| correction history | built | closed over three gates |
| singular extensions | built | undecided, closed by cost |

Two things stand out from that table.

**We have most of the tree-cutting techniques and are still at 2.0.** So the gap
is not a missing list item. It is that ours are individually less effective, and
the LMR result is the evidence: the technique was present and shipped, and
implementing it properly rather than as `const int R = 1` was worth +26.4 and
took the branching factor from 2.3 to 2.0. There is likely more of that kind of
gain in the ones already present than in the four that are absent.

**Continuation and correction history are core to Stockfish and measured null
here, and we may have tested the wrong use of them.** We gated both as *move
ordering* devices. In modern engines their main job is as **inputs to the
reduction and pruning decisions**: a quiet move with poor continuation history is
reduced further and pruned sooner. That is a different mechanism from ordering,
it is the one that moves branching factor, and it is untested here. Same tables,
different consumer.

## The evaluation coupling, which bounds all of it

Sorting our recent results by whether the technique consults the evaluation:

| consults the eval? | result |
|---|---|
| LMR table: pure structure, depth times move count | **+26.4** |
| `improving`: compares two static evals | −10.6 |
| `corrhist`: learns the eval's error | closed |
| `razortight`: tightens a margin sized by eval error | −1.0 |

The one clear winner is the one that never reads the evaluation. Our razoring
margin is 500 centipawns because the evaluation's own error is a median of 125
and a 90th percentile of 407, and `razortight` measured −1.0 trying to tighten it
to 350. **You cannot prune tightly on a signal that noisy**, so every
eval-dependent pruning technique underperforms its published value here.

Stockfish prunes at 1.4 partly because NNUE gives it an evaluation worth pruning
on. That is the uncomfortable part: some of the remaining branching-factor gain
is downstream of evaluation quality, and `BUGS.md` 20 records three attempts at
improving the evaluation that all failed because changing it invalidates the
pruning margins calibrated against the old one.

## What this says to do, in order

1. **Move-level futility pruning**, **SEE pruning in the main search**, and
   **history-based reductions**. All absent, all standard, all direct
   branching-factor mechanisms. History-based reductions are the most
   interesting because they reuse tables we already have and already populate.
2. **Staged move generation in quiescence.** Worth a 4.3x-shaped correction to
   the base rather than to the branching factor, and the cause is already
   identified in `generateCaptures`.
3. **Not speed work beyond that.** We are at 80% of Stockfish's nps. There is
   nothing there.
4. **Not NNUE yet**, but note that the evaluation coupling above is the ceiling
   on items 1 and 3, so the case for it strengthens as the pruning work runs out
   rather than weakening.

## Method note

The comparison above is worth more than everything the searching turned up, and
it cost one background invocation of a binary that was already installed at
`/usr/games/stockfish`. Where a reference implementation exists, measure against
it before reading about it.

Sources consulted: Chess Programming Wiki on
[Branching Factor](https://chessprogramming.org/Branching_Factor),
[Late Move Reductions](https://www.chessprogramming.org/Late_Move_Reductions),
[Futility Pruning](https://www.chessprogramming.org/Futility_Pruning) and
[Pruning](https://www.chessprogramming.org/Pruning); Chessify on
[NPS and time to depth](https://chessify.me/blog/nps-vs-time-to-depth-what-you-should-look-at-when-analyzing-with-stockfish).
