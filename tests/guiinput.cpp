// Headless check of the GUI input state machine: click-to-move, drag-and-drop
// and the promotion dialog hitboxes. No window is created; sf::Event is a plain
// struct, and Input::handleEvent only touches the board and the move generator.
#include "gui/input.hpp"
#include "gui/constants.hpp"
#include "engine/fen.hpp"
#include "engine/move_lookup.hpp"
#include <iostream>
#include <string>

static int failures = 0;
static void check(bool cond, const std::string& what) {
    std::cout << (cond ? "  ok   " : "  FAIL ") << what << std::endl;
    if (!cond) failures++;
}

static sf::Event press(int px, int py) {
    sf::Event e{};
    e.type = sf::Event::MouseButtonPressed;
    e.mouseButton.button = sf::Mouse::Left;
    e.mouseButton.x = px;
    e.mouseButton.y = py;
    return e;
}
static sf::Event release(int px, int py) {
    sf::Event e = press(px, py);
    e.type = sf::Event::MouseButtonReleased;
    return e;
}
static sf::Event moveTo(int px, int py) {
    sf::Event e{};
    e.type = sf::Event::MouseMoved;
    e.mouseMove.x = px;
    e.mouseMove.y = py;
    return e;
}
static sf::Event keyPress(sf::Keyboard::Key key) {
    sf::Event e{};
    e.type = sf::Event::KeyPressed;
    e.key.code = key;
    return e;
}

// Centre pixel of a board square (unflipped view).
static void centreOf(const std::string& alg, int& px, int& py) {
    int file = alg[0] - 'a';
    int rank = alg[1] - '1';
    int square = (7 - rank) * 8 + file;
    int x, y;
    squareToScreen(square, false, x, y);
    px = x * TILE_SIZE + TILE_SIZE / 2;
    py = y * TILE_SIZE + TILE_SIZE / 2;
}

static void clickSquare(Input& in, Board& b, const std::string& alg) {
    int px, py;
    centreOf(alg, px, py);
    in.handleEvent(press(px, py), b);
    in.handleEvent(release(px, py), b);
}

static Board fromFEN(const std::string& fen) {
    Board b;
    FENInfo info;
    if (!parseFEN(fen, b, info)) {
        std::cerr << "bad FEN: " << fen << std::endl;
        std::exit(1);
    }
    return b;
}

int main() {
    initMoveLookupTables();
    // --- click-to-move ------------------------------------------------------
    {
        Board b = fromFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        Input in;
        clickSquare(in, b, "e2");
        check(in.getSelectedSquare() != -1, "click on own piece selects it");
        check(!in.hasCompletedMove(), "selecting does not complete a move");
        clickSquare(in, b, "e4");
        check(in.hasCompletedMove(), "click on a destination plays the move");
        check(in.getCompletedMove().toString().find("e2e4") != std::string::npos,
              "the move played is e2e4 (got " + in.getCompletedMove().toString() + ")");
        check(in.getSelectedSquare() == -1, "selection cleared after the move");
    }

    // --- click-to-capture ---------------------------------------------------
    {
        Board b = fromFEN("rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2");
        Input in;
        clickSquare(in, b, "e4");
        clickSquare(in, b, "d5");
        check(in.hasCompletedMove(), "click on an enemy piece captures it");
        check(in.getCompletedMove().toString().find("e4d5") != std::string::npos,
              "the capture played is e4d5 (got " + in.getCompletedMove().toString() + ")");
    }

    // --- clicking an illegal square deselects -------------------------------
    {
        Board b = fromFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        Input in;
        clickSquare(in, b, "e2");
        clickSquare(in, b, "e5");
        check(!in.hasCompletedMove(), "illegal destination plays nothing");
        check(in.getSelectedSquare() == -1, "illegal destination deselects");
    }

    // --- reselecting another own piece --------------------------------------
    {
        Board b = fromFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        Input in;
        clickSquare(in, b, "e2");
        clickSquare(in, b, "d2");
        check(!in.hasCompletedMove(), "clicking another own piece plays nothing");
        check(in.getSelectedSquare() == 51, "clicking another own piece reselects (d2)");
    }

    // --- drag-and-drop still works ------------------------------------------
    {
        Board b = fromFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        Input in;
        int fx, fy, tx, ty;
        centreOf("g1", fx, fy);
        centreOf("f3", tx, ty);
        in.handleEvent(press(fx, fy), b);
        check(in.isDragging(), "press on a piece starts a drag");
        // The renderer skips this square, so the piece is not drawn twice.
        check(in.getDragSquare() == 62, "the drag origin is reported while dragging (g1)");
        in.handleEvent(release(tx, ty), b);
        check(in.getDragSquare() == -1, "no drag origin once the piece is dropped");
        check(in.hasCompletedMove(), "dropping on a legal square plays the move");
        check(in.getCompletedMove().toString().find("g1f3") != std::string::npos,
              "the dragged move is g1f3 (got " + in.getCompletedMove().toString() + ")");
    }

    // --- promotion: dialog hitboxes -----------------------------------------
    {
        const char* names[4] = {"queen", "rook", "bishop", "knight"};
        const PieceType want[4] = {QUEEN, ROOK, BISHOP, KNIGHT};
        for (int i = 0; i < 4; ++i) {
            Board b = fromFEN("6r1/4P3/8/8/8/8/8/K6k w - - 0 1");
            Input in;
            clickSquare(in, b, "e7");
            clickSquare(in, b, "e8");
            check(in.isPromotionActive(), std::string("promoting opens the dialog (") + names[i] + ")");
            check(!in.hasCompletedMove(), "the move is held until a piece is chosen");

            int ox, oy;
            promotionOptionPos(i, ox, oy);
            const int cx = ox + PROMO_PIECE_SIZE / 2;
            const int cy = oy + PROMO_PIECE_SIZE / 2;
            check(cx < BOARD_PIXELS && cy < BOARD_PIXELS,
                  std::string("option ") + names[i] + " sits on the board, not the panel");
            in.handleEvent(press(cx, cy), b);
            check(in.hasCompletedMove(), std::string("clicking the ") + names[i] + " option plays a move");
            check(in.getCompletedMove().promotionPiece.type() == want[i],
                  std::string("the piece chosen is the ") + names[i]);
            check(!in.isPromotionActive(), "the dialog closes after choosing");
        }
    }

    // --- promotion: clicking outside cancels ---------------------------------
    {
        Board b = fromFEN("6r1/4P3/8/8/8/8/8/K6k w - - 0 1");
        Input in;
        clickSquare(in, b, "e7");
        clickSquare(in, b, "e8");
        in.handleEvent(press(2, 2), b);
        check(!in.isPromotionActive(), "clicking outside the dialog cancels promotion");
        check(!in.hasCompletedMove(), "cancelling plays nothing");
    }

    // --- promotion: the keyboard ---------------------------------------------
    {
        const sf::Keyboard::Key keys[4] = {sf::Keyboard::Q, sf::Keyboard::R,
                                           sf::Keyboard::B, sf::Keyboard::N};
        const PieceType want[4] = {QUEEN, ROOK, BISHOP, KNIGHT};
        const char* names[4] = {"Q", "R", "B", "N"};
        for (int i = 0; i < 4; ++i) {
            Board b = fromFEN("6r1/4P3/8/8/8/8/8/K6k w - - 0 1");
            Input in;
            clickSquare(in, b, "e7");
            clickSquare(in, b, "e8");
            in.handleEvent(keyPress(keys[i]), b);
            check(in.hasCompletedMove(), std::string("pressing ") + names[i] + " promotes");
            check(in.getCompletedMove().promotionPiece.type() == want[i],
                  std::string("pressing ") + names[i] + " picks the right piece");
            check(!in.isPromotionActive(), "the dialog closes after a key");
        }

        Board b = fromFEN("6r1/4P3/8/8/8/8/8/K6k w - - 0 1");
        Input in;
        clickSquare(in, b, "e7");
        clickSquare(in, b, "e8");
        in.handleEvent(keyPress(sf::Keyboard::Escape), b);
        check(!in.isPromotionActive(), "Escape cancels the dialog");
        check(!in.hasCompletedMove(), "Escape plays nothing");
    }

    // --- promotion: board clicks are ignored while the dialog is open ---------
    {
        Board b = fromFEN("6r1/4P3/8/8/8/8/8/K6k w - - 0 1");
        Input in;
        clickSquare(in, b, "e7");
        clickSquare(in, b, "e8");
        // a1 holds the white king, and is far outside the dialog
        in.handleEvent(moveTo(8, BOARD_PIXELS - 8), b);
        check(in.isPromotionActive(), "moving the mouse does not close the dialog");
        check(in.getSelectedSquare() == 12, "the promoting pawn stays selected (e7)");
    }

    std::cout << (failures ? "\nFAILURES: " : "\nall checks passed (")
              << failures << (failures ? "\n" : " failures)\n");
    return failures ? 1 : 0;
}
