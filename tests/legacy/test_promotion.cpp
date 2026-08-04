#include "src/engine/board.hpp"
#include "src/engine/movegen.hpp"
#include "src/engine/fen.hpp"
#include <iostream>

int main() {
    std::cout << "=== Testing Promotion Logic ===" << std::endl;
    
    // Create a board from starting position first to test move generation
    Board testBoard;
    std::cout << "Testing with starting position:" << std::endl;
    MoveList startingMoves = generateLegalMoves(testBoard, COLOR_WHITE);
    std::cout << "Starting position has " << startingMoves.size() << " moves" << std::endl;
    
    if (startingMoves.empty()) {
        std::cout << "Error: No moves generated from starting position!" << std::endl;
        return 1;
    }
    
    // Now test promotion position - corrected FEN with white pawn on 6th rank ready to promote
    Board board;
    // FEN: "8/8/8/8/8/8/P7/8 w - - 0 1" (white pawn on a2 in FEN = rank 6 in our array)
    std::string testFEN = "8/8/8/8/8/8/P7/8 w - - 0 1";
    if (!board.setFromFEN(testFEN)) {
        std::cout << "Failed to set FEN position!" << std::endl;
        return 1;
    }
    
    std::cout << "Board squares debug:" << std::endl;
    for (int rank = 0; rank < 8; rank++) {
        for (int file = 0; file < 8; file++) {
            int idx = rank * 8 + file;
            Piece p = board.squares[idx];
            if (p.type() != NONE) {
                std::cout << "Square " << idx << " (file=" << file << ", rank=" << rank << "): " 
                          << static_cast<int>(p.type()) << " color=" << static_cast<int>(p.color()) << std::endl;
            }
        }
    }
    
    std::cout << "Initial position: " << testFEN << std::endl;
    std::cout << "Board state:" << std::endl;
    std::cout << board.getFEN() << std::endl;
    
    // Generate legal moves
    MoveList moves = generateLegalMoves(board, COLOR_WHITE);
    
    std::cout << "\nGenerated moves (total: " << moves.size() << "):" << std::endl;
    for (const Move& move : moves) {
        std::cout << "From: " << move.from << ", To: " << move.to 
                  << ", Flag: " << move.flag;
        if (move.flag == PROMOTION) {
            std::cout << ", Promotion piece: " << static_cast<int>(move.promotionPiece.type());
        }
        std::cout << std::endl;
    }
    
    // Find a queen promotion move
    Move queenPromotion;
    bool foundQueenPromotion = false;
    for (const Move& move : moves) {
        if (move.flag == PROMOTION && move.promotionPiece.type() == QUEEN) {
            queenPromotion = move;
            foundQueenPromotion = true;
            break;
        }
    }
    
    if (foundQueenPromotion) {
        std::cout << "\nFound queen promotion move: " << queenPromotion.toString() << std::endl;
        
        // Make the promotion move
        board.makeMove(queenPromotion);
        
        std::cout << "After queen promotion:" << std::endl;
        std::cout << board.getFEN() << std::endl;
        
        // Check if the pawn was replaced with a queen
        Piece promotedPiece = board.squares[queenPromotion.to];
        if (promotedPiece.type() == QUEEN && promotedPiece.color() == COLOR_WHITE) {
            std::cout << "✓ Promotion successful! Pawn promoted to Queen." << std::endl;
        } else {
            std::cout << "✗ Promotion failed! Expected Queen, got: " << static_cast<int>(promotedPiece.type()) << std::endl;
        }
    } else {
        std::cout << "✗ No queen promotion move found!" << std::endl;
    }
    
    return 0;
}
