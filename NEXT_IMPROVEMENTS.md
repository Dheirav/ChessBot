// Example implementation for threading improvement
// This would go in chessbot_engine.cpp

#include <thread>
#include <future>
#include <atomic>

void ChessBotEngine::findBestMoveAsync(const Board& board, MoveCallback callback) {
    // Stop any existing search
    stopThinking();
    
    // Start new search in background thread
    auto future = std::async(std::launch::async, [this, board, callback]() {
        thinking = true;
        
        try {
            Move bestMove = this->findBestMove(board, searchDepth);
            
            // Only call callback if we weren't stopped
            if (thinking && !stopSearch) {
                callback(bestMove);
            }
        } catch (const std::exception& e) {
            std::cerr << "Engine error: " << e.what() << std::endl;
            callback(Move()); // Invalid move on error
        }
        
        thinking = false;
    });
}

void ChessBotEngine::stopThinking() {
    stopSearch = true;
    thinking = false;
    
    // Wait for search thread to finish
    if (searchThread.joinable()) {
        searchThread.join();
    }
}

// Modified minimax to check for stop condition
static int minimax(Board& board, int depth, int alpha, int beta, bool maximizingPlayer, 
                  const std::atomic<bool>& shouldStop) {
    // Check if we should stop searching
    if (shouldStop.load()) {
        return 0; // Return neutral score
    }
    
    if (depth == 0) {
        return evaluate(board);
    }
    
    // ... rest of minimax logic
}
