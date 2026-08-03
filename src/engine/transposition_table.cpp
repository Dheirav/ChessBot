#include "transposition_table.hpp"
#include <iostream>
#include <iomanip>

TranspositionTable::TranspositionTable(size_t sizeMB) {
    resize(sizeMB);
}

bool TranspositionTable::probe(uint64_t hash, int depth, int alpha, int beta, 
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
    // Check if we can use the score
    if (entry.canUseScore(depth, alpha, beta)) {
        score = entry.score;
        return true;
    }
    // Hash hit but can't use score (wrong depth/bounds)
    return false;
}

void TranspositionTable::store(uint64_t hash, int depth, int score, Move bestMove, 
                              TTEntry::NodeType nodeType) {
    size_t index = getIndex(hash);
    TTEntry& entry = table[index];
    
    // Create new entry
    TTEntry newEntry;
    newEntry.hash = hash;
    newEntry.depth = depth;
    newEntry.score = score;
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
    clearStats();
}

void TranspositionTable::resize(size_t sizeMB) {
    tableSize = sizeMB * ENTRIES_PER_MB;
    table.clear();
    table.resize(tableSize);
    clearStats();
    
    std::cout << "Transposition table resized to " << sizeMB << "MB (" 
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
    
    std::cout << "=== Transposition Table Stats ===" << std::endl;
    std::cout << "Size: " << getSizeMB() << "MB (" << tableSize << " entries)" << std::endl;
    std::cout << "Hits: " << hits << std::endl;
    std::cout << "Misses: " << misses << std::endl;
    std::cout << "Total lookups: " << total << std::endl;
    std::cout << "Hit rate: " << std::fixed << std::setprecision(2) 
              << (getHitRate() * 100) << "%" << std::endl;
    std::cout << "Collisions: " << collisions << std::endl;
    std::cout << "=================================" << std::endl;
}

bool TranspositionTable::shouldReplace(const TTEntry& existing, const TTEntry& newEntry) const {
    // Always replace empty entries
    if (existing.hash == 0) {
        return true;
    }
    
    // Replace if same position (hash collision is very unlikely)
    if (existing.hash == newEntry.hash) {
        return true;
    }
    
    // Replace if new entry has greater depth
    if (newEntry.depth > existing.depth) {
        return true;
    }
    
    // Keep existing entry if it has greater depth
    return false;
}
