#pragma once
#include "move.hpp"
#include <vector>
#include <cstdint>

/**
 * Transposition Table Entry
 * Stores position evaluation and best move for previously searched positions
 */
// Sixteen bytes, and every field is sized to keep it there.
//
// The size divides into ENTRIES_PER_MB, so it sets the table's length, its
// index distribution, and therefore every node count the search produces.
// Changing it changes the bench signature by construction.
//
// It was 40 bytes until 2026-08-25 — a 20-byte `Move` plus two ints — which
// bought 6.7M slots at the default 256MB against a median search of 9.9M
// nodes. The table was being recycled through inside a single move rather
// than caching across one. At 16 bytes the same memory holds 16.8M.
//
// Sixteen also divides 64, so entries no longer straddle cache lines. At 40
// bytes three in eight did, and every probe of one of those cost two cache
// misses instead of one.
struct TTEntry {
    uint64_t hash = 0;
    // int16 because the widest score the search can produce is INF (32 000),
    // and scoreToTT adds at most one ply's worth (64) to a mate score, giving
    // 32 064 against a ceiling of 32 767. Anything wider than this would cost
    // two bytes the entry does not have.
    int16_t score = 0;
    // The best move, packed (see packMove). 0 is "none".
    uint16_t bestMove = 0;
    // depth is a search depth, 0..64, so it does not need 32 bits.
    int8_t depth = -1;
    // Which search stored this. See shouldReplace: an entry left over from an
    // earlier search is evictable regardless of how deep it was.
    uint8_t generation = 0;

    enum NodeType : uint8_t {
        EXACT,       // Exact score (PV node)
        LOWER_BOUND, // Alpha cutoff (fail-high)
        UPPER_BOUND  // Beta cutoff (fail-low)
    };
    uint8_t nodeType = EXACT;

    // Check if this entry is valid for the given hash
    bool isValid(uint64_t searchHash) const {
        return hash == searchHash && depth >= 0;
    }
};

static_assert(sizeof(TTEntry) == 16, "TTEntry must stay 16 bytes: it sets "
                                     "ENTRIES_PER_MB and every node count");

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
    // Bumped once per search. Wrapping at 256 is harmless: it means an entry
    // 256 searches old can survive one more, which is 256 moves ago.
    uint8_t generation = 0;
    
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
    
    // Begin a new search. Entries stored before this call become evictable by
    // any entry from the new search, however deep they were.
    //
    // Without it the table is depth-preferred and ageless: entries from moves
    // already played, for positions that will never occur again, can only be
    // displaced by something deeper still. Over a game the live search is left
    // with a shrinking share of the table, which is how a warm table comes to
    // play worse than an empty one.
    void newSearch() { ++generation; }

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
