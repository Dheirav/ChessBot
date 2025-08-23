
# ChessBot

## Overview
ChessBot is a C++ chess engine and GUI built with SFML. It features:
- Move generation for all pieces, including special rules for pawns, castling, and en passant
- Minimax search with alpha-beta pruning for engine move selection
- Evaluation function with material, mobility, king safety, center control, pawn structure, development, piece safety, and more
- Persistent move lookup tables for fast move generation
- SFML-based graphical interface with drag-and-drop piece movement
- User can choose to play as white or black; engine plays as the opponent
- Console output for evaluation after each move

## Installation
Install required packages (SFML and dependencies):

```bash
sudo apt update
xargs -a packages.txt sudo apt install -y
```

## Build
Open a terminal in your project directory and run:

```bash
make
```
This will compile the code and produce an executable named `chessbot`.

## Run

```bash
./chessbot
```
You will be prompted to choose your side (white or black). The GUI will open and you can play against the engine.

## Features
- Drag and drop pieces to make moves
- Undo/redo moves with Ctrl+Z / Ctrl+Y
- Engine plays automatically as the opponent
- Evaluation score printed after each move
- Move lookup tables are saved/loaded for fast startup

## Project Structure
- `src/engine/` - Chess engine logic (board, move generation, evaluation, search)
- `src/gui/` - SFML-based GUI (input, rendering, assets)
- `src/main.cpp` - Main entry point and game loop
- `src/engine/lookup_data/` - Persistent move lookup tables
- `src/gui/assets/` - Piece images and fonts
- `tests/` - Test code

## Development
To clean build files:
```bash
make clean
```
To rebuild:
```bash
make remake
```

## License
See LICENSE for details.