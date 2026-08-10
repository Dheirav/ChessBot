// Static Exchange Evaluation unit tests.
//
// SEE is self-contained: given a position and a capture it returns a number,
// with no search involved. That makes it one of the few strength-affecting
// pieces of an engine that can be verified outright rather than by playing
// games — so it is, before it is ever wired into the search.
//
// Every expected value below is hand-computed from the piece values in see.cpp
// (P=100 N=320 B=330 R=500 Q=900) and stated in the comment, so a failure says
// which specific reasoning the code disagrees with.

#include "engine/board.hpp"
#include "engine/move_lookup.hpp"
#include "engine/movegen.hpp"
#include "engine/see.hpp"

#include <cstdio>
#include <cstring>
#include <string>

static int failures = 0;

// Find the legal move matching a UCI-style "e2e4" (optionally with a promotion
// suffix), so tests name moves the way a human does.
static bool findMove(Board& board, const std::string& text, Move& out) {
    MoveList legal = generateLegalMoves(board, board.activeColor);
    for (const Move& m : legal) {
        std::string s;
        s += (char)('a' + (m.from % 8));
        s += (char)('8' - (m.from / 8));
        s += (char)('a' + (m.to % 8));
        s += (char)('8' - (m.to / 8));
        if (m.flag == PROMOTION) {
            switch (m.promotionPiece.type()) {
                case QUEEN:  s += 'q'; break;
                case ROOK:   s += 'r'; break;
                case BISHOP: s += 'b'; break;
                case KNIGHT: s += 'n'; break;
                default: break;
            }
        }
        if (s == text) { out = m; return true; }
    }
    return false;
}

static void expect(const char* fen, const char* moveText, int expected,
                   const char* why) {
    Board board;
    if (!board.setFromFEN(fen)) {
        std::printf("  %-8s FEN PARSE FAILED  %s\n", moveText, fen);
        ++failures;
        return;
    }
    Move m;
    if (!findMove(board, moveText, m)) {
        std::printf("  %-8s NOT A LEGAL MOVE in %s\n", moveText, fen);
        ++failures;
        return;
    }

    int got = see(board, m);
    bool ok = (got == expected);
    if (!ok) ++failures;
    std::printf("  %-8s expected %+6d  got %+6d  %s   %s\n",
                moveText, expected, got, ok ? "ok " : "FAIL", why);
}

int main() {
    initMoveLookupTables();
    std::printf("Static Exchange Evaluation\n");

    // --- Undefended targets: the capture simply wins the piece ---
    expect("4k3/8/8/3p4/8/8/8/3RK3 w - - 0 1", "d1d5", +100,
           "Rxd5, pawn undefended: +100");
    expect("4k3/8/8/3n4/8/8/8/3RK3 w - - 0 1", "d1d5", +320,
           "Rxd5, knight undefended: +320");

    // --- Defended targets: the recapture is what decides it ---
    expect("4k3/8/2p5/3p4/8/8/8/3RK3 w - - 0 1", "d1d5", -400,
           "Rxd5 cxd5: +100-500 = -400");
    expect("4k3/8/2p5/3p4/8/8/8/3QK3 w - - 0 1", "d1d5", -800,
           "Qxd5 cxd5: +100-900 = -800");
    expect("4k3/8/2p5/3p4/8/4N3/8/4K3 w - - 0 1", "e3d5", -220,
           "Nxd5 cxd5: +100-320 = -220");

    // --- Winning a defended piece anyway, when the victim is worth more ---
    expect("4k3/8/2p5/3q4/8/8/8/3RK3 w - - 0 1", "d1d5", +400,
           "Rxd5 cxd5: +900-500 = +400, still winning");

    // --- Equal trades resolve to zero ---
    expect("3qk3/8/8/8/8/8/8/3QK3 w - - 0 1", "d1d8", 0,
           "Qxd8+ Kxd8: +900-900 = 0");

    // --- Declining: the side to move stops when continuing would lose ---
    // A pawn defended by a pawn, attacked by a rook AND a pawn. The pawn
    // capture is the profitable one; the rook capture is not.
    expect("4k3/8/2p5/3p4/4P3/8/8/3RK3 w - - 0 1", "e4d5", +100,
           "exd5 cxd5: white can simply stop after winning the pawn: +100");

    // --- X-ray: a second attacker behind the first joins the exchange ---
    // White rooks stacked on d1/d2, black pawn d5 defended by a rook on d7.
    // Rxd5 rxd5 Rxd5 nets +100; black declining also leaves +100.
    expect("3rk3/8/8/3p4/8/8/3R4/3RK3 w - - 0 1", "d2d5", +100,
           "Rxd5 rxd5 Rxd5: battery outnumbers the defender: +100");

    // The same position without the second white rook loses the exchange.
    expect("3rk3/8/8/3p4/8/8/8/3RK3 w - - 0 1", "d1d5", -400,
           "Rxd5 rxd5, no support: +100-500 = -400");

    // --- Least-valuable-attacker ordering ---
    // d5 pawn is defended by a knight on f6 and a queen on d8. Black must
    // recapture with the knight, not the queen.
    expect("3qk3/8/5n2/3p4/8/8/8/3RK3 w - - 0 1", "d1d5", -400,
           "Rxd5 Nxd5 (not Qxd5): +100-500 = -400");

    // --- En passant: the captured pawn is not on the destination square ---
    expect("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1", "e5d6", +100,
           "exd6 e.p., pawn undefended: +100");

    // --- Promotion: the exchange continues against the promoted piece ---
    expect("4k3/P7/8/8/8/8/8/4K3 w - - 0 1", "a7a8q", +800,
           "a8=Q undefended: gains queen minus pawn = +800");

    if (failures) {
        std::printf("\nFAILED: %d case(s)\n", failures);
        return 1;
    }
    std::printf("\nPASSED: all SEE cases\n");
    return 0;
}
