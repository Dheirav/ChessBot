#include "src/engine/board.hpp"
#include "src/engine/movegen.hpp"
#include "src/engine/fen.hpp"
#include "src/engine/legal_move_validator.hpp"
#include <iostream>

int main() {
    // Test position where king is in check but has legal moves
    // Black rook on a8 giving check to white king on a1, king can move to b1, b2
    std::string testFen = "r7/8/8/8/8/8/8/1K6 w - - 0 1";
    
    Board board;
    FENInfo info;
    if (!parseFEN(testFen, board, info)) {
        std::cout << "Failed to load FEN position" << std::endl;
        return 1;
    }
    
    std::cout << "Testing position: " << testFen << std::endl;
    std::cout << "Active side: " << (board.activeColor == COLOR_WHITE ? "White" : "Black") << std::endl;
    
    // Print board state for debugging
    std::cout << "Board state:" << std::endl;
    for (int rank = 7; rank >= 0; rank--) {
        for (int file = 0; file < 8; file++) {
            int square = rank * 8 + file;
            Piece piece = board.squares[square];
            if (piece.type() == NONE) {
                std::cout << ". ";
            } else {
                char c = '?';
                switch (piece.type()) {
                    case PAWN: c = 'P'; break;
                    case ROOK: c = 'R'; break;
                    case KNIGHT: c = 'N'; break;
                    case BISHOP: c = 'B'; break;
                    case QUEEN: c = 'Q'; break;
                    case KING: c = 'K'; break;
                    case NONE: c = '.'; break;
                }
                if (piece.color() == COLOR_BLACK) c = tolower(c);
                std::cout << c << " ";
            }
        }
        std::cout << std::endl;
    }
    
    // Check if king is in check
    bool inCheck = LegalMoveValidator::isInCheck(board, board.activeColor);
    std::cout << "King in check: " << (inCheck ? "YES" : "NO") << std::endl;
    
    // Generate pseudo-legal moves
    MoveList pseudoLegalMoves = generateMoves(board, board.activeColor);
    std::cout << "Pseudo-legal moves: " << pseudoLegalMoves.size() << std::endl;
    
    // Generate legal moves
    MoveList legalMoves = generateLegalMoves(board, board.activeColor);
    std::cout << "Legal moves: " << legalMoves.size() << std::endl;
    
    if (inCheck && legalMoves.size() < pseudoLegalMoves.size()) {
        std::cout << "SUCCESS: Legal move generation correctly filtered out " 
                  << (pseudoLegalMoves.size() - legalMoves.size()) 
                  << " illegal moves when king was in check." << std::endl;
    } else if (!inCheck && legalMoves.size() == pseudoLegalMoves.size()) {
        std::cout << "SUCCESS: No moves filtered when king not in check." << std::endl;
    } else {
        std::cout << "WARNING: Unexpected move counts." << std::endl;
    }
    
    // List legal moves
    std::cout << "\nLegal moves available:" << std::endl;
    for (size_t i = 0; i < legalMoves.size(); ++i) {
        const Move& move = legalMoves[i];
        std::cout << "  " << (i+1) << ". " << move.from << " -> " << move.to << std::endl;
    }
    
    return 0;
}
