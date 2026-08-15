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

## R2. Classification — **DONE 2026-08-15**

Moves are judged in **win probability**, not centipawns, and running the whole
game archive is what forced that.

### The measurement that decided it — **taken through a broken instrument**

The original argument here was empirical: over 49 games, raw average centipawn
loss came out **83.0 in games the bot won** against **29.8 in games it lost**,
which reads as "it plays worse when it wins" and was taken as the metric
failing.

**That inversion was mostly `BUGS.md` 10.** `score mate 0` was parsed as a win
for the side that had just been mated, so every game the bot won by mate carried
a phantom 2 000-centipawn blunder on the mating move. Won games contain more
mates, so won games scored worst. Re-measured on the corrected parser over the
62 games now in the archive, centipawn loss does not invert:

| | accuracy | avg cp loss | games | moves |
|---|---|---|---|---|
| games won | 95.8% | 16.4 | 40 | 1 479 |
| games drawn | 95.4% | 18.8 | 6 | 369 |
| games lost | 92.8% | 30.7 | 16 | 727 |
| **overall** | **94.9%** | **20.8** | **62** | **2 575** |

Two claims this section used to make are withdrawn: that centipawn loss inverts,
and that accuracy is flat across results. Neither survives the fix. Accuracy is
*lower* in lost games, by three points, which is the direction it should run —
games are lost partly by playing worse in them.

**The decision to judge moves in win probability stands**, and it now rests on
the argument rather than on that data. In a decided position a 300-centipawn
slip changes nothing: +900 to +600 still wins, and costs **6.4** percentage
points of win probability. The same 300 from level is the whole game, and costs
**25.1**. Centipawns score those identically and win probability does not. That
compression is real whether or not the measurement above ever demonstrated it,
which is why the unit is not being changed back. Centipawn loss is kept in the
output as a diagnostic rather than a judgement.

The lesson is `BUGS.md` 10's: a metric validated against a broken instrument can
be right for the wrong reason, and nobody finds out until the instrument is
fixed.

### What was built

- `winPercent(cp)` — the standard logistic, so the compression is built in.
- `accuracy(wpLoss)` — Lichess's curve. One defensible mapping of many, and
  adopted rather than invented precisely because a curve designed here would
  end up tuned until the numbers flattered whoever was being reviewed.
- Thresholds in win-probability points: Blunder ≥ 20, Mistake ≥ 10, Inaccuracy
  ≥ 5, then Good, Excellent, Best.
- Per-side accuracy, average centipawn loss, and a count of each label.

### Regenerating this profile

```bash
make review                      # builds tools/review; skip if a gate is running
./tools/archive-profile.py       # the whole archive
./tools/archive-profile.py --compare 2026.08.15-13:27:00   # before vs after a build
```

`tools/archive-profile.py` produces every number in this section. It caches each
game's review under `--work`, so re-running after new games only analyses the
new ones, and `--jobs` defaults to 4 rather than the core count because the bot's
own games are on a real clock and starving them corrupts the evidence being
collected.

It exists because these figures were quoted as fact while being reproducible
only by whoever still had the throwaway scripts. A documented number with no way
to regenerate it is the same defect as a documented command nobody has run —
and this file has already been wrong once, when `BUGS.md` 10 corrupted the
figures it published.

Timestamps are **Lichess UTC**, not local time. `--compare` takes the moment a
build went live; **2026.08.15-13:27:00** is when the engine with the
hanging-piece term removed started playing.

### Whole-archive profile, regenerated 2026-08-15 on the corrected parser

62 games, 2 575 of the bot's own moves, Stockfish 16 at depth 14:

| | accuracy | avg cp | games | score |
|---|---|---|---|---|
| overall | **94.9%** | 20.8 | 62 | 69% |
| as White | 94.4% | 23.3 | 33 | 62% |
| as Black | 95.4% | 18.4 | 29 | 78% |

By opponent strength, which is the cut that matters and the one the first
profile did not make:

| opponent | games | score | accuracy | avg cp |
|---|---|---|---|---|
| under 1500 | 17 | 97% | 96.6% | 14.2 |
| 1500-1900 | 18 | 86% | 95.4% | 19.3 |
| 1900-2100 | 11 | 73% | 94.6% | 24.3 |
| 2100-2300 | 13 | 23% | 93.9% | 22.0 |
| 2300+ | 3 | 0% | 93.2% | 29.2 |

Three and a half accuracy points separate the band the bot beats 97% of the time
from the one it has never scored in. `ROADMAP.md` 6.1 is what that implies.

**Do not compare that 94.9% to a Chess.com number.** Accuracy is a function of
the analysing engine, its depth, and the curve — all three differ. It is a
baseline to compare *this* engine against itself over time, and nothing else.
It is also not comparable to the 92.6% this file used to quote: different
parser, different archive size.

### Not implemented, deliberately

`Book`, `Brilliant`, `Great` and `Miss` are all still unimplemented. `Great`
needs MultiPV — Stockfish has it, `UciEngine` does not request it. `Brilliant`
needs "material sacrificed and still best". `Miss` needs mate scores preserved
rather than clamped. Each is a small piece of work and none of them change what
the tool is for, which is why the core landed without them.

---

## R3. Term attribution — **DONE 2026-08-15**

`--explain` adds a line under each criticised move naming which of this
engine's evaluation terms changed:

```
  6...dxc3     Blunder     -25.1 win%  (-414 cp, best Qd6, eval -157)
        threats -750 material +200 piece placement -75
 13. Qa8+      Blunder     -36.8 win%  (-511 cp, best Qxd7+, eval +511)
        threats -750 material -100 piece placement +75
```

### The trap, and how it is handled

This plan warned that `evaluate_details` is *static* while the score being
explained comes from a *search*, so comparing the two starting positions would
explain a hanging queen as a change in centre control. Attribution therefore
happens **at the end of each principal variation**, not at the move —
`UciEngine` now captures the PV for that purpose.

That introduced a second problem the plan did not anticipate. Two lines starting
from *different* moves diverge, and the deeper they run the more a term diff
measures the divergence rather than the move: a 55-centipawn inaccuracy was
being explained by a 397-point swing in threats, which is two different
positions talking. Both lines are now walked a bounded **6 plies**, far enough
for a tactic to resolve and no further. The same inaccuracy now reads
`bishop pair -50, piece placement +45`.

### What this actually is, stated plainly

It explains a move **in the vocabulary of this engine's evaluation**, which is
not the same as explaining why the *analysing* engine scored it that way. The
two can disagree, and when they do the tool says so — "no term accounts for it"
rather than inventing something.

That disagreement is the most useful output here, and not for players: **it
points at what this engine's evaluation is blind to.** A move Stockfish prices
at −500 that moves no term is a position this engine cannot see. That is a
direct feed into evaluation work, which is the project's binding constraint.

Trust it most when **material** dominates the list — `6...dxc3 → material +200`
is exactly what happened. Trust it least on quiet positional moves, and not at
all for losses that are not positional in nature: `13. Qa8+` threw away a won
game by *repetition*, and no static term expresses "you agreed to a draw".

---

## R4. Presentation — **DONE 2026-08-15**

`--annotate <out.pgn>` writes the review back as an annotated PGN. Chosen over
an HTML report or a GUI panel because it needs no interface at all and reaches
every tool that already reads PGN.

Each move carries `[%eval X.XX]` in a comment — from White's point of view and
in pawns, which is the convention Lichess and several other viewers parse to
draw an evaluation graph. Criticisms carry a NAG: `$6` (?!), `$2` (?), `$4`
(??). Only criticisms: "!" on a merely-best move is noise, and viewers already
highlight the engine's preference.

```
6. Bb5 {[%eval 1.56]} dxc3 $4 {[%eval 1.57] Blunder, -25.1 win%; best Qd6}
...
13. Qa8+ $4 {[%eval 5.11] Blunder, -36.8 win%; best Qxd7+} 1/2-1/2
```

Paste that into Lichess analysis and the graph, the glyphs and the explanations
come with it.

`toPgn()` gained an overload taking per-move `MoveNote`s rather than the tool
formatting movetext itself, so numbering, the black-to-move ellipsis and
80-column wrapping stay in one place.

**The output is read back by the same parser that produced it**, which is the
property worth guarding: if a glyph or a brace confused the reader, the tool
would silently corrupt the games it was asked to explain. `tests/pgn` checks it.

---

## R5. An HTML report — **DONE 2026-08-15**

```bash
./tools/review game.pgn --html out.html      # one self-contained page
./tools/review-open.sh --latest              # newest archived game, opened
```

**This reverses R4's stated choice, and the reason matters.** R4 picked the
annotated PGN *over* a report because it "needs no interface at all and reaches
every tool that already reads PGN". That is still true and the PGN output stays.
What it does not reach is the term attribution: no PGN viewer on earth renders
"threats −750, material +200", and that line is the whole reason this tool
exists as an instrument rather than a toy. Pasting into Lichess gives you the
graph and the glyphs; it cannot give you what this engine's evaluation thought.

So the two outputs answer different questions and both are kept. Use
`--annotate` to study the game, `--html` to study the engine.

**One file, no dependencies.** No CDN, no webfont, no images — a review is
something you send to someone, and a page that fetches anything is a page that
breaks on the machine you sent it to. Pieces are Unicode glyphs for the same
reason. A generated report is ~15 KB.

**What it shows.** Board with the played move framed, a scoresheet with a
severity rail so criticised moves are findable in a list of eighty, per-side
accuracy and average centipawn loss, the term attribution under each criticised
move, and an evaluation graph from White's point of view. Click a move or use
the arrow keys.

`--html` implies `--explain`: a page that classified moves without saying which
terms moved would be the less useful half of the tool.

**Typography is fixed-pitch for notation** because chess notation has been set
that way on every printed scoresheet, and it makes the evaluation column align.
Deliberately no embedded font: inlining a face as a data URI would bloat every
report the tool ever writes, which is a real cost for something that emits one
file per game.

**How the layout was actually checked.** Reading the HTML is not checking it:
three defects survived inspection and only fell to a screenshot — uneven ranks,
white pieces reading as black, and black pieces disappearing on dark squares.
Chrome renders it headlessly without a display:

```bash
cp ~/reviews/GAME.html /mnt/c/Users/<user>/Temp/r.html
"/mnt/c/Program Files/Google/Chrome/Application/chrome.exe" --headless=new \
  --disable-gpu --window-size=1050,900 --screenshot=C:\Users\<user>\Temp\shot.png \
  "file:///C:/Users/<user>/Temp/r.html"
```

Stamp `<html data-theme="light">` on a copy to see the other theme: the default
is whatever the renderer's `prefers-color-scheme` reports, so one screenshot
only ever tests one of the two.

**Not done, and cheap when wanted:** flipping the board when the bot played
Black, an opening name, the `Book`/`Brilliant`/`Great`/`Miss` labels below, and
a batch index across the archive to pair with `tools/archive-profile.py`.

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

R0 through R4 are all done. The tool is complete; what is left is using it. Each is independently
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
