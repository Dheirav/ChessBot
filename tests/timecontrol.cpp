// Time control regression test.
//
// Guards the one property that matters: a search given a budget returns inside
// it. A search that ignores its deadline does not degrade gracefully — it hangs
// the GUI and forfeits on the clock.
//
// Deliberately asserts only an upper bound. Returning early is correct and
// expected: the search refuses to start an iteration it predicts cannot finish,
// because a partial iteration's result is discarded and the time buys nothing.
// How much it undershoots is a tuning question, not a correctness one.

#include "engine/board.hpp"
#include "engine/move_lookup.hpp"
#include "engine/search.hpp"
#include "engine/transposition_table.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>

// The search checks the clock every 2048 nodes, so it can overshoot by however
// long 2048 nodes take. At the measured ~165k nodes/s that is ~12ms; this
// leaves an order of magnitude of headroom for a slow or loaded machine.
static const long TOLERANCE_MS = 250;

static const struct { const char* name; const char* fen; } POSITIONS[] = {
    {"midgame", "r1bq1rk1/pp2bppp/2n1pn2/2pp4/3P1B2/2PBPN2/PP1N1PPP/R2Q1RK1 w - - 0 1"},
    {"kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"},
    {"pawn-endg", "8/1p3pp1/7p/5P1P/2k3P1/8/2K2P2/8 w - - 0 1"},
};
static const int NUM_POSITIONS = (int)(sizeof(POSITIONS) / sizeof(POSITIONS[0]));

static const long BUDGETS_MS[] = {100, 300, 1000};
static const int NUM_BUDGETS = (int)(sizeof(BUDGETS_MS) / sizeof(BUDGETS_MS[0]));

int main() {
    initMoveLookupTables();
    g_searchOptions.quiet = true;

    int failures = 0;
    std::printf("%-11s %8s %8s  %s\n", "position", "budget", "actual", "result");

    for (int b = 0; b < NUM_BUDGETS; ++b) {
        for (int i = 0; i < NUM_POSITIONS; ++i) {
            Board board;
            if (!board.setFromFEN(POSITIONS[i].fen)) {
                std::printf("%-11s FEN PARSE FAILED\n", POSITIONS[i].name);
                ++failures;
                continue;
            }

            TranspositionTable tt(16);
            std::atomic<bool> stop{false};

            auto start = std::chrono::steady_clock::now();
            // maxDepth is deliberately far higher than reachable, so the clock
            // is what ends the search rather than the depth limit.
            Move best = findBestMoveIterativeDeepening(
                board, SearchLimits(64, BUDGETS_MS[b]), stop, tt);
            long elapsed = (long)std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();

            bool inBudget = elapsed <= BUDGETS_MS[b] + TOLERANCE_MS;
            // A search that returns no move has failed regardless of timing:
            // every one of these positions has legal moves.
            bool haveMove = (best.from != -1);
            if (!inBudget || !haveMove) ++failures;

            std::printf("%-11s %6ldms %6ldms  %s\n", POSITIONS[i].name,
                        BUDGETS_MS[b], elapsed,
                        !haveMove ? "NO MOVE RETURNED"
                                  : (inBudget ? "ok" : "OVER BUDGET"));
        }
    }

    if (failures) {
        std::printf("\nFAILED: %d case(s)\n", failures);
        return 1;
    }
    std::printf("\nPASSED: every search returned within its budget\n");
    return 0;
}
