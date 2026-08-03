#pragma once
#include "board.hpp"
#include "move.hpp"
#include "piece.hpp"

// Generate pseudo-legal moves (may leave king in check)
MoveList generateMoves(const Board& board, PieceColor sideToMove, bool includeCastling);
MoveList generateMoves(const Board& board, PieceColor sideToMove);

// Generate only legal moves (filters out moves that leave king in check)
MoveList generateLegalMoves(const Board& board, PieceColor sideToMove);