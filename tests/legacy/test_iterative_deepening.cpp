#include "src/engine/search.hpp"
#include "src/engine/board.hpp"
#include "src/engine/transposition_table.hpp"
#include "src/engine/move_lookup.hpp"
#include <iostream>
#include <atomic>

int main() {
    // Initialize the engine
    initMoveLookupTables();
    
    // Create a board in starting position (simple, no mates)
    Board board;
    
    // Create transposition table
    TranspositionTable tt(32);
    
    // Create stop condition
    std::atomic<bool> shouldStop{false};
    
    std::cout << "Testing iterative deepening search with starting position..." << std::endl;
    
    // Test with depth 4 to see clear progression
    Move bestMove = findBestMoveIterativeDeepening(board, 4, shouldStop, tt);
    
    std::cout << "\nSearch complete!" << std::endl;
    
    return 0;
}
