# What makes our evaluation worse than a top-tier one, 2026-09-10

Prompted by the finding in `RESEARCH-SPEED-AND-SEARCH.md` that our pruning is
capped by evaluation quality: five recent search features stalled because they
prune on the evaluation, while the two that ignore it both shipped. So the
question became what specifically is wrong with the evaluation.

Measured with `tests/evalerror`, 1 051 positions from real games, scored against
Stockfish.

## There are two separate defects, not one

    tag     count   mean |err|    bias    sign flips
    comp      363       543.7     44.7    210 (57.9%)
    ctl       688       181.9   -10.4      10 (1.5%)

A sign flip is calling a won position lost or the reverse, by at least a pawn.
**On compensation positions we do that more than half the time.**

Split by game phase, the two defects separate cleanly:

| phase (material) | ctl | comp |
|---|---|---|
| opening/middle (>5000) | 146.0 | 536.3 |
| middlegame (3000-5000) | 205.8 | 545.9 |
| endgame (<3000) | **230.0** | 587.8 |

**Phase drift.** On ordinary positions the error grows 146 to 230 into the
endgame, a 58% increase. That is the untapered evaluation showing exactly where
middlegame and endgame values diverge most, and it is what
`EVAL-VS-ETHEREAL.md` predicted: every scalar in our evaluation is a single
number where a strong one has a middlegame and endgame pair, and only the king
is currently tapered.

**Compensation blindness.** Roughly 540 to 590cp in *every* phase. This is not a
tapering problem at all. It is 35% of the positions and **61% of the total error
mass**, so it is the larger defect by a wide margin.

## The cause of compensation blindness is a single disabled constant

    static const int KING_DANGER_SCALE = 0;   // percent; 0 is off

**There is no attacking-side king safety.** A queen, rook and knight swarming
the king score the same as an empty board. King safety is a placement term plus
a pawn shield capped at 24 centipawns that switches off the moment the king
leaves the back rank.

That is a complete explanation of a 540cp error on positions defined as "material
says one player is winning and the position says the other is". Compensation
*is* activity against a king, and we do not price it.

## What top evaluations do instead

The standard approach is an attack-unit count over a king zone, converted
through a **non-linear table**:

- minor pieces 2 units per attack, rooks 3, queens 5
- extra units for safe contact checks
- a lookup table with an S-shaped curve, rising slowly, then steeply, then
  flattening, so that several attackers together are worth far more than the sum
  of each alone
- fed alongside pawn shelter, pawn storm, open files near the king, and control
  of squares in the king zone

Ours already has most of this shape: a nine-square zone, per-piece weights, and a
quadratic curve. It is off, not missing.

The Chess Programming Wiki also notes that king safety is "one of the most
challenging tasks" to tune, and research on Stockfish's NNUE found that the
hand-crafted king-safety concept "does not have a clear correspondence in the
NNUE network; instead, the NNUE model seemingly has found an alternative and
more effective way of evaluating king safety". So this is the term that hand
crafting handles worst and that networks handle differently rather than better
at the same thing.

## The part that should change what we do next

King safety was rejected **six times**, most recently at −33.1 [−43.2, −23.0]
over 3 360 games. Every one of those was **self-play**.

`ROADMAP.md` 6.4 records why that may be the wrong instrument, in its own words:

> Self-play may be structurally unable to see this. A king-safety term pays
> against opponents who attack kings, and in self-play both sides share this
> engine's disinclination to. **Do not reopen 6.4 without building it first.**

The instrument it demanded is `tests/gauntlet.sh`, a fixed external opponent
instead of ourselves. **It now exists.** It was run once alongside the 08-21
gate, at 80 games: 30.6% against 32.5%, an interval of about ±80 Elo, which
resolves nothing in either direction.

So the current state of the largest defect in our evaluation is:

- the term is written and works on the diagnostic position, re-scoring a mating
  attack by −135cp where the shipped evaluation read +4
- it costs about 1.4% of nodes and 37cp of `comp` error for 1.1cp of `ctl`
- it was rejected six times by an instrument the project's own documentation
  says cannot measure it
- the correct instrument exists and has never been run at a sample size that
  could decide anything

## What I would do, in order

1. **Run a real gauntlet on king danger.** 80 games gave ±80 Elo; resolving a
   30 Elo effect needs roughly sixteen times that. This is the one experiment
   that could recover 61% of our evaluation error mass, and the blocker the docs
   named has already been removed.
2. **Taper the evaluation**, which addresses the other defect: 146 to 230cp of
   drift into the endgame. Bigger than it looks, because the pruning margins are
   calibrated against the current error distribution and would need re-deriving
   in the same piece of work (`BUGS.md` 20).
3. **Not more search pruning** until one of those lands, since
   `RESEARCH-SPEED-AND-SEARCH.md` shows that work is capped by exactly these
   numbers.

## Sources

Chess Programming Wiki on [King Safety](https://www.chessprogramming.org/King_Safety);
[Unveiling Concepts Learned by a World-Class Chess-Playing Agent](https://www.ijcai.org/proceedings/2023/0541.pdf)
on NNUE and king safety.
