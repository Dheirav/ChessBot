#include "chessbot_engine.hpp"
#include "move_lookup.hpp"
#include <iostream>

ChessBotEngine::ChessBotEngine() 
    : searchDepth(3), thinking(false), stopSearch(false), 
      engineName("ChessBot"), engineVersion("1.0") {
}

void ChessBotEngine::initialize() {
    std::cout << "Initializing " << engineName << " v" << engineVersion << std::endl;
    initMoveLookupTables();
    thinking = false;
}

void ChessBotEngine::shutdown() {
    std::cout << "Shutting down " << engineName << std::endl;
    stopThinking();
    
    // Wait for any running search thread to complete
    if (searchThread.joinable()) {
        searchThread.join();
    }
}

Move ChessBotEngine::findBestMove(const Board& board, int depth) {
    std::lock_guard<std::mutex> lock(engineMutex);
    
    thinking = true;
    stopSearch = false;
    
    // Use the existing search function with stop condition
    Move bestMove = ::findBestMoveWithStop(board, depth > 0 ? depth : searchDepth, stopSearch);
    
    thinking = false;
    return bestMove;
}

int ChessBotEngine::evaluatePosition(const Board& board) {
    // Use the existing evaluation function
    return evaluate(board);
}

std::string ChessBotEngine::getEngineName() const {
    return engineName;
}

std::string ChessBotEngine::getEngineVersion() const {
    return engineVersion;
}

void ChessBotEngine::setSearchDepth(int depth) {
    if (depth > 0 && depth <= 10) {  // Reasonable bounds
        searchDepth = depth;
    }
}

int ChessBotEngine::getSearchDepth() const {
    return searchDepth;
}

void ChessBotEngine::findBestMoveAsync(const Board& board, MoveCallback callback) {
    // Stop any existing search
    stopThinking();
    
    // Wait for previous thread to finish
    if (searchThread.joinable()) {
        searchThread.join();
    }
    
    // Start new search in background thread
    searchThread = std::thread([this, board, callback]() {
        try {
            thinking = true;
            stopSearch = false;
            
            std::cout << "Engine thinking asynchronously..." << std::endl;
            
            // Perform the search with stop condition checking
            Move bestMove = ::findBestMoveWithStop(board, searchDepth, stopSearch);
            
            // Only call callback if we weren't stopped
            if (!stopSearch.load()) {
                std::cout << "Engine found move: " << bestMove.toString() << std::endl;
                callback(bestMove);
            } else {
                std::cout << "Engine search was stopped" << std::endl;
            }
            
        } catch (const std::exception& e) {
            std::cerr << "Engine error: " << e.what() << std::endl;
            callback(Move()); // Return invalid move on error
        }
        
        thinking = false;
    });
}

bool ChessBotEngine::isThinking() const {
    return thinking.load();
}

void ChessBotEngine::stopThinking() {
    stopSearch = true;
    thinking = false;
    
    std::cout << "Stopping engine search..." << std::endl;
    
    // Note: The search thread will check stopSearch and exit gracefully
    // We don't force-kill the thread, just signal it to stop
}
