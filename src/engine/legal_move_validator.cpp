#include "legal_move_validator.hpp"
#include "movegen.hpp"
#include <iostream>

bool LegalMoveValidator::isMoveLegal(const Board& board, const Move& move) {
    // Make a search copy of the board and try the move (skips the undo/redo stacks)
    Board testBoard = board.copyForSearch();
    testBoard.makeMove(move);
    
    // Find our king on the test board
    PieceColor ourColor = move.movedPiece.color();
    int kingSquare = findKing(testBoard, ourColor);
    
    if (kingSquare == -1) {
        // King not found - this should never happen
        std::cerr << "Error: King not found after move!" << std::endl;
        return false;
    }
    
    // Check if our king is under attack after the move
    PieceColor enemyColor = (ourColor == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
    return !testBoard.isSquareAttacked(kingSquare, enemyColor);
}

MoveList LegalMoveValidator::filterLegalMoves(const Board& board, const MoveList& pseudoLegalMoves) {
    MoveList legalMoves;
    
    for (const Move& move : pseudoLegalMoves) {
        if (isMoveLegal(board, move)) {
            legalMoves.push_back(move);
        }
    }
    
    return legalMoves;
}

bool LegalMoveValidator::isInCheck(const Board& board, PieceColor side) {
    int kingSquare = findKing(board, side);
    if (kingSquare == -1) {
        return false; // No king found
    }
    
    PieceColor enemyColor = (side == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
    return board.isSquareAttacked(kingSquare, enemyColor);
}

bool LegalMoveValidator::isCheckmate(const Board& board, PieceColor side) {
    // Must be in check for checkmate
    if (!isInCheck(board, side)) {
        return false;
    }
    
    // Generate all pseudo-legal moves
    MoveList pseudoLegalMoves = generateMoves(board, side);
    
    // Filter to legal moves
    MoveList legalMoves = filterLegalMoves(board, pseudoLegalMoves);
    
    // Checkmate if no legal moves exist while in check
    return legalMoves.empty();
}

bool LegalMoveValidator::isStalemate(const Board& board, PieceColor side) {
    // Must NOT be in check for stalemate
    if (isInCheck(board, side)) {
        return false;
    }
    
    // Generate all pseudo-legal moves
    MoveList pseudoLegalMoves = generateMoves(board, side);
    
    // Filter to legal moves
    MoveList legalMoves = filterLegalMoves(board, pseudoLegalMoves);
    
    // Stalemate if no legal moves exist while not in check
    return legalMoves.empty();
}

int LegalMoveValidator::findKing(const Board& board, PieceColor color) {
    for (int square = 0; square < 64; ++square) {
        const Piece& piece = board.squares[square];
        if (piece.type() == KING && piece.color() == color) {
            return square;
        }
    }
    return -1; // King not found
}
