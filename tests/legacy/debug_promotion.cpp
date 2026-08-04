#include "src/engine/board.hpp"
#include "src/engine/movegen.hpp"
#include "src/engine/move_lookup.hpp"
#include <iostream>

int main() {
    std::cout << "=== Testing Promotion Move Generation ===" << std::endl;
    
    // Initialize move lookup tables - this was missing!
    initMoveLookupTables();
    
    // First test with starting position to verify move generation works
    Board startBoard;
    MoveList startMoves = generateLegalMoves(startBoard, COLOR_WHITE);
    std::cout << "Starting position generates " << startMoves.size() << " moves (should be 20)" << std::endl;
    
    if (startMoves.empty()) {
        std::cout << "ERROR: Move generation is broken!" << std::endl;
        return 1;
    }
    
    Board board;
    
    // Create a position where white can promote without capturing
    // Put the black king on b8 so the pawn can promote to a8 freely
    std::string promotionFEN = "1k6/P7/8/8/8/8/8/7K w - - 0 1";
    if (!board.setFromFEN(promotionFEN)) {
        std::cout << "Failed to set promotion FEN!" << std::endl;
        return 1;
    }
    
    std::cout << "Board FEN: " << board.getFEN() << std::endl;
    
    // Print the board squares to understand the coordinate system
    std::cout << "\nBoard layout (rank 0 = top, rank 7 = bottom):" << std::endl;
    for (int rank = 0; rank < 8; rank++) {
        std::cout << "Rank " << rank << ": ";
        for (int file = 0; file < 8; file++) {
            int idx = rank * 8 + file;
            Piece p = board.squares[idx];
            if (p.type() == NONE) {
                std::cout << ". ";
            } else {
                char pieceChar = 'P';
                switch (p.type()) {
                    case PAWN:   pieceChar = 'P'; break;
                    case KNIGHT: pieceChar = 'N'; break;
                    case BISHOP: pieceChar = 'B'; break;
                    case ROOK:   pieceChar = 'R'; break;
                    case QUEEN:  pieceChar = 'Q'; break;
                    case KING:   pieceChar = 'K'; break;
                }
                if (p.color() == COLOR_BLACK) pieceChar = tolower(pieceChar);
                std::cout << pieceChar << " ";
            }
        }
        std::cout << std::endl;
    }
    
    // Generate legal moves
    MoveList moves = generateLegalMoves(board, COLOR_WHITE);
    std::cout << "\nGenerated " << moves.size() << " moves:" << std::endl;
    
    // Debug: Check what moves are in the pawn lookup table for square 8 (a7)
    int pawnSquare = 8; // a7 = rank 1, file 0 = 1*8 + 0 = 8
    std::cout << "\nWhite pawn lookup table for square " << pawnSquare << " (a7):" << std::endl;
    
    // We need to access the move lookup tables, but they might not be public
    // Let's check if we can access them
    std::cout << "Pawn should be able to move from " << pawnSquare << " (a7) to " << (pawnSquare - 8) << " (a8)" << std::endl;
    
    for (const Move& move : moves) {
        int fromFile = move.from % 8;
        int fromRank = move.from / 8;
        int toFile = move.to % 8;
        int toRank = move.to / 8;
        
        std::cout << "Move: " << char('a' + fromFile) << (8 - fromRank) 
                  << " to " << char('a' + toFile) << (8 - toRank);
        std::cout << " (from=" << move.from << " to=" << move.to << ")";
        
        if (move.flag == PROMOTION) {
            std::cout << " PROMOTION to ";
            switch (move.promotionPiece.type()) {
                case QUEEN:  std::cout << "Queen"; break;
                case ROOK:   std::cout << "Rook"; break;
                case BISHOP: std::cout << "Bishop"; break;
                case KNIGHT: std::cout << "Knight"; break;
                default:     std::cout << "Unknown"; break;
            }
        }
        std::cout << std::endl;
    }
    
    // Let's manually check if the pawn can move from a7 to a8
    std::cout << "\nManual check:" << std::endl;
    std::cout << "Square 8 (a7) has piece type: " << static_cast<int>(board.squares[8].type()) << std::endl;
    std::cout << "Square 0 (a8) has piece type: " << static_cast<int>(board.squares[0].type()) << std::endl;
    std::cout << "This should be a capture move if the black king is on a8" << std::endl;
    
    return 0;
}
