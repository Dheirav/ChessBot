#include "move_ordering.hpp"
#include "board.hpp"
#include "piece.hpp"
#include "search.hpp"
#include "see.hpp"
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
    const size_t count = std::min(moves.size(), MAX_ORDERED_MOVES);
    if (count < 2) return;

    // Score every move once, then sort the scores.
    //
    // The obvious form — scoring inside the comparator — calls getMoveScore
    // about 2·n·log(n) times instead of n. That was merely wasteful while a
    // score was a table lookup; with SEE ordering enabled a score resolves a
    // whole exchange, so it is the difference between one exchange per move and
    // roughly a dozen. Same resulting order either way: identical keys produce
    // identical comparison results, and so the identical permutation.
    struct ScoredMove {
        int score;
        Move move;
    };
    ScoredMove scored[MAX_ORDERED_MOVES];
    for (size_t i = 0; i < count; ++i) {
        scored[i] = ScoredMove{getMoveScore(moves[i], board, depth, ttMove), moves[i]};
    }

    std::sort(scored, scored + count,
              [](const ScoredMove& a, const ScoredMove& b) { return a.score > b.score; });

    for (size_t i = 0; i < count; ++i) {
        moves[i] = scored[i].move;
    }
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
    
    // 2. Captures.
    //
    // MVV-LVA ranks by what is being taken, which is right about half the time
    // and badly wrong the rest: it puts QxP ahead of every quiet move even when
    // the pawn is defended and the queen is simply lost. SEE plays the exchange
    // out and knows the difference.
    //
    // A capture that loses material therefore drops *below* the killers rather
    // than sitting near the top of the list. It is still searched — SEE is a
    // one-square approximation and can miss a pin or a deflection, so this is
    // an ordering decision, not a pruning one — but it is searched last, where
    // a wrong guess costs almost nothing.
    // SEE decides which band; MVV-LVA still orders within it. Using the
    // exchange result as the key directly is worse — most sound captures
    // resolve to 0, which collapses QxQ, RxR and PxP into one block and
    // discards the victim-value ordering that produces early cutoffs.
    if (move.capturedPiece.type() != NONE) {
        int mvvlva = getMVVLVAScore(move, board);
        if (g_searchOptions.seeOrdering && see(board, move) < 0) {
            return 600000 + mvvlva;  // losing: below the killers
        }
        return 900000 + mvvlva;
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
