#pragma once
#include "move.hpp"
#include <vector>
#include <cstdint>

/**
 * Transposition Table Entry
 * Stores position evaluation and best move for previously searched positions
 */
struct TTEntry {
    uint64_t hash = 0;
    int depth = -1;
    int score = 0;
    Move bestMove;
    
    enum NodeType {
        EXACT,       // Exact score (PV node)
        LOWER_BOUND, // Alpha cutoff (fail-high)
        UPPER_BOUND  // Beta cutoff (fail-low)
    } nodeType = EXACT;
    
    // Check if this entry is valid for the given hash
    bool isValid(uint64_t searchHash) const {
        return hash == searchHash && depth >= 0;
    }
    
    // Check if we can use this score for the current search
    bool canUseScore(int searchDepth, int alpha, int beta) const {
        if (depth < searchDepth) return false;
        
        switch (nodeType) {
            case EXACT:
                return true;
            case LOWER_BOUND:
                return score >= beta;
            case UPPER_BOUND:
                return score <= alpha;
        }
        return false;
    }
};

/**
 * Transposition Table
 * Hash table for storing previously computed position evaluations
 */
class TranspositionTable {
private:
    static constexpr size_t DEFAULT_SIZE_MB = 64;
    static constexpr size_t ENTRIES_PER_MB = 1024 * 1024 / sizeof(TTEntry);
    
    std::vector<TTEntry> table;
    size_t tableSize;
    
    // Statistics
    uint64_t hits = 0;
    uint64_t misses = 0;
    uint64_t collisions = 0;
    
public:
    explicit TranspositionTable(size_t sizeMB = DEFAULT_SIZE_MB);
    
    // Probe the table for a position
    bool probe(uint64_t hash, int depth, int alpha, int beta, int& score, Move& bestMove);
    
    // Store a position in the table
    void store(uint64_t hash, int depth, int score, Move bestMove, 
               TTEntry::NodeType nodeType);
    
    // Table management
    void clear();
    void resize(size_t sizeMB);
    
    // Statistics
    double getHitRate() const;
    uint64_t getHits() const { return hits; }
    uint64_t getMisses() const { return misses; }
    uint64_t getCollisions() const { return collisions; }
    void clearStats();
    void printStats() const;
    
    // Size information
    size_t getSize() const { return tableSize; }
    size_t getSizeMB() const { return tableSize * sizeof(TTEntry) / (1024 * 1024); }
    
private:
    size_t getIndex(uint64_t hash) const { return hash % tableSize; }
    bool shouldReplace(const TTEntry& existing, const TTEntry& newEntry) const;
};
