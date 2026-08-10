#pragma once
#include "board.hpp"
#include "move.hpp"
#include "transposition_table.hpp"
#include <atomic>

// Search heuristics. Unlike alpha-beta these are not exact: they trade a small
// risk of missing a line for a much smaller tree, so they are toggleable both
// for A/B match testing and so they can be disabled if they ever misbehave.
struct SearchOptions {
    bool nullMove = true;    // null-move pruning: skip a turn, prune if still failing high
    bool lmr = true;         // late move reductions: search unpromising moves shallower
    bool aspiration = true;  // aspiration windows: narrow root window around the last score
    bool quiet = false;      // suppress the per-depth progress output
};
extern SearchOptions g_searchOptions;

// The engine's only search entry point. Iterative deepening over a
// transposition table, with the heuristics in SearchOptions above.
//
// There is deliberately no non-TT variant: a second copy of the search logic
// drifts from this one, and benchmarking it produces numbers that describe a
// search the application never runs.
Move findBestMoveIterativeDeepening(Board& board, int maxDepth,
                                   const std::atomic<bool>& shouldStop,
                                   TranspositionTable& tt);

