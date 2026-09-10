#include "bb_see.hpp"
#include "bitboard_attacks.hpp"
#include <algorithm>

namespace {

// Deliberately SEE's own values, matching see.cpp exactly. They measure
// material on one square, not position, so they are not the evaluation's.
constexpr int SEE_VALUE[7] = { 100, 320, 330, 500, 900, 20000, 0 };

// The cheapest attacker of `side` still standing, as a bit. Ordered by value
// so the exchange always recaptures with the least it can afford to lose,
// which is what makes the sequence the one both sides would actually play.
inline Bitboard leastValuable(const Position& pos, Bitboard attackers,
                              BitboardColor side, BitboardPieceType& typeOut) {
    const auto& theirs = (side == BB_WHITE) ? pos.white : pos.black;
    for (int t = BB_PAWN; t <= BB_KING; ++t) {
        const Bitboard set = attackers & theirs[t];
        if (set) { typeOut = (BitboardPieceType)t; return set & (~set + 1); }
    }
    typeOut = BB_NONE;
    return 0;
}

} // namespace

int bbSee(const Position& pos, const BitboardMove& move) {
    const int target = move.to;
    const BitboardColor us = pos.sideToMove;
    const BitboardColor them = (us == BB_WHITE) ? BB_BLACK : BB_WHITE;

    Bitboard occ = pos.occupancyAll;
    int captured;

    if (move.flag == BBM_EN_PASSANT) {
        // The captured pawn is beside the mover, not on the destination.
        const int capSq = (move.from / 8) * 8 + (move.to % 8);
        occ &= ~(1ULL << capSq);
        captured = SEE_VALUE[BB_PAWN];
    } else {
        captured = SEE_VALUE[move.captured == BB_NONE ? BB_NONE : move.captured];
    }

    // What stands on the target afterwards is what the opponent stands to win.
    // For a promotion that is the new piece, and the promotion gains the
    // difference on top of whatever was taken.
    BitboardPieceType standing = move.moved;
    if (move.flag == BBM_PROMOTION) {
        standing = move.promotionType;
        captured += SEE_VALUE[standing] - SEE_VALUE[BB_PAWN];
    }
    occ &= ~(1ULL << move.from);

    int gain[36];
    int d = 0;
    gain[0] = captured;
    BitboardColor side = them;

    while (true) {
        // Recomputed against the shrinking occupancy rather than maintained,
        // which is what rediscovers a slider that was hidden behind the piece
        // just spent. Masking by occ drops attackers already in the exchange,
        // whose bits are still set in the piece boards.
        const Bitboard attackers = attackersTo(pos, target, occ, side) & occ;
        BitboardPieceType attackerType;
        const Bitboard from = leastValuable(pos, attackers, side, attackerType);
        if (!from) break;

        ++d;
        gain[d] = SEE_VALUE[standing] - gain[d - 1];

        // Neither side continues an exchange that loses even in the best case,
        // so once the running score and its refutation are both negative the
        // rest cannot change the answer.
        if (std::max(-gain[d - 1], gain[d]) < 0) break;

        standing = attackerType;
        occ &= ~from;
        side = (side == BB_WHITE) ? BB_BLACK : BB_WHITE;
        if (d >= 33) break;
    }

    // Resolve backwards: at each step the side to move takes the exchange only
    // if it beats standing pat. Runs down to and including d == 1, which is the
    // single recapture and the whole answer in the commonest case of all.
    while (d > 0) {
        gain[d - 1] = -std::max(-gain[d - 1], gain[d]);
        --d;
    }
    return gain[0];
}
