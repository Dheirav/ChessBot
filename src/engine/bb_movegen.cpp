#include "bb_movegen.hpp"
#include "bitboard_attacks.hpp"
#include "search.hpp"
#include "board.hpp"   // CastlingRight

namespace {

constexpr Bitboard FILE_A = 0x0101010101010101ULL;
constexpr Bitboard FILE_H = 0x8080808080808080ULL;
constexpr Bitboard RANK_8 = 0x00000000000000FFULL;  // white promotes here
constexpr Bitboard RANK_7 = 0x000000000000FF00ULL;  // black pawns start
constexpr Bitboard RANK_2 = 0x00FF000000000000ULL;  // white pawns start
constexpr Bitboard RANK_1 = 0xFF00000000000000ULL;  // black promotes here

constexpr int SQ_A8 = 0,  SQ_E8 = 4,  SQ_H8 = 7;
constexpr int SQ_A1 = 56, SQ_E1 = 60, SQ_H1 = 63;

inline int popLsb(Bitboard& b) { const int sq = lsb(b); b &= b - 1; return sq; }

// One array read. This used to test six bitboards per capture, which is the
// question a square array exists to answer.
inline BitboardPieceType victimAt(const Position& pos, BitboardColor, int sq) {
    static constexpr BitboardPieceType FROM_MAILBOX[7] = {
        BB_NONE, BB_KING, BB_PAWN, BB_KNIGHT, BB_BISHOP, BB_ROOK, BB_QUEEN
    };
    return FROM_MAILBOX[pos.squares[sq].type()];
}

inline void add(BBMoveList& out, int from, int to, BitboardMoveFlag flag,
                BitboardPieceType moved, BitboardPieceType captured,
                BitboardPieceType promo = BB_NONE) {
    BitboardMove& m = out.moves[out.count++];
    m.from = (uint8_t)from;
    m.to = (uint8_t)to;
    m.flag = flag;
    m.moved = moved;
    m.captured = captured;
    m.promotionType = promo;
    m.isCapture = (captured != BB_NONE) || flag == BBM_EN_PASSANT;
}

// Order matters and is not cosmetic: it has to match the pseudo-legal
// generator's, because move ordering breaks ties by generation order and the
// acceptance test is an exact node count.
// `tacticalOnly` says this is the quiescence set rather than the full move list.
// Under qNoUnderpromo a quiet promotion then contributes only the queen,
// matching Stockfish's make_promotions, which files rook, bishop and knight
// promotions under QUIETS unless the promotion also captures. It must NOT apply
// to the full list: those are legal moves and dropping them loses them from the
// main search, which is what tests/bbequiv caught when this was written into
// the generator instead.
void addPromotions(BBMoveList& out, int from, int to, BitboardPieceType captured,
                   bool tacticalOnly) {
    if (tacticalOnly && g_searchOptions.qNoUnderpromo && captured == BB_NONE) {
        add(out, from, to, BBM_PROMOTION, BB_PAWN, captured, BB_QUEEN);
        return;
    }
    for (BitboardPieceType p : {BB_QUEEN, BB_ROOK, BB_BISHOP, BB_KNIGHT})
        add(out, from, to, BBM_PROMOTION, BB_PAWN, captured, p);
}

// The one move whose legality no mask can express. Playing it removes two pawns
// from the same rank at once, so a rook or queen that was blocked twice becomes
// a checker. Resolved by building the occupancy the move would produce and
// asking the question directly.
bool epIsLegal(const Position& pos, int from, int to, int ksq, BitboardColor us) {
    const BitboardColor them = (us == BB_WHITE) ? BB_BLACK : BB_WHITE;
    const int capSq = (from / 8) * 8 + (to % 8);
    Bitboard occ = pos.occupancyAll;
    occ &= ~(1ULL << from);
    occ &= ~(1ULL << capSq);
    occ |= 1ULL << to;

    const auto& theirs = (them == BB_WHITE) ? pos.white : pos.black;
    // The captured pawn is gone, so it cannot be one of the attackers even
    // though its bit is still set in the piece boards.
    const Bitboard gone = ~(1ULL << capSq);
    const Bitboard rookLike   = (theirs[BB_ROOK]   | theirs[BB_QUEEN]) & gone;
    const Bitboard bishopLike = (theirs[BB_BISHOP] | theirs[BB_QUEEN]) & gone;
    if (rookAttacks(ksq, occ) & rookLike) return false;
    if (bishopAttacks(ksq, occ) & bishopLike) return false;
    // A pawn, knight or king cannot newly attack the king as a result of this
    // move, but the mover may have been standing on a square that mattered, so
    // the non-slider attackers are still worth one cheap test.
    if (knightAttacks(ksq) & theirs[BB_KNIGHT]) return false;
    if (pawnAttacks(us, ksq) & theirs[BB_PAWN] & gone) return false;
    if (kingAttacks(ksq) & theirs[BB_KING]) return false;
    return true;
}

void generatePawns(const Position& pos, BitboardColor us, BBMoveList& out,
                   Bitboard pawns, Bitboard checkMask, Bitboard pinRay,
                   BBGenType type) {
    if (!pawns) return;
    const bool white = (us == BB_WHITE);
    const BitboardColor them = white ? BB_BLACK : BB_WHITE;
    const Bitboard theirOcc = white ? pos.occupancyBlack : pos.occupancyWhite;
    const Bitboard empty = ~pos.occupancyAll;
    const Bitboard promoRank = white ? RANK_8 : RANK_1;
    const Bitboard startRank = white ? RANK_2 : RANK_7;
    const int fwd = white ? -8 : 8;
    auto push = [&](Bitboard b) { return white ? (b >> 8) : (b << 8); };

    // pinRay is all ones for the unpinned bulk pass and the single pin line for
    // a pinned pawn, so one body serves both.
    const bool wantQuiet = (type != BB_GEN_CAPTURES);
    const bool wantCaps  = (type != BB_GEN_QUIETS);

    Bitboard single = push(pawns) & empty;
    Bitboard dbl    = push(push(pawns & startRank) & empty) & empty;

    if (wantQuiet) {
        Bitboard quiet = single & ~promoRank & checkMask & pinRay;
        while (quiet) { const int to = popLsb(quiet); add(out, to - fwd, to, BBM_NORMAL, BB_PAWN, BB_NONE); }
        Bitboard d = dbl & checkMask & pinRay;
        while (d) { const int to = popLsb(d); add(out, to - 2 * fwd, to, BBM_DOUBLE_PUSH, BB_PAWN, BB_NONE); }
    }

    // Promotions count as tactical whether or not they capture, so they are
    // emitted in the capture set rather than with the quiet pushes.
    if (wantCaps) {
        Bitboard promo = single & promoRank & checkMask & pinRay;
        while (promo) { const int to = popLsb(promo); addPromotions(out, to - fwd, to, BB_NONE, type == BB_GEN_CAPTURES); }
    }

    if (wantCaps) {
        const Bitboard capLeft  = white ? ((pawns & ~FILE_A) >> 9) : ((pawns & ~FILE_A) << 7);
        const Bitboard capRight = white ? ((pawns & ~FILE_H) >> 7) : ((pawns & ~FILE_H) << 9);
        const int deltaLeft  = white ? -9 : 7;
        const int deltaRight = white ? -7 : 9;

        for (int side = 0; side < 2; ++side) {
            Bitboard caps = (side == 0 ? capLeft : capRight) & theirOcc & checkMask & pinRay;
            const int delta = (side == 0) ? deltaLeft : deltaRight;
            while (caps) {
                const int to = popLsb(caps);
                const int from = to - delta;
                const BitboardPieceType victim = victimAt(pos, them, to);
                if ((1ULL << to) & promoRank)
                    addPromotions(out, from, to, victim, type == BB_GEN_CAPTURES);
                else add(out, from, to, BBM_CAPTURE, BB_PAWN, victim);
            }
        }
    }
}

// En passant is generated once for every pawn rather than inside the pinned and
// unpinned passes, because neither mask applies to it. Under check the legal
// answer may be to capture the checking pawn, which does not sit on the
// destination square, and a pinned pawn may still capture along its own pin
// ray. epIsLegal answers both by building the resulting occupancy.
void generateEnPassant(const Position& pos, BitboardColor us, BBMoveList& out,
                       Bitboard pawns, int ksq) {
    if (pos.enPassantSquare < 0 || ksq < 0) return;
    const bool white = (us == BB_WHITE);
    const int to = pos.enPassantSquare;
    const int tx = to % 8;
    const int fromH = white ? to + 9 : to - 7;
    const int fromA = white ? to + 7 : to - 9;
    if (tx < 7 && fromH >= 0 && fromH < 64 && (pawns & (1ULL << fromH)) &&
        epIsLegal(pos, fromH, to, ksq, us))
        add(out, fromH, to, BBM_EN_PASSANT, BB_PAWN, BB_PAWN);
    if (tx > 0 && fromA >= 0 && fromA < 64 && (pawns & (1ULL << fromA)) &&
        epIsLegal(pos, fromA, to, ksq, us))
        add(out, fromA, to, BBM_EN_PASSANT, BB_PAWN, BB_PAWN);
}

inline Bitboard attacksOf(BitboardPieceType t, int from, Bitboard occ) {
    switch (t) {
        case BB_KNIGHT: return knightAttacks(from);
        case BB_BISHOP: return bishopAttacks(from, occ);
        case BB_ROOK:   return rookAttacks(from, occ);
        case BB_QUEEN:  return queenAttacks(from, occ);
        case BB_KING:   return kingAttacks(from);
        default: return 0;
    }
}

void generatePieces(const Position& pos, BitboardColor us, BitboardPieceType t,
                    BBMoveList& out, Bitboard pieces, Bitboard target,
                    Bitboard pinned, int ksq) {
    const BitboardColor them = (us == BB_WHITE) ? BB_BLACK : BB_WHITE;
    while (pieces) {
        const int from = popLsb(pieces);
        Bitboard moves = attacksOf(t, from, pos.occupancyAll) & target;
        if (pinned & (1ULL << from)) moves &= lineThrough(ksq, from);
        while (moves) {
            const int to = popLsb(moves);
            const BitboardPieceType victim = victimAt(pos, them, to);
            add(out, from, to, victim == BB_NONE ? BBM_NORMAL : BBM_CAPTURE, t, victim);
        }
    }
}

void generateCastling(const Position& pos, BitboardColor us, BBMoveList& out) {
    const bool white = (us == BB_WHITE);
    const BitboardColor them = white ? BB_BLACK : BB_WHITE;
    const int kingFrom = white ? SQ_E1 : SQ_E8;

    const Bitboard kingBB = white ? pos.white[BB_KING] : pos.black[BB_KING];
    if (!(kingBB & (1ULL << kingFrom))) return;

    const uint8_t kingSideBit  = white ? CASTLE_WK : CASTLE_BK;
    const uint8_t queenSideBit = white ? CASTLE_WQ : CASTLE_BQ;
    const int rookKingSide  = white ? SQ_H1 : SQ_H8;
    const int rookQueenSide = white ? SQ_A1 : SQ_A8;
    const Bitboard rooks = white ? pos.white[BB_ROOK] : pos.black[BB_ROOK];

    if ((pos.castlingRights & kingSideBit) && (rooks & (1ULL << rookKingSide))) {
        const int f = kingFrom + 1, g = kingFrom + 2;
        if (!(pos.occupancyAll & ((1ULL << f) | (1ULL << g))) &&
            !isSquareAttackedBB(pos, f, them) && !isSquareAttackedBB(pos, g, them))
            add(out, kingFrom, g, BBM_CASTLE, BB_KING, BB_NONE);
    }
    if ((pos.castlingRights & queenSideBit) && (rooks & (1ULL << rookQueenSide))) {
        const int d = kingFrom - 1, c = kingFrom - 2, b = kingFrom - 3;
        if (!(pos.occupancyAll & ((1ULL << d) | (1ULL << c) | (1ULL << b))) &&
            !isSquareAttackedBB(pos, d, them) && !isSquareAttackedBB(pos, c, them))
            add(out, kingFrom, c, BBM_CASTLE, BB_KING, BB_NONE);
    }
}

} // namespace

bool bbInCheck(const Position& pos) {
    return checkers(pos, pos.sideToMove) != 0;
}

void bbGenerate(const Position& pos, BBMoveList& out, BBGenType type) {
    out.clear();

    const BitboardColor us = pos.sideToMove;
    const BitboardColor them = (us == BB_WHITE) ? BB_BLACK : BB_WHITE;
    const auto& ours = (us == BB_WHITE) ? pos.white : pos.black;
    const Bitboard ownOcc   = (us == BB_WHITE) ? pos.occupancyWhite : pos.occupancyBlack;
    const Bitboard theirOcc = (us == BB_WHITE) ? pos.occupancyBlack : pos.occupancyWhite;

    const int ksq = kingSquare(pos, us);
    if (ksq < 0) return;   // test positions may have no king

    // What a move is allowed to land on, before check and pins.
    Bitboard typeMask;
    switch (type) {
        case BB_GEN_CAPTURES: typeMask = theirOcc; break;
        case BB_GEN_QUIETS:   typeMask = ~pos.occupancyAll; break;
        default:              typeMask = ~ownOcc; break;
    }

    // The king is generated first and separately: its legality does not depend
    // on the check mask at all, only on where the enemy attacks once the king
    // has stepped off its square.
    {
        Bitboard moves = kingAttacks(ksq) & typeMask;
        const Bitboard occNoKing = pos.occupancyAll & ~(1ULL << ksq);
        while (moves) {
            const int to = popLsb(moves);
            if (attackersTo(pos, to, occNoKing, them)) continue;
            const BitboardPieceType victim = victimAt(pos, them, to);
            add(out, ksq, to, victim == BB_NONE ? BBM_NORMAL : BBM_CAPTURE, BB_KING, victim);
        }
    }

    const Bitboard checkersBB = checkers(pos, us);
    const int numCheckers = popcount(checkersBB);
    if (numCheckers > 1) return;   // double check: the king move above is all there is

    // Under a single check every other piece must either capture the checker or
    // interpose, which is exactly the checker's square plus the squares between
    // it and the king. A knight or pawn checker has nothing between, so the mask
    // collapses to "take it".
    const Bitboard checkMask = (numCheckers == 1)
        ? (checkersBB | betweenSquares(ksq, lsb(checkersBB)))
        : ~0ULL;

    const Bitboard target = typeMask & checkMask;
    const Bitboard pinned = blockersForKing(pos, us);

    generatePieces(pos, us, BB_KNIGHT, out, ours[BB_KNIGHT] & ~pinned, target, 0, ksq);
    generatePieces(pos, us, BB_BISHOP, out, ours[BB_BISHOP], target, pinned, ksq);
    generatePieces(pos, us, BB_ROOK,   out, ours[BB_ROOK],   target, pinned, ksq);
    generatePieces(pos, us, BB_QUEEN,  out, ours[BB_QUEEN],  target, pinned, ksq);
    // A pinned knight can never move: its move is always off the pin line. It is
    // dropped above rather than masked, since lineThrough would give zero anyway.

    const Bitboard pawns = ours[BB_PAWN];
    generatePawns(pos, us, out, pawns & ~pinned, checkMask, ~0ULL, type);
    Bitboard pinnedPawns = pawns & pinned;
    while (pinnedPawns) {
        const int from = popLsb(pinnedPawns);
        // One pawn at a time, because the bulk shifts cannot carry a per-pawn
        // mask. There is rarely more than one, so the loop costs nothing.
        generatePawns(pos, us, out, 1ULL << from, checkMask, lineThrough(ksq, from), type);
    }

    if (type != BB_GEN_QUIETS) generateEnPassant(pos, us, out, pawns, ksq);

    if (type != BB_GEN_CAPTURES && numCheckers == 0) generateCastling(pos, us, out);
}
