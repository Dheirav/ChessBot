#include "transposition_table.hpp"
#include <iostream>
#include <iomanip>

TranspositionTable::TranspositionTable(size_t sizeMB) {
    resize(sizeMB);
}

bool TranspositionTable::probe(uint64_t hash, int depth, int ply, int alpha, int beta,
                              int& score, Move& bestMove) {
    size_t index = getIndex(hash);
    const TTEntry& entry = table[index];

    if (!entry.isValid(hash)) {
        misses++;
        return false;
    }
    // We have a hash match
    hits++;
    // Always return the best move if available
    if (entry.bestMove.from != -1) {
        bestMove = entry.bestMove;
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
    size_t index = getIndex(hash);
    TTEntry& entry = table[index];

    // Create new entry
    TTEntry newEntry;
    newEntry.hash = hash;
    newEntry.depth = (int8_t)depth;
    newEntry.generation = generation;
    newEntry.score = scoreToTT(score, ply);
    newEntry.bestMove = bestMove;
    newEntry.nodeType = nodeType;
    
    // Check if we should replace the existing entry
    if (entry.hash != 0 && entry.hash != hash) {
        collisions++;
    }
    if (shouldReplace(entry, newEntry)) {
        entry = newEntry;
    }
}

void TranspositionTable::clear() {
    for (auto& entry : table) {
        entry = TTEntry{};
    }
    generation = 0;
    clearStats();
}

void TranspositionTable::resize(size_t sizeMB) {
    tableSize = sizeMB * ENTRIES_PER_MB;
    table.clear();
    table.resize(tableSize);
    clearStats();
    
    // stderr, not stdout: in UCI mode stdout carries the protocol and any
    // stray line on it desynchronises the GUI.
    std::cerr << "Transposition table resized to " << sizeMB << "MB ("
              << tableSize << " entries)" << std::endl;
}

double TranspositionTable::getHitRate() const {
    uint64_t total = hits + misses;
    return total > 0 ? (double)hits / total : 0.0;
}

void TranspositionTable::clearStats() {
    hits = misses = collisions = 0;
}

void TranspositionTable::printStats() const {
    uint64_t total = hits + misses;
    
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

bool TranspositionTable::shouldReplace(const TTEntry& existing, const TTEntry& newEntry) const {
    // Always replace empty entries
    if (existing.hash == 0) {
        return true;
    }
    
    // Same position: keep the deeper result; at equal depth prefer the newer
    // entry (fresher bounds and best move). This stops a depth-0 quiescence
    // store from evicting a deep search result for the same position.
    if (existing.hash == newEntry.hash) {
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
