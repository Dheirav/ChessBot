#pragma once
#include "board.hpp"
#include "move.hpp"
#include <string>
#include <functional>
#include <cstdint>
#include <vector>

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
    //
    // `gameHistory` carries the zobrist keys of the positions the game already
    // visited. A board is a position; a repetition is a property of a game, so
    // an engine handed only a board cannot see one (BUGS.md 1). It defaults to
    // empty for callers analysing a position rather than playing a game.
    virtual Move findBestMove(const Board& board, int depth = 3,
                              const std::vector<uint64_t>& gameHistory = {}) = 0;
    
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
    virtual void findBestMoveAsync(const Board& board, MoveCallback callback,
                                   const std::vector<uint64_t>& gameHistory = {}) {
        // Default implementation: call sync version
        Move move = findBestMove(board, 3, gameHistory);
        callback(move);
    }
    
    virtual bool isThinking() const { return false; }
    virtual void stopThinking() {}
};
