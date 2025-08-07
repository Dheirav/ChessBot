#pragma once
#include "board.hpp"

// Returns a score from the perspective of the side to move (positive = good for white, negative = good for black)
int evaluate(const Board& board);
