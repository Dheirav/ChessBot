#include "bb_check.hpp"
#include "bitboard_attacks.hpp"

CheckInfo bbCheckInfo(const Position& pos) {
    CheckInfo ci{};
    const BitboardColor us = pos.sideToMove;
    const BitboardColor them = (us == BB_WHITE) ? BB_BLACK : BB_WHITE;
    const int ksq = kingSquare(pos, them);
    ci.theirKing = ksq;
    if (ksq < 0) return ci;

    const Bitboard occ = pos.occupancyAll;
    // A pawn of ours checks their king from the squares their king "attacks as
    // a pawn of our colour" -- the one asymmetric case, and the one that is
    // easy to get backwards.
    ci.checkSquares[BB_PAWN]   = pawnAttacks(them, ksq);
    ci.checkSquares[BB_KNIGHT] = knightAttacks(ksq);
    ci.checkSquares[BB_BISHOP] = bishopAttacks(ksq, occ);
    ci.checkSquares[BB_ROOK]   = rookAttacks(ksq, occ);
    ci.checkSquares[BB_QUEEN]  = ci.checkSquares[BB_BISHOP] | ci.checkSquares[BB_ROOK];
    ci.checkSquares[BB_KING]   = 0;

    // Our pieces that block one of *our own* sliders from their king: move one
    // and the slider behind it gives check.
    //
    // blockersForKing() cannot answer this. It returns the pieces *of the side
    // it is asked about* that block enemy sliders, so blockersForKing(them)
    // gives their pieces, and intersecting that with our occupancy is always
    // empty. The first version of this did exactly that and silently reported
    // "no check" for every discovered check on the board, which tests/bbequiv
    // caught as 365 false negatives and zero false positives.
    const Bitboard ourOcc = (us == BB_WHITE) ? pos.occupancyWhite : pos.occupancyBlack;
    const auto& ours = (us == BB_WHITE) ? pos.white : pos.black;
    Bitboard snipers = (rookAttacks(ksq, 0)   & (ours[BB_ROOK]   | ours[BB_QUEEN]))
                     | (bishopAttacks(ksq, 0) & (ours[BB_BISHOP] | ours[BB_QUEEN]));
    while (snipers) {
        const int s = lsb(snipers);
        snipers &= snipers - 1;
        const Bitboard between = betweenSquares(ksq, s) & occ;
        // Exactly one piece in the way, and it is ours.
        if (between && !(between & (between - 1)) && (between & ourOcc))
            ci.blockers |= between;
    }
    return ci;
}

bool bbGivesCheck(const Position& pos, const CheckInfo& ci, const BitboardMove& move) {
    if (ci.theirKing < 0) return false;
    const Bitboard toBit = 1ULL << move.to;

    // Direct check by whatever piece arrives on the destination square, which
    // for a promotion is the promoted piece rather than the pawn.
    const BitboardPieceType arriving =
        (move.flag == BBM_PROMOTION) ? move.promotionType : move.moved;
    if (arriving <= BB_QUEEN && (ci.checkSquares[arriving] & toBit)) return true;

    // A promotion needs its own test, because the check squares were computed
    // with the pawn still standing on its origin square. A pawn on e2 with the
    // enemy king on e3 blocks the e-file, so e1 is not in the queen mask, yet
    // promoting to a queen on e1 checks along the file the pawn just vacated.
    // Stockfish gives promotions the same special case for the same reason.
    if (move.flag == BBM_PROMOTION) {
        const Bitboard occ = (pos.occupancyAll & ~(1ULL << move.from)) | toBit;
        const Bitboard kingBit = 1ULL << ci.theirKing;
        switch (move.promotionType) {
            case BB_QUEEN:  if (queenAttacks(move.to, occ)  & kingBit) return true; break;
            case BB_ROOK:   if (rookAttacks(move.to, occ)   & kingBit) return true; break;
            case BB_BISHOP: if (bishopAttacks(move.to, occ) & kingBit) return true; break;
            case BB_KNIGHT: if (knightAttacks(move.to)      & kingBit) return true; break;
            default: break;
        }
    }

    // Discovered check: the mover was blocking one of our sliders, and has left
    // the line it was blocking. Staying on the line blocks it still.
    if ((ci.blockers & (1ULL << move.from)) &&
        !(lineThrough(move.from, ci.theirKing) & toBit))
        return true;

    // Three moves that relocate a second piece, or a piece that is not on the
    // destination square, and so are not covered by either test above.
    if (move.flag == BBM_CASTLE) {
        // The rook lands beside the king it came from, and it is the rook that
        // can give the check.
        const bool kingSide = (move.to > move.from);
        const int rookTo = kingSide ? move.from + 1 : move.from - 1;
        Bitboard occ = pos.occupancyAll;
        occ &= ~((1ULL << move.from) | (1ULL << (kingSide ? move.from + 3 : move.from - 4)));
        occ |= toBit | (1ULL << rookTo);
        return (rookAttacks(ci.theirKing, occ) & (1ULL << rookTo)) != 0;
    }
    if (move.flag == BBM_EN_PASSANT) {
        // Two pawns leave the board and one arrives, so a rank or diagonal can
        // open that neither test above looks at. Cheap enough to answer
        // directly, and rare enough not to matter.
        const int capSq = (move.from / 8) * 8 + (move.to % 8);
        Bitboard occ = pos.occupancyAll;
        occ &= ~((1ULL << move.from) | (1ULL << capSq));
        occ |= toBit;
        const BitboardColor us = pos.sideToMove;
        const auto& ours = (us == BB_WHITE) ? pos.white : pos.black;
        const Bitboard rookLike   = ours[BB_ROOK] | ours[BB_QUEEN];
        const Bitboard bishopLike = ours[BB_BISHOP] | ours[BB_QUEEN];
        if (rookAttacks(ci.theirKing, occ) & rookLike & occ) return true;
        if (bishopAttacks(ci.theirKing, occ) & bishopLike & occ) return true;
    }
    return false;
}
