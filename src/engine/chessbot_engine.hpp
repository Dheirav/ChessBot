#pragma once
#include "chess_engine_interface.hpp"
#include "board.hpp"
#include "move.hpp"
#include "search.hpp"
#include "evaluation.hpp"
#include <thread>
#include <atomic>
#include <future>
#include <mutex>

/**
 * Implementation of the chess engine interface
 * Wraps the existing search and evaluation functions with threading support
 */
class ChessBotEngine : public IChessEngine {
private:
    int searchDepth;
    std::atomic<bool> thinking;
    std::atomic<bool> stopSearch;
    std::string engineName;
    std::string engineVersion;
    
    // Threading support
    std::thread searchThread;
    std::mutex engineMutex;

public:
    ChessBotEngine();
    virtual ~ChessBotEngine() = default;
    
    // IChessEngine interface
    void initialize() override;
    void shutdown() override;
    
    Move findBestMove(const Board& board, int depth = 3) override;
    int evaluatePosition(const Board& board) override;
    
    std::string getEngineName() const override;
    std::string getEngineVersion() const override;
    
    void setSearchDepth(int depth) override;
    int getSearchDepth() const override;
    
    void findBestMoveAsync(const Board& board, MoveCallback callback) override;
    bool isThinking() const override;
    void stopThinking() override;
};
