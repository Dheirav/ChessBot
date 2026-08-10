#pragma once
#include "board.hpp"
#include "move.hpp"
#include "piece.hpp"

// Generate pseudo-legal moves into 'out', which is cleared first. These may
// leave the king in check. Takes an output parameter so callers that run this
// in a hot loop can reuse one buffer instead of allocating a MoveList per call.
void generatePseudoLegalMoves(const Board& board, PieceColor sideToMove,
                              bool includeCastling, MoveList& out);

// Generate fully legal moves: the pseudo-legal set above, filtered by making
// each move and testing whether the side's own king is left attacked.
//
// This is the only legal generator. It used to have a second name,
// generateMoves(), which was documented as "pseudo-legal" and was in fact the
// identical function — callers reasonably believed they were skipping the
// legality filter when they were not.
MoveList generateLegalMoves(const Board& board, PieceColor sideToMove,
                            bool includeCastling = true);
