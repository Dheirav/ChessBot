#pragma once
#include "chess_engine_interface.hpp"
#include "board.hpp"
#include "move.hpp"
#include "search.hpp"
#include "evaluation.hpp"
#include "transposition_table.hpp"
#include <thread>
#include <atomic>
#include <future>
#include <mutex>
#include <memory>

/**
 * Implementation of the chess engine interface
 * Wraps the existing search and evaluation functions with threading support
 */
class ChessBotEngine : public IChessEngine {
private:
    // Atomic: written from the GUI thread (setSearchDepth/setMoveTimeMs) and
    // read inside the search thread; a plain int would be a data race.
    std::atomic<int> searchDepth;
    // Wall-clock budget per move, 0 for depth-only. This is normally what ends
    // the search; searchDepth is a ceiling.
    std::atomic<int> moveTimeMs;
    std::atomic<bool> thinking;
    std::atomic<bool> stopSearch;
    std::string engineName;
    std::string engineVersion;
    
    // Threading support
    std::thread searchThread;
    std::mutex engineMutex;
    mutable std::mutex ttMutex;
    
    // Transposition table
    std::unique_ptr<TranspositionTable> transpositionTable;

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

    void setMoveTimeMs(int ms);
    int getMoveTimeMs() const;
    
    void findBestMoveAsync(const Board& board, MoveCallback callback) override;
    bool isThinking() const override;
    void stopThinking() override;
    
    // Transposition table management
    void clearTranspositionTable();
    void printTranspositionTableStats() const;
    void resizeTranspositionTable(size_t sizeMB);
};
