# ChessBot — Known Issues and Backlog

Findings from a profiling and optimization session on 2026-08-10. Everything
here was measured or verified against the code, not guessed. Numbers are from
iterative deepening with a 256 MB transposition table unless stated otherwise.

Items are ordered within each section by how much they matter.

> **Read this as a dated archive, not as current state.** It is the record of
> what was found on 2026-08-10, and `PLAN.md` cites its section numbers as
> fixed references — so entries are kept in place rather than edited as they are
> resolved. Much of sections 2 and 3 has since been fixed, and **§2.1 is now
> flatly wrong**: the bitboard module is complete and perft-verified, and is
> unconnected by choice rather than broken. The measured ceilings in §4 and the
> baselines in §7 are the parts that stay useful. For current state, start from
> `README.md` and `PLAN.md`'s status lines.

---

## 0. Start here next session

The engine is now **correct** (perft-exact, game-state tested) and **~22×
faster at depth 5**, with an effective branching factor cut from ~4.5 to ~2.3
per ply. Depth 10 takes about 15 s per position; before this session it would
have taken roughly 15 minutes.

The bottleneck is no longer the code. It is that **nothing is configured to
spend the speedup**, and **the one open question was measured in the wrong
regime**. In priority order:

1. ~~**Add time control** (§5.1)~~ — **done**, PLAN.md 1.1.
2. ~~**Raise `ChessBotEngine`'s default depth** from 5 (§1.3)~~ — **done and
   measured at +246 Elo**, PLAN.md 1.2. See §1.3.
3. ~~**Re-run the strength match time-equalized** (§1.1)~~ — **done and
   measured at +140 Elo**, SPRT H1 accepted in 136 games. See §1.3.

**All three gates are cleared, and Phase 1 is complete.** Both Phase 1 matches
came back large and positive (+246 and +140), which together confirm the premise
this whole plan was built on: the engine was fast and had nothing configured to
spend the speed.

**Next: Phase 3.** SEE is now wired in behind two toggles and is at its gate
(§5.2). After it: bound quiescence, check extensions, futility/razoring, IID and
the LMR formula. Each is gated by `./tests/match -t 3000 --sprt --optA <f>=on
--optB <f>=off` against the current build.

**Two habits, both learned the expensive way:**

- **Look at the tree before spending a day on a match.** `./tests/bench 6 --opt
  <feature>=on` prints the node signature with one option flipped, in seconds
  rather than hours. It caught a real ordering bug in SEE before the gate ran.
- **A timed match needs a quiet machine.** `-t 3000` is wall clock, so parallel
  builds mean both engines get fewer nodes per move and the result describes a
  time control you did not ask for. Use `make -j4`, never two matches at once,
  and `tee` the output — a timed match is not reproducible from its seed.

Since this file was written the engine has also gained a UCI interface, a
negamax search, an evaluation regression test, a search bench signature, SEE
(unit-tested, not yet wired in), CI, and a further 1.45× in speed. See PLAN.md
for what is done and what remains.

Do **not** start with the "obvious" engine optimizations. Bitboard move
generation (~1.02× ceiling), pin-aware move generation (1.24×) and replacing
the `std::string` board fields (~1.05×) are all measured dead ends — see §4.

**Two methodology notes that cost real time this session.** First, run
benchmarks through `findBestMoveIterativeDeepening` with a TT, the way the app
does — `findBestMove()` is a different, far slower search (§2.5) and
benchmarking it produces meaningless numbers. Second, test in the regime you
care about: three separate wrong conclusions this session came from measuring
at a convenient depth rather than a representative one.

Where a change can be verified by identity (evaluation, move generation), do
that rather than playing games — it is faster and conclusive. Where it cannot
(anything that changes the search tree), only matches will tell you anything.

---

## 1. Open questions from this session

### 1.1 Are the search heuristics worth it? — RESOLVED: +140 Elo

**Yes, emphatically.** Time-equalized SPRT match at 3 s/move, null-move + LMR +
aspiration against plain alpha-beta, `./tests/match -n 400 -t 3000 --sprt`:

```
games   : 136  (W 69 / D 50 / L 17)
score   : 69.1%
Elo     : +140   95% CI [+94, +191]
wall    : 27 505 s
LLR     : +2.96  (bounds -2.94 / 2.94)
SPRT    : H1 accepted
```

The heuristics stay on. Nothing further to investigate here.

**The size of the correction is the lesson.** The same three heuristics measured
**−30 Elo** at fixed depth 4 and **+140 Elo** at equal time — a 170 Elo swing
from changing nothing but the measurement regime. The depth-4 number was not
noisy, it was answering a different question ("do these cost accuracy at equal
depth?" — mildly) than the one that mattered ("are they worth it?" — a question
about equal *time*). All three scale with tree size:

| depth | speedup vs heuristics-off |
|---|---|
| 4 | 1.31×  ← the old match was run here |
| 6 | 4.09× |
| 8 | 19.97× |
| 9 | 31.07× |

At depth 4 they pay nearly their full accuracy cost while delivering almost none
of their benefit. At 3 s/move they are searching several plies deeper than the
opponent, and depth beats everything.

**SPRT paid for itself on the first use.** The fixed-N estimate in the old
version of this section was ~800 games for a ±16 Elo interval. The sequential
test stopped at **136** — the effect was far larger than the +10 Elo H1 bound,
so the log-likelihood ratio crossed almost as fast as it arithmetically could.
That is roughly 5.9× fewer games, or about 37 hours of wall clock saved at this
time control.

### 1.2 Correction: aspiration windows are NOT the problem

An earlier reading of this file, and advice given during the session, said
aspiration windows were useless-to-harmful and should be disabled. That was
based only on depths 5–7. **It is wrong.** Measured contribution of aspiration
on top of null-move + LMR:

| depth | 6 | 7 | 8 | 9 | 10 |
|---|---|---|---|---|---|
| aspiration's effect | −2% | +1% | +8% | **+27%** | **+46%** |

The crossover is around depth 7–8, which makes sense: a narrow window prunes
proportionally more as the tree grows, while the fail-and-re-search penalty is
roughly fixed. At depth 9, the no-aspiration build takes 31 722 ms against
24 888 ms with it. **Keep aspiration enabled.**

This is a good example of the general trap: a heuristic measured outside its
operating range will lie to you about its value.

### 1.3 The app's default search depth is far too low — RESOLVED

**Settled by measurement on 2026-08-10.** Depth 8 against depth 5, both with
heuristics on, time-unequalized by design (this asks whether the extra depth is
worth having, not whether it is free):

```
./tests/match -n 150 --ha on --hb on --da 8 --db 5 --sprt --elo0 0 --elo1 20

games : 23  (W 14 / D 9 / L 0)
score : 80.4%
Elo   : +246   95% CI [+151, +390]
LLR   : +3.22  -> H1 accepted
wall  : 2422 s
```

Depth 8 did not lose a single game. The default is now a 3000 ms clock with a
depth-8 ceiling (PLAN.md 1.2), so this is banked. The prediction in the original
note — "should be a rout" — was correct.

Two things this also establishes. The measuring apparatus works, which was the
real reason to run it first: a depth-8 engine that could not beat a depth-5 one
would have meant something was broken in the search, the harness or the
evaluation, and every later gate would have been measuring noise. And SPRT
earns its place — 23 games rather than the 300 a fixed-size match would have
played, 40 minutes rather than 6 to 10 hours.

The original note follows.



`ChessBotEngine`'s constructor sets `searchDepth(5)`. That was a reasonable
setting for the old engine. It now costs about 1.1 s per move where depth 8
costs about 2.7 s and depth 9 about 6 s (per position, from §7).

Because the engine searches to a fixed depth rather than a clock, **none of
this session's speedup reaches the player** until this changes. Raising it is
the single cheapest strength improvement available. Confirm with a match:
depth 8 against depth 5 should be a rout.

### 1.4 The match harness has no time control — RESOLVED

`tests/match.cpp` now takes `-t <ms>` for an equal budget, `--ta/--tb` for
per-side budgets, `--da/--db` for per-side depths, `--optA/--optB` to set
individual search options, and `--sprt` for sequential testing with early
stopping. Search time control itself is PLAN.md 1.1.

---

## 2. Bugs and correctness issues

### 2.1 The bitboard module is dead, and broken if revived

`src/engine/bitboard.cpp`, `bitboard_move_gen.cpp`, `magic_bitboards.cpp` and
`bitboard_pawn_moves.hpp` compile and link into the binary but **nothing
outside them references any of it**. Beyond being unused, it would not work if
wired up:

- **`initMagicBitboards()` is never called.** The magic attack tables are
  zero-initialized globals, so `getRookAttacks()` and `getBishopAttacks()`
  return 0 for every query. Rooks, bishops and queens would generate no moves
  at all.
- **`BitboardMove` has no castling or en-passant flag.** Castling comes out as
  a bare king move e1→g1 that a make-move routine cannot distinguish from a
  quiet king move.
- **No legality filtering**, no pin or check handling, and no make/unmake for
  `BitboardState`.
- **Written in the anti-idiomatic style** that discards the entire point of
  bitboards: `for (int to = 0; to < 64; ++to) if (testBit(bb, to))` instead of
  `while (bb) { sq = lsb(bb); bb &= bb - 1; }`. Pawn generation makes about
  seven separate 64-iteration scans; the castling `isSquareAttacked` lambda
  scans all 64 squares five times.

Decide: delete it, or finish it. Note §4.1 — measurement says a bitboard
movegen rewrite is not worth doing for speed.

### 2.2 `tempoBonus` silently shifts negative evaluations by +1

`evaluation.cpp:235` declares `float tempoBonus = 0.01f`, and it is added into
the `e.total` sum at line 588. Because it is a float, the whole sum promotes to
float and is then truncated on assignment to the `int` field. Truncation is
toward zero, so a total of `-5` becomes `-4.99` and lands as **−4**, while a
total of `+5` stays `+5`. The result is a one-centipawn asymmetry favouring
black that has nothing to do with tempo. Either make it an honest `int` tempo
term applied to the side to move, or delete it.

### 2.3 "Defended" in the undefended-pieces term is not chess defence

The undefended term (`evaluation.cpp`, search for `whiteUndefended`) counts a
piece as defended if **any friendly piece stands on one of the eight adjacent
squares**. That is not what defending means: a knight defends from a knight's
move away, a rook defends down a file. The term is measuring adjacency, i.e.
roughly "pawn-chain-ness", not protection.

The attack maps built for the threat term (`forEachAttackedSquare`) already
compute real defence — `attackedBy[own][sq]` is exactly "is this square
defended". Switching to that would be nearly free and would make the term mean
what its name says. **It will change evaluation output**, so it needs match
testing, not an identity check.

### 2.4 `generateLegalMoves()` and `generateMoves()` are the same function

`movegen.cpp`: `generateLegalMoves(b, side)` is literally
`return generateMoves(b, side, true);`, and `generateMoves` already filters for
legality. The header comment calls one "pseudo-legal", which is wrong and
actively misleading — `generatePseudoLegalMoves()` is the genuinely
pseudo-legal one. Either collapse the two names or rename them honestly.

### 2.5 Search entry points that no longer work the way callers expect

`findBestMove(const Board&, int)` in `search.cpp` runs a **plain alpha-beta
with no transposition table** via `findBestMoveWithStop`. The application uses
`findBestMoveIterativeDeepening` instead. Anyone benchmarking or testing
through `findBestMove` measures a completely different and far slower search
than the one that actually ships. See §3.2 — it is dead code and should go.

---

## 3. Dead and unused code

### 3.1 Empty files that are still compiled and linked

- `src/engine/uci.cpp` — 0 bytes. There is no UCI support; the engine cannot be
  driven by a standard chess GUI. See §5.4.
- `src/engine/magic.cpp` — 0 bytes.

### 3.2 Unreachable search functions

None of these have callers outside `search.cpp`:

- `findBestMove(const Board&, int)` (the free function, not the `ChessBotEngine`
  member of the same name)
- `findBestMoveWithStop(...)`
- `findBestMoveWithTT(...)` — no callers anywhere
- `static minimax(...)` — reachable only from `findBestMoveWithStop`

That is the entire non-TT search path. Deleting it removes a maintenance
hazard: it is a second, subtly different copy of the search logic that will
drift from the real one.

### 3.3 Unused constants and fields

- `captureBonus[]` (`evaluation.cpp:15`) — declared, never read.
- `threatBonus[KING] = 500` — the threat loop explicitly skips king targets, so
  this entry can never be reached.
- `Piece::fromValue()` — no callers.
- `EvalDetails::coordination` is always 0; `coordinationBonus` is initialized to
  zero and never assigned. Same for the `nnueEvalScore`, `repetitionPenalty` and
  `drawishPenalty` stubs.

(The dead 64×64 coordination loops that fed `coordinationBonus`, plus the unused
outpost/trapped/king-activity counters, were removed this session.)

### 3.4 `tests/legacy/` is not built by anything

Nine test files that no target compiles. They reference an older API in places.
Either port them to the `tests/` layout added this session, or delete them.

---

## 4. Performance — with measured ceilings

The eval and movegen work this session took the five-position benchmark from
**78.8 s to 3.5 s (22.7×)**. What is left, and what each is actually worth:

### 4.0 Bitboard move generation — measured ceiling ~1.02×, not recommended

The question that started this session. Move generation itself is **0.58 µs,
about 2% of search time**; the rest of `generateLegalMoves()` is the legality
filter (§4.1). Even an infinitely fast generator caps the whole search at about
1.02×. Combined with the state of the existing module (§2.1), this is a large,
high-risk rewrite of the most correctness-critical code in the engine for
essentially nothing. Delete the module or leave it; do not finish it for speed.

### 4.1 Pin-aware legal move generation — measured ceiling 1.24×, not recommended

The legality filter (make each move, test the king square) is **19.4% of search
time**. Eliminating it *entirely* yields 1.24× overall; a real implementation
still has to compute pins and checkers, so expect ~1.15×.

Against that: pin-aware generation is where engines get their most notorious
bugs — en-passant discovered check along a rank, pinned pawns capturing en
passant, king moves that stay on a slider's ray after stepping back along it.
`make test-perft` now exists as a safety net, which makes this *approachable*,
but the payoff is small. Do the search work in §5 first.

### 4.2 `castlingRights` and `enPassantTarget` are `std::string` — ceiling ~1.05×

Every `makeMove` re-hashes strings through
`ZobristHash::castlingRightsToIndex(const std::string&)`, and every `UndoInfo`
carries two `std::string` copies. `makeMove` is 10.4% of runtime and the string
handling is only part of that. Converting to a 4-bit mask and an `int` square
touches `fen.cpp`, `zobrist_hash.cpp`, `pgn.cpp` and `board.cpp`. Low risk but
low reward.

### 4.3 Evaluation is still the largest single item — 34.1%

Down from 79.5% but still the biggest bucket, at 192,927 calls in a depth-5
search. Options, cheapest first:

- **Cache the static eval in the TT entry** so repeated visits to a position
  skip it entirely.
- **Lazy evaluation**: compute material and PST first, and if that is already
  far outside the alpha-beta window, skip the expensive terms. Changes results,
  so it needs match testing.
- Incremental material and PST updates in `makeMove` — now low value, since the
  eval is 4 µs and this would save perhaps 1 µs, against real risk of the
  incremental state drifting from the board.

### 4.4 Smaller items

- `generateLegalMoves()` returns `MoveList` by value — one heap allocation per
  node. `generatePseudoLegalMoves()` already takes an out-parameter; give the
  legal version the same treatment.
- Repetition detection is a linear `std::find` over `pathHashes` at every node.
- `move_lookup` tables are `std::vector<int>[64]` — a pointer chase per piece.
  Fixed-size arrays would keep them in cache.

---

## 5. Search and engine features

### 5.1 Real time control — the highest-value missing feature

`findBestMoveIterativeDeepening` takes `maxDepth` and no time budget, despite
comments describing "time management". The GUI interrupts via a stop flag
instead. Without a time budget the engine cannot play a real game with a clock,
and none of the speed work converts into strength automatically (§1.1).

Wants: a deadline parameter, a check against it between depths and periodically
inside the search, and a rule for whether there is time to start the next
iteration.

### 5.2 Search techniques not yet implemented

- ~~**Static Exchange Evaluation (SEE)**~~ — **written, unit-tested and wired
  in** (PLAN.md 3.2). Two toggles, both default OFF pending their gates:
  `seepruning` cuts the tree 41.1% (1.73× faster) and `seeordering` 28.7%
  (1.37×); together 42.7%, 1.78×. The prediction that this was the biggest
  strength win available is so far supported by tree size; the Elo is what the
  gate is for.

  Two findings worth keeping, both from measuring rather than assuming:
  **SEE classifies captures, it does not order them** — using the exchange
  result as the sort key collapses QxQ, RxR and PxP into one block (they all
  resolve to 0) and destroys the victim-value ordering that produces cutoffs,
  which measured *worse* than not ordering at all. Band by SEE, sort by MVV-LVA
  within the band. And **never score inside a sort comparator**: that is
  ~2n·log(n) evaluations instead of n, invisible for a table lookup and ruinous
  for an exchange resolution.
- **Check extensions** — extend the search by a ply when in check.
- **Futility pruning / razoring** at shallow depths.
- **Internal iterative deepening** when there is no TT move to order on.
- **Better LMR formula** (§1.1).

### 5.3 Quiescence has no depth limit

`quiescence()` recurses on captures with no bound on ply. On positions with long
capture sequences this is what makes kiwipete 200× more expensive than a normal
middlegame position in the benchmark. A ply cap or a delta-pruning rule would
bound it.

### 5.4 No UCI interface

`uci.cpp` is empty. With UCI the engine could be run under cutechess-cli or
Arena, played against other engines, and tested with standard tooling — which
would make everything in §1 dramatically easier to measure than the bespoke
harness in `tests/match.cpp`.

---

## 6. Testing

What exists now (`make tests`):

- `make test-perft` — move generation against published reference counts.
- `make test-gamestate` — terminal detection, that a finished game refuses
  moves, and that terminal state is not sticky.
- `make test-match` — engine-vs-engine strength measurement.

Gaps, in order:

- **No evaluation regression test in the repo.** This is the most valuable one
  missing. The technique used throughout this session: dump every
  `EvalDetails` field over ~24k positions from seeded random games, store it as
  a reference, and `cmp` after any change. It made an aggressive rewrite of the
  entire evaluation provably safe, and it is the only reason that work could be
  done quickly. Reconstruct it as `tests/evalref.cpp` before touching
  evaluation again.
- **`tests/match.cpp` has no time control** — see §1.4.
- **`tests/perft.cpp` has no divide mode.** When a count disagrees, perft divide
  (per-root-move counts) bisects to the offending move in a few steps. Add it
  the day a count first fails.
- **No test runs in CI**, because there is no CI. All three tests return
  non-zero on failure, so they are ready for one.
- `tests/legacy/` — see §3.4.

### Already fixed this session — do not re-investigate

Reported as "checkmated but could still play". Checkmate detection itself was
correct; `tests/gamestate.cpp` now covers fool's mate, back rank, scholar's,
smothered and stalemate. The actual bugs were:

- `makeHumanMove()` did not check `isGameOver()`. Invisible in checkmate and
  stalemate, because those have no legal moves and `isValidMove()` rejects
  everything anyway — but the draws and resignation are terminal *with* legal
  moves, and there the game simply continued.
- `isHumanTurn()` was `board.activeColor == humanSide`, which stays true after
  checkmate since the mated side is still to move, so the GUI kept pieces
  draggable after the game ended.
- `startNewGame()` and `loadGameFromFEN()` did not clear `currentState`, and
  `updateGameState()` returns early when the game is already over — so a
  terminal state was sticky and every game after the first began already
  finished.

The general shape is worth remembering: the bug was not in the feature being
reported, it was in the states adjacent to it, hidden behind a different check
that happened to mask it.

---

## 7. Baseline measurements (2026-08-10)

### Bench signature — the search's identity check

`make bench` (12 positions, depth 6, fresh 64 MB table per position). Verified
deterministic across runs. Any change that claims to leave search behaviour
alone must reproduce **total = 2 056 371** and every best move below exactly.

```
position           nodes  best
startpos           32714  g1f3
open-ital         100209  g8f6
open-sicil         83402  b8c6
kiwipete         1141100  e2a6
midgame-1         241066  d1c2
midgame-2         269735  c1c2
tactical          137091  c5c4
promo-race         14193  b4f4
rook-endg          17264  e2d3
pawn-endg           5620  f2f3
queen-endg            60  d1d8
zugzwang           13917  e1e5

total            2056371     12 424 ms     165 516 nps
```

Established after the Phase 0 deletions (PLAN.md 0.1, 0.4–0.7), which were
behaviour-preserving, and before the negamax conversion (0.9), which must not
change it.

**The node count has not moved since.** Negamax (0.9), time control (1.1), UCI
(2.1) and all three Phase 5 performance items reproduced it exactly — the same
search, doing the same work, faster. Wall clock over the same 12 positions:

| stage | depth 5 | depth 6 | vs Phase 0 |
|---|---|---|---|
| after Phase 0 | 5 084 ms | ~12 450 ms | 1.00× |
| + in-place legality filter (5.2) | 4 972 ms | 12 451 ms | 1.02× |
| + evaluation cache (5.1) | 4 154 ms | 10 373 ms | 1.22× |
| + mask/square board state (5.3) | **3 507 ms** | **8 557 ms** | **1.45×** |

Two of those beat their predicted ceilings, for the same reason in both cases:
the cost was not where the profile's category names suggested.

- **5.1, predicted "cheapest first", measured 1.20×.** 46.1% of `evaluate()`
  calls are for a position already evaluated (1 077 632 calls, 580 993 distinct
  positions), so memoizing removes nearly half the largest item in the profile.
- **5.3, predicted ~1.05× and "low reward", measured ~1.20×.** The expense was
  never the string *copies* — both fields are short enough to avoid a heap
  allocation. It was `erase(std::remove(...))` up to four times per `makeMove`
  to clear castling rights that are now a single AND, plus re-deriving the
  zobrist index by scanning characters on every hash update.


Keep these to compare against. Five-position benchmark, depth 5, iterative
deepening, 256 MB TT:

| stage | total | vs start |
|---|---|---|
| session start | 78 820 ms | 1.00× |
| after mobility + movegen split | 44 446 ms | 1.77× |
| after evaluation rewrite | 16 753 ms | 4.70× |
| after make/unmake legality filter | 14 063 ms | 5.60× |
| after null-move + LMR + aspiration | 3 465 ms | **22.7×** |

### Depth scaling — the most useful table here

Four positions (startpos, an opening, a middlegame, an endgame), total ms:

| depth | heuristics-on | nm+lmr, no asp | heuristics-off | speedup |
|---|---|---|---|---|
| 4 | 628 | 643 | 824 | 1.31× |
| 5 | 1 102 | 1 170 | 2 020 | 1.83× |
| 6 | 2 263 | 2 220 | 9 261 | 4.09× |
| 7 | 5 154 | 5 225 | 40 632 | 7.88× |
| 8 | 10 753 | 11 622 | 214 726 | 19.97× |
| 9 | 24 888 | 31 722 | 773 164 | 31.07× |
| 10 | 57 840 | 84 636 | *(abandoned, ~45–65 min projected)* | ~50–65× |

**Effective branching factor**, i.e. cost per extra ply, stable from depth 6:

- heuristics-on: **~2.3×**
- heuristics-off: **~4.5×**

Those are two different exponentials, which is why the speedup keeps widening
with depth and why any single-depth measurement of these heuristics is
misleading. Depths 1–4 in the table are dominated by transposition table
allocation, not search, and should be ignored.

Component costs (single call, `-O2`, midgame position):

| | before | after |
|---|---|---|
| `evaluate()` | 26.47 µs | 4.02 µs |
| `generateLegalMoves()` | 12.10 µs | 7.46 µs |
| `generatePseudoLegalMoves()` | 0.59 µs | 0.59 µs |

Where search time goes now (depth 5, four positions): evaluation 34.1%,
legality filter 19.4%, `makeMove` 10.4%, pseudo-legal generation 2.9%,
move ordering 2.2%, remainder in recursion, TT and allocation.

Heuristic ablation, depth 5, four positions: null-move 1.41×, LMR 1.68×,
aspiration 1.10×, all three 2.01×.

Move generation is verified correct against published perft counts: startpos
depth 4 = 197 281, kiwipete depth 3 = 97 862, position 3 depth 4 = 43 238,
position 4 depth 3 = 9 467.

### Gate 1.4 — depth 8 vs depth 5 (2026-08-10)

```
games : 23  (W 14 / D 9 / L 0)   score 80.4%
Elo   : +246   95% CI [+151, +390]   LLR +3.22 -> H1 accepted
wall  : 2422 s  (~105 s per game)
```

See §1.3. This is the measurement that validates every later gate.

Self-play match, heuristics on vs off, depth 4, seed 20260810
(`./tests/match 100 4 20260810`), completed:

```
games : 200  (W 35 / D 113 / L 52)
score : 45.8%
Elo   : -30   95% CI [-62, +2]
wall  : 1542 s
```

The confidence interval includes zero by a hair, so strictly this match does
not establish a regression at 95% confidence. What it does establish is that
the heuristics are **not** buying strength at equal depth: 113 of 200 games
were drawn, and the point estimate sits 30 Elo down.

**Superseded — keep both numbers side by side.** The same comparison run
time-equalized at 3 s/move (`./tests/match -n 400 -t 3000 --sprt`):

```
games   : 136  (W 69 / D 50 / L 17)
score   : 69.1%
Elo     : +140   95% CI [+94, +191]
wall    : 27 505 s
LLR     : +2.96  (bounds -2.94 / 2.94)   H1 accepted
```

Same code, same heuristics, same opponent — **−30 Elo at fixed depth 4, +140 Elo
at equal time.** A 170 Elo swing produced entirely by the choice of measurement
regime. This pair is the most useful thing in this file: it is the concrete
answer to "does it matter which depth I benchmark at?"

Draw rate fell from 56.5% to 36.8%, because the stronger side now converts
rather than shuffling. That is the opposite of the drift predicted when this
section assumed the comparison would stay at fixed depth.

Both Phase 1 gates, for reference:

| gate | comparison | result | games |
|---|---|---|---|
| 1.4 | depth 8 vs depth 5 | **+246** Elo [+151, +390] | — |
| 1.5 | heuristics on vs off @3 s | **+140** Elo [+94, +191] | 136 (SPRT) |
