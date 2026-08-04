#include "src/engine/board.hpp"
#include "src/engine/fen.hpp"
#include "src/engine/movegen.hpp"
#include "src/engine/legal_move_validator.hpp"
#include <iostream>

int main() {
    // Test the exact position from the game log
    // After the moves: d7d5, f2f3, e7e5, g2g4, e5e4, f3f4, d5d4, h2h4, e4e3, d2d3, Qd8h4+
    // This should be checkmate for White
    
    // Simplified position with queen giving checkmate on h4
    std::string testFen = "rnb1kbnr/ppp2ppp/8/8/3p3q/3P4/PPP2P1P/RNBQKBNR w KQkq - 0 1";
    
    Board board;
    FENInfo info;
    if (!parseFEN(testFen, board, info)) {
        std::cout << "Failed to load FEN position" << std::endl;
        return 1;
    }
    
    std::cout << "Testing the actual game position where checkmate occurred" << std::endl;
    std::cout << "FEN: " << testFen << std::endl;
    std::cout << "Active side: " << (board.activeColor == COLOR_WHITE ? "White" : "Black") << std::endl;
    
    // Print board state
    std::cout << "\nBoard position:" << std::endl;
    for (int rank = 7; rank >= 0; rank--) {
        std::cout << (rank + 1) << " ";
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
    std::cout << "  a b c d e f g h" << std::endl;
    
    // Find the white king
    int whiteKingSq = -1;
    for (int i = 0; i < 64; ++i) {
        if (board.squares[i].type() == KING && board.squares[i].color() == COLOR_WHITE) {
            whiteKingSq = i;
            break;
        }
    }
    
    std::cout << "\nWhite king is on square " << whiteKingSq;
    if (whiteKingSq >= 0) {
        int file = whiteKingSq % 8;
        int rank = whiteKingSq / 8;
        std::cout << " (" << char('a' + file) << char('1' + rank) << ")";
    }
    std::cout << std::endl;
    
    // Check if white king is in check
    bool inCheck = LegalMoveValidator::isInCheck(board, COLOR_WHITE);
    std::cout << "White king in check: " << (inCheck ? "YES" : "NO") << std::endl;
    
    // Generate legal moves for white
    MoveList legalMoves = generateLegalMoves(board, COLOR_WHITE);
    std::cout << "Legal moves for White: " << legalMoves.size() << std::endl;
    
    // Check if it's checkmate or stalemate
    if (legalMoves.empty()) {
        if (inCheck) {
            std::cout << "RESULT: Checkmate! Black wins." << std::endl;
        } else {
            std::cout << "RESULT: Stalemate! Draw." << std::endl;
        }
    } else {
        std::cout << "RESULT: Game continues." << std::endl;
        std::cout << "Legal moves:" << std::endl;
        for (size_t i = 0; i < legalMoves.size(); ++i) {
            const Move& move = legalMoves[i];
            int fromFile = move.from % 8;
            int fromRank = move.from / 8;
            int toFile = move.to % 8;
            int toRank = move.to / 8;
            std::cout << "  " << char('a' + fromFile) << char('1' + fromRank) 
                      << char('a' + toFile) << char('1' + toRank);
            if (move.capturedPiece.type() != NONE) {
                std::cout << " (capture)";
            }
            std::cout << std::endl;
        }
    }
    
    return 0;
}
