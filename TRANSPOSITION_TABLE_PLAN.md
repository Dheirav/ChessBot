// Quick implementation template for Transposition Table

// 1. Add to Board class (board.hpp):
class Board {
    // ... existing code ...
public:
    uint64_t getHash() const; // Zobrist hash of current position
private:
    uint64_t currentHash = 0;
    static uint64_t zobristTable[64][12]; // [square][piece]
    static uint64_t zobristSideToMove;
    static uint64_t zobristCastling[16];
    static uint64_t zobristEnPassant[8];
    static bool zobristInitialized;
    
    void initializeZobrist();
    void updateHashForMove(const Move& move);
};

// 2. Transposition Table (transposition_table.hpp):
#pragma once
#include "move.hpp"
#include <vector>
#include <cstdint>

struct TTEntry {
    uint64_t hash = 0;
    int depth = -1;
    int score = 0;
    Move bestMove;
    enum NodeType { EXACT, LOWER_BOUND, UPPER_BOUND } nodeType = EXACT;
    
    bool isValid(uint64_t searchHash) const {
        return hash == searchHash && depth >= 0;
    }
};

class TranspositionTable {
private:
    static constexpr size_t TABLE_SIZE = 1024 * 1024; // 1M entries ~ 32MB
    std::vector<TTEntry> table;
    uint64_t hits = 0;
    uint64_t misses = 0;
    
public:
    TranspositionTable();
    
    bool probe(uint64_t hash, int depth, int alpha, int beta, int& score, Move& bestMove);
    void store(uint64_t hash, int depth, int score, Move bestMove, int nodeType, int alpha, int beta);
    
    void clear();
    double getHitRate() const { return hits + misses > 0 ? (double)hits / (hits + misses) : 0.0; }
    void printStats() const;
    
private:
    size_t getIndex(uint64_t hash) const { return hash % TABLE_SIZE; }
};

// 3. Modified search function:
int minimaxWithTT(Board& board, int depth, int alpha, int beta, bool maximizingPlayer, 
                  TranspositionTable& tt, const std::atomic<bool>& shouldStop) {
    
    if (shouldStop.load()) return 0;
    
    uint64_t hash = board.getHash();
    Move ttMove;
    int ttScore;
    
    // Probe transposition table
    if (tt.probe(hash, depth, alpha, beta, ttScore, ttMove)) {
        return ttScore; // Table hit!
    }
    
    if (depth == 0) {
        int score = evaluate(board);
        tt.store(hash, depth, score, Move(), TTEntry::EXACT, alpha, beta);
        return score;
    }
    
    // ... rest of minimax with TT storage at the end
}

// Expected performance improvement:
// Before: depth 3 = ~1-2 seconds
// After:  depth 5-6 = ~1-2 seconds (3-4x deeper!)
