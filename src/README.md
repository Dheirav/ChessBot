# Architecture

A desktop chess application, not a reusable library. Build and run from the
repository root:

```bash
make && ./chessbot          # GUI
./chessbot --uci            # UCI, no window
```

## The three layers

```
                 ┌────────────────────────────────────────┐
   main.cpp ───► │ --uci?  ──yes──►  engine/uci.cpp        │
                 └───────────────────────┬────────────────┘
                          no             │  (no SFML, no window)
                          ▼              ▼
        gui/  ──────►  game_manager  ──────►  engine/
     input, render     turn flow,           board, movegen,
     HUD, assets       terminal state,      eval, search
                       undo/redo, PGN
```

The dependency arrows only point right. The GUI owns no chess rules — it asks
the game manager what is legal and draws the answer. The engine knows nothing
about a *game* being played: it is handed a position and a budget, and returns a
move. Everything about turn order, draw claims, undo history and results lives
in the middle layer, which is exactly why `tests/gamestate` can link the game
manager without SFML and drive whole games headlessly.

`main.cpp` checks for `--uci` before anything touches SFML. That ordering is
load-bearing: it is what lets the engine run on a machine with no display, which
is how cutechess-cli, lichess-bot and CI drive it.

## Threading

The search runs on its own `std::thread` so a multi-second think does not freeze
the window. Two things make that safe:

- **The position is copied on the command thread**, at the point the search is
  requested, not inside the search.
- **A generation counter guards the hand-off.** The engine stores its result in
  a mutex-guarded slot that the main thread drains each frame. Undo, redo,
  resign and new-game all bump the generation under the same mutex; a search
  that finishes after the position was reset sees a mismatch and its move is
  dropped. Without this, a search racing a discard could play a move into a
  position it never examined.

`ChessBotEngine` holds `searchDepth`, `moveTimeMs`, `thinking` and `stopSearch`
as `std::atomic` for the same reason — they are written from the GUI thread and
read from the search thread.

## `src/engine`

| file | role |
|---|---|
| `board.{hpp,cpp}` | position state, `makeMove`/`unmakeMove`, FEN, incremental Zobrist hash |
| `movegen.{hpp,cpp}` | **the** move generator (mailbox + lookup tables) |
| `move_lookup.{hpp,cpp}` | precomputed per-piece move tables, cached to `lookup_data/` |
| `evaluation.{hpp,cpp}` | the handcrafted evaluation, term by term |
| `search.{hpp,cpp}` | iterative deepening, alpha-beta, quiescence, all heuristics |
| `move_ordering.{hpp,cpp}` | MVV-LVA, killer moves, history heuristic |
| `transposition_table.{hpp,cpp}` | depth-preferred TT with per-search aging |
| `zobrist_hash.{hpp,cpp}` | the hash keys |
| `see.{hpp,cpp}` | static exchange evaluation |
| `bitboard*.{hpp,cpp}`, `magic_bitboards.*` | complete, verified, **not connected** |
| `uci.{hpp,cpp}` | the UCI protocol loop |
| `pgn.{hpp,cpp}` | SAN and PGN document export |
| `fen.{hpp,cpp}` | FEN parsing helpers |
| `chess_engine_interface.hpp` | the interface the game manager depends on |
| `chessbot_engine.{hpp,cpp}` | the implementation: threading, TT ownership |

### Things that will surprise you

**There is one legal move generator, and it is mailbox-based.** The bitboard
module is finished and perft-correct but nothing outside it calls it. That is
deliberate and documented at the top of `bitboard.hpp`: swapping it in for
`movegen.cpp` was measured at a ~1.02x ceiling, because move generation is only
about 2% of search time. It exists for its *consumers* — SEE at every node, and
replacing the make-move legality filter, which is the biggest item left in the
profile.

**The bitboard module uses a8 = bit 0**, the opposite of the near-universal
a1 = 0 convention, to match `Board`'s indexing. Two orientations in one codebase
is exactly how a bitboard module ends up silently generating wrong moves, so
everything in that module assumes this one.

**`generateLegalMoves` mutates the board it is given.** The search relies on it:
moves are made and unmade in place, so no board copy is paid per node. The
position is exactly as it came in on return. A `const Board&` overload exists for
callers that only hold a const board (the GUI, the in-check mobility path) and
it pays for one copy.

**Mate scores are stored ply-relative in the TT** and converted back on probe,
so a mate found via one path transfers correctly to a different ply.

**`TTEntry.depth` is an `int8_t` on purpose.** The byte freed pays for
`generation` without growing the entry — and entry size divides into
`ENTRIES_PER_MB`, so widening it would change the table's length, its index
distribution, and therefore every node count the search produces.

**Search heuristics live in one table**, `SEARCH_OPTIONS` in `search.hpp`. Both
`setSearchOption()` and `describeSearchOptions()` read it, so a feature cannot
become settable but undescribable — drift that invalidated a match gate once.
Adding a heuristic means adding one line there; it is then switchable from the
match harness and over UCI at once.

**`g_searchNodes` is deliberately not atomic.** One search runs at a time and it
is read after that search returns, keeping the increment a single add in the
hottest loop in the engine. If the search ever goes concurrent, this becomes
per-thread state, not an atomic.

## `src/gui`

`renderer.cpp` draws, `input.cpp` is a state machine over selection,
drag-and-drop and the promotion dialog, `hud.cpp` owns the side panel and
receives the search's per-iteration output through `installSearchCallback()`.
`constants.hpp` holds the geometry the board and panel scale by.

The promotion dialog's hitboxes and its drawing were once computed separately,
and drifted apart the moment the side panel widened the window. `tests/guiinput`
now pins both to the same geometry, headlessly.

## Where to look before changing something

The headers carry the reasoning, not just the declarations. Read these first:
`search.hpp` (what each heuristic costs and why it is a toggle), `bitboard.hpp`
(why the module is unconnected), `see.hpp` (what SEE does and does not model),
`transposition_table.hpp` (aging and replacement), `config.hpp` (why each
default is what it is).

Then check [PLAN.md](../PLAN.md) — the work is phased so each piece is
verifiable when it lands, and many surprising choices are a phase boundary
rather than an oversight.
