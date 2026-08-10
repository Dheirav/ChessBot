// Search bench: a reproducible signature for the search tree.
//
// Searches a fixed set of positions to a fixed depth with a fixed transposition
// table and prints the total node count and the best move per position. That
// total is a signature. Any change that is supposed to leave search behaviour
// alone — a refactor, a data structure swap, a rename — must reproduce it
// exactly. Any change that is supposed to alter behaviour must change it in a
// way you can explain.
//
// This is the search-side counterpart to tests/evalref.cpp, and it is what
// makes an otherwise unverifiable refactor (the negamax conversion, PLAN.md
// 0.9) checkable by identity instead of by playing games.
//
//   make bench              default depth
//   ./tests/bench <depth>   any other depth
//
// Determinism: one thread, no clock-dependent decisions in the search, a fresh
// table per position and move-ordering state cleared by the search itself. If
// this ever stops being reproducible run to run, that is itself a bug — most
// likely a time-based cutoff that has crept into the search path.

#include "engine/board.hpp"
#include "engine/move_lookup.hpp"
#include "engine/search.hpp"
#include "engine/transposition_table.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

// Twelve positions spanning the phases the search behaves differently in:
// quiet openings, tactical middlegames, and endgames where null-move pruning
// and LMR are least safe.
static const struct { const char* name; const char* fen; } POSITIONS[] = {
    {"startpos",   "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"},
    {"open-ital",  "r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 0 1"},
    {"open-sicil", "rnbqkbnr/pp1ppppp/8/2p5/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 2"},
    {"kiwipete",   "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"},
    {"midgame-1",  "r1bq1rk1/pp2bppp/2n1pn2/2pp4/3P1B2/2PBPN2/PP1N1PPP/R2Q1RK1 w - - 0 1"},
    {"midgame-2",  "2rq1rk1/pb1nbppp/1p2pn2/3p4/3P4/1BN1PN2/PP2QPPP/2RR2K1 w - - 0 1"},
    {"tactical",   "r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1"},
    {"promo-race", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"},
    {"rook-endg",  "8/8/8/4k3/8/8/4K3/R7 w - - 0 1"},
    {"pawn-endg",  "8/1p3pp1/7p/5P1P/2k3P1/8/2K2P2/8 w - - 0 1"},
    {"queen-endg", "6k1/5ppp/8/8/8/8/5PPP/3Q2K1 w - - 0 1"},
    {"zugzwang",   "8/8/p1p5/1p5p/1P5p/8/PPP2K1p/4R1rk w - - 0 1"},
};
static const int NUM_POSITIONS = (int)(sizeof(POSITIONS) / sizeof(POSITIONS[0]));

static const int DEFAULT_DEPTH = 6;
static const size_t TT_SIZE_MB = 64;

int main(int argc, char** argv) {
    initMoveLookupTables();

    int depth = (argc > 1) ? std::atoi(argv[1]) : DEFAULT_DEPTH;
    if (depth < 1) {
        std::printf("usage: %s [depth]\n", argv[0]);
        return 1;
    }

    // The search prints per-depth progress by default; that is noise here.
    g_searchOptions.quiet = true;

    std::atomic<bool> stop{false};
    uint64_t totalNodes = 0;
    int failures = 0;

    std::printf("bench depth %d, %d MB table\n\n", depth, (int)TT_SIZE_MB);
    std::printf("%-11s %12s  %s\n", "position", "nodes", "best");

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < NUM_POSITIONS; ++i) {
        Board board;
        if (!board.setFromFEN(POSITIONS[i].fen)) {
            std::printf("%-11s FEN PARSE FAILED\n", POSITIONS[i].name);
            ++failures;
            continue;
        }

        // A fresh table per position: carrying one over would make each
        // position's node count depend on the ones before it, so inserting or
        // reordering a position would change every later number.
        TranspositionTable tt(TT_SIZE_MB);

        Move best = findBestMoveIterativeDeepening(board, depth, stop, tt);
        totalNodes += g_searchNodes;

        std::printf("%-11s %12llu  %s\n", POSITIONS[i].name,
                    (unsigned long long)g_searchNodes, best.toString().c_str());
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    std::printf("\n%-11s %12llu\n", "total", (unsigned long long)totalNodes);
    std::printf("%-11s %12lld ms\n", "time", (long long)elapsed);
    if (elapsed > 0) {
        std::printf("%-11s %12llu\n", "nps",
                    (unsigned long long)(totalNodes * 1000ULL / (uint64_t)elapsed));
    }

    if (failures) {
        std::printf("\nFAILED: %d position(s) did not parse\n", failures);
        return 1;
    }
    return 0;
}
