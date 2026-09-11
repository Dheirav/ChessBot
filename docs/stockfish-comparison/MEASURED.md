# What the branching-factor work actually measured

2026-09-11. Companion to `README.md`, which said what to build; this says what
happened when it was built. Every toggle below is off by default.

## The instrument mattered more than any of the changes

Three instruments, and they disagree so badly that the choice decided the
answer twice.

**`tests/bench`** totals twelve positions. It read the null-move change at
-14.8% at depth 7 and +5.0% at depth 8, two plies apart, opposite signs. It is
useless below about ten percent and `BUGS.md` 21 now says so.

**`tools/treecost`** searches hundreds of positions both ways and reports the
distribution of per-position ratios plus a sign test. It overturned bench twice:
`detsort` looked like a 5% cost and is not systematic at all, and `nulldepthr`
looked inert and is the largest single tree reduction found.

**`tools/configsweep` plus `tools/adjudicate.py`** measures depth reached in a
fixed time *and* whether the move was right. The first version scored "right" as
"matches what this engine concludes with four times the budget", which is not
ground truth and is biased against the configurations under test: a setting that
searches deeper finds moves the shallow reference missed, and every one of those
scored as an error. Adjudicating the disagreements against Stockfish moved about
seventy percent of them out of the error column.

## Tree effects, paired, median of per-position ratios

| change | depth 8 | depth 10 | shape |
|--------------|---------|----------|--------------------------------|
| `nulldepthr` | -10.8% | -21.4% | slope: the cut doubles with depth |
| `interiorpvs`| -9.0% | -10.1% | slope, after the fix below |
| `lmpdeep` | -6.2% | -10.8% | slope |
| `qmovecount` | -5.5% | -5.8% | base, at limit 3 |
| `qnounderpromo` | -0.02% | -0.05% | negligible |
| `histmalus` | -0.6% | -2.5% | **null**, sign test -0.7 sd |
| `conthist` | +0.00% | +0.05% | **null**, sign test -0.1 sd |

Slope changes move the branching factor; base changes divide the whole tree and
leave the ply-to-ply ratio alone. Both are worth having and only the first
answers "how do we get deeper".

## Two things that were built wrong and measured right

**`interiorpvs` was not doing what its number said.** Scouting by itself costs
+1.8% nodes. All of its apparent -14.1% came from a side effect: `isPV` was
derived from the window as `beta - alpha > 1`, so adding null-window scouting
made it read false almost everywhere, which does not merely remove the PV
exemption, it *enables* razoring, reverse futility and late move pruning inside
the principal variation. Those are the eval-margin rules and this evaluation's
margins are 500 centipawns wide. Fixed by carrying an explicit `onPvLine` flag.
Honest version: -9.0%, and now growing with depth instead of shrinking.

**The quiescence limit was on the wrong side of a knee.** At depth 9 it is worth
-1.6% at 4 and -14.0% at 3, because most quiescence nodes do not have four
tactical moves so a limit of 4 almost never fires. The idea was right; the
parameter was wrong, and only sweeping several settings exposed it.

## Depth in five seconds, one thread, and whether the moves survived

24 positions, 5 s each, disagreements adjudicated by Stockfish at depth 18.

| configuration | depth | agreement said | actually wrong | found better |
|---------------------|-------|----------------|----------------|--------------|
| shipped | 11.75 | 20/24 | **2** | 1 |
| `null+verify+lmp` | 12.67 | 19/24 | 1 | **4** |
| `q2 bare` | 13.08 | 19/24 | 1 | 3 |
| everything | 14.04 | 17/24 | **1** | 2 |

The agreement column is wrong and the adjudicated column is the one to read.
`everything` was called a trap on the strength of 17/24; one of its seven
disagreements was a mistake, four were equally good moves and two were better
than the reference found.

**Every genuine miss is in a position already decided.** The four real errors
occur at evaluations of -318, -283, +767 and -242, and cost 15 to 162
centipawns. Losing 66 centipawns in a position already lost by three pawns
changes nothing. The improvements, by contrast, are in live positions: +48 to
+137, +69 to +116, -164 to -126.

So the trade is the reverse of what a disagreement count implies. These settings
play better where the game is still open and slightly worse where it is already
over. One of the four misses appears in five of the six configurations including
the shipped one, so it is a position five seconds cannot solve rather than
anything a feature caused.

## Where the engine stands

| | morning | measured best |
|-------------------------|-----------|---------------|
| depth in 5 s, 1 thread | 10 to 11.6| 14.0 |
| branching factor | 2.03 | ~1.85 |
| depth-10 tree | 7 874 754 | 3 683 574 |

## What is not established

Nothing here is a gate. Node counts and move quality over 24 positions are not
Elo, and the one gate that did run, on the bitboard core at `--tc 10+0.1`,
returned +3 [-17, +24] at a control that reaches depth 7, where that core's
node penalty is 1.28x against 1.08x by depth 9. It measured the wrong regime.
