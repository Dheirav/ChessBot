#include "legal_move_validator.hpp"

bool LegalMoveValidator::isInCheck(const Board& board, PieceColor side) {
    int kingSquare = findKing(board, side);
    if (kingSquare == -1) {
        return false; // No king found
    }

    PieceColor enemyColor = (side == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
    return board.isSquareAttacked(kingSquare, enemyColor);
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
