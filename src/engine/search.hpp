#pragma once
#include "board.hpp"
#include "move.hpp"
#include "transposition_table.hpp"
#include <atomic>

// Returns the best move for the given board and side, searching to the given depth
Move findBestMove(const Board& board, int depth);

// Thread-safe version that can be stopped
Move findBestMoveWithStop(Board& board, int depth, const std::atomic<bool>& shouldStop);

// Version with transposition table
Move findBestMoveWithTT(Board& board, int depth, const std::atomic<bool>& shouldStop, 
                       TranspositionTable& tt);

// Iterative deepening version with time management
Move findBestMoveIterativeDeepening(Board& board, int maxDepth, 
                                   const std::atomic<bool>& shouldStop, 
                                   TranspositionTable& tt);

