#pragma once
#include "board.hpp"
#include "move.hpp"

// Returns the best move for the given board and side, searching to the given depth
Move findBestMove(const Board& board, int depth);

