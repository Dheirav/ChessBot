#pragma once
//
// Phase B6 of the bitboard replacement (docs/BITBOARD-REPLACEMENT.md).
//
// The search on a Position. Same algorithm, same toggles, same transposition
// table as search.cpp, because the acceptance test is that it visits exactly
// the same nodes: bench must read 463,295 at depth 6 and the same numbers at
// 7, 8 and 9, with the same principal variation.
//
// Not ported here: the threading, the time management, the aspiration loop and
// the UCI reporting. Those sit above the node function and are representation-
// independent, so they stay in search.cpp and get pointed at this core when the
// two are connected at B7. Porting them now would mean maintaining two copies
// of the Lazy SMP work for no verification benefit.
//
#include "bb_move_ordering.hpp"
#include "bb_position.hpp"
#include "search.hpp"
#include "transposition_table.hpp"
#include <atomic>
#include <vector>

struct BBSearchContext {
    BBMoveOrderer orderer;
    uint64_t nodes = 0;
    uint64_t nextTimeCheck = 0;
    int corrHist[2][16384] = {};
    static constexpr int EVAL_STACK_PLIES = SearchContext::EVAL_STACK_PLIES;
    static constexpr int NO_EVAL = SearchContext::NO_EVAL;
    int evalStack[EVAL_STACK_PLIES];
    const std::atomic<bool>* extraStop = nullptr;
};

// One iteration of iterative deepening from the root, single-threaded. Enough
// to compare trees against the mailbox search, which is what B6 is for.
struct BBSearchResult {
    BitboardMove best{};
    int score = 0;
    uint64_t nodes = 0;
};

BBSearchResult bbSearchRoot(Position& pos, int depth, TranspositionTable& tt,
                            const std::atomic<bool>& shouldStop,
                            std::vector<uint64_t>& pathHashes);
