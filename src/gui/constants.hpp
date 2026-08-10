#pragma once

constexpr int TILE_SIZE = 64; // Size of each tile in pixels
constexpr int BOARD_SIZE = 8;

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
