# ChessBot — Known Issues and Backlog

Findings from a profiling and optimization session on 2026-08-10. Everything
here was measured or verified against the code, not guessed. Numbers are from
a depth-5 search with iterative deepening and a 256 MB transposition table
unless stated otherwise.

Items are ordered within each section by how much they matter.

---

## 1. Open questions from this session

### 1.1 The search heuristics lose Elo at fixed depth — decide what to do

Null-move pruning, LMR and aspiration windows were added and made the search
**4.1× faster at the same depth**. But a 200-game self-play match at depth 4
(`make test-match`) measured them at roughly **−40 Elo versus the same engine
with all three disabled**. See §7 for the exact result.

This matters more than it looks, because `ChessBotEngine` searches to a **fixed
depth** (`searchDepth`, default 5) rather than to a time budget. At a fixed
depth the 4.1× buys nothing the user sees — the engine just moves faster and
plays slightly worse. The speed only converts into strength if the depth goes up.

Three ways forward, roughly in order of expected value:

- **Tune the heuristics.** The current parameters are deliberately simple and
  almost certainly too aggressive. LMR reduces every quiet move after the third
  by a flat `R = 1`; real engines use a formula over depth and move index, skip
  reduction for killers and checks, and re-search more carefully. Null-move uses
  a flat `R = 2` with no verification search, which is what causes tactical
  oversights in the lines it prunes.
- **Raise the default depth** from 5 to 6 to spend the speedup. Needs measuring
  first: play depth-6-with-heuristics against depth-5-without and confirm it
  wins clearly.
- **Add real time control** (§5.1) so the engine searches to a time budget.
  That is the setting these heuristics are designed for, and the one where a
  4.1× speedup pays for itself automatically.

Until one of those is done, the toggles in `SearchOptions` (`search.hpp`) let
you turn any of them off.

### 1.2 The match harness has no time control

`tests/match.cpp` plays at fixed depth only. That answers "do the heuristics
cost accuracy?" but not "are they worth it?", which is the question that
actually matters. Add a time-per-move mode so both sides get equal wall clock
and the faster engine gets to search deeper.

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

- **Static Exchange Evaluation (SEE)** — for move ordering and to prune losing
  captures in quiescence. Probably the single biggest strength win available;
  quiescence currently searches every capture including obviously losing ones.
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

## 6. Testing gaps

- **`tests/perft.cpp` has no divide mode.** When a count disagrees, perft divide
  (per-root-move counts) bisects to the offending move in a few steps. Add it
  the day a count first fails.
- **No evaluation regression test in the repo.** The byte-identical eval
  comparison used throughout this session (dump all `EvalDetails` fields over
  ~24k positions from seeded random games, compare against a stored reference)
  caught nothing only because it was watched closely. It belongs in `tests/`.
- **No test runs in CI**, because there is no CI.
- `tests/legacy/` — see §3.4.

---

## 7. Baseline measurements (2026-08-10)

Keep these to compare against. Five-position benchmark, depth 5, iterative
deepening, 256 MB TT:

| stage | total | vs start |
|---|---|---|
| session start | 78 820 ms | 1.00× |
| after mobility + movegen split | 44 446 ms | 1.77× |
| after evaluation rewrite | 16 753 ms | 4.70× |
| after make/unmake legality filter | 14 063 ms | 5.60× |
| after null-move + LMR + aspiration | 3 465 ms | **22.7×** |

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

Self-play match, heuristics on vs off, depth 4, seed 20260810
(`./tests/match 100 4 20260810`), read at 167 of 200 games:
**W 27 / D 94 / L 45, score 44.6%, about −37 Elo** for heuristics-on. The
margin was stable from roughly game 60 onward, so this is signal rather than
noise. Rerun to completion to confirm the final figure.
