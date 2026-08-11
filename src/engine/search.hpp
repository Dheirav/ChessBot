#pragma once
#include "board.hpp"
#include "move.hpp"
#include "transposition_table.hpp"
#include <atomic>
#include <cstdint>
#include <string>

// Search heuristics. Unlike alpha-beta these are not exact: they trade a small
// risk of missing a line for a much smaller tree, so they are toggleable both
// for A/B match testing and so they can be disabled if they ever misbehave.
struct SearchOptions {
    bool nullMove = true;    // null-move pruning: skip a turn, prune if still failing high
    bool lmr = true;         // late move reductions: search unpromising moves shallower
    bool aspiration = true;  // aspiration windows: narrow root window around the last score
    bool quiet = false;      // suppress the per-depth progress output

    // Static exchange evaluation, as two independently gated uses (PLAN.md 3.2).
    // Both default OFF: each is accepted by its own A/B match, and until then
    // the shipped engine and the bench signature must stay exactly as they were.
    bool seeOrdering = false;  // sort captures by the exchange result, not the victim
    bool seePruning = false;   // quiescence skips captures that lose material
};
extern SearchOptions g_searchOptions;

// Set one option by name: "nullmove", "lmr", "aspiration". Returns false if the
// name is unknown, so a caller can report a typo rather than silently ignore it.
//
// Named lookup exists so a single heuristic can be toggled on its own. Every
// Phase 3 search feature is accepted or rejected by an A/B match, and that
// needs the control arm to differ in exactly one thing — which a single "all
// heuristics on/off" switch cannot express. Each new feature adds one line
// here and becomes testable from the match harness and over UCI at once.
bool setSearchOption(SearchOptions& opts, const std::string& name, bool value);

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

// Checkmate score, from the perspective of the side to move: being mated is
// -(SEARCH_MATE_SCORE - ply). Exposed so callers can recognise a mate score and
// convert it to a distance (UCI reports "mate N" rather than a centipawn value).
constexpr int SEARCH_MATE_SCORE = 30000;

// Called once per completed iteration, if set. `score` is centipawns from the
// side to move's point of view, or a mate score as above. Used by the UCI layer
// to emit "info depth ... score ... nodes ... pv ...". Null by default, so the
// search prints nothing extra unless someone asks.
using SearchInfoFn = void (*)(int depth, int score, uint64_t nodes,
                              long elapsedMs, const Move& best);
extern SearchInfoFn g_searchInfo;

// What the search is allowed to spend.
//
// A depth limit alone cannot play a real game: the same depth costs
// milliseconds in an endgame and seconds in a dense middlegame. A time limit
// alone cannot be benchmarked: node counts stop being reproducible. Both exist,
// and whichever binds first ends the search.
struct SearchLimits {
    int maxDepth = 64;      // hard ceiling on iterations
    long moveTimeMs = 0;    // wall-clock budget for this move; 0 = no budget

    SearchLimits() = default;
    explicit SearchLimits(int depth) : maxDepth(depth) {}
    SearchLimits(int depth, long ms) : maxDepth(depth), moveTimeMs(ms) {}
};

// The engine's only search entry point. Iterative deepening over a
// transposition table, with the heuristics in SearchOptions above.
//
// There is deliberately no non-TT variant: a second copy of the search logic
// drifts from this one, and benchmarking it produces numbers that describe a
// search the application never runs.
//
// With a time budget the search returns the best move from the last *completed*
// iteration. A partial iteration is never used: its move list is only partly
// searched, so its "best" move is just the best of an arbitrary prefix.
Move findBestMoveIterativeDeepening(Board& board, const SearchLimits& limits,
                                   const std::atomic<bool>& shouldStop,
                                   TranspositionTable& tt);

// Depth-only convenience overload, for tests and for callers with no clock.
Move findBestMoveIterativeDeepening(Board& board, int maxDepth,
                                   const std::atomic<bool>& shouldStop,
                                   TranspositionTable& tt);

