#pragma once
#include "board.hpp"
#include "move.hpp"
#include "piece.hpp"

// Generate pseudo-legal moves into 'out', which is cleared first. These may
// leave the king in check. Takes an output parameter so callers that run this
// in a hot loop can reuse one buffer instead of allocating a MoveList per call.
void generatePseudoLegalMoves(const Board& board, PieceColor sideToMove,
                              bool includeCastling, MoveList& out);

// Generate pseudo-legal moves (may leave king in check)
MoveList generateMoves(const Board& board, PieceColor sideToMove, bool includeCastling);
MoveList generateMoves(const Board& board, PieceColor sideToMove);

// Generate only legal moves (filters out moves that leave king in check)
MoveList generateLegalMoves(const Board& board, PieceColor sideToMove);