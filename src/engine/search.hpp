#pragma once
#include "board.hpp"
#include "move.hpp"
#include <atomic>

// Returns the best move for the given board and side, searching to the given depth
Move findBestMove(const Board& board, int depth);

// Thread-safe version that can be stopped
Move findBestMoveWithStop(const Board& board, int depth, const std::atomic<bool>& shouldStop);

