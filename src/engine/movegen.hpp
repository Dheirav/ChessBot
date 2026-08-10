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
// The search uses these two: `board` is mutated and restored in place, so no
// board copy is paid per node. On return the position is exactly as it came in.
void generateLegalMoves(Board& board, PieceColor sideToMove, bool includeCastling,
                        MoveList& out);
MoveList generateLegalMoves(Board& board, PieceColor sideToMove,
                            bool includeCastling = true);

// For callers holding only a const board (the GUI, the evaluation's in-check
// mobility path). Copies the board once, then defers to the above.
MoveList generateLegalMoves(const Board& board, PieceColor sideToMove,
                            bool includeCastling = true);
