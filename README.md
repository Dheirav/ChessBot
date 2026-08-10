
# ChessBot

## Overview
ChessBot is a local C++ chess application with an SFML-based graphical interface and a built-in chess engine. The current codebase focuses on a playable desktop experience rather than a standalone engine protocol such as UCI.

It includes:
- A full chess board with drag-and-drop move input
- A search-based engine using alpha-beta pruning and a transposition table
- Move generation for normal moves, castling, en passant, and promotion
- Undo/redo support, resignation, and engine-interrupt controls
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

When the program starts, it asks which side you want to play. The GUI opens and you can make moves against the engine.

## Controls
- Drag and drop pieces to make moves
- Ctrl+Z: undo the last move
- Ctrl+Y: redo a move
- R: resign the current game
- ESC: interrupt the engine while it is thinking

## Features and design
ChessBot is structured around a clean separation between the game layer, the engine, and the UI:

- Game flow and turn management are handled by the game manager, which coordinates the human player, the engine, and the board state.
- The engine uses a board representation with legal move generation and move application logic, so it can evaluate positions and search for strong replies.
- The search system uses alpha-beta pruning to reduce the number of positions examined, which makes deeper searches practical without exploding the branching factor.
- A transposition table caches previously evaluated positions so repeated states can be evaluated faster during search.
- Move ordering improves search efficiency by trying stronger moves earlier, which increases the effectiveness of pruning.
- Evaluation is based on a handcrafted positional assessment that considers material, mobility, king safety, piece activity, center control, and structure.
- The UI layer is intentionally lightweight and event-driven, allowing the user to interact with the board without needing to understand the engine internals.

## How it works
The application runs in three layers:

1. The GUI layer handles mouse input, rendering, and user interaction.
2. The game manager validates moves, updates game state, and decides when the engine should think.
3. The engine layer generates legal moves, searches the tree of possible continuations, and selects the strongest candidate move.

This structure allows the program to feel responsive while still giving the engine enough computation to play meaningful moves.

## Optimizations and performance notes
Several implementation details are aimed at making the engine faster and more practical:

- Bitboard-based move generation and lookup structures reduce overhead in move generation and make the engine more efficient than naive per-piece scanning.
- The transposition table avoids recomputing the same positions repeatedly during search.
- Move ordering improves pruning efficiency and helps the engine cut off branches sooner.
- Search depth is configurable through the settings file, allowing a balance between strength and responsiveness.
- The engine can be interrupted mid-search, which keeps the interface responsive even when the search is still running.

## Configuration
The project reads an optional config file named `chessbot.conf` from the repository root. It can override a few engine settings, for example:

```ini
searchDepth = 5
transpositionTableSizeMB = 256
```

Supported keys are `searchDepth` and `transpositionTableSizeMB`.

## Project layout
- [src/main.cpp](src/main.cpp) - application entry point and startup flow
- [src/game_manager.hpp](src/game_manager.hpp) - game-state coordination between GUI and engine
- [src/engine](src/engine) - board logic, move generation, evaluation, search, and hashing
- [src/gui](src/gui) - SFML UI, input handling, rendering, and assets
- [tests/legacy](tests/legacy) - older standalone test sources

## Development
Useful maintenance commands:

```bash
make clean
make remake
```

## License
See [LICENSE](LICENSE) for details.