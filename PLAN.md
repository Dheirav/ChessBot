# ChessBot — Action Plan

Derived from `BACKLOG.md` (2026-08-10). Every backlog item appears here exactly
once, ordered so that each piece of work is *verifiable when it lands* rather
than in the order it was discovered.

Three items (0.8, 0.9, and SPRT in 1.3) are **not** from the backlog. They came
out of comparing this engine's structure against Stockfish's; each is marked
where it appears.

Two rules govern the ordering, both taken from the backlog's own methodology
notes:

- **Build the measuring instrument before the thing it measures.** Time control
  before heuristic tuning; the eval regression harness before touching
  evaluation; perft divide before touching move generation.
- **Measure in the regime you care about.** Fixed-depth 4 matches and
  `findBestMove()` benchmarks are both known to produce meaningless numbers.
  Every match in this plan is time-equalized at a realistic budget.

Phases are sequential. Items inside a phase are ordered but mostly independent.

---

## Phase 0 — Safety nets and clearing the ground

Nothing here changes engine behaviour except by deletion. It exists so that
Phases 1–5 can be verified cheaply. Expect one session.

**Status: COMPLETE.** All ten items landed. `make tests` builds five binaries;
`test-perft`, `test-gamestate`, `test-evalref` and `test-bench` all pass, and
CI runs them on every push. The negamax conversion (0.9) reproduced the bench
signature bit-identically — 2,056,371 nodes and the same best move in all 12
positions — which is the strongest evidence available that it was an exact
restatement rather than a rewrite.

**0.1 Remove dead eval fields and constants first** (§3.3)
`captureBonus[]` (`evaluation.cpp:15`), `threatBonus[KING]`, `Piece::fromValue()`,
and the always-zero `EvalDetails` members: `coordination`, `coordinationBonus`,
`nnueEvalScore`, `repetitionPenalty`, `drawishPenalty`.
*Why first:* these are `EvalDetails` fields, and 0.2 freezes that struct into a
reference file. Removing them afterwards means regenerating the baseline.
*Verify:* compiles; no behaviour change possible (all are unread or zero).

**0.2 Rebuild the evaluation regression harness as `tests/evalref.cpp`** (§6)
The single most valuable missing test. Dump every `EvalDetails` field across
~24k positions from seeded random games; store as `tests/data/evalref.txt`;
`cmp` after any change. Add `make test-evalref` and a `make evalref-regen`
escape hatch.
*Blocks:* 3.1, 3.2, 5.4 (lazy eval) — do not touch `evaluation.cpp` until this
exists.

**0.3 Add perft divide to `tests/perft.cpp`** (§6)
Per-root-move counts plus a CLI mode (`./tests/perft divide <fen> <depth>`).
Cheap now, and it is the only practical way to bisect a movegen bug.
*Blocks:* 5.5 (pin-aware movegen).

**0.4 Delete the non-TT search path** (§2.5, §3.2)
`findBestMove(const Board&, int)` (`search.cpp:396`), `findBestMoveWithStop`
(`:399`), `findBestMoveWithTT` (`:444`), and `static minimax`. Confirmed: no
callers outside `search.cpp` and the unbuilt `tests/legacy/`. `search.hpp` drops
to two entry points.
*Why:* it is a second, subtly different copy of the search that will drift, and
it is the trap that produced bad benchmark numbers last session.

*Extended during execution:* `LegalMoveValidator` turned out to carry the same
problem. `isMoveLegal()`, `filterLegalMoves()`, `isCheckmate()` and
`isStalemate()` had no callers anywhere outside their own file, and
`filterLegalMoves()` was a redundant second legality pass over a list
`generateLegalMoves()` had already filtered. Only `isInCheck()` is live. The
class is now just `isInCheck()` and `findKing()`.

**0.5 Keep the bitboard module, but label it** (§2.1, §4.0)
**Decision: retained.** `bitboard.{cpp,hpp}`, `bitboard_move_gen.{cpp,hpp}`,
`bitboard_pawn_moves.hpp` and `magic_bitboards.{cpp,hpp}` stay in the repo and
in the build.

What changes is only that the module stops being a trap for a future reader. Add
a `STATUS:` block at the top of `bitboard.hpp` recording, in place, the four
defects from backlog §2.1 — `initMagicBitboards()` is never called so every
slider attack query returns 0; `BitboardMove` has no castling or en-passant
flag; there is no legality filtering, pin or check handling, and no
make/unmake for `BitboardState`; and the generation loops are written as
`for (int to = 0; to < 64; ++to) if (testBit(...))` rather than
`while (bb) { sq = lsb(bb); bb &= bb - 1; }` — plus the measured context: a
bitboard *movegen* rewrite caps the whole search at ~1.02× (§4.0), because
movegen is 2% of search time. The module's real potential value is elsewhere —
shared attack maps for SEE (3.2), pins (5.5) and evaluation — and that is what
the note should point at.

Only the two 0-byte files are removed: `magic.{cpp,hpp}`. *Keep* the 0-byte
`uci.{cpp,hpp}` — Phase 2 fills them in.

*If build time becomes annoying later*, dropping the module from the Makefile's
`SRC` wildcard is a one-line change that keeps every file on disk. Not doing it
now: the cost is a couple of seconds and the coupling is zero.

**0.6 Delete `tests/legacy/`** (§3.4)
Nine files no target builds, referencing a pre-rewrite API. `perft`,
`gamestate`, `match` and the new `evalref` cover more than they did. Also drop
the stray root-level `debug_promotion.cpp` and the 21 `evaluation_log_*.txt`
files; add `evaluation_log_*.txt` to `.gitignore`.

**0.7 Fix the movegen naming lie** (§2.4)
`generateLegalMoves(b, side)` is `return generateMoves(b, side, true);` and
`generateMoves` already filters legality. Collapse to two honest names:
`generateLegalMoves(board, side, includeCastling = true)` and
`generatePseudoLegalMoves(...)`. Fix the header comment that calls a legal
generator "pseudo-legal".
*Verify:* `make test-perft` unchanged.

**0.8 Add a search bench signature as `tests/bench.cpp`**
*Not in the backlog — the search equivalent of 0.2, and a prerequisite for 0.9.*
Search a fixed set of ~12 positions to a fixed depth with a fixed TT size and a
cleared move orderer, and print the **total node count** plus the best move per
position. That triple (positions, depth, nodes) is a signature: any change that
is supposed to be behaviour-preserving must reproduce it exactly, and any change
that is not must change it in an explainable way.

This is what Stockfish's `bench` is for, and it is the only cheap way to verify
0.9 and most of Phase 5. Requires a node counter threaded through the search —
which Phase 2 needs anyway for UCI `info nodes`/`nps`, so it is not throwaway
work. Add `make bench`; record the signature in `BACKLOG.md §7`.

**0.9 Convert the search from white-perspective minimax to negamax**
*Not in the backlog. The largest structural problem in the engine.*

Every node in `search.cpp` branches on `whiteToMove` and carries two mirrored
copies of the same logic: quiescence stand-pat (`:75-81`), the quiescence move
loop (`:121-131`), null-move (`:259-268`), LMR (`:321-331`), and the alpha-beta
update (`:337-361`). Negamax makes scores relative to the side to move, and
`-search(-beta, -alpha)` collapses every one of those pairs into a single path.

*Why it is worth a session on its own:* this is not a tidiness argument. It
roughly halves the search's code and bug surface, and the codebase already
records a bug it caused — the comment at `search.cpp:375-386` documents that the
black branch shrinks `beta`, which misfiled **every black PV node** as
`LOWER_BOUND` in the transposition table. That class of bug cannot be expressed
in negamax. Phase 3 adds six search features; each one written against the
current structure has to be written, tested and debugged twice.

Scope: `evaluate()` keeps its white-perspective contract (so `test-evalref` is
untouched) and the search negates at the boundary — one sign flip in one place,
against rewriting the evaluation. `mateScore()` loses its `whiteToMove`
parameter and becomes `MATE_SCORE - ply`. The root and `ChessBotEngine` convert
back to white-perspective for display.

*Verify:* the 0.8 bench signature must be **bit-identical** before and after —
same nodes, same best moves. Negamax is an exact restatement of the same search,
so any difference is a bug in the conversion, not a judgement call. Follow with
`test-perft`, `test-gamestate`, `test-evalref`, and a short match as a sanity
check.

*Ordering:* after 0.4 (so the dead non-TT search is not converted too) and after
0.8 (so there is something to verify against). Before Phase 1 — time control
touches the same iteration loop, and before Phase 3 at all costs.

**0.10 Add CI** (§6)
GitHub Actions on push/PR: `make tests`, then `test-perft`, `test-gamestate`,
`test-evalref`, `bench` (signature comparison), and a short `test-match` smoke
run (not a strength measurement). All return non-zero on failure.

**Exit criteria:** `make tests` green including `test-evalref`; bench signature
recorded and reproducible; `search.cpp` has no `whiteToMove` branching; the only
unreferenced translation units left in `src/engine/` are the bitboard module,
and it carries a `STATUS:` block saying so.

---

## Phase 1 — Convert the 22× speedup into playing strength

This is the top of the backlog (§0 items 1–3). Nothing in this repo currently
spends the speedup. Expect one to two sessions, plus unattended match time.

**Status: COMPLETE.** Code (1.1, 1.2, 1.3) and both gates:

| gate | comparison | result |
|---|---|---|
| 1.4 | depth 8 vs depth 5 | **+246** Elo [+151, +390] |
| 1.5 | heuristics on vs off @3 s/move | **+140** Elo [+94, +191], SPRT H1 in 136 games |

Both large, both positive, both in the predicted direction. The premise this
plan was built on — a fast engine with nothing configured to spend the speed —
is confirmed.

**1.1 Real time control in the search** (§5.1)
Replace the `maxDepth`-only signature with a limits struct:

```cpp
struct SearchLimits {
    int  maxDepth      = 64;
    long moveTimeMs    = 0;   // 0 = no budget, depth-limited only
    // clock-based fields (wtime/btime/inc/movestogo) added in 2.1 for UCI
};
Move findBestMoveIterativeDeepening(Board&, const SearchLimits&,
                                    const std::atomic<bool>& stop,
                                    TranspositionTable&);
```

Three mechanics:
- a deadline check between iterations;
- a periodic check inside the search (every ~2048 nodes — cheap enough not to
  show in the profile, responsive enough at ~4 µs/eval);
- a **start-the-next-iteration rule**: with an effective branching factor of
  ~2.3 (measured, §7), starting depth *d+1* is worth it only if elapsed × 2.3
  fits the remaining budget. Otherwise return.

An aborted iteration must not overwrite the best move from the last completed
one — partial-iteration results are unsound at the root.
*Verify:* wall-clock per move stays within ~10% of budget across the
five-position benchmark; `test-perft`/`test-evalref` unaffected (search-only
change).

**1.2 Raise the app's default depth and give it a clock** (§1.3)
`ChessBotEngine` ctor sets `searchDepth(5)` (`chessbot_engine.cpp:7`), which is
~1.1 s/move where depth 8 is ~2.7 s and depth 9 ~6 s. Add a `moveTimeMs` setting
alongside it, default it to a real budget (suggest 3000 ms), and raise the depth
fallback to 8. Wire both through `src/config.hpp` (note: it currently clamps
`searchDepth` to ≤10 — raise the cap to 64 now that a clock bounds the search)
and `chessbot.conf`.

**1.3 Time control and SPRT in the match harness** (§1.4)
`tests/match.cpp` plays fixed depth only. Add a time-per-move mode so both sides
get equal wall clock. While in there, add **SPRT** (sequential probability ratio
test, H0: 0 Elo, H1: +10 Elo, α=β=0.05) with early stopping. The backlog's own
maths says a fixed-N answer at the observed 56% draw rate needs ~800 games for
±16 Elo; SPRT typically settles the same question in a fraction of that, and
every gate in Phase 3 depends on this being cheap.

**1.4 Confirm the depth raise pays** (§1.3)
Time-equalized match: the Phase-1.2 default vs. the old depth-5 default. The
backlog predicts a rout; if it is not, something in 1.1 is wrong and everything
downstream is measured on a broken instrument. **Treat a non-rout as a stop
condition.**

**1.5 Settle the heuristics question** (§1.1) — **DONE: +140 Elo**
Time-equalized SPRT match, heuristics on vs. off, 3 s/move:
`games 136 (W 69 / D 50 / L 17), 69.1%, Elo +140 [+94, +191], LLR +2.96, H1
accepted, wall 27 505 s.` The heuristics stay on; §1.1 is closed.

Two things worth carrying forward:

- **The prediction held, and the magnitude is the point.** Same code, same
  opponent: **−30 Elo at fixed depth 4, +140 Elo at equal time.** A 170 Elo
  swing from the measurement regime alone. Recorded side by side in
  `BACKLOG.md §7` as the concrete answer to "does the benchmark depth matter?"
- **SPRT paid for itself immediately.** The fixed-N plan was ~800 games for a
  ±16 Elo interval; the sequential test stopped at 136, because the true effect
  was 14× the +10 Elo H1 bound. ~5.9× fewer games, ~37 hours of wall clock saved
  on this one gate — and Phase 3 has six more.

*Already settled, no work needed:* §1.2 — aspiration windows stay on (+27% at
depth 9, +46% at depth 10).

**Exit criteria: met.** Engine plays to a clock; §1.1, §1.3, §1.4 all closed in
the backlog with measured numbers.

---

## Phase 2 — UCI, so measurement stops being bespoke

**Status: COMPLETE.** `./chessbot --uci`, verified by `make test-uci`.

**2.1 Implement UCI in `src/engine/uci.cpp`** (§5.4)
`uci`/`isready`/`ucinewgame`/`position [startpos|fen] moves …`/`go`/`stop`/
`quit`, plus `go wtime btime winc binc movestogo movetime depth` mapping onto
the `SearchLimits` from 1.1, and `info depth score cp nodes nps pv` output.
Options: `Hash`, and the three `SearchOptions` toggles as `UCI_` checkboxes so
A/B testing runs through standard tooling. Add a `--uci` flag to `main.cpp` (or
a separate `chessbot-uci` target) so the SFML binary is unaffected.

*Why here and not later:* Phase 3 contains six changes, each needing a match to
justify it. Under cutechess-cli that means opening books, proper gauntlets,
concurrency across cores, and standard SPRT — versus maintaining all of that by
hand in `tests/match.cpp`. The UCI work pays for itself by roughly the second
gate. `tests/match.cpp` stays as the fast in-process smoke test.

*Verify:* `cutechess-cli` runs a self-play match to completion with no illegal
moves or time forfeits.

---

## Phase 3 — Strength: the search techniques that are missing

Every item is gated by a time-equalized SPRT match against the previous
accepted build. Reject anything that does not clear H1. Expect two to three
sessions — and note that this estimate assumes 0.9 landed, so each feature below
is written once rather than twice.

**3.1 Quiescence depth limit and delta pruning** (§5.3)
`quiescence()` has no ply bound, which is why kiwipete costs 200× a normal
middlegame position. Add a ply cap (~8 beyond the horizon) and a delta-pruning
rule (skip a capture that cannot raise alpha even with a queen's worth of
margin). Bounds worst-case node counts and is close to free in strength.

**3.2 Static Exchange Evaluation** (§5.2 — biggest single win available)
*Status: implemented, unit-tested (`make test-see`, 13 hand-computed positions)
and **wired into the search** as of `1c0c6d6`. Both uses default OFF and are
awaiting their gates.*

Standard swap-off algorithm over the attacker sets, in two independently
toggleable uses:

| option | what it does | nodes @ bench 6 | vs baseline |
|---|---|---|---|
| — | baseline | 2 056 371 | — |
| `seeordering` | captures banded by exchange result, losing ones last | 1 465 771 | −28.7%, 1.37× |
| `seepruning` | quiescence skips captures that lose material | 1 212 044 | −41.1%, 1.73× |
| both | | 1 177 851 | −42.7%, **1.78×** |

**SEE must classify captures, not order them.** The obvious reading of "sort
captures by SEE" — use the exchange result as the sort key — measured *worse
combined than pruning alone* (1 362 996 against 1 212 044). Sound captures
nearly all resolve to 0, so QxQ, RxR and PxP collapse into one indistinguishable
block and the victim-value ordering that produces early cutoffs is destroyed.
Banding instead — SEE picks the winning/losing group, MVV-LVA orders within it —
was worth 120k nodes by itself and flipped both-on from worse than pruning to
better than it. Applies to both `move_ordering.cpp` and the quiescence sort.

**Never score inside a sort comparator.** Doing so calls the scorer ~2n·log(n)
times rather than n. Harmless while a score was a table lookup; with SEE it is
about a dozen exchange resolutions per move. Both orderers now score once into a
stack buffer and sort that. The permutation is unchanged — identical keys give
identical comparisons — which `bench --check` confirms byte for byte.

*Gates (each hours of wall clock, run one at a time):*
```
./tests/shard-gate.sh 14 60 -N 100000 --optA seepruning=on  --optB seepruning=off
./tests/shard-gate.sh 14 60 -N 100000 --optA seeordering=on --optB seeordering=off
```
Expect a smaller effect than gate 1.5 and therefore many more games: 1.5 stopped
at 136 because +140 Elo is 14× the H1 bound, and SPRT game counts scale roughly
as 1/effect². A 1.73× speedup is about three-quarters of a ply, so +40–60 Elo is
the plausible range and the 800-game cap is reachable. A slow upward LLR drift
is not a failure — it is the test correctly reporting a real but modest effect,
and the point estimate with its CI is still a good answer.

*Also done, and a prerequisite for every gate below:* search options have names
(`setSearchOption`), the match harness takes `--optA`/`--optB`, and `uci.cpp`
advertises each one. Without that no single feature could be A/B'd, because
`--ha/--hb` moved all three heuristics at once. Each feature below adds one line
to `setSearchOption` and becomes testable from the harness and over UCI at once.

*New tool, and the reason the two findings above were caught before a match was
spent on them:* `./tests/bench <depth> --opt <name>=<on|off>` prints the
signature with any option flipped. A match says whether a feature wins games, in
hours; this says how it changes the tree, in seconds. **Run it on every Phase 3
feature before gating it.** It is refused with `--check`, since the stored
signature describes the defaults.

**3.3 Check extensions** (§5.2) — extend a ply when in check. Small, standard,
usually worth 10–20 Elo.

**3.4 Futility pruning and razoring at shallow depths** (§5.2) — needs the
lazy-eval margins to be sane; keep it after 3.2 so SEE already removes the worst
noise.

**3.5 Internal iterative deepening** (§5.2) — when there is no TT move to order
on, do a shallow search to find one. Pays off most in the deeper regime Phase 1
unlocks.

**3.6 Retune the LMR formula** (§1.1, §5.2) — only now, with a time-equalized
harness and a search that has stopped changing shape underneath it. Grid over
reduction depth/move-count thresholds, SPRT the best two or three candidates.

---

## Phase 4 — Evaluation correctness

Both items change evaluation output, so both need `test-evalref` to *show a
diff* (confirming the change landed and is shaped as expected) and then a match
to confirm it is an improvement.

**4.1 Fix `tempoBonus`** (§2.2)
`evaluation.cpp:235` declares it `float 0.01f`; summed into `e.total` at `:588`
it promotes the sum to float, and truncation-toward-zero turns −5 into −4. The
net effect is a one-centipawn asymmetry favouring black that has nothing to do
with tempo. Make it an honest `int` applied to the side to move, or delete it —
prefer an honest int and let the match say whether it earns its place.

**4.2 King safety is asymmetric in a symmetric position**
*Not in the backlog — found by 0.2 on its first line of output.*

The reference file's first entry is the starting position, which is mirror
symmetric, so every white-perspective term must be exactly 0:

```
rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1,-2,0,0,-4,0,...
                                       total ^   ^  ^   ^ kingSafety
                                        material ^  ^ mobility
```

`kingSafety` is **−4**. Material, mobility, PST and centre control are all
correctly 0, so this is specific to the king-safety term rather than a general
orientation error. Whatever it measures, it charges black four centipawns for a
position identical to white's.

It also demonstrates §2.2 exactly: the total is `-4 + 1 (game phase) + 0.01
(tempo) = -2.99`, truncated toward zero to **−2**. Fix 4.1 first and this line
becomes −3, which is still wrong but wrong for only one reason.

*While fixing:* add a **mirror-symmetry check** to `tests/evalref.cpp` — for
each position, flip colours and ranks and assert every term negates exactly.
It is the strongest cheap invariant an evaluation has, it needs no reference
file, and it would have caught this the day the term was written. Add it as
part of this fix rather than before it, since it fails today.

**4.3 Make "defended" mean defended** (§2.3)
The undefended-pieces term counts a piece as defended if any friendly piece
stands on an adjacent square — that measures pawn-chain-ness, not protection.
`attackedBy[own][sq]`, already built by `forEachAttackedSquare` for the threat
term, is exactly the right predicate. Nearly free at runtime.

---

## Phase 5 — Performance, cheapest ceiling first

The backlog's measured ceilings are the whole point of this ordering. Current
split at depth 5: evaluation 34.1%, legality filter 19.4%, `makeMove` 10.4%.

**Status: 5.1, 5.2 and 5.3 are DONE** — 1.45× together, all verified by the
bench signature staying at 2 056 371. Remaining: 5.4 (lazy eval, needs a match)
and 5.5 (pin-aware movegen). See `BACKLOG.md §7` for the measurements.

**5.1 Cache the static eval in the TT entry** (§4.3) — repeated visits skip
evaluation entirely. No change to search results, so `test-evalref` and
`test-perft` fully cover it. Best ratio of payoff to risk in this phase.

**5.2 Small allocation and cache wins** (§4.4)
- `generateLegalMoves()` returns `MoveList` by value — one heap allocation per
  node; give it the out-parameter treatment `generatePseudoLegalMoves()` already
  has.
- Repetition detection is a linear `std::find` over `pathHashes` at every node.
- `move_lookup` tables are `std::vector<int>[64]` — a pointer chase per piece;
  fixed-size arrays stay in cache.
All three are behaviour-preserving.

**5.3 `castlingRights`/`enPassantTarget` as `std::string` → 4-bit mask + int
square** (§4.2, ceiling ~1.05×) — touches `fen.cpp`, `zobrist_hash.cpp`,
`pgn.cpp`, `board.cpp`. Low risk, low reward; behaviour-preserving, so
`test-perft` covers it.

**5.4 Lazy evaluation** (§4.3) — material + PST first, skip the expensive terms
when already far outside the alpha-beta window. *Changes results*, so it needs
a match, not just `evalref`.

**5.5 Pin-aware legal move generation** (§4.1, ceiling 1.24× ideal / ~1.15×
real — note this ceiling was measured against the *old* profile; with the
legality filter now a larger share of a faster search, re-measure before
committing to it) — last, deliberately. It is the most notorious source of engine bugs
(en-passant discovered check along a rank, pinned pawns capturing en passant,
king moves that stay on a slider's ray). Only attempt with `test-perft` and
perft divide (0.3) in place, and only after Phases 1–4 have banked their much
larger strength gains.

**Explicitly not doing:** bitboard move generation (§4.0, ~1.02× ceiling) and
incremental material/PST updates (§4.3, ~1 µs against real drift risk).

---

## Sequencing summary

| Phase | Content | Gate to advance |
|---|---|---|
| 0 | Safety nets, deletions, **negamax**, CI | tests green; bench identical across 0.9 |
| 1 | Time control, depth, match TC + SPRT | ✅ 1.4 +246 Elo, 1.5 +140 Elo |
| 2 | UCI | cutechess-cli self-play completes clean |
| 3 | Qsearch bound, SEE, extensions, pruning, IID, LMR | each SPRT-accepted |
| 4 | tempo, real defence | evalref diff as expected + match |
| 5 | TT eval cache → allocations → strings → lazy eval → pins | perft/evalref clean; match for 5.4 |

**Done so far:** Phases 0, 1 (code **and** both gates) and 2. Six tests now run
in CI: perft, gamestate, evalref, bench, timecontrol, UCI, plus a match smoke
test. **Phase 3 is unblocked**, starting with wiring in the already-written SEE.

Phases 0 and 1 are where nearly all the available strength is. Phase 5 in total
is worth less than raising the default depth in 1.2. Phase 0 buys no strength at
all — it buys the ability to tell whether anything after it worked.

## Standing discipline

- Benchmark through `findBestMoveIterativeDeepening` with a TT, the way the app
  runs. After Phase 0.4 there is no other path to accidentally benchmark.
- Where a change can be verified by identity (movegen, evaluation), do that
  instead of playing games — faster and conclusive. Where it cannot (anything
  that changes the search tree), only matches say anything.
- Test in the operating regime. Three wrong conclusions last session came from
  measuring at a convenient depth rather than a representative one.
- **A timed match needs a quiet machine.** `-t 3000` is wall clock: anything
  else competing for CPU means both engines get fewer nodes in their three
  seconds, so the result describes a shorter and blurrier time control than the
  one requested. The load lands on both sides roughly equally — they alternate
  moves and swap colours — so this inflates variance rather than biasing the
  result, and SPRT simply needs more games. But a timed match is also **not
  reproducible**: unlike a fixed-depth run, the same seed does not replay the
  same games under different load. In practice: no parallel builds (`make -j4`,
  not `-j16`), and never two matches at once.
- Do not "fix" that by gating on fixed nodes instead. It would be load-immune
  and it would be wrong: a feature whose value is a smaller tree gets no credit
  at equal nodes, so it measures only the accuracy cost. That is the depth-4
  mistake in a new costume. Equal time is correct *because* the feature is a
  speedup; a quiet machine is the price.
- Deterministic tests are immune to all of the above and can run any time:
  `perft`, `evalref`, `see_test`, `gamestate`, and `bench`'s node counts (its
  time column is not). This is what makes `bench --opt` trustworthy under load.
- Update `BACKLOG.md §7` with new measurements as each phase lands; the
  baseline table is the thing that makes the next session cheap.

---

## The two Phase 1 gates — both cleared

**1.4 — does the depth raise pay?** New defaults vs. the old depth-5 ones.

```
./tests/match -n 150 --ha on --hb on --da 8 --db 5 --sprt --elo0 0 --elo1 20
→ +246 Elo [+151, +390].  H1 accepted.
```

**1.5 — are the search heuristics worth it?** Time-equalized at 3 s/move, the
regime where the heuristics are ~20–31× faster rather than the 1.31× they get at
depth 4.

```
./tests/match -n 400 -t 3000 --sprt
→ 136 games (W 69 / D 50 / L 17), 69.1%, +140 Elo [+94, +191]
  LLR +2.96, H1 accepted, wall 27 505 s.
```

Both stopped well short of their game counts — SPRT ends as soon as the evidence
is conclusive, and 1.5 needed 136 games where the fixed-N plan wanted ~800.

## Running a Phase 3 gate

**First, look at the tree — it costs seconds, not hours:**

```
./tests/bench 6 --opt <feature>=on
```

Compare against the 2 056 371-node baseline. A feature that barely moves the
count, or moves it the wrong way, is wired in wrong or is not doing what you
think; find that out now rather than a day into a match. This is how 3.2's
classify-don't-order bug was caught.

**Then gate it.** Each feature gets its own `SearchOptions` toggle so A and B
differ by exactly one thing:

```
./tests/shard-gate.sh 14 60 -N 100000 --optA <feature>=on --optB <feature>=off
```

- **Budget nodes, not milliseconds.** `-N 100000` per move is the standing
  gate time control, replacing `-t 3000`. A node budget is spent identically
  whatever else the machine is doing, so the result reproduces exactly and the
  gate can be sharded across cores; the first `seepruning` attempt ran 12 hours
  on the clock, half of it against a job pinning fifteen cores, and produced
  103 pairs of unusable data. Use `-t` only to gate a change whose point is
  speed per node rather than quality per node.
- **Shard it.** `tests/match` is single-threaded; `shard-gate.sh` runs one
  gate as N seeded shards and pools them with `pool-shards.sh`. 14 shards × 60
  pairs is 1 680 games in roughly the time 100 games used to take.
- **No `--sprt` under sharding**, and the script refuses it: a stopping rule
  applied per shard stops each one on its own favourable noise. Fixed N per
  shard, pooled after, is the valid form. `--sprt` is still right for a single
  unsharded run.
- `tee` it to a log — or rather, the script writes one per shard already.
- Changing the gate's time control between features makes their results
  incomparable, and 1.5 is the standing demonstration of what that costs. The
  move from `-t 3000` to `-N 100000` is that break, made once and deliberately;
  every Phase 3 result from here is on nodes.
- Expect far more games than 1.5 needed. It stopped at 136 only because +140 Elo
  is 14× the H1 bound; game counts scale roughly as 1/effect². Hitting the cap
  with the LLR drifting slowly upward is a real but modest effect, not a
  failure, and the point estimate with its CI is still a good answer.
