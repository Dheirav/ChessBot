#pragma once
#include "board.hpp"
#include "move.hpp"

/**
 * Legal move validation utilities
 * Ensures generated moves don't leave the king in check
 */
class LegalMoveValidator {
public:
    // Check if a move is legal (doesn't leave own king in check)
    static bool isMoveLegal(const Board& board, const Move& move);
    
    // Filter a move list to only include legal moves
    static MoveList filterLegalMoves(const Board& board, const MoveList& pseudoLegalMoves);
    
    // Check if the current side is in check
    static bool isInCheck(const Board& board, PieceColor side);
    
    // Check if the current side is in checkmate
    static bool isCheckmate(const Board& board, PieceColor side);
    
    // Check if the current side is in stalemate
    static bool isStalemate(const Board& board, PieceColor side);
    
private:
    // Find the king of the given color
    static int findKing(const Board& board, PieceColor color);
};
