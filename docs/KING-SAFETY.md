# King safety: why seven measurements said no, and what is actually missing

## The defect it is meant to fix

`tests/evalerror` over 1 051 positions from real games, scored against Stockfish:

| tag | count | mean abs error | sign flips |
|---|---|---|---|
| `comp` | 363 | **543.7** | **210 (57.9%)** |
| `ctl` | 688 | 181.9 | 10 (1.5%) |

A sign flip is calling a won position lost or the reverse by at least a pawn.
On compensation positions, where material says one side is winning and the
position says the other is, **we pick the wrong winner more than half the time**.
That is 35% of the corpus and **61% of total error mass**, and it does not vary
with game phase (536 / 546 / 588 from opening to endgame), so it is not the
tapering problem.

The cause is one constant:

    static const int KING_DANGER_SCALE = 0;   // percent; 0 is off

There is no attacking-side king safety. A queen, rook and knight swarming the
king score the same as an empty board.

## Seven measurements, all negative

| instrument | result |
|---|---|
| self-play, four arms, 10 080 games | negative, `ROADMAP.md` 6.4 |
| self-play, exposure 100% + danger 300%, 3 360 games | **−33.1 [−43.2, −23.0]** |
| gauntlet, 80 games, 2026-08-21 | 30.6% vs 32.5%, ±80 Elo, decides nothing |
| **gauntlet, 1 680 games, 2026-09-10** | **62.11% → 59.79%** |

The last one matters because it removes the standing excuse. `ROADMAP.md` 6.4
recorded that self-play may be structurally unable to see this: both sides get
the term, both share this engine's disinclination to attack, so it prices at
zero. It said *do not reopen without building a gauntlet first*. The gauntlet was
built, and had only ever been run at 80 games, which resolved nothing.

Run properly at 1 680 games against Stockfish held at 400 nodes, the candidate
scored **59.79% against the baseline's 62.11%**, a difference of 17.0 Elo in the
baseline's favour with a 95% interval of [−4.2, +38.2]. Not decisive alone, but
the same direction as self-play's −33.1.

**The instrument objection does not rescue king safety.** Two different kinds of
measurement, both negative.

*Method note:* the two runs used different seed bases, so they played different
openings and the comparison is unpaired. Same openings would have paired it and
tightened the interval materially. A re-run should fix that.

## Why it fails, which is now measured rather than guessed

The term improves the evaluation and worsens the play:

| | comp error | ctl error |
|---|---|---|
| off | 543.7 | 181.9 |
| scale 500 | 504.5 | **189.3** |

**It makes the rare case better and the common case worse.** Compensation
positions are a few percent of what a search visits; ordinary positions are
nearly all of it. So the engine spends almost all its time using an evaluation
that got worse.

The reason is the curve. It is quadratic in a weighted count of attacked squares,
starting from zero, so it charges *something* almost everywhere:

    one queen touching 4 zone squares  ->  160 cp
    one rook touching 3                ->   50 cp

Those are ordinary positions. A queen on an open diagonal near a king is piece
activity, not an attack. **Our term counts proximity, not danger.**

## Two fixes that did not work, and why that is informative

**A threshold on distinct attackers.** Raising it moved `comp` and `ctl` back
toward baseline *together* rather than separating them:

| min attackers | comp | ctl |
|---|---|---|
| 1 | 504.5 | 189.3 |
| 2 | 509.2 | 188.6 |
| 3 | 524.9 | 184.6 |
| 4 | 539.9 | 183.8 |

**An offset on the danger score**, which is Ethereal's actual mechanism
(`SafetyAdjustment = S(-74, -26)` clamped by `MAX(0, mg)`). Same shape:

| offset | comp | ctl |
|---|---|---|
| 0 | 504.5 | 189.3 |
| 8 | 524.7 | 182.4 |
| 14 | 535.1 | 182.2 |

Both are one-dimensional trades along a single curve. Neither separates the two
error classes, and the reason is important: **a threshold controls how much
wrongness gets through, not whether the signal is right.** Ethereal's threshold
works because everything feeding it is accurate; ours is fed by raw proximity.

## What Ethereal actually computes

From `src/evaluate.c`:

    safety  = kingAttackersWeight                    // N 48, B 24, R 36, Q 30
    safety += SafetyAttackValue   * scaledAttackCounts        //   45
            + SafetyWeakSquares   * weakSquaresInKingArea     //   42
            + SafetyNoEnemyQueens * !enemyQueens              // -237
            + SafetySafeQueenCheck  * queenChecks
            + SafetySafeRookCheck   * rookChecks
            + SafetySafeBishopCheck * bishopChecks
            + SafetySafeKnightCheck * knightChecks            //  112
            + pksafety                                        // shelter + storm
            + SafetyAdjustment;                               //  -74

    eval += -mg * MAX(0, mg) / 720;

Against ours:

| | Ethereal | ours |
|---|---|---|
| king zone | 9 squares | 9 squares, same |
| attacks counted | **excludes squares defended by two pawns** | counts everything |
| trigger | `attackersCount > 1 - popcount(enemyQueens)` | none |
| **safe checks** | dominant term, knight check = 112 | **absent** |
| **weak squares** | attacked, defended ≤1, only by queen or king | **absent** |
| **defenders** | `KingDefenders[count]` subtracted | **absent** |
| no enemy queen | −237, effectively switches the term off | **absent** |
| offset | −74 with `MAX(0, mg)` | absent |
| tapering | every constant is `S(mg, eg)` | single scalars |

**We copied the shape and none of the content.** Nine-square zone, weighted
attackers, quadratic curve, and no notion of whether the attack can achieve
anything or whether anything is defending.

`SafetySafeKnightCheck = 112` against `SafetyAttackValue = 45` states the point:
in Ethereal a single check the defender cannot answer outweighs two attacked
squares. We do not know what a check is.

## What did work, partially

Adding a defender term, friendly pawns and minors in the king's own zone,
subtracted before charging:

| defender weight | comp | ctl |
|---|---|---|
| 0 | 504.5 | 189.3 |
| **4** | **509.5** | **186.0** |
| 6 | 512.1 | 186.0 |
| baseline | 543.7 | 181.9 |

**The `ctl` damage nearly halves**, +7.4 to +4.1, for 5cp of `comp`. That is the
first change that improved the *trade* rather than sliding along it, and it
confirms the diagnosis: we were charging for undefended proximity.

It is still only about 3.0% of total error mass, against 2.8% without it, so it
is not enough on its own to expect the gauntlet result to flip.

## The missing terms, now built, and they do not help either

Weak squares, safe checks, defenders and the no-enemy-queen condition are all
implemented, behind compile-time flags that default to zero. Measured against
the same corpus:

| config (scale 500) | comp | ctl | comp flips |
|---|---|---|---|
| baseline, term off | 543.7 | 181.9 | 210 |
| proximity only | 504.5 | 189.3 | 178 |
| + defenders | 509.5 | 186.0 | 187 |
| + weak squares | 501.1 | 210.8 | 177 |
| + safe checks | 493.9 | 226.3 | 179 |
| + both | 494.3 | **269.0** | 170 |

The new terms improve `comp` slightly and wreck `ctl`, because our danger is
squared and adding contributions to a raw count amplifies quadratically in every
position. Ethereal avoids that with `SafetyAdjustment = -74` and `MAX(0, mg)`,
so ordinary positions charge nothing *before* the quadratic. Applying an offset
over the full set:

| offset | comp | ctl |
|---|---|---|
| 0 | 493.3 | 256.8 |
| 25 | 532.7 | 199.3 |
| 35 | 539.6 | 189.0 |
| 50 | 542.9 | 183.9 |

**The same one-dimensional trade, and worse than the crude version.** At matched
`ctl` damage of 189, proximity-only gives `comp` 504.5 while the full
Ethereal-shaped term gives 539.6.

## Why, and what the blocker actually is now

Eight configurations were tried: scale sweep, attacker-count threshold, danger
offset, defenders, weak squares, safe checks, no-enemy-queen, and the offset
over the full set. Every one produces the same curve and none beats the crude
version.

The likely cause is not the design but the **weights**. The term now has six
interacting parameters and all of them are hand-guessed. Ethereal's are not
guesses: `S(48,41)`, `S(24,35)`, `S(42,41)`, `S(112,117)`, `S(-74,-26)` are
fitted together against a large position set, and each is a *pair* because that
evaluation is tapered throughout. Six interacting constants will not land near
an optimum by inspection.

**So the blocker has moved.** It is no longer "we lack the terms"; the terms
exist. It is "we cannot set six interacting weights by hand", which is a tuning
problem, and `tools/tune` already does coordinate descent of exactly this kind.

That is also a warning. `BUGS.md` 20 closed hand-crafted evaluation tuning after
three attempts that each produced a more accurate evaluation and were each
cancelled by node cost. Tuning these six would be a fourth, on the term with
seven prior rejections.

## What is left to build



**Safe checks** and **weak squares**, which are the two largest missing terms.
Both need a per-side attack map with counts. One already exists in
`evaluation.cpp` (`attackedBy[3][64]`, built in the threat loop) but it is a
boolean and it is built at line ~838, *after* king safety runs at ~808. So it
needs reordering and upgrading to a count.

Safe checks additionally need the squares a piece *could* check from, not the
squares it currently attacks, which is a different query.

## Standing conclusion

Do not re-gate this term until safe checks and weak squares exist. Seven
measurements have rejected a version that lacks all three of Ethereal's
defensive components, and the eighth would be measuring the same caricature with
one component added.

Worth holding alongside: research on Stockfish's NNUE found that the
hand-crafted king-safety concept "does not have a clear correspondence in the
NNUE network; instead, the NNUE model seemingly has found an alternative and
more effective way of evaluating king safety". This is the term hand-crafting
handles worst, and it is 61% of our evaluation error.

Sources: [Ethereal `evaluate.c`](https://github.com/AndyGrant/Ethereal),
Chess Programming Wiki on [King Safety](https://www.chessprogramming.org/King_Safety),
[Unveiling Concepts Learned by a World-Class Chess-Playing Agent](https://www.ijcai.org/proceedings/2023/0541.pdf).
