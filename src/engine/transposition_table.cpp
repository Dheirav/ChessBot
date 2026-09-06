#include "transposition_table.hpp"
#include <iostream>
#include <iomanip>

TranspositionTable::TranspositionTable(size_t sizeMB) {
    resize(sizeMB);
}

bool TranspositionTable::probe(uint64_t hash, int depth, int ply, int alpha, int beta,
                              int& score, Move& bestMove) {
    const TTSlot& slot = table[getIndex(hash)];
    // One read of each word. Re-reading to "confirm" would widen the window
    // rather than close it; the checksum already decides.
    const uint64_t k = slot.key.load(std::memory_order_relaxed);
    const uint64_t d = slot.data.load(std::memory_order_relaxed);

    if ((k ^ d) != hash) {          // empty, a different position, or torn
        bump(misses);
        return false;
    }
    const TTEntry entry = unpackData(d);
    if (entry.depth < 0) { bump(misses); return false; }
    bump(hits);
    // Always return the best move if available
    if (entry.bestMove != 0) {
        bestMove = unpackMove(entry.bestMove);
    }
    // Check if we can use the score (bound checks run on the root-relative
    // score, so mate scores are converted before comparing to the window)
    if (entry.depth >= depth) {
        int adjusted = scoreFromTT(entry.score, ply);
        bool usable = false;
        switch (entry.nodeType) {
            case TTEntry::EXACT:       usable = true; break;
            case TTEntry::LOWER_BOUND: usable = adjusted >= beta; break;
            case TTEntry::UPPER_BOUND: usable = adjusted <= alpha; break;
        }
        if (usable) {
            score = adjusted;
            return true;
        }
    }
    // Hash hit but can't use score (wrong depth/bounds)
    return false;
}

void TranspositionTable::store(uint64_t hash, int depth, int ply, int score, Move bestMove,
                              TTEntry::NodeType nodeType) {
    TTSlot& slot = table[getIndex(hash)];

    const uint64_t k = slot.key.load(std::memory_order_relaxed);
    const uint64_t d = slot.data.load(std::memory_order_relaxed);
    const uint64_t existingHash = k ^ d;
    const TTEntry existing = unpackData(d);

    TTEntry newEntry;
    newEntry.depth = (int8_t)depth;
    newEntry.generation = generation;
    newEntry.score = (int16_t)scoreToTT(score, ply);
    newEntry.bestMove = packMove(bestMove);
    newEntry.nodeType = nodeType;

    if (existingHash != 0 && existingHash != hash) bump(collisions);

    if (shouldReplace(existingHash, existing, hash, newEntry)) {
        // Two independent stores, deliberately not made to look atomic. A
        // reader catching one of them computes key ^ data != its hash and
        // treats the slot as a miss: a re-search, never a wrong score. Order
        // does not matter -- either half-written combination fails the checksum.
        const uint64_t nd = packData(newEntry);
        slot.key.store(hash ^ nd, std::memory_order_relaxed);
        slot.data.store(nd, std::memory_order_relaxed);
    }
}

void TranspositionTable::clear() {
    for (size_t i = 0; i < tableSize; ++i) {
        table[i].key.store(0, std::memory_order_relaxed);
        table[i].data.store(0, std::memory_order_relaxed);
    }
    generation = 0;
    clearStats();
}

void TranspositionTable::resize(size_t sizeMB) {
    tableSize = sizeMB * ENTRIES_PER_MB;
    // value-initialised, so every slot starts (0,0) == empty
    table = std::make_unique<TTSlot[]>(tableSize);
    clearStats();
    
    // stderr, not stdout: in UCI mode stdout carries the protocol and any
    // stray line on it desynchronises the GUI.
    std::cerr << "Transposition table resized to " << sizeMB << "MB ("
              << tableSize << " entries)" << std::endl;
}

double TranspositionTable::getHitRate() const {
    const uint64_t h = getHits(), total = h + getMisses();
    return total > 0 ? (double)h / total : 0.0;
}

void TranspositionTable::clearStats() {
    hits.store(0, std::memory_order_relaxed);
    misses.store(0, std::memory_order_relaxed);
    collisions.store(0, std::memory_order_relaxed);
}

void TranspositionTable::printStats() const {
    if (!STATS) {
        std::cerr << "=== Transposition Table Stats ===\n"
                     "counting is compiled out; rebuild with -DTT_STATS. It costs\n"
                     "~5% of search time -- an atomic RMW on the hottest path, and a\n"
                     "single shared cache line across every thread.\n";
        return;
    }
    const uint64_t hits = getHits(), misses = getMisses(), collisions = getCollisions();
    uint64_t total = hits + misses;
    
    if (!STATS) {
        std::cerr << "=== Transposition Table Stats ===\n"
                  << "counting is compiled out; rebuild with -DTT_STATS.\n"
                  << "It costs ~5% of search time and is a shared cache line "
                     "written on every probe.\n";
        return;
    }
    std::cerr << "=== Transposition Table Stats ===" << std::endl;
    std::cerr << "Size: " << getSizeMB() << "MB (" << tableSize << " entries)" << std::endl;
    std::cerr << "Hits: " << hits << std::endl;
    std::cerr << "Misses: " << misses << std::endl;
    std::cerr << "Total lookups: " << total << std::endl;
    std::cerr << "Hit rate: " << std::fixed << std::setprecision(2) 
              << (getHitRate() * 100) << "%" << std::endl;
    std::cerr << "Collisions: " << collisions << std::endl;
    std::cerr << "=================================" << std::endl;
}

bool TranspositionTable::shouldReplace(uint64_t existingHash, const TTEntry& existing,
                                       uint64_t newHash, const TTEntry& newEntry) const {
    // Always replace empty entries
    if (existingHash == 0) {
        return true;
    }
    
    // Same position: keep the deeper result; at equal depth prefer the newer
    // entry (fresher bounds and best move). This stops a depth-0 quiescence
    // store from evicting a deep search result for the same position.
    if (existingHash == newHash) {
        return newEntry.depth >= existing.depth;
    }

    // A different position, left behind by an earlier search. Its depth says
    // how much work it cost, not how much it is worth now: the game has moved
    // on and most of those positions cannot occur again. Depth-preference
    // between searches is what lets them accumulate until the current search
    // has almost no table left, so age wins over depth here.
    if (existing.generation != newEntry.generation) {
        return true;
    }

    // A different position from this same search: depth-preferred, as before.
    return newEntry.depth > existing.depth;
}
