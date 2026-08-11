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

// Promotion dialog geometry.
//
// Drawing and hit-testing each derived this layout on their own, and the two
// disagreed the moment the side panel widened the window: the dialog was drawn
// centred on the whole window but clicked as if it were still centred on the
// board, so every option sat PANEL_WIDTH / 2 to the right of its hitbox. Both
// sides now read the same constants, which are anchored to the board.
constexpr int PROMO_PIECE_SIZE = 80;
constexpr int PROMO_SPACING    = 10;  // gap between options
constexpr int PROMO_PADDING    = 10;  // dialog edge to first option
constexpr int PROMO_TITLE_H    = 30;  // title strip above the options
constexpr int PROMO_DIALOG_W = 4 * PROMO_PIECE_SIZE + 3 * PROMO_SPACING + 2 * PROMO_PADDING;
constexpr int PROMO_DIALOG_H = PROMO_PIECE_SIZE + PROMO_TITLE_H + PROMO_PADDING;
constexpr int PROMO_DIALOG_X = (BOARD_PIXELS - PROMO_DIALOG_W) / 2;
constexpr int PROMO_DIALOG_Y = (BOARD_PIXELS - PROMO_DIALOG_H) / 2;

// Top-left corner of promotion option i (0 = queen .. 3 = knight).
inline void promotionOptionPos(int i, int& x, int& y) {
    x = PROMO_DIALOG_X + PROMO_PADDING + (PROMO_PIECE_SIZE + PROMO_SPACING) * i;
    y = PROMO_DIALOG_Y + PROMO_TITLE_H;
}

// Which promotion option a pixel falls in, or -1 for none.
inline int promotionOptionAt(int px, int py) {
    for (int i = 0; i < 4; ++i) {
        int ox, oy;
        promotionOptionPos(i, ox, oy);
        if (px >= ox && px < ox + PROMO_PIECE_SIZE &&
            py >= oy && py < oy + PROMO_PIECE_SIZE) {
            return i;
        }
    }
    return -1;
}
