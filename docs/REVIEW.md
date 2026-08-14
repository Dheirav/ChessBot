# Game Review — plan

A post-game analysis pass in the style of Chess.com's Game Review: classify
every move, score each side's accuracy, and say what changed and why.

Not started. This is the plan, written 2026-08-15 while the engine was still in
Phase 3/5, so that the prerequisites can be built in the right order rather than
discovered later.

---

## The constraint that shapes everything below

**Review quality is bounded by the strength of the engine doing the reviewing,
and this engine is not yet strong enough to be the judge.**

Chess.com runs Stockfish at high depth. Relative to the players it grades, that
is an oracle: when it says "blunder", it is right. This engine is Lichess rapid
**2065, rd=77** — a peer of the players whose games it would be reviewing. It
will confidently call sound moves blunders and miss real ones, and nothing in
the output will indicate which.

Two consequences, and they are design decisions rather than caveats:

1. **The analysis engine must be swappable.** The review pipeline should drive
   *a* UCI engine, not *this* one. Point it at Stockfish for trustworthy output;
   point it at ChessBot to see what ChessBot thinks. `tests/uci_engine.hpp`
   already drives an arbitrary UCI binary — it was written for gating, and it is
   most of what this needs.
2. **The pipeline is worth building before the engine is ready.** Every phase
   below is independently verifiable, and none of them get *wrong* as the engine
   improves — they get better.

Do not ship a review feature that presents this engine's judgements as
authoritative until it has a demonstrated reason to be believed.

---

## R0. A PGN reader — **DONE 2026-08-15**

`fromSan()`, `parsePgn()` and `readPgn()` in `pgn.cpp`. **All 48 games in
`game_records/` parse and round-trip, 3 738 moves**, and `tests/pgn` now
round-trips 200 seeded random games (23 669 moves) with no external files, so
the property is guarded in CI.

Implemented by generating the legal moves and asking `toSan()` which one spells
that way, rather than by parsing the notation. SAN's hard part is
disambiguation, `toSan()` already solves it, and a second implementation would
be a second place for it to be wrong. Decoration PGN permits but `toSan()` never
emits — `0-0`, `e8Q` without the `=`, `+`/`#`, `!?` — is handled by normalising
both sides before comparing.

**It found one real bug on the way in.** `0-0-0` begins with a digit, so the
movetext loop classified it as a move number and skipped it *silently* — a game
two moves shorter than the record, with no error. That is precisely the failure
this parser refuses to commit for illegal moves, and it took a test to notice.

The original note follows.

**The hard prerequisite.** `pgn.cpp` writes and does not read: `toSan`,
`toPgn`, `writePgn` and nothing in the other direction.

Reading is the harder half — disambiguation (`Nbd2`), castling (`O-O` and
`0-0`), promotions, check and mate suffixes, comments, NAGs, variations, and
the header block. The legal move generator makes it tractable: for each SAN
token, generate the legal moves and find the one whose SAN matches.

**Verification is exact and free.** Read a PGN, write it back, compare byte for
byte. There are already **48 games** in
`/home/dheirav/Code/lichess-bot/game_records/` — real games, from a real
opponent pool, including promotions, en passant, castling and adjudicated
finishes. A round-trip test over all of them is a genuine test suite that costs
nothing to obtain.

Do this first even if the rest is deferred: it is independently useful, it is
the only piece with a perfect correctness criterion, and every later phase needs
it.

---

## R1. The analysis loop — **DONE 2026-08-15**

`tools/review.cpp`, built by `make review`.

```
./tools/review game.pgn [--engine <path>] [--engine-arg <a>] [--depth N] [--hash MB]
```

Defaults to `/usr/games/stockfish`. ChessBot needs `--engine-arg --uci`, since
its default mode opens a window.

**One search per position, not two.** This plan called for searching each
position and then the position after the move played — but the position after
move *i* is position *i+1*, which the loop already visits. `n` moves need `n+1`
searches, not `2n`. Halves the cost for free, and no MultiPV is required.

Loss for move *i* is `score[i] − (−score[i+1])`: the second score is from the
opponent's point of view and has to be negated onto the mover's scale. That was
flagged here as the easiest thing to get wrong, and the check that it is right is
that losses are near zero for most moves — if they are large everywhere, the sign
is inverted.

Mate scores are clamped to ±1000 rather than subtracted as if they were
centipawns: "mate in 3" minus "mate in 5" is not 200 of anything, and one missed
mate should not swamp a game's average.

**It works, and its first real run found the bug this project spent a day on.**
Reviewing `Crimsy_Bot vs sargon-2ply` (CTGzqoeY), the game drawn a rook up:

```
 13. Qa8+     Blunder     - 525 cp   (best Qb7, eval +525)
```

That is `BUGS.md` 1 — the threefold the engine walked into — independently
priced by Stockfish at the full value of the win it gave away.

Two things it turned up on the way:

- `UciEngine::start()` hardcoded `--uci`, because it was written to drive
  ChessBot. A standard engine treats an unrecognised argument as a command and
  exits, so Stockfish died on the handshake and reported itself as a broken
  pipe. Arguments are a parameter now.
- The engine answers in UCI (`c6a8`); a review has to say `Qa8+`. Both the
  played move and the engine's preference are translated through `toSan()`.

---

## R2. Classification

Centipawn loss into buckets: Blunder, Mistake, Inaccuracy, Good, Excellent,
Best. The thresholds are conventional and can be copied; what makes the output
feel right is the special cases:

| label | rule | needs |
|---|---|---|
| Book | position is in an opening database | an opening book — nothing here has one |
| Brilliant | material sacrificed *and* still the best move | SEE, which exists |
| Great | the only move that holds the position | second-best is much worse — a real MultiPV need |
| Miss | a forced win existed and was not played | mate detection, which exists |

**Accuracy %** is a function of average centipawn loss. Chess.com's exact curve
is theirs; any monotonic mapping works, but pick one and write down *why*, or it
will be tuned to flatter whoever is being reviewed.

Note that "Great" is the one label that genuinely wants MultiPV. It may be
cheaper to drop the label than to add the feature.

---

## R3. Term attribution — where this could be better than Chess.com

`EvalDetails` exposes **25 named terms**. Chess.com says "Blunder, −3.5" and
draws a graph. This could say *which part of the position changed*:

> **Mistake** (−1.8). King safety −45: the pawn shield is gone. Mobility −22.

That falls out of infrastructure that already exists, and it is the one place
where a weaker engine is least of a handicap — attributing a change to named
terms is *descriptive*, not a judgement about best play.

**The trap:** `evaluate_details` returns a *static* evaluation, and the score
being explained comes from a *search*. Explaining a tactical blunder in terms of
king safety would be actively misleading — the position at the root did not
change much; the position at the end of the principal variation did. Attribution
must be computed at the end of the PV, not at the move.

If that turns out to be hard, say nothing rather than something plausible and
wrong. A confident wrong explanation is worse than a bare number.

---

## R4. Presentation

Undecided, deliberately. The obvious options are a PGN with annotations and NAGs
(works in every existing viewer, no UI to build), a self-contained HTML report,
or something in the existing SFML GUI.

The first is by far the cheapest and reaches the most tools. Prefer it until
there is a reason not to.

---

## Explicitly not doing

- **Reimplementing Stockfish's analysis.** If trustworthy output is the goal,
  drive Stockfish. The interesting work here is the pipeline, not competing on
  engine strength.
- **A tablebase or cloud-eval integration.** Both are how Chess.com gets
  authority cheaply, and both are external dependencies that would make the
  review depend on the network.
- **An opening book, for now.** "Book" is the least interesting label and the
  only one needing a new data dependency.

---

## Order, and the honest prerequisite

R0 and R1 are done. R2 → R4 next, with R3 whenever it is wanted. Each is independently
demonstrable.

But the prerequisite is not on this list: **the engine's evaluation is the
binding constraint on everything here**, and there is no phase in `PLAN.md` for
improving it. Phase 4 made the evaluation *correct* — mirror-symmetric, no
colour-blind constants, honest "defended" — which is not the same as making it
*good*. It remains hand-written piece-square tables whose own gate returned
+6.1 with the interval spanning zero.

The strength curve says the same thing: 96% against opponents under 1500, 31%
against 2100-2300. A review tool built on that judgement is a tool that is
confidently wrong about the games most worth reviewing.

Build R0 now — it is useful regardless and perfectly testable. Weigh the rest
against tuning the evaluation, which is the work that would make the reviewer
worth listening to.
