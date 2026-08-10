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
};

// Scores with absolute value above this are mate scores (MATE_SCORE - ply).
// Mate scores are stored in the table relative to the entry's node (distance
// to mate from that position) and converted back to root-relative on probe,
// so a mate found via one path transfers correctly to a different ply.
constexpr int TT_MATE_THRESHOLD = 29000;

inline int scoreToTT(int score, int ply) {
    if (score > TT_MATE_THRESHOLD) return score + ply;
    if (score < -TT_MATE_THRESHOLD) return score - ply;
    return score;
}

inline int scoreFromTT(int score, int ply) {
    if (score > TT_MATE_THRESHOLD) return score - ply;
    if (score < -TT_MATE_THRESHOLD) return score + ply;
    return score;
}

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
    
    // Probe the table for a position. `ply` is the distance from the search
    // root, used to convert stored mate scores back to root-relative.
    bool probe(uint64_t hash, int depth, int ply, int alpha, int beta,
               int& score, Move& bestMove);

    // Store a position in the table. `ply` converts root-relative mate
    // scores to node-relative before storing.
    void store(uint64_t hash, int depth, int ply, int score, Move bestMove,
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
