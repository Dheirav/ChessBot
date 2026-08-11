#pragma once
#include "move.hpp"
#include "board.hpp"
#include <vector>
#include <array>

/**
 * Move ordering utilities for improving search efficiency
 * Implements killer moves, history heuristic, and MVV-LVA
 */
class MoveOrderer {
public:
    static constexpr int MAX_DEPTH = 64;
    static constexpr int MAX_KILLER_MOVES = 2;
    static constexpr int HISTORY_MAX = 16384; // 2^14 for scaling

    // Upper bound on moves ordered in one call. No legal chess position has
    // more than 218 legal moves; the buffer is stack-allocated, so this caps
    // it rather than allocating per node.
    static constexpr size_t MAX_ORDERED_MOVES = 256;
    
    MoveOrderer();
    
    // Reset all move ordering data
    void clear();
    
    // Order moves for a given position and depth
    void orderMoves(MoveList& moves, const Board& board, int depth, Move ttMove = Move()) const;
    
    // Update killer moves when a move causes a cutoff
    void updateKillerMove(const Move& move, int depth);
    
    // Update history heuristic when a move causes a cutoff
    void updateHistory(const Move& move, int depth);
    
    // Reduce history scores periodically to prevent overflow
    void ageHistory();
    
private:
    // Killer moves: moves that caused beta cutoffs at each depth
    std::array<std::array<Move, MAX_KILLER_MOVES>, MAX_DEPTH> killerMoves;
    
    // History heuristic: [from][to] -> score
    std::array<std::array<int, 64>, 64> historyTable;
    
    // MVV-LVA (Most Valuable Victim - Least Valuable Attacker) scoring
    int getMVVLVAScore(const Move& move, const Board& board) const;
    
    // Get move priority for ordering
    int getMoveScore(const Move& move, const Board& board, int depth, Move ttMove) const;
    
    // Check if move is a killer move at given depth
    bool isKillerMove(const Move& move, int depth) const;
    
    // Get history score for a move
    int getHistoryScore(const Move& move) const;
};

// Global move orderer instance
extern MoveOrderer g_moveOrderer;
