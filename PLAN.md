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

**Status: COMPLETE.** All ten items landed. `make tests` built five binaries at
the time; it now builds ten, and CI runs eleven test steps. `test-perft`,
`test-gamestate`, `test-evalref` and `test-bench` all pass on every push. The negamax conversion (0.9) reproduced the bench
signature bit-identically — 2,056,371 nodes and the same best move in all 12
positions — which is the strongest evidence available that it was an exact
restatement rather than a rewrite. (That figure is the signature *as it stood
then*; it is 1 599 675 today. The claim here is about matching the baseline of
the day, so the old number is the right one to keep.)

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

> **Superseded — the defects listed below are all fixed.** The module was
> subsequently *completed*, not merely labelled: magics are initialised and
> validated exhaustively, `BitboardMove` carries castling and en-passant flags,
> `checkers()`/`blockersForKing()` and a legal generator with make/unmake all
> exist, and `tests/bitboard_test` perft-checks it against the same published
> counts as `movegen.cpp`. It remains **unconnected** — nothing outside the
> module calls it — for the reason this item already gives: a movegen swap caps
> the search at ~1.02×. The `STATUS:` block at the top of `bitboard.hpp` is
> current; the paragraph below is kept as the record of why the module was not
> deleted. Backlog §2.1 is stale in the same way.

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

**3.1 Quiescence depth limit and delta pruning** (§5.3) — **DONE 2026-08-14**,
as two independently toggled halves.

`qbound` (**on**): an 8-ply cap past the horizon. Bench 1 759 990 → 1 464 599
(−16.8%), concentrated exactly where it should be — kiwipete −26.0%, tactical
−23.4%, six positions unchanged, no best move changed. A repair, so it defaults
on, like `ttAging`. It also pays back the entire +20.1% that the Phase 4
evaluation fixes cost, which is what the Phase 4 gate could not measure.

`deltapruning` (**off**): gated 2026-08-14 at a 200cp margin and **rejected,
−50.0 Elo, 95% CI [−60.3, −39.7]** over 3 360 games. All twelve shards negative,
−36 to −69 — uniform, not one bad shard.

That margin was my error: this item specifies *a queen's worth*, and 200 is four
and a half times more aggressive. The constant is now 900 as written, where the
same rule cuts 8.3% of nodes instead of 37.5%. **Re-gate before drawing any
conclusion about the technique** — the rejection above is a verdict on the wrong
margin, and leaving it as the record would be an unfair one.

**Re-gated at 900 the same day: +7.1 Elo, 95% CI [−2.9, +17.2].** The interval
spans zero, so no gain is demonstrated — but the swing from the margin alone is
**57 Elo**, which is the finding worth carrying out of this item. The technique
was never the problem; the number I chose for it was.

It stays **off**, on the same reasoning that keeps `seepruning` off: the project
does not ship defaults a gate has not demonstrated, and +7.1 [−2.9, +17.2] has
not demonstrated one. It is a better candidate than `seepruning` was — the point
estimate is higher, the interval is mostly positive, and it also cuts 8.3% of
nodes, so it may be positive on both axes rather than neutral on both. Resolving
it needs about twice the games, which is roughly four hours at this rate; the
seeds are `20260810 + i*1000`, so a second run must vary them or it replays the
same games.

The rejection at 200 is still informative about *this* engine. Delta pruning is
a bet on the static evaluation, and it is only as good as the evaluation making
it. This one is hand-written piece-square tables that carried three correctness
bugs until 2026-08-14 and whose quality remains unmeasured — Phase 4 returned
+6.1 with the interval spanning zero. A tight margin asks that evaluation to be
right about positions it has never been shown to judge well, and 3 360 games say
it is not. **Read that before 3.4**, which is the same bet.
`quiescence()` has no ply bound, which is why kiwipete costs 200× a normal
middlegame position. Add a ply cap (~8 beyond the horizon) and a delta-pruning
rule (skip a capture that cannot raise alpha even with a queen's worth of
margin). Bounds worst-case node counts and is close to free in strength.

**3.2 Static Exchange Evaluation** (§5.2 — biggest single win available)
*Status: implemented, unit-tested (`make test-see`, 13 hand-computed positions)
and **wired into the search** as of `1c0c6d6`. Both gated 2026-08-13:
`seeordering` **accepted** (+25.6 Elo) and **now on by default**, which moved the
bench signature to 1 465 771; `seepruning` **not demonstrated at equal nodes**
(+2.2, CI spans zero). Re-gated on the clock 2026-08-14 and **still not
demonstrated** (+4, CI [−7, +14]): this item is now closed and `seepruning`
stays off. See the gate results below.*

**The timed gate, and what it settles.** The case for a second gate was that
`seepruning` buys speed per node rather than quality per node, and a node budget
pays both sides the same nodes, so the equal-nodes result could not see it. That
reasoning was sound; the measurement did not support the conclusion. 3 360 games
at `-t 100`, sequential on an otherwise idle machine, returned +4 Elo with a
95% interval of [−7, +14] — overlapping the equal-nodes interval almost exactly.
Two instruments chosen to disagree, agreeing.

What is *not* settled is long time controls. `-t 100` is 100 ms per move; the
Lichess bot spends 15-25 seconds. A timed gate is sequential (`shard-gate.sh`
refuses anything without `-N`, since shards under a clock compete for the CPU),
so this one cost 6 h 36 m of wall clock, and halving the interval costs four
times the games. A gate at a realistic time control is days of machine time for
one question, which is why this one was run at 100 ms and why the answer is
qualified rather than universal.

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

*Gates, run 2026-08-13, 14 shards × 120 pairs each (3 360 games), baseline
`nullmove+lmr+asp+ttage`:*

| gate | Elo | 95% CI | verdict |
|---|---|---|---|
| `seeordering` on vs off | **+25.6** | [+16.1, +35.2] | **accepted** |
| `seepruning` on vs off | +2.2 | [−7.2, +11.6] | not demonstrated |

```
./tests/shard-gate.sh 14 120 -N 100000 --optA seeordering=on --optB seeordering=off
./tests/shard-gate.sh 14 120 -N 100000 --optA seepruning=on  --optB seepruning=off
```

The estimate of +40–60 Elo was made for the *combined* 1.73× and came in high;
banding alone is worth about +26, which is roughly the two-thirds of the node
saving it accounts for.

**The two results do not mean what the node table predicts, and the reason is
the gate's own budget.** `seepruning` cuts the most nodes (−41.1%) and won the
fewest games; `seeordering` cuts fewer (−28.7%) and won clearly. That is not a
contradiction, because `-N 100000` *pays both sides the same nodes* and so
deliberately measures quality per node with speed per node divided out.
Banding changes which move is searched first, which is quality; skipping losing
captures in quiescence mostly changes how fast the same conclusion is reached,
which the budget hides by construction. So the honest reading is that
`seepruning` is **neutral at equal nodes and 1.73× cheaper**, and its value is
real but invisible to this instrument.

*Therefore the open question on `seepruning` is a timed gate, not a longer
node-budgeted one* — this is the exception the standing "gate on nodes" rule
names, a change whose value is speed per node:
```
./tests/shard-gate.sh 14 120 -t 3000 --optA seepruning=on --optB seepruning=off
```
Read it knowing a timed match is load-dependent: run it on a quiet machine, and
treat a narrow result as suspect in a way the node-budgeted ones need not be.

*Turning `seeordering` on moved the bench signature to **1 465 771** (from
2 056 371) and changed two stored best moves*, which is expected — ordering
decides which of several moves reaching the same score is returned, and it
changes what gets pruned at a fixed depth. `midgame-1` went `d1c2` → `d4c5`;
the engine picks `d4c5` under unrelated conditions too (UCI, 64 MB and 256 MB
hash), so that one is corroborated. `open-sicil` went `b8c6` → `d7d5`, which is
the weaker-looking of the two: after 1.e4 c5 2.Nf3, `2...d5 3.exd5 Qxd5` costs
Black time, and the same position over UCI still returns `b8c6`. It is specific
to bench's conditions and is beyond a depth-6 horizon. **Recorded rather than
resolved:** the bench is a signature test, not a strength test, and the strength
question was answered by 3 360 games. If a future eval change is expected to fix
horizon effects, this is a position worth re-reading.

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
usually worth 10–20 Elo. **DONE 2026-08-14: accepted at +23.0 Elo, 95% CI
[+13.3, +32.7]** over 3 360 games at equal nodes, 11 of 12 shards positive. On
by default. Slightly above the range this line predicted, and the largest
accepted gain since `seeordering`.

Placed before the `depth == 0` drop into quiescence rather than inside the move
loop, which is the whole decision: a check arriving exactly at the horizon is
the case worth extending, and by the time depth hits zero the node has already
been handed to quiescence — which searches evasions but cannot search the quiet
consolidating move after them. The TT probe moved below the extension so the
entry asked for matches the depth about to be searched.

Bench with it on: 1 464 599 → **1 599 675, +9.2%**, and two best moves change.
`open-sicil` returns to `d7d5`, which 3.2 and 4.3 both moved *away* from and
which is dubious on principle (`2...d5 3.exd5 Qxd5` costs black time), and
`zugzwang` moves `e1e5` → `e1e6` at +61% nodes for that position. Neither was a
verdict at depth 6, and the gate did not disappoint — but they are still the
first places to look if check extensions ever need retuning.

**Why this one worked where the last two did not.** `seepruning` and
`deltapruning` buy *speed* per node, and a node budget pays both sides the same
nodes, so the instrument hides most of what they are for. Check extensions buy
*quality* per node — they cost 9.2% more nodes and win anyway. An equal-nodes
gate is exactly the right instrument for that, and it saw the effect on the
first run. Match the instrument to what the change is supposed to buy.

**3.4 Futility pruning and razoring at shallow depths** (§5.2) — needs the
lazy-eval margins to be sane; keep it after 3.2 so SEE already removes the worst
noise.

**3.5 Internal iterative deepening** (§5.2) — when there is no TT move to order
on, do a shallow search to find one. Pays off most in the deeper regime Phase 1
unlocks.

**3.6 Retune the LMR formula** (§1.1, §5.2) — only now, with a time-equalized
harness and a search that has stopped changing shape underneath it. Grid over
reduction depth/move-count thresholds, SPRT the best two or three candidates.

**3.7 Transposition table aging**
*Not in the backlog — found while preparing the 3.2 gates.*

*Status: implemented and **on by default**, unlike everything else in Phase 3.
Gated 2026-08-13 — **+11.5 Elo, 95% CI [+3.2, +19.8]** over 3 360 games
(14 shards × 120 pairs, `-N 100000`). The default it shipped with was the right
one, and it is now measured rather than assumed.*

The table was depth-preferred and **ageless**: an entry could only be displaced
by one at least as deep, with no notion of which search stored it. Over a game
that means positions from moves already played — which will never occur again —
permanently occupy slots the live search cannot reclaim, so the useful share of
the table shrinks move by move. This is how a warm table comes to play *worse*
than an empty one.

The fix is one generation counter, bumped once per search
(`TranspositionTable::newSearch()`), and one clause in `shouldReplace`: an entry
from an earlier generation is evictable regardless of depth. It costs a byte
per entry, paid for by narrowing `depth` to `int8_t` so the entry does not grow
— which matters beyond memory, since entry size divides into `ENTRIES_PER_MB`
and a wider entry would change the table length, the index distribution, and
therefore every node count the search produces.

**It defaults ON because it is a repair, not a feature.** A Phase 3 toggle
defaults off until a gate accepts it — as `seeordering` did on 2026-08-13 —
whereas this one restores intended behaviour that was never there, so the burden
runs the other way. It is still a
toggle — so the repair can be measured, and so the old behaviour is one flag
away if the measurement disagrees.

```
./tests/shard-gate.sh 14 120 -N 100000 --optA ttaging=on --optB ttaging=off
```

---

## Phase 4 — Evaluation correctness

Both items change evaluation output, so both need `test-evalref` to *show a
diff* (confirming the change landed and is shaped as expected) and then a match
to confirm it is an improvement.

**4.1 Fix `tempoBonus`** (§2.2) — **DONE 2026-08-14.** Deleted rather than made
an honest int: as written it was a constant, not a bonus to the side to move, so
there was no correct integer version of it to keep. A real tempo bonus is a
separate, gateable idea. The mirror-symmetry test also found a second addend
with the same defect, `(int)(gamePhaseFactor * 1.5f)`, which is gone too. See
`BUGS.md` 2.
`evaluation.cpp:235` declares it `float 0.01f`; summed into `e.total` at `:588`
it promotes the sum to float, and truncation-toward-zero turns −5 into −4. The
net effect is a one-centipawn asymmetry favouring black that has nothing to do
with tempo. Make it an honest `int` applied to the side to move, or delete it —
prefer an honest int and let the match say whether it earns its place.

**4.2 King safety is asymmetric in a symmetric position** — **DONE 2026-08-14.**
Cause was `|x - 3|` as distance from the centre, which is not symmetric on an
eight-coordinate axis whose centre lies between 3 and 4. Replaced by
`centreDistance()`, measuring to the nearer of the two central coordinates. The
mirror-symmetry check below was added with it and now passes across 4 691
positions. Cost: bench 1 465 771 -> 1 725 755 (+17.7%), no best move changed.
See `BUGS.md` 3.

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

**4.3 Make "defended" mean defended** (§2.3) — **DONE 2026-08-14.** Swapped to
`attackedBy[own][sq]`, which also removed a nine-square scan per piece. bench
1 725 755 -> 1 759 990; `open-sicil` returns to `b8c6`, resolving the unexplained
best-move change 3.2 recorded when `seeordering` landed. See `BUGS.md` 5.
The undefended-pieces term counts a piece as defended if any friendly piece
stands on an adjacent square — that measures pawn-chain-ness, not protection.
`attackedBy[own][sq]`, already built by `forEachAttackedSquare` for the threat
term, is exactly the right predicate. Nearly free at runtime.

---

## Phase 5 — Performance, cheapest ceiling first

The backlog's measured ceilings are the whole point of this ordering. Current
split at depth 5: evaluation 34.1%, legality filter 19.4%, `makeMove` 10.4%.

**Status: 5.1, 5.2 and 5.3 are DONE** — 1.45× together, all verified by the
bench signature staying at 2 056 371, which was the baseline at the time (it is
1 599 675 now). Remaining: 5.4 (lazy eval, needs a match)
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

**Its premise has largely collapsed — re-profile before starting it.** `BACKLOG`
§7 put evaluation at 34.1% of search time on 2026-08-10, and that is the number
this item was justified by. A gprof run on 2026-08-15 (`make profile`) puts
`evaluate_details` at 12.4% and `evaluate` at 4.3%, about **16.7% together**.
5.1's evaluation cache already took most of what 5.4 was going to take, and the
two overlap by construction — both avoid work the cache may already be avoiding.
Whatever is left is worth less than half what the plan assumed, against a change
that alters results and so costs a full gate.

**5.5 Pin-aware legal move generation** (§4.1, ceiling 1.24× ideal / ~1.15×
real — the ceiling was measured against the *old* profile, and re-measuring on
2026-08-15 **confirmed the instinct behind that warning**: move generation and
the legality filter are now the largest real cost in the search, at roughly
**27.6%** — `isSquareAttacked` 13.8%, `generatePseudoLegalMoves` 10.6%,
`Board::isSquareAttacked` 2.2%, `generateLegalMoves` 1.0%. That is where the
time is, and it is what this item removes) — last, deliberately. It is the most notorious source of engine bugs
(en-passant discovered check along a rank, pinned pawns capturing en passant,
king moves that stay on a slider's ray). Only attempt with `test-perft` and
perft divide (0.3) in place, and only after Phases 1–4 have banked their much
larger strength gains.

**5.6 Inline the `Piece` accessors** — **DONE 2026-08-15, 1.87×.** Not in the
original plan, and found only by profiling. `Piece::type()`, `Piece::color()`
and the default constructor are each a single instruction, and each lived in
`piece.cpp`, so every use was a real cross-translation-unit call: 1.87 billion
calls to `type()`, 381 million to `color()`, 545 million to the constructor —
about 21% of runtime, essentially all overhead. Moved into the header as
`constexpr`. Bench **7 173 ms → ~3 830 ms** on an identical 1 599 675-node
search, and the signature is unchanged, which is the proof it changed nothing
but speed. `piece.cpp` is deleted.

The lesson is worth more than the speedup: this was the single largest
performance win in the project, it took five lines, and no amount of reasoning
about algorithms would have found it. Profile before choosing what to optimise —
and `make profile` now exists so there is no excuse not to.

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

**Done so far:** Phases 0, 1 (code **and** both gates) and 2. Eleven tests now
run in CI: perft, gamestate, evalref, bench, see, bitboard, timecontrol, UCI,
guiinput, pgn, plus a match smoke test. **Phase 3 is unblocked**; SEE is wired
in and both its gates have run (3.2) — ordering accepted and on, pruning still
off pending a timed gate.

Landed since this plan was written, and not covered by any item above: PGN
export with a SAN test, the headless GUI-input test, and transposition-table
aging (3.7 below).

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
- ~~Do not "fix" that by gating on fixed nodes instead. It would be load-immune
  and it would be wrong: a feature whose value is a smaller tree gets no credit
  at equal nodes, so it measures only the accuracy cost. That is the depth-4
  mistake in a new costume. Equal time is correct *because* the feature is a
  speedup; a quiet machine is the price.~~

  **Reversed — nodes are now the standing gate budget** (`-N 100000`), for the
  reasons in "Running a Phase 3 gate" below. What this bullet got right is the
  cost, and it still stands as the caveat: a change whose value is *speed per
  node* rather than *quality per node* earns nothing at equal nodes and must be
  gated on the clock (`-t`) instead. What it got wrong is treating "a quiet
  machine" as a price that can actually be paid — it cannot be guaranteed over
  a multi-hour run, and the first `seepruning` attempt is the proof: 12 hours,
  half of it against a job pinning fifteen cores, 103 pairs of unusable data.
  A budget that silently depends on machine load is not a budget.
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

Compare against the current baseline, **1 599 675** nodes at depth 6 (2 056 371
until `seeordering` was gated on 2026-08-13; then 1 465 771, 1 725 755 and
1 759 990 as the three Phase 4 evaluation fixes landed on 2026-08-14; then back
down when the quiescence bound landed the same day). A feature that barely moves the
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
