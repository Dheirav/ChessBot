#pragma once
//
// Phase B5 of the bitboard replacement (docs/BITBOARD-REPLACEMENT.md).
//
// Move ordering on BitboardMove. Same tables, same bands, same scores as
// MoveOrderer, because the search tree is shaped more by the order moves are
// tried in than by anything else, and the acceptance test is an exact node
// count.
//
// The one thing that had to change on the mailbox side to make that test
// possible is MoveOrderer::tieKey: both sorts now break ties on the move rather
// than leaving them to std::sort, so two implementations whose generators emit
// in different orders still produce the same sorted sequence. That change is
// inert on the existing engine -- bench is identical at depths 5 through 9,
// because the mailbox generator already emitted in ascending from-square order,
// which is the order tieKey specifies.
//
#include "bb_movegen.hpp"
#include "move_ordering.hpp"
#include <array>

class BBMoveOrderer {
public:
    static constexpr int MAX_DEPTH = MoveOrderer::MAX_DEPTH;
    static constexpr int MAX_KILLER_MOVES = MoveOrderer::MAX_KILLER_MOVES;
    static constexpr int HISTORY_MAX = MoveOrderer::HISTORY_MAX;
    static constexpr int PIECE_CODES = MoveOrderer::PIECE_CODES;
    static constexpr size_t CONT_HIST_SIZE = MoveOrderer::CONT_HIST_SIZE;
    static constexpr size_t CAPT_HIST_SIZE = MoveOrderer::CAPT_HIST_SIZE;
    static constexpr int CAPT_HIST_SHIFT = MoveOrderer::CAPT_HIST_SHIFT;
    static constexpr size_t MAX_ORDERED_MOVES = MoveOrderer::MAX_ORDERED_MOVES;

    // A null move: from and to both zero is not a legal move, so it cannot
    // match anything generated. Stands in for Move() at the root and after a
    // null move.
    static BitboardMove none() { return BitboardMove{}; }
    static bool isNone(const BitboardMove& m) { return m.from == 0 && m.to == 0; }

    BBMoveOrderer();
    void clear();

    void orderMoves(BBMoveList& moves, const Position& pos, int depth,
                    BitboardMove ttMove = none(),
                    const BitboardMove* prevMove = nullptr) const;

    void updateKillerMove(const BitboardMove& move, int depth);

    // `us` is the colour that played `move`. MoveOrderer reads it off
    // Move::movedPiece; BitboardMove does not carry a colour, so the search
    // passes it. The continuation table is indexed by both a reply and the move
    // it answers, which are played by opposite sides, so getting this wrong
    // would index the table consistently and wrongly -- the kind of bug that
    // shows up as a feature simply not working.
    void updateHistory(const BitboardMove& move, int depth, BitboardColor us,
                       const BitboardMove* prevMove = nullptr);
    void penaliseHistory(const BitboardMove& move, int depth, BitboardColor us,
                         const BitboardMove* prevMove = nullptr);
    void updateCaptureHistory(const BitboardMove& move, int depth, BitboardColor us);
    void ageHistory();

    int quietHistory(const BitboardMove& move) const;

    // See the note on MoveOrderer::captureHistoryScore.
    int captureHistoryScore(const BitboardMove& move, BitboardColor us) const {
        return getCaptHistScore(move, us);
    }

    // Mirrors MoveOrderer::tieKey exactly, including the field widths, so the
    // two sorts agree on which of two equally scored moves comes first.
    static int tieKey(const BitboardMove& m) {
        const int promo = (m.flag == BBM_PROMOTION)
            ? (int)toMailboxType(m.promotionType) : (int)NONE;
        return ((int)m.from << 9) | ((int)m.to << 3) | promo;
    }

private:
    std::array<std::array<BitboardMove, MAX_KILLER_MOVES>, MAX_DEPTH> killerMoves;
    std::array<std::array<int, 64>, 64> historyTable;
    std::array<int, CONT_HIST_SIZE> contHistory;
    std::array<int, CAPT_HIST_SIZE> captureHistory;

    int getMVVLVAScore(const BitboardMove& move) const;
    int getMoveScore(const BitboardMove& move, const Position& pos, int depth,
                     const BitboardMove& ttMove, const BitboardMove* prevMove) const;
    bool isKillerMove(const BitboardMove& move, int depth) const;
    int getHistoryScore(const BitboardMove& move) const;

    static int pieceCode(BitboardPieceType t, BitboardColor c);
    static long contIndex(const BitboardMove& prevMove, const BitboardMove& move,
                          BitboardColor us);
    static long captIndex(const BitboardMove& move, BitboardColor us);

    int getContHistScore(const BitboardMove& move, const BitboardMove* prevMove,
                         BitboardColor us) const;
    int getCaptHistScore(const BitboardMove& move, BitboardColor us) const;
};
