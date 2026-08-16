
# ChessBot

## Overview
ChessBot is a local C++ chess application with an SFML-based graphical interface and a built-in chess engine. It plays in its own window, and also speaks UCI (`./chessbot --uci`), so it can be driven by standard chess tooling or put online as a Lichess bot.

It includes:
- A full chess board with click-to-move and drag-and-drop input
- A negamax alpha-beta engine with iterative deepening, a transposition table,
  bounded quiescence search, null-move pruning, late move reductions, check
  extensions and aspiration windows, under a wall-clock budget it is required
  to respect
- Perft-verified move generation, including castling, en passant and promotion
- Full terminal detection — checkmate, stalemate, fifty-move, threefold
  repetition, insufficient material — plus PGN export, undo/redo, resignation,
  and an interruptible search
- A UCI mode, so standard chess tooling can drive it
- Optional runtime settings loaded from a config file

## Requirements
This project expects:
- A C++17 compiler such as g++
- SFML development libraries for graphics, windowing, and system support

On Debian/Ubuntu systems, the package list in [packages.txt](packages.txt) can be used:

```bash
sudo apt update
xargs -a packages.txt sudo apt install -y
```

## Build
From the repository root, run:

```bash
make
```

That produces an executable named `chessbot` in the project root.

## Run

```bash
./chessbot
```

The window opens and asks which side you want to play; pick one and the game starts against the engine.

## Play-along mode

For a game happening somewhere else — on a board, on another site, against a
friend. You type the moves as they are played and the engine tells you what it
would play. It never moves for you: it advises, and you type what you actually
played, which may not be what it suggested.

```bash
./chessbot --coach --black            # you are Black
./chessbot --coach --white --time 10  # you are White, 10s per suggestion
```

```
  8  r n b q k b n r
  7  p p p p p p p p
  ...
  opponent> e4

  >>> play  Nc6   (+0.14, 2s)
  you>
```

Moves go in as you would write them — `e4`, `Nf3`, `O-O`, `exd5`, `e8=Q` — and
long form (`e2e4`) is accepted too. Pressing Enter alone on your turn plays the
suggestion. The board is drawn from your side, and the evaluation is from your
side as well: `+0.4` means *you* are better, whichever colour you have.

`board`, `undo`, `moves`, `eval`, `help` and `quit` do what they say. `--time`
is seconds per suggestion and defaults to 5.

## Controls
- Click a piece, then click one of its highlighted squares, to make a move
- Or drag and drop a piece onto its destination
- Clicking an empty square or an illegal destination clears the selection
- Promotions open a dialog on the board: click a piece, or press Q, R, B or N.
  Escape or a click outside the dialog cancels the move
- Ctrl+Z: undo the last move (this also revives a finished game)
- Ctrl+Y: redo a move
- Ctrl+S: save the game as PGN under `games/`, ready to paste into
  Lichess analysis or open in any viewer
- R, twice: resign the current game. The first press asks; any other key cancels
- ESC: interrupt the engine while it is thinking

The window is resizable: the board and panel scale together and stay centred.

## UCI mode

```bash
./chessbot --uci
```

UCI is checked before anything touches SFML, so this mode opens no window and
needs no display — it is a plain stdin/stdout protocol, which is what lets
cutechess-cli, Arena, python-chess and lichess-bot drive the engine.
`chessbot-uci.sh` wraps the same thing for tools that want a bare command.

Supported commands are `uci`, `isready`, `ucinewgame`, `position [startpos |
fen <6 fields>] [moves ...]`, `go`, `stop` and `quit`. `go` understands
`depth`, `movetime`, `infinite`, and `wtime`/`btime`/`winc`/`binc`/`movestogo`;
a clock is divided rather than spent — with no `movestogo` it assumes a
reasonable number of moves remain and keeps a reserve, so one slow iteration
cannot forfeit. `go` with nothing specified falls back to a depth-limited
search rather than thinking forever. (The search itself also supports a node
budget, but that is reached through `tests/match`, not over UCI.)

During the search the engine emits `info depth … score … nodes … nps … pv …`,
reporting mates as `mate N` rather than a centipawn value.

| option | type | default | notes |
|---|---|---|---|
| `Hash` | spin, 1–4096 | 256 | transposition table size in MB |
| `NullMove` | check | true | null-move pruning |
| `LMR` | check | true | late move reductions |
| `Aspiration` | check | true | aspiration windows |
| `TtAging` | check | true | age the TT once per search |
| `SeeOrdering` | check | true | order captures by SEE; gated **+25.6** Elo |
| `SeePruning` | check | **false** | drop losing captures in quiescence; gated twice, **not demonstrated** either way |
| `QBound` | check | true | cap quiescence 8 plies past the horizon; a repair, ungated |
| `CheckExt` | check | true | extend a ply when in check; gated **+23.0** Elo |
| `DeltaPruning` | check | **false** | skip captures that cannot reach alpha; **+7.1**, interval spans zero |

The heuristic toggles are exposed on purpose: A/B testing a single feature can
then run through standard tooling instead of only through `tests/match`. They
share one named-lookup table with the match harness, so a feature added there
becomes switchable over UCI without a second edit — and cannot end up settable
but undescribable, which invalidated a gate once. The defaults advertised here
are read out of a default-constructed `SearchOptions` rather than written by
hand, because the hand-written version went stale the moment a gate flipped one.

**A default of `false` is not a rejection.** It means no gate has demonstrated a
gain, which is a different claim. `SeePruning` cuts 41% of nodes and measured
+2.2 at equal nodes and +4.0 on the clock, both intervals spanning zero.
`DeltaPruning` measured +7.1, also spanning zero — and −50.0 at a tighter margin,
which is what settled the margin rather than the technique. Every figure above
is 3,360 games; see [`docs/PLAN.md`](docs/PLAN.md) for the gate that produced it.

Note that this engine advertises no `Threads`, `SyzygyPath` or `Move Overhead`.
python-chess raises on any option the engine did not advertise, so a GUI config
copied from a template will fail on the first game.

## Playing online

Because it speaks UCI, it can play on Lichess as a bot account against other
bots and humans. See [`lichess/README.md`](lichess/README.md); the short version
is a token in the environment and `./lichess/run.sh`. Rated games there are the
point rather than a demo: a Lichess rating is an *independent* strength
measurement, which self-play never produces — that only measures a change
against the previous version of itself.

## How it works
The application runs in three layers, and the split is what keeps the window
responsive while the engine is allowed to think for seconds at a time:

1. **GUI** (`src/gui`) — mouse input, rendering, the side panel. Owns no game
   rules; it asks the game manager what is legal and draws the answer.
2. **Game manager** (`src/game_manager.cpp`) — turn order, terminal detection,
   undo/redo, PGN export. It is the only layer that knows a *game* is being
   played rather than a position being searched.
3. **Engine** (`src/engine`) — board representation, move generation,
   evaluation, and search.

The engine searches on its own `std::thread`, and hands its move back through a
mutex-guarded slot that the main thread drains each frame. That is why the board
still redraws and still accepts Esc while the engine is thinking. The hand-off
carries a generation counter: undo, redo, resign and new-game all bump it, so a
search that finishes *after* the position was reset has its result dropped
instead of being played into a position it never examined.

## Search, and why each piece is there
The search is a single negamax alpha-beta with iterative deepening — there is
deliberately no second, simpler variant, because a second copy drifts from the
one the application actually runs and then benchmarks describe a search nobody
plays.

| technique | what it buys | why it is there |
|---|---|---|
| **Iterative deepening** | depth 1, 2, 3, … until the budget runs out | It is not wasted work: each iteration fills the transposition table and the killer/history tables, so the *next* iteration orders its moves far better. It is also the only way to honour a clock — there is always a completed depth to fall back on. |
| **Transposition table** | positions reached by different move orders are evaluated once | Chess transposes constantly. Entries are depth-preferred and store a best move, which doubles as the first move tried next iteration. |
| **TT aging** | one generation counter per search | Without it the table is *ageless*: positions from moves already played, which will never occur again, can only be displaced by something deeper still, so the live search gets a shrinking share of the table. A warm table came to play worse than an empty one. |
| **Quiescence search** | leaf nodes keep searching captures until the position is quiet | Fixes the horizon effect. Evaluating a position mid-exchange scores a queen "won" one ply before it is recaptured. |
| **Move ordering** (TT move → MVV-LVA → killers → history) | alpha-beta cuts off sooner | Alpha-beta's payoff depends entirely on trying the best move first. Perfect ordering turns O(b^d) into roughly O(b^(d/2)); bad ordering makes the pruning nearly worthless. |
| **Null-move pruning** | skip a turn; if the position is *still* too good, prune | Rests on the null-move observation: having a free move is almost always better than any real move. Disabled in check and with no non-pawn material, which is where zugzwang makes that observation false. |
| **Late move reductions** | moves ordered late are searched shallower first | If ordering is good, moves after the first few are unlikely to be best. Search them cheaply; re-search at full depth only if one unexpectedly beats alpha. |
| **Aspiration windows** | search a narrow band around the previous iteration's score | The score rarely moves far between iterations, and a narrow window cuts off far more of the tree. The window widens on a fail high/low rather than jumping straight to infinity, so a wrong guess costs one re-search, not the whole benefit. |
| **Static exchange evaluation** | plays an exchange out to see if a capture actually wins material | MVV-LVA sorts by victim alone, so it cannot tell QxP-that-hangs-a-queen from QxP-that-wins-a-pawn. Ships **half on** — see below. |
| **Check extensions** | search one ply deeper when in check | The reply to a check is usually forced, so few evasions exist and the extra ply is cheap. It is also where the horizon effect does most damage: a search that stops while in check evaluates a position whose material is about to change. Costs 9.2% more nodes and won its gate by **+23.0** Elo anyway. |
| **Bounded quiescence** | quiescence stops 8 plies past the horizon | It had no bound at all. In check it searches every legal evasion rather than captures only, so a long forcing sequence could recurse without limit — and an overrun on a clock is a forfeit, not a bad move. A repair rather than a feature, so it defaults on. |

Only alpha-beta itself, the transposition table and quiescence are exact — they
return the same move a full search would. The rest are heuristics: they trade a
small risk of missing a line for a much smaller tree. That is why each one is an
*independent* toggle rather than a single "heuristics on/off" switch — a
strength measurement is only interpretable if the two arms differ in exactly one
thing, which one combined switch cannot express.

**SEE ships half on**, and the split is the method working rather than an
inconsistency. `SeeOrdering` defaults **on**: it won its match by +25.6 Elo
(95% CI [+16.1, +35.2], 3 360 games) and was turned on the same day. `SeePruning`
defaults **off**: it measured +2.2 with the interval spanning zero.

That second result was not "SEE pruning does nothing". The gate pays both sides
the same *nodes*, which is what makes it reproducible — and that budget divides
out exactly what pruning buys. Skipping losing captures in quiescence cuts 41%
of the tree but barely changes the conclusion reached, so at equal nodes there
was nothing left for it to win with.

So it was re-gated on the clock, the instrument that *can* see a speed change:
**+4.0, 95% CI [−7, +14]** over another 3 360 games. Two instruments chosen to
disagree, agreeing. It stays off — not because it was rejected, but because a
heuristic is not accepted here until it wins a match against the version without
it (`docs/PLAN.md` 3.2), and neither match did.

That distinction is the whole method. `DeltaPruning` makes the point again: at a
200-centipawn margin it lost by **−50.0**, at the 900 the plan actually specified
it gained **+7.1** — a 57-Elo swing from one constant, which is a verdict on the
constant and not on the technique.

### Evaluation
Handcrafted and material-plus-terms: piece-square tables, mobility, king safety
and king activity, centre control, space, threats and undefended pieces, bishop
pair, rooks on open/semi-open files and the 7th, outposts, trapped pieces, and a
full pawn-structure set (doubled, isolated, passed, backward, connected,
chains), with a drawishness correction. Every term is broken out individually in
`EvalDetails` — not for display, but because `tests/evalref` diffs all of them
against a stored reference over ~23k positions, so an unintended change to one
term is caught by name.

### Time management
A depth limit alone cannot play a real game: the same depth costs milliseconds
in an endgame and seconds in a dense middlegame. A time limit alone cannot be
benchmarked: node counts stop being reproducible. So the search takes both a
depth ceiling and a wall-clock budget, and whichever binds first ends it. When
the clock binds, the move returned is from the last *completed* iteration — a
partial iteration has only searched an arbitrary prefix of its move list, so its
"best" move is not a best move.

There is a third limit, `maxNodes`, which exists for measurement rather than for
play: two configurations given the same milliseconds are only comparable if they
get the same share of the CPU for the whole match, which stops being true the
moment anything else runs on the machine. A node budget is spent identically
wherever it runs, so matches become reproducible from a seed and can be sharded
across cores. See [tests/README.md](tests/README.md).

## Performance notes
- **Move generation is mailbox-based**, in `src/engine/movegen.cpp`, using
  precomputed per-piece lookup tables. Legality is resolved by making each move
  and testing the king square, on the board in place rather than on a copy, so
  no board copy is paid per node.
- **The bitboard module is connected — for what it is good at, not as a movegen
  replacement.** `src/engine/bitboard*.cpp` provides magic sliding attacks,
  `attackersTo` with caller-supplied occupancy, checkers and pin detection. The
  legality filter in `movegen.cpp` uses all of it: instead of making every
  candidate move and asking whether the king is attacked, it computes the king
  square, the checkers and the pinned pieces once per position and answers each
  candidate with a bit test. That was worth **1.28×** and was verified by the
  bench signature staying byte-identical.

  It is still deliberately *not* used to replace `movegen.cpp` wholesale. That
  swap was measured at a ~1.02× ceiling, because it targets pseudo-legal
  generation rather than the legality filter that actually dominated. The module
  spent weeks as finished infrastructure with no consumer, which was the right
  call: the value was never in generating moves faster, it was in answering
  questions about the position without moving anything.

  One caveat on the numbers in this section: the ~1.02× and "2% of search time"
  figures come from the 2026-08-10 profile, and that profile has since been
  wrong twice about where the time goes. Re-measure with `make profile` before
  trusting either.
- **Incremental Zobrist hashing.** The position hash is updated as moves are
  made and unmade, not recomputed, so a transposition table probe costs a lookup
  rather than a board scan.
- **The search can be interrupted mid-flight** (Esc in the GUI, `stop` over
  UCI), which is what keeps a long think from feeling like a hang.

Historical numbers and the profiling that produced them are in
[BACKLOG.md](docs/BACKLOG.md); the ordered plan of work is in [PLAN.md](docs/PLAN.md).

## Configuration
Settings are read from `chessbot.conf` in the working directory, if it exists.
Every key is optional; missing ones keep the defaults in
[src/config.hpp](src/config.hpp).

```ini
moveTimeMs = 3000               # wall-clock budget per move; 0 searches by depth alone
searchDepth = 8                 # ceiling on iterative deepening (1..64)
transpositionTableSizeMB = 256  # hash table size
logEvaluations = off            # per-move evaluation dump, for debugging by hand
```

## Project layout
- [src/main.cpp](src/main.cpp) — entry point; dispatches to UCI or the GUI
- [src/config.hpp](src/config.hpp) — settings and the config-file parser
- [src/game_manager.cpp](src/game_manager.cpp) — turn flow, terminal detection,
  undo/redo, PGN
- [src/engine](src/engine) — board, move generation, evaluation, search,
  hashing, SEE, UCI. See [src/README.md](src/README.md)
- [src/gui](src/gui) — SFML input, rendering, HUD, assets
- [tests](tests) — the regression suite and the match harness. See
  [tests/README.md](tests/README.md)
- [lichess](lichess) — configuration for playing online as a bot

## Development

```bash
make            # build ./chessbot
make tests      # build every test binary
make clean
make remake
```

Every test has its own target, and CI runs all of them on push and pull request:

```bash
make test-perft        # move generation vs. published perft counts
make test-evalref      # every evaluation term vs. a stored reference
make test-bench        # the search's node-count signature
make test-see          # static exchange evaluation vs. hand-computed positions
make test-bitboard     # magic tables exhaustively + cross-check vs. mailbox
make test-gamestate    # a finished game refuses moves
make test-timecontrol  # a search with a clock returns inside it
make test-uci          # the UCI protocol (needs ./chessbot)
make test-guiinput     # click-to-move, drag, promotion hitboxes — headless
make test-pgn          # SAN export and disambiguation
make test-match        # short self-play match
make lichess           # play online (needs LICHESS_BOT_TOKEN)
```

Two of these are reference-diff tests and will fail on *any* change to what they
cover, intentionally. When the change was intended, review the reported diff and
regenerate: `make evalref-regen`, `make bench-regen`.

Strength changes are not accepted on judgement — they are gated on a match.
[tests/README.md](tests/README.md) covers how those are run, why they are
node-limited rather than time-limited, and how sharding across cores stays
statistically valid.

## Documentation map
- [HANDOFF.md](docs/HANDOFF.md) — **start here**: current state, what is in flight,
  what to pick up next
- [README.md](README.md) — this file: build, play, features, UCI
- [src/README.md](src/README.md) — architecture and where each concern lives
- [tests/README.md](tests/README.md) — the test suite and match methodology
- [lichess/README.md](lichess/README.md) — playing online, and why the config
  says what it says
- [BUGS.md](docs/BUGS.md) — known defects, ordered by what fixing them is worth, each
  with the game or line of code that demonstrates it
- [PLAN.md](docs/PLAN.md) — ordered plan of work, phase by phase
- [BACKLOG.md](docs/BACKLOG.md) — the 2026-08-10 profiling session, kept as a dated
  archive; its measured ceilings and baselines are still the reference, but read
  its header before trusting any of its status claims

The source carries the rest. Headers here explain *why* a thing is the way it
is, not just what it does — `search.hpp`, `bitboard.hpp`, `see.hpp` and
`transposition_table.hpp` in particular are worth reading before changing them.

## License
[LICENSE](LICENSE) is currently empty — no licence has been chosen yet, so
default copyright applies and no permissions are granted.