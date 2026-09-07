#pragma once
#include "move.hpp"
#include <atomic>
#include <memory>
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

// --- The stored form: two 64-bit words, read and written without a lock ---
//
// `TTEntry` above is the *logical* entry, the shape callers think in. What is
// actually stored is a pair of words: the payload, and the position hash XORed
// with it. A reader recomputes `key ^ data` and compares it to the hash it was
// looking for, so an entry torn by a concurrent writer fails its own checksum
// and reads as a miss rather than as someone else's position.
//
// This is Hyatt's scheme, and it is already in this codebase: `g_evalCache`
// does the same thing because the GUI thread can evaluate during a search
// (`evaluation.cpp`). It is not a lock, it is a *detector* -- torn entries are
// discarded, never believed.
//
// The residual risk is a torn read whose XOR happens to equal the probed hash.
// That needs `hash_new ^ data_new ^ data_old == hash_probed` to hold by
// accident, and the consequence is a wrong best move tried first, which the
// move list validates anyway. This is the standard accepted risk of the scheme.
//
// Relaxed ordering throughout: there is nothing to synchronise-with. Each word
// is independently atomic, the checksum catches any combination that does not
// belong together, and a missed entry costs a re-search rather than a wrong
// answer.
struct TTSlot {
    std::atomic<uint64_t> key{0};    // hash XOR data
    std::atomic<uint64_t> data{0};   // packed payload; see packData
};
static_assert(sizeof(TTSlot) == 16, "TTSlot must stay 16 bytes: it sets "
                                    "ENTRIES_PER_MB and every node count");

// Payload packing. 56 of 64 bits used; the spare byte is deliberate headroom
// so a future field does not force the slot wider.
inline uint64_t packData(const TTEntry& e) {
    return  (uint64_t)(uint16_t)e.score
         | ((uint64_t)e.bestMove            << 16)
         | ((uint64_t)(uint8_t)e.depth      << 32)
         | ((uint64_t)e.generation          << 40)
         | ((uint64_t)(uint8_t)e.nodeType   << 48);
}

inline TTEntry unpackData(uint64_t d) {
    TTEntry e;
    e.score      = (int16_t)(uint16_t)(d & 0xFFFFu);
    e.bestMove   = (uint16_t)((d >> 16) & 0xFFFFu);
    e.depth      = (int8_t)(uint8_t)((d >> 32) & 0xFFu);
    e.generation = (uint8_t)((d >> 40) & 0xFFu);
    e.nodeType   = (TTEntry::NodeType)((d >> 48) & 0xFFu);
    return e;
}

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
    
    // unique_ptr<TTSlot[]> rather than vector: std::atomic is neither copyable
    // nor movable, so a vector of them cannot be resized.
    std::unique_ptr<TTSlot[]> table;
    size_t tableSize = 0;
    // Bumped once per search. Wrapping at 256 is harmless: it means an entry
    // 256 searches old can survive one more, which is 256 moves ago.
    uint8_t generation = 0;
    
    // Statistics, opt-in at compile time (-DTT_STATS).
    //
    // These are diagnostics with exactly one caller (printStats, from
    // ChessBotEngine) and nothing depends on them. Counting them costs a
    // measured **5% of bench wall time** single-threaded, because it is an
    // atomic read-modify-write on the hottest path in the program -- and a
    // single shared cache line bounced between eight cores is worse than 5%,
    // which is the whole reason this had to be settled before threading rather
    // than after.
    //
    // Atomic rather than plain when enabled: a raced plain increment is
    // undefined behaviour, not merely an inaccurate number.
    std::atomic<uint64_t> hits{0};
    std::atomic<uint64_t> misses{0};
    std::atomic<uint64_t> collisions{0};

#ifdef TT_STATS
    static constexpr bool STATS = true;
#else
    static constexpr bool STATS = false;
#endif
    // Compiles to nothing when STATS is false: the branch is on a constexpr,
    // so there is no test at runtime.
    static void bump(std::atomic<uint64_t>& c) {
        if (STATS) c.fetch_add(1, std::memory_order_relaxed);
    }
    
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
    
    // Raw entry access, for singular extensions.
    //
    // `probe` deliberately hides the stored depth and bound type behind its own
    // decision about whether the score is usable *here*. A singular search
    // needs those directly: it only fires on a lower-bound entry searched deep
    // enough to be worth trusting, and that judgement belongs to the caller.
    // Returns false when the slot holds another position.
    bool peek(uint64_t hash, TTEntry& out) const {
        const TTSlot& s = table[hash % tableSize];
        const uint64_t k = s.key.load(std::memory_order_relaxed);
        const uint64_t d = s.data.load(std::memory_order_relaxed);
        if ((k ^ d) != hash) return false;    // empty, another position, or torn
        out = unpackData(d);
        out.hash = hash;
        return out.depth >= 0;
    }

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
    uint64_t getHits() const { return hits.load(std::memory_order_relaxed); }
    uint64_t getMisses() const { return misses.load(std::memory_order_relaxed); }
    uint64_t getCollisions() const { return collisions.load(std::memory_order_relaxed); }
    void clearStats();
    void printStats() const;
    
    // Size information
    size_t getSize() const { return tableSize; }
    size_t getSizeMB() const { return tableSize * sizeof(TTEntry) / (1024 * 1024); }
    
private:
    size_t getIndex(uint64_t hash) const { return hash % tableSize; }
    bool shouldReplace(uint64_t existingHash, const TTEntry& existing,
                       uint64_t newHash, const TTEntry& newEntry) const;
};
