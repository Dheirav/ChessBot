#include "chessbot_engine.hpp"
#include "move_lookup.hpp"
#include "search.hpp"
#include <iostream>

ChessBotEngine::ChessBotEngine() 
    : searchDepth(5), thinking(false), stopSearch(false), 
      engineName("ChessBot with Iterative Deepening"), engineVersion("1.1") {
    // Initialize transposition table with 256MB (increased size for better performance)
    transpositionTable = std::make_unique<TranspositionTable>(256);
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
    
    // Use iterative deepening search for better move ordering and time management
    Board searchBoard = board;
    Move bestMove;
    {
        std::lock_guard<std::mutex> ttLock(ttMutex);
        bestMove = ::findBestMoveIterativeDeepening(searchBoard, depth > 0 ? depth : searchDepth, stopSearch, *transpositionTable);
    }
    
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
            
            // Perform the search with iterative deepening for better time management
            Board searchBoard = board;
            Move bestMove;
            {
                // Hold the TT lock for the whole search so that
                // clear/resize/stats calls from other threads are serialized.
                std::lock_guard<std::mutex> ttLock(ttMutex);
                bestMove = ::findBestMoveIterativeDeepening(searchBoard, searchDepth, stopSearch, *transpositionTable);
            }
            
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

void ChessBotEngine::clearTranspositionTable() {
    std::lock_guard<std::mutex> ttLock(ttMutex);
    if (transpositionTable) {
        transpositionTable->clear();
        std::cout << "Transposition table cleared" << std::endl;
    }
}

void ChessBotEngine::printTranspositionTableStats() const {
    std::lock_guard<std::mutex> ttLock(ttMutex);
    if (transpositionTable) {
        transpositionTable->printStats();
    }
}

void ChessBotEngine::resizeTranspositionTable(size_t sizeMB) {
    std::lock_guard<std::mutex> ttLock(ttMutex);
    if (transpositionTable) {
        transpositionTable->resize(sizeMB);
    }
}
