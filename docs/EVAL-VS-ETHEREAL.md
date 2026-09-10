# Our evaluation against an Ethereal-class hand-crafted evaluation, 2026-09-09

Prompted by advice from someone who has built engines rated 3400 and 3300: add
NNUE only after about 2900, get the search fast enough to reach depth 20 in five
seconds, optimise the search before improving the evaluation, and use Ethereal
12.x as the reference for a hand-crafted evaluation.

**A limit on this document.** Our side is read directly from
`src/engine/evaluation.cpp` and is exact. The Ethereal side is from general
knowledge of how that engine and its contemporaries are built, not from reading
its source, so treat the structural claims as reliable and any specific constant
as unverified.

## The terms he asked us to check

| term | ours |
|---|---|
| material value | present: 100, 325, 335, 525, 950 |
| piece square tables | present |
| mobility | present |
| space | present |
| bishop pair | present, flat 50 |
| bishop outpost | present, flat 10 |
| PeSTO tables | not those values, we have our own |
| knight fortress | absent |

We also have doubled, isolated, backward, connected, chained and passed pawns,
rook on open and semi open files, rook on the seventh, trapped pieces,
undefended pieces, centre control, king danger, king exposure, king activity and
tempo. So the *list* of concepts is close to complete, and the missing entries
are two of eight.

That matters because it means the gap is not a missing term. It is structural.

## The structural difference, which is the whole finding

**Every scalar term in our evaluation is a single number.**

    EVAL_WEIGHT PASSED_PAWN  = 20;
    EVAL_WEIGHT MOBILITY     =  2;
    EVAL_WEIGHT BISHOP_PAIR  = 50;
    EVAL_WEIGHT ROOK_OPEN_FILE = 10;

A passed pawn is worth 20 centipawns whether it sits on the third rank or the
seventh, and whether there are queens on the board or two kings and a pawn.
Mobility is worth two centipawns per available square, the same for a knight and
for a queen, in every position.

An Ethereal-class evaluation writes each of these as a tapered pair, one value
for the middlegame and one for the endgame, interpolated by material phase, and
usually indexes them by context as well: passed pawns by rank and by whether the
path is blocked, mobility by piece type and by a table over the number of
squares rather than a flat multiplier.

**We do taper, but only for the king.** `gamePhaseFactor` is computed at line
748 and used in exactly three places: king safety, king activity, and the king
piece square table, which is the one table stored as a middlegame and endgame
pair. The pawn, knight, bishop, rook and queen tables are single tables applied
identically in every phase.

So the difference is not that we are missing concepts. It is that we have one
number where a strong evaluation has two, and one table where it has a family of
them indexed by the thing that changes their value.

## Why this is not the next thing to fix

The obvious conclusion is to taper everything, and the evidence says do not,
which is worth stating carefully because it is counterintuitive.

`BUGS.md` 20 closed hand-crafted evaluation tuning after three attempts. Each
one produced a *more accurate* evaluation and each was cancelled by node cost:
eighteen Texel-tuned scalars gave +25.2 Elo for +59% nodes, five tuned piece
values transferred perfectly for +32.5% nodes, and an adaptive aspiration window
closed only to +22.7%.

The reason a better evaluation costs nodes is not arithmetic. It is that
razoring and reverse futility have margins calibrated against this evaluation's
measured error, which is a median of 125 centipawns and a 90th percentile of
407. Change the evaluation and those margins are calibrated against a
distribution that no longer exists, so the pruning gets less effective and the
tree grows. The gain and the cost arrive together and roughly cancel.

Tapering would be a larger change to that distribution than any of the three
attempts that failed, so the prior has to be that it behaves the same way unless
the pruning margins are re-derived at the same time. That is a much bigger piece
of work than it looks.

## What the measurements say to do instead

Depth reached in five seconds, measured 2026-09-09:

    Threads=1   depth 10.0
    Threads=6   depth 10.7

The target he named is 20. At our branching factor, depth 20 costs about 344
times the nodes of depth 13, which is roughly 18 billion nodes and about an hour
and a half rather than five seconds. So depth 20 in five seconds is not a next
step, it is a description of where a 3300 engine already sits, and the whole gap
between 2187 and 3300 is inside it.

The effective branching factor is where the room is. Measured on kiwipete at
`Threads=1`:

    depth  8    802,869     x2.11
    depth 10  4,459,463     x2.47
    depth 12 22,351,238     x2.14
    depth 13 52,156,680     x2.33

**About 2.3, against roughly 1.7 to 2.0 for a strong engine.** That is
respectable rather than broken, and it is also the single most valuable number
in this document: bringing 2.3 down to 1.9 would cut the node count at depth 13
by about twelve times, which is roughly three and a half extra plies at the same
clock. No evaluation change can do that. Only pruning, reductions and move
ordering can.

And the crudest thing in the search is the one that most directly sets branching
factor:

    bool reduce = lmr && depth >= 3 && moveIndex > 3 && !inCheck && move.flag == NORMAL;
    const int R = 1;

One fixed ply, whatever the depth and whatever the move number. Strong engines
use a logarithmic table indexed by depth and move count, adjusted by the move's
history score. Reducing the thirtieth move at depth 12 by the same single ply as
the fourth move at depth 3 is exactly what holds a branching factor at 2.3.

`PLAN.md` 3.6 has always listed retuning this, and `TODO.md` says it was
deferred "deliberately last, against a search that has stopped changing shape".
The search has now stopped changing shape.

## What I take from the advice

Three of his four points are confirmed by our own measurements, and the fourth
is confirmed by our own failures.

**Search before evaluation** is right, and the branching factor of 2.3 says
where. **NNUE only after about 2900** is right, and it is a better argument than
the one this project had: we parked NNUE on the finding that the hand-crafted
evaluation already explains 94.5% of the labels a self-play corpus produces,
which is a narrower objection than "you are 700 points too early". Both point
the same way. **Ethereal as the reference** is right about structure, and the
structural lesson is taper and context indexing rather than any missing term.

The one place I would push back is on **fixing the evaluation next**, even
though ours is clearly a generation behind. This project has measured, three
times, that a more accurate evaluation does not convert while the pruning
margins are tuned to the old one. That is a local fact about this engine rather
than a disagreement with him, and it means the evaluation work he would
recommend has a prerequisite: the search has to stop being the thing that
cancels it.
