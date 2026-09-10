#include "bb_move_ordering.hpp"
#include "bb_see.hpp"
#include "search.hpp"
#include <algorithm>

namespace {

// Move ordering's own values, matching move_ordering.cpp's PIECE_VALUES, which
// are indexed by mailbox PieceType and are not the evaluation's.
constexpr int PIECE_VALUES[7] = { 0, 20000, 100, 320, 330, 500, 900 };

// BitboardMove carries no colour: the mover is whoever was to move. The
// continuation table is indexed by both the reply and the move it answers, and
// those are made by opposite sides, so the caller has to say which is which.
inline BitboardColor other(BitboardColor c) {
    return (c == BB_WHITE) ? BB_BLACK : BB_WHITE;
}

// The mailbox flag this move would carry, so killer comparison means the same
// thing on both sides. BBM_DOUBLE_PUSH has no mailbox counterpart and is a
// NORMAL move there.
inline MoveFlag mailboxFlag(const BitboardMove& m) {
    switch (m.flag) {
        case BBM_CAPTURE:    return CAPTURE;
        case BBM_PROMOTION:  return PROMOTION;
        case BBM_EN_PASSANT: return EN_PASSANT;
        case BBM_CASTLE:     return CASTLING;
        default:             return NORMAL;
    }
}

// Move::operator== ignores the captured piece and compares the promotion piece
// only for promotions, because it matches a remembered move against a freshly
// generated one. Same rule here.
inline bool sameMove(const BitboardMove& a, const BitboardMove& b) {
    if (a.from != b.from || a.to != b.to) return false;
    if (mailboxFlag(a) != mailboxFlag(b)) return false;
    return mailboxFlag(a) != PROMOTION || a.promotionType == b.promotionType;
}

} // namespace

BBMoveOrderer::BBMoveOrderer() { clear(); }

void BBMoveOrderer::clear() {
    for (int d = 0; d < MAX_DEPTH; ++d)
        for (int i = 0; i < MAX_KILLER_MOVES; ++i)
            killerMoves[d][i] = none();
    for (int from = 0; from < 64; ++from)
        for (int to = 0; to < 64; ++to)
            historyTable[from][to] = 0;
    if (g_searchOptions.contHist) contHistory.fill(0);
    if (g_searchOptions.captHist) captureHistory.fill(0);
}

int BBMoveOrderer::pieceCode(BitboardPieceType t, BitboardColor c) {
    if (t == BB_NONE) return -1;
    // Mailbox numbering, because these index the same tables MoveOrderer builds
    // and a different numbering would be a different table.
    return (c == BB_BLACK ? 6 : 0) + (int)toMailboxType(t) - 1;
}

long BBMoveOrderer::contIndex(const BitboardMove& prevMove, const BitboardMove& move,
                              BitboardColor us) {
    const int prevPiece = pieceCode(prevMove.moved, other(us));
    const int piece     = pieceCode(move.moved, us);
    if (prevPiece < 0 || piece < 0) return -1;
    return (((long)prevPiece * 64 + prevMove.to) * (PIECE_CODES * 64))
         + (long)piece * 64 + move.to;
}

long BBMoveOrderer::captIndex(const BitboardMove& move, BitboardColor us) {
    const int piece  = pieceCode(move.moved, us);
    const int victim = (move.captured == BB_NONE) ? (int)NONE
                                                  : (int)toMailboxType(move.captured);
    if (piece < 0 || victim == NONE) return -1;
    return ((long)piece * 64 + move.to) * 7 + victim;
}

int BBMoveOrderer::getContHistScore(const BitboardMove& move,
                                    const BitboardMove* prevMove,
                                    BitboardColor us) const {
    if (!g_searchOptions.contHist || prevMove == nullptr) return 0;
    const long idx = contIndex(*prevMove, move, us);
    return idx < 0 ? 0 : contHistory[(size_t)idx];
}

int BBMoveOrderer::getCaptHistScore(const BitboardMove& move, BitboardColor us) const {
    if (!g_searchOptions.captHist) return 0;
    const long idx = captIndex(move, us);
    return idx < 0 ? 0 : captureHistory[(size_t)idx] >> CAPT_HIST_SHIFT;
}

int BBMoveOrderer::getMVVLVAScore(const BitboardMove& move) const {
    if (move.captured == BB_NONE) return 0;
    // The attacker is the move's own piece rather than a board lookup, which is
    // what the mailbox version reads out of board.squares[move.from].
    return PIECE_VALUES[toMailboxType(move.captured)] * 10
         - PIECE_VALUES[toMailboxType(move.moved)];
}

bool BBMoveOrderer::isKillerMove(const BitboardMove& move, int depth) const {
    if (depth >= MAX_DEPTH || depth < 0) return false;
    for (int i = 0; i < MAX_KILLER_MOVES; ++i)
        if (sameMove(killerMoves[depth][i], move)) return true;
    return false;
}

int BBMoveOrderer::getHistoryScore(const BitboardMove& move) const {
    return historyTable[move.from][move.to];
}

int BBMoveOrderer::quietHistory(const BitboardMove& move) const {
    return historyTable[move.from][move.to];
}

int BBMoveOrderer::getMoveScore(const BitboardMove& move, const Position& pos,
                                int depth, const BitboardMove& ttMove,
                                const BitboardMove* prevMove) const {
    const BitboardColor us = pos.sideToMove;

    if (!isNone(ttMove) && sameMove(move, ttMove)) return 1000000;

    if (move.captured != BB_NONE) {
        const int mvvlva = getMVVLVAScore(move);
        const int ch = getCaptHistScore(move, us);
        if (g_searchOptions.seeOrdering && bbSee(pos, move) < 0)
            return 600000 + mvvlva + ch;   // losing: below the killers
        return 900000 + mvvlva + ch;
    }

    if (move.flag == BBM_PROMOTION)
        return 800000 + PIECE_VALUES[toMailboxType(move.promotionType)];

    if (isKillerMove(move, depth)) {
        if (depth < MAX_DEPTH && sameMove(killerMoves[depth][0], move)) return 700000;
        return 690000;
    }

    return getHistoryScore(move) + getContHistScore(move, prevMove, us);
}

void BBMoveOrderer::orderMoves(BBMoveList& moves, const Position& pos, int depth,
                               BitboardMove ttMove, const BitboardMove* prevMove) const {
    const size_t count = std::min((size_t)moves.size(), MAX_ORDERED_MOVES);
    if (count < 2) return;

    struct Scored { int score; int tie; BitboardMove move; };
    Scored scored[MAX_ORDERED_MOVES];
    for (size_t i = 0; i < count; ++i)
        scored[i] = Scored{getMoveScore(moves[i], pos, depth, ttMove, prevMove),
                           tieKey(moves[i]), moves[i]};

    const bool tie = g_searchOptions.orderTieBreak;
    std::sort(scored, scored + count, [tie](const Scored& a, const Scored& b) {
        if (a.score != b.score) return a.score > b.score;
        return tie && a.tie < b.tie;
    });

    for (size_t i = 0; i < count; ++i) moves[i] = scored[i].move;
}

void BBMoveOrderer::updateKillerMove(const BitboardMove& move, int depth) {
    if (depth >= MAX_DEPTH || depth < 0) return;
    if (move.captured != BB_NONE) return;
    for (int i = 0; i < MAX_KILLER_MOVES; ++i)
        if (sameMove(killerMoves[depth][i], move)) return;
    for (int i = MAX_KILLER_MOVES - 1; i > 0; --i)
        killerMoves[depth][i] = killerMoves[depth][i - 1];
    killerMoves[depth][0] = move;
}

void BBMoveOrderer::updateHistory(const BitboardMove& move, int depth,
                                  BitboardColor us, const BitboardMove* prevMove) {
    if (depth <= 0) return;
    if (move.captured != BB_NONE) return;

    const int bonus = depth * depth;
    historyTable[move.from][move.to] += bonus;
    if (historyTable[move.from][move.to] > HISTORY_MAX) ageHistory();

    // Gravity rather than a clamp, for the reason move_ordering.cpp records:
    // a clamped table beside an aged one drifts apart, and this one is 2.4 MB
    // and cannot afford the halving pass.
    if (g_searchOptions.contHist && prevMove != nullptr) {
        const long idx = contIndex(*prevMove, move, us);
        if (idx >= 0) {
            int& entry = contHistory[(size_t)idx];
            entry += bonus - (entry * bonus) / HISTORY_MAX;
        }
    }
}

void BBMoveOrderer::penaliseHistory(const BitboardMove& move, int depth,
                                    BitboardColor us, const BitboardMove* prevMove) {
    if (!g_searchOptions.histMalus) return;
    if (depth <= 0) return;
    if (move.captured != BB_NONE) return;

    // Same magnitude as the bonus and the same gravity, so a move that cuts as
    // often as it fails settles near zero rather than drifting.
    const int malus = depth * depth;
    int& e = historyTable[move.from][move.to];
    e -= malus - (e * malus) / HISTORY_MAX;

    if (g_searchOptions.contHist && prevMove != nullptr) {
        const long idx = contIndex(*prevMove, move, us);
        if (idx >= 0) {
            int& ce = contHistory[(size_t)idx];
            ce -= malus - (ce * malus) / HISTORY_MAX;
        }
    }
}

void BBMoveOrderer::updateCaptureHistory(const BitboardMove& move, int depth,
                                         BitboardColor us) {
    if (!g_searchOptions.captHist || depth <= 0) return;
    if (move.captured == BB_NONE) return;
    const long idx = captIndex(move, us);
    if (idx < 0) return;
    int& entry = captureHistory[(size_t)idx];
    const int bonus = depth * depth;
    entry += bonus - (entry * bonus) / HISTORY_MAX;
}

void BBMoveOrderer::ageHistory() {
    for (int from = 0; from < 64; ++from)
        for (int to = 0; to < 64; ++to)
            historyTable[from][to] /= 2;
}
