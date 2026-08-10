#pragma once
#include "board.hpp"
#include "move.hpp"

/**
 * Check detection.
 *
 * This class used to also carry isMoveLegal(), filterLegalMoves(),
 * isCheckmate() and isStalemate(). All four were dead: generateLegalMoves()
 * already filters for legality, so filterLegalMoves() was a redundant second
 * pass over an already-legal list, and nothing outside this file called any of
 * them. Terminal-state detection lives in GameManager, which asks
 * generateLegalMoves() for an empty list and isInCheck() for which kind of
 * ending it is.
 */
class LegalMoveValidator {
public:
    // Is 'side' king currently attacked?
    static bool isInCheck(const Board& board, PieceColor side);

private:
    // Find the king of the given color
    static int findKing(const Board& board, PieceColor color);
};
