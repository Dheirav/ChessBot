#pragma once
#include "board.hpp"
#include "move.hpp"
#include <string>
#include <functional>

/**
 * Abstract interface for a chess engine
 * Separates engine implementation from GUI and game management
 */
class IChessEngine {
public:
    virtual ~IChessEngine() = default;
    
    // Engine control
    virtual void initialize() = 0;
    virtual void shutdown() = 0;
    
    // Move calculation (should be non-blocking in real implementation)
    virtual Move findBestMove(const Board& board, int depth = 3) = 0;
    
    // Position evaluation
    virtual int evaluatePosition(const Board& board) = 0;
    
    // Engine information
    virtual std::string getEngineName() const = 0;
    virtual std::string getEngineVersion() const = 0;
    
    // Configuration
    virtual void setSearchDepth(int depth) = 0;
    virtual int getSearchDepth() const = 0;
    
    // Optional: Async move calculation with callback
    using MoveCallback = std::function<void(const Move&)>;
    virtual void findBestMoveAsync(const Board& board, MoveCallback callback) {
        // Default implementation: call sync version
        Move move = findBestMove(board);
        callback(move);
    }
    
    virtual bool isThinking() const { return false; }
    virtual void stopThinking() {}
};
