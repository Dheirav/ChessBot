#include "src/engine/board.hpp"
#include "src/engine/movegen.hpp"
#include "src/engine/move_lookup.hpp"
#include <iostream>

int main() {
    std::cout << "=== Simple Promotion Test ===" << std::endl;
    
    // Initialize move lookup tables
    initMoveLookupTables();
    
    Board board;
    // Simple promotion test: white pawn on a7, black king on b8
    std::string testFEN = "1k6/P7/8/8/8/8/8/7K w - - 0 1";
    if (!board.setFromFEN(testFEN)) {
        std::cout << "Failed to set FEN!" << std::endl;
        return 1;
    }
    
    std::cout << "Test FEN: " << testFEN << std::endl;
    std::cout << "Actual FEN: " << board.getFEN() << std::endl;
    
    // Check the squares
    std::cout << "Square 0 (a8): " << static_cast<int>(board.squares[0].type()) << std::endl;
    std::cout << "Square 1 (b8): " << static_cast<int>(board.squares[1].type()) << std::endl;
    std::cout << "Square 8 (a7): " << static_cast<int>(board.squares[8].type()) << std::endl;
    
    MoveList moves = generateLegalMoves(board, COLOR_WHITE);
    std::cout << "Generated " << moves.size() << " moves:" << std::endl;
    
    for (const Move& move : moves) {
        std::cout << "Move from " << move.from << " to " << move.to;
        if (move.flag == PROMOTION) {
            std::cout << " PROMOTION!";
        }
        std::cout << std::endl;
    }
    
    return 0;
}
