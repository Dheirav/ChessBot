#pragma once
#include "board.hpp"
#include "move.hpp"
#include "transposition_table.hpp"
#include <atomic>
#include <cstdint>

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

// Nodes visited by the last search: every entry to the main search or to
// quiescence. Reset at the start of findBestMoveIterativeDeepening.
//
// Deliberately not atomic. Only one search runs at a time, and the value is
// read after that search returns, so the increment stays a single add in the
// hottest loop in the engine. If the search is ever made concurrent (Lazy SMP),
// this becomes per-thread state, not an atomic.
//
// It exists for two reasons: UCI reports nodes and nps, and tests/bench.cpp
// uses the total as a signature. Any change that claims to preserve search
// behaviour must reproduce the signature exactly.
extern uint64_t g_searchNodes;

// The engine's only search entry point. Iterative deepening over a
// transposition table, with the heuristics in SearchOptions above.
//
// There is deliberately no non-TT variant: a second copy of the search logic
// drifts from this one, and benchmarking it produces numbers that describe a
// search the application never runs.
Move findBestMoveIterativeDeepening(Board& board, int maxDepth,
                                   const std::atomic<bool>& shouldStop,
                                   TranspositionTable& tt);

