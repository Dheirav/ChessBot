#pragma once

constexpr int TILE_SIZE = 64; // Size of each tile in pixels
constexpr int BOARD_SIZE = 8;

// The window is the board plus a panel to its right. The board used to be the
// whole window, which left the engine with nowhere to report anything: no
// clock, no depth or score, no move list, and no on-screen indication that a
// game had ended.
constexpr int BOARD_PIXELS = TILE_SIZE * BOARD_SIZE;   // 512
constexpr int PANEL_WIDTH  = 280;
constexpr int WINDOW_WIDTH  = BOARD_PIXELS + PANEL_WIDTH;
constexpr int WINDOW_HEIGHT = BOARD_PIXELS;

// Board orientation.
//
// Board square indices run 0 = a8 .. 63 = h1, so the unflipped view maps
// screen cell (x, y) straight to y * 8 + x: white sits at the bottom.
// Flipping is a 180-degree rotation of that layout, which is exactly
// 63 - index, putting black at the bottom.
//
// Every screen<->square conversion in the GUI must go through these two
// helpers, or rendering and input will disagree about what a click means.
inline int screenToSquare(int x, int y, bool flipped) {
    int index = y * BOARD_SIZE + x;
    return flipped ? 63 - index : index;
}

inline void squareToScreen(int square, bool flipped, int& x, int& y) {
    int index = flipped ? 63 - square : square;
    x = index % BOARD_SIZE;
    y = index / BOARD_SIZE;
}
