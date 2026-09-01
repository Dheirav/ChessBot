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

    // Once per search, not once per node -- but 2.4 MB is worth not paying for
    // when the feature is off, which matters because tests/match runs both
    // sides of a gate in one process.
    if (g_searchOptions.contHist) contHistory.fill(0);
    if (g_searchOptions.captHist) captureHistory.fill(0);
}

int MoveOrderer::pieceCode(const Piece& p) {
    const PieceType t = p.type();
    if (t == NONE) return -1;
    return (p.color() == COLOR_BLACK ? 6 : 0) + (int)t - 1;
}

long MoveOrderer::contIndex(const Move& prevMove, const Move& move) {
    const int prevPiece = pieceCode(prevMove.movedPiece);
    const int piece     = pieceCode(move.movedPiece);
    if (prevPiece < 0 || piece < 0) return -1;
    if (prevMove.to < 0 || prevMove.to >= 64 || move.to < 0 || move.to >= 64) return -1;
    return (((long)prevPiece * 64 + prevMove.to) * (PIECE_CODES * 64))
         + (long)piece * 64 + move.to;
}

long MoveOrderer::captIndex(const Move& move) {
    const int piece  = pieceCode(move.movedPiece);
    const int victim = (int)move.capturedPiece.type();
    if (piece < 0 || victim == NONE) return -1;
    if (move.to < 0 || move.to >= 64) return -1;
    return ((long)piece * 64 + move.to) * 7 + victim;
}

int MoveOrderer::getContHistScore(const Move& move, const Move* prevMove) const {
    if (!g_searchOptions.contHist || prevMove == nullptr) return 0;
    const long idx = contIndex(*prevMove, move);
    return idx < 0 ? 0 : contHistory[(size_t)idx];
}

int MoveOrderer::getCaptHistScore(const Move& move) const {
    if (!g_searchOptions.captHist) return 0;
    const long idx = captIndex(move);
    return idx < 0 ? 0 : captureHistory[(size_t)idx] >> CAPT_HIST_SHIFT;
}

void MoveOrderer::orderMoves(MoveList& moves, const Board& board, int depth,
                             Move ttMove, const Move* prevMove) const {
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
        scored[i] = ScoredMove{getMoveScore(moves[i], board, depth, ttMove, prevMove),
                               moves[i]};
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

void MoveOrderer::updateHistory(const Move& move, int depth, const Move* prevMove) {
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

    // Same bonus into the continuation table, but applied with gravity rather
    // than clamped -- and the difference is not cosmetic.
    //
    // historyTable halves *everything* when one entry overflows (ageHistory).
    // A clamped table next to an aged one drifts apart: continuation scores
    // pile up at the ceiling and stay there while butterfly scores are
    // repeatedly cut in half, so saturated entries become indistinguishable
    // from each other and outrank every aged move. The first bench of this
    // showed exactly that -- +12.2% nodes, with one position up 81%.
    //
    // Gravity is the standard answer and needs no aging pass: the increment
    // shrinks as the entry grows, so it approaches HISTORY_MAX without ever
    // reaching it and keeps its resolution near the top of the range. That
    // also spares the 2.4 MB halving this table could not afford.
    if (g_searchOptions.contHist && prevMove != nullptr) {
        const long idx = contIndex(*prevMove, move);
        if (idx >= 0) {
            int& entry = contHistory[(size_t)idx];
            entry += bonus - (entry * bonus) / HISTORY_MAX;
        }
    }
}

void MoveOrderer::updateCaptureHistory(const Move& move, int depth) {
    if (!g_searchOptions.captHist || depth <= 0) return;
    if (move.capturedPiece.type() == NONE) return;
    const long idx = captIndex(move);
    if (idx < 0) return;
    // Gravity, for the same reason as the continuation table above.
    int& entry = captureHistory[(size_t)idx];
    const int bonus = depth * depth;
    entry += bonus - (entry * bonus) / HISTORY_MAX;
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

int MoveOrderer::getMoveScore(const Move& move, const Board& board, int depth,
                              Move ttMove, const Move* prevMove) const {
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
    // Capture history is added *inside* whichever band SEE chose, never across
    // it: MVV-LVA tops out at 9 000 and the shifted history at 4 096, against
    // bands 300 000 apart. So it reorders sound captures among themselves and
    // losing captures among themselves, and cannot promote a losing capture
    // above a sound one -- which is the ordering SEE was gated to produce.
    if (move.capturedPiece.type() != NONE) {
        int mvvlva = getMVVLVAScore(move, board);
        const int ch = getCaptHistScore(move);
        if (g_searchOptions.seeOrdering && see(board, move) < 0) {
            return 600000 + mvvlva + ch;  // losing: below the killers
        }
        return 900000 + mvvlva + ch;
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
    
    // 5. History heuristic, plus the continuation table.
    //
    // Summed rather than ranked: they answer different questions -- "has this
    // move been good lately" and "has it been good *after that* move" -- and a
    // move both tables like should outrank one only either likes. Both cap at
    // 16 384, so the pair stays far below the killer band at 690 000.
    return getHistoryScore(move) + getContHistScore(move, prevMove);
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
