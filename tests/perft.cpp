// Perft: counts the leaf nodes of the legal-move tree to a given depth and
// compares against published reference values.
//
// This is the standard regression guard for move generation. Any change to
// generateLegalMoves(), makeMove()/unmakeMove(), castling, en passant or promotion
// handling should be checked against it: a single wrong or missing move shows
// up as a mismatched count.
//
// Build and run:  make test-perft
//
// Reference counts are from the Chess Programming Wiki's perft results page.
#include "engine/board.hpp"
#include "engine/movegen.hpp"
#include "engine/move_lookup.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>

static uint64_t perft(const Board& board, int depth) {
    MoveList moves = generateLegalMoves(board, board.activeColor);
    if (depth <= 1) return moves.size();
    uint64_t nodes = 0;
    for (const Move& m : moves) {
        Board next = board.copyForSearch();
        next.makeMove(m);
        nodes += perft(next, depth - 1);
    }
    return nodes;
}

struct Case {
    const char* name;
    const char* fen;
    int maxDepth;
    uint64_t expected[5]; // depth 1..maxDepth
};

static const Case CASES[] = {
    // Initial position.
    {"startpos", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 4,
     {20, 400, 8902, 197281, 0}},
    // "Kiwipete": dense middlegame exercising castling, pins and promotions.
    {"kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 3,
     {48, 2039, 97862, 0, 0}},
    // Endgame with en passant and promotion races.
    {"position3", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 4,
     {14, 191, 2812, 43238, 0}},
    // Position 4: heavy promotion and check interaction.
    {"position4", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 3,
     {6, 264, 9467, 0, 0}},
};

int main() {
    initMoveLookupTables();

    int failures = 0;
    for (const Case& c : CASES) {
        Board b;
        if (!b.setFromFEN(c.fen)) {
            std::printf("%-10s FEN PARSE FAILED\n", c.name);
            ++failures;
            continue;
        }
        std::printf("%s\n", c.name);
        for (int d = 1; d <= c.maxDepth; ++d) {
            uint64_t got = perft(b, d);
            uint64_t exp = c.expected[d - 1];
            bool ok = (got == exp);
            if (!ok) ++failures;
            std::printf("  depth %d: %10llu  expected %10llu  %s\n",
                        d, (unsigned long long)got, (unsigned long long)exp,
                        ok ? "ok" : "MISMATCH");
        }
    }

    if (failures) {
        std::printf("\nFAILED: %d mismatch(es)\n", failures);
        return 1;
    }
    std::printf("\nPASSED: all perft counts match\n");
    return 0;
}
