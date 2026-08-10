#include "move_ordering.hpp"
#include "board.hpp"
#include "piece.hpp"
#include <algorithm>
#include <limits>

// Global move orderer instance
// Global mutable search state (killer moves / history table). Not
// synchronized: safe only because every search in the process runs under
// ChessBotEngine::ttMutex. Concurrent searches from multiple engine
// instances would race on this and need per-search state instead.
MoveOrderer g_moveOrderer;

// Piece values for MVV-LVA
static const int PIECE_VALUES[7] = {
    0,     // NONE
    20000, // KING (should never be captured, but high value for safety)
    100,   // PAWN
    320,   // KNIGHT
    330,   // BISHOP
    500,   // ROOK
    900    // QUEEN
};

MoveOrderer::MoveOrderer() {
    clear();
}

void MoveOrderer::clear() {
    // Clear killer moves
    for (int depth = 0; depth < MAX_DEPTH; ++depth) {
        for (int i = 0; i < MAX_KILLER_MOVES; ++i) {
            killerMoves[depth][i] = Move(); // Invalid move
        }
    }
    
    // Clear history table
    for (int from = 0; from < 64; ++from) {
        for (int to = 0; to < 64; ++to) {
            historyTable[from][to] = 0;
        }
    }
}

void MoveOrderer::orderMoves(MoveList& moves, const Board& board, int depth, Move ttMove) const {
    // Sort moves by priority score (highest first)
    std::sort(moves.begin(), moves.end(), [&](const Move& a, const Move& b) {
        return getMoveScore(a, board, depth, ttMove) > getMoveScore(b, board, depth, ttMove);
    });
}

void MoveOrderer::updateKillerMove(const Move& move, int depth) {
    if (depth >= MAX_DEPTH || depth < 0) return;
    
    // Don't store captures as killer moves (they have their own ordering)
    if (move.capturedPiece.type() != NONE) return;
    
    // Check if this move is already a killer move at this depth
    for (int i = 0; i < MAX_KILLER_MOVES; ++i) {
        if (killerMoves[depth][i] == move) {
            return; // Already stored
        }
    }
    
    // Shift existing killer moves and add new one at the front
    for (int i = MAX_KILLER_MOVES - 1; i > 0; --i) {
        killerMoves[depth][i] = killerMoves[depth][i - 1];
    }
    killerMoves[depth][0] = move;
}

void MoveOrderer::updateHistory(const Move& move, int depth) {
    if (move.from >= 64 || move.to >= 64 || depth <= 0) return;
    
    // Don't update history for captures (they have their own ordering)
    if (move.capturedPiece.type() != NONE) return;
    
    // Increment history score (bonus based on depth)
    int bonus = depth * depth;
    historyTable[move.from][move.to] += bonus;
    
    // Prevent overflow
    if (historyTable[move.from][move.to] > HISTORY_MAX) {
        ageHistory();
    }
}

void MoveOrderer::ageHistory() {
    // Divide all history scores by 2 to prevent overflow
    for (int from = 0; from < 64; ++from) {
        for (int to = 0; to < 64; ++to) {
            historyTable[from][to] /= 2;
        }
    }
}

int MoveOrderer::getMVVLVAScore(const Move& move, const Board& board) const {
    if (move.capturedPiece.type() == NONE) {
        return 0; // Not a capture
    }
    
    // Get the piece being moved
    Piece movingPiece = board.squares[move.from];
    
    // MVV-LVA: Most Valuable Victim - Least Valuable Attacker
    // Higher score = better move
    int victimValue = PIECE_VALUES[move.capturedPiece.type()];
    int attackerValue = PIECE_VALUES[movingPiece.type()];
    
    // Scale victim value higher and subtract attacker value
    return victimValue * 10 - attackerValue;
}

int MoveOrderer::getMoveScore(const Move& move, const Board& board, int depth, Move ttMove) const {
    // 1. Transposition table move (highest priority)
    if (move == ttMove) {
        return 1000000;
    }
    
    // 2. Captures (MVV-LVA ordering)
    if (move.capturedPiece.type() != NONE) {
        return 900000 + getMVVLVAScore(move, board);
    }
    
    // 3. Promotions
    if (move.promotionPiece.type() != NONE) {
        return 800000 + PIECE_VALUES[move.promotionPiece.type()];
    }
    
    // 4. Killer moves
    if (isKillerMove(move, depth)) {
        // First killer move gets higher priority than second
        if (depth < MAX_DEPTH && killerMoves[depth][0] == move) {
            return 700000;
        } else {
            return 690000;
        }
    }
    
    // 5. History heuristic
    return getHistoryScore(move);
}

bool MoveOrderer::isKillerMove(const Move& move, int depth) const {
    if (depth >= MAX_DEPTH || depth < 0) return false;
    
    for (int i = 0; i < MAX_KILLER_MOVES; ++i) {
        if (killerMoves[depth][i] == move) {
            return true;
        }
    }
    return false;
}

int MoveOrderer::getHistoryScore(const Move& move) const {
    if (move.from >= 64 || move.to >= 64) return 0;
    return historyTable[move.from][move.to];
}
