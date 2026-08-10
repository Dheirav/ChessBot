#include "bitboard_move_gen.hpp"
#include "bitboard_attacks.hpp"
#include "board.hpp"   // CastlingRight

namespace {

// Files and ranks in this module's orientation: a8 is bit 0, so y = 0 is the
// eighth rank and white advances toward *lower* indices.
constexpr Bitboard FILE_A = 0x0101010101010101ULL;  // x == 0
constexpr Bitboard FILE_H = 0x8080808080808080ULL;  // x == 7
constexpr Bitboard RANK_8 = 0x00000000000000FFULL;  // y == 0, white promotes here
constexpr Bitboard RANK_7 = 0x000000000000FF00ULL;  // y == 1, black pawns start
constexpr Bitboard RANK_2 = 0x00FF000000000000ULL;  // y == 6, white pawns start
constexpr Bitboard RANK_1 = 0xFF00000000000000ULL;  // y == 7, black promotes here

// Home squares, needed for castling and for revoking rights when a rook moves
// or is captured.
constexpr int SQ_A8 = 0,  SQ_E8 = 4,  SQ_H8 = 7;
constexpr int SQ_A1 = 56, SQ_E1 = 60, SQ_H1 = 63;

// Iterate the set bits of a bitboard. This is the whole point of the
// representation: pop the lowest set bit and clear it, visiting only the
// squares that are occupied. The previous version of this file scanned all 64
// squares testing one bit at a time — for pawns, about seven times per
// generation — which does the work of a mailbox loop while paying for a
// bitboard.
inline int popLsb(Bitboard& b) {
    const int sq = lsb(b);
    b &= b - 1;
    return sq;
}

inline void addMove(BitboardMoveList& out, int from, int to,
                    BitboardMoveFlag flag, BitboardPieceType moved,
                    BitboardPieceType captured,
                    BitboardPieceType promo = BB_NONE) {
    BitboardMove m;
    m.from = (uint8_t)from;
    m.to = (uint8_t)to;
    m.flag = flag;
    m.moved = moved;
    m.captured = captured;
    m.promotionType = promo;
    m.isCapture = (captured != BB_NONE) || flag == BBM_EN_PASSANT;
    out.push_back(m);
}

BitboardPieceType pieceAt(const BitboardState& state, BitboardColor color, int sq) {
    const Bitboard bit = 1ULL << sq;
    const auto& side = (color == BB_WHITE) ? state.white : state.black;
    for (int t = 0; t < 6; ++t)
        if (side[t] & bit) return (BitboardPieceType)t;
    return BB_NONE;
}

// A promotion is four moves, not one. Forgetting the under-promotions is a
// classic way to fail perft by a small margin in positions where they matter.
void addPromotions(BitboardMoveList& out, int from, int to,
                   BitboardPieceType captured) {
    for (BitboardPieceType promo : {BB_QUEEN, BB_ROOK, BB_BISHOP, BB_KNIGHT})
        addMove(out, from, to, BBM_PROMOTION, BB_PAWN, captured, promo);
}

void generatePawns(const BitboardState& state, BitboardColor color,
                   BitboardMoveList& out) {
    const bool white = (color == BB_WHITE);
    const Bitboard pawns = white ? state.white[BB_PAWN] : state.black[BB_PAWN];
    const Bitboard them  = white ? state.occupancyBlack : state.occupancyWhite;
    const Bitboard empty = ~state.occupancyAll;
    const Bitboard promoRank = white ? RANK_8 : RANK_1;
    const Bitboard startRank = white ? RANK_2 : RANK_7;

    // Forward is -8 for white, +8 for black. The captures are -9/-7 and +7/+9,
    // guarded by file masks so a capture cannot wrap around the board edge.
    auto push  = [&](Bitboard b) { return white ? (b >> 8) : (b << 8); };
    const int  fwd = white ? -8 : 8;

    Bitboard single = push(pawns) & empty;
    Bitboard dbl    = push(push(pawns & startRank) & empty) & empty;

    Bitboard quiet = single & ~promoRank;
    while (quiet) {
        const int to = popLsb(quiet);
        addMove(out, to - fwd, to, BBM_NORMAL, BB_PAWN, BB_NONE);
    }

    Bitboard promo = single & promoRank;
    while (promo) {
        const int to = popLsb(promo);
        addPromotions(out, to - fwd, to, BB_NONE);
    }

    while (dbl) {
        const int to = popLsb(dbl);
        addMove(out, to - 2 * fwd, to, BBM_DOUBLE_PUSH, BB_PAWN, BB_NONE);
    }

    // Captures. Left/right are in board-x terms: one shifts toward file a, the
    // other toward file h, and each drops the file it would wrap off.
    const Bitboard capLeft  = white ? ((pawns & ~FILE_A) >> 9) : ((pawns & ~FILE_A) << 7);
    const Bitboard capRight = white ? ((pawns & ~FILE_H) >> 7) : ((pawns & ~FILE_H) << 9);
    const int deltaLeft  = white ? -9 : 7;
    const int deltaRight = white ? -7 : 9;

    for (int side = 0; side < 2; ++side) {
        Bitboard caps = (side == 0 ? capLeft : capRight) & them;
        const int delta = (side == 0) ? deltaLeft : deltaRight;
        while (caps) {
            const int to = popLsb(caps);
            const int from = to - delta;
            const BitboardPieceType victim =
                pieceAt(state, white ? BB_BLACK : BB_WHITE, to);
            if ((1ULL << to) & promoRank) addPromotions(out, from, to, victim);
            else addMove(out, from, to, BBM_CAPTURE, BB_PAWN, victim);
        }
    }

    // En passant. The captured pawn is beside the mover, not on the
    // destination square, which is why this needs its own flag rather than
    // being a capture with an implied victim.
    if (state.enPassantSquare >= 0) {
        const int to = state.enPassantSquare;
        const int tx = to % 8;
        // A capturing pawn sits one file either side of the target, on the rank
        // it would be moving from. Written as index arithmetic rather than
        // shifts because the two directions have different file guards and
        // getting one wrong produces a pawn that captures around the board edge
        // — a bug that only shows up in perft, and only sometimes.
        const int fromToward_h = white ? to + 9 : to - 7;  // capturer on file tx+1
        const int fromToward_a = white ? to + 7 : to - 9;  // capturer on file tx-1
        if (tx < 7 && fromToward_h >= 0 && fromToward_h < 64 &&
            (pawns & (1ULL << fromToward_h)))
            addMove(out, fromToward_h, to, BBM_EN_PASSANT, BB_PAWN, BB_PAWN);
        if (tx > 0 && fromToward_a >= 0 && fromToward_a < 64 &&
            (pawns & (1ULL << fromToward_a)))
            addMove(out, fromToward_a, to, BBM_EN_PASSANT, BB_PAWN, BB_PAWN);
    }
}

void generatePieceMoves(const BitboardState& state, BitboardColor color,
                        BitboardPieceType type, BitboardMoveList& out) {
    const auto& ours = (color == BB_WHITE) ? state.white : state.black;
    const Bitboard own = (color == BB_WHITE) ? state.occupancyWhite : state.occupancyBlack;
    const BitboardColor them = (color == BB_WHITE) ? BB_BLACK : BB_WHITE;

    Bitboard pieces = ours[type];
    while (pieces) {
        const int from = popLsb(pieces);
        Bitboard targets = 0;
        switch (type) {
            case BB_KNIGHT: targets = knightAttacks(from); break;
            case BB_BISHOP: targets = bishopAttacks(from, state.occupancyAll); break;
            case BB_ROOK:   targets = rookAttacks(from, state.occupancyAll); break;
            case BB_QUEEN:  targets = queenAttacks(from, state.occupancyAll); break;
            case BB_KING:   targets = kingAttacks(from); break;
            default: break;
        }
        targets &= ~own;   // cannot capture our own pieces

        while (targets) {
            const int to = popLsb(targets);
            const BitboardPieceType victim = pieceAt(state, them, to);
            addMove(out, from, to, victim == BB_NONE ? BBM_NORMAL : BBM_CAPTURE,
                    type, victim);
        }
    }
}

// Castling is generated here rather than with the other king moves because its
// legality is positional, not just occupancy: the king may not start in check,
// pass through an attacked square, or land on one.
void generateCastling(const BitboardState& state, BitboardColor color,
                      BitboardMoveList& out) {
    const bool white = (color == BB_WHITE);
    const BitboardColor them = white ? BB_BLACK : BB_WHITE;
    const int kingFrom = white ? SQ_E1 : SQ_E8;

    const Bitboard kingBB = white ? state.white[BB_KING] : state.black[BB_KING];
    if (!(kingBB & (1ULL << kingFrom))) return;      // king not at home
    if (isSquareAttackedBB(state, kingFrom, them)) return;  // may not castle out of check

    const uint8_t kingSideBit  = white ? CASTLE_WK : CASTLE_BK;
    const uint8_t queenSideBit = white ? CASTLE_WQ : CASTLE_BQ;
    const int rookKingSide  = white ? SQ_H1 : SQ_H8;
    const int rookQueenSide = white ? SQ_A1 : SQ_A8;
    const Bitboard rooks = white ? state.white[BB_ROOK] : state.black[BB_ROOK];

    if ((state.castlingRights & kingSideBit) && (rooks & (1ULL << rookKingSide))) {
        const int f = kingFrom + 1, g = kingFrom + 2;
        if (!(state.occupancyAll & ((1ULL << f) | (1ULL << g))) &&
            !isSquareAttackedBB(state, f, them) &&
            !isSquareAttackedBB(state, g, them)) {
            addMove(out, kingFrom, g, BBM_CASTLE, BB_KING, BB_NONE);
        }
    }
    if ((state.castlingRights & queenSideBit) && (rooks & (1ULL << rookQueenSide))) {
        const int d = kingFrom - 1, c = kingFrom - 2, b = kingFrom - 3;
        if (!(state.occupancyAll & ((1ULL << d) | (1ULL << c) | (1ULL << b))) &&
            !isSquareAttackedBB(state, d, them) &&
            !isSquareAttackedBB(state, c, them)) {
            addMove(out, kingFrom, c, BBM_CASTLE, BB_KING, BB_NONE);
        }
    }
}

// Rights are revoked by the squares a move touches, which covers king moves,
// rook moves and rook captures in one rule.
uint8_t castlingRightsAfter(uint8_t rights, int from, int to) {
    auto touch = [&](int sq) {
        switch (sq) {
            case SQ_E1: rights &= ~(CASTLE_WK | CASTLE_WQ); break;
            case SQ_E8: rights &= ~(CASTLE_BK | CASTLE_BQ); break;
            case SQ_H1: rights &= ~CASTLE_WK; break;
            case SQ_A1: rights &= ~CASTLE_WQ; break;
            case SQ_H8: rights &= ~CASTLE_BK; break;
            case SQ_A8: rights &= ~CASTLE_BQ; break;
            default: break;
        }
    };
    touch(from);
    touch(to);
    return rights;
}

inline void movePiece(BitboardState& state, BitboardColor color,
                      BitboardPieceType type, int from, int to) {
    auto& side = (color == BB_WHITE) ? state.white : state.black;
    auto& occ  = (color == BB_WHITE) ? state.occupancyWhite : state.occupancyBlack;
    const Bitboard mask = (1ULL << from) | (1ULL << to);
    side[type] ^= mask;
    occ ^= mask;
}

inline void togglePiece(BitboardState& state, BitboardColor color,
                        BitboardPieceType type, int sq) {
    auto& side = (color == BB_WHITE) ? state.white : state.black;
    auto& occ  = (color == BB_WHITE) ? state.occupancyWhite : state.occupancyBlack;
    side[type] ^= 1ULL << sq;
    occ ^= 1ULL << sq;
}

}  // namespace

void generateBitboardPseudoLegal(const BitboardState& state, BitboardColor color,
                                 BitboardMoveList& moves) {
    moves.clear();
    generatePawns(state, color, moves);
    generatePieceMoves(state, color, BB_KNIGHT, moves);
    generatePieceMoves(state, color, BB_BISHOP, moves);
    generatePieceMoves(state, color, BB_ROOK, moves);
    generatePieceMoves(state, color, BB_QUEEN, moves);
    generatePieceMoves(state, color, BB_KING, moves);
    generateCastling(state, color, moves);
}

void generateBitboardLegal(BitboardState& state, BitboardColor color,
                           BitboardMoveList& moves) {
    BitboardMoveList pseudo;
    generateBitboardPseudoLegal(state, color, pseudo);

    moves.clear();
    moves.reserve(pseudo.size());
    for (const BitboardMove& m : pseudo) {
        BitboardUndo undo = makeBitboardMove(state, m);
        const int ksq = kingSquare(state, color);
        const BitboardColor them = (color == BB_WHITE) ? BB_BLACK : BB_WHITE;
        if (ksq < 0 || !isSquareAttackedBB(state, ksq, them)) moves.push_back(m);
        unmakeBitboardMove(state, undo);
    }
}

BitboardUndo makeBitboardMove(BitboardState& state, const BitboardMove& move) {
    BitboardUndo undo;
    undo.move = move;
    undo.castlingBefore = state.castlingRights;
    undo.enPassantBefore = state.enPassantSquare;

    const BitboardColor us = state.sideToMove;
    const BitboardColor them = (us == BB_WHITE) ? BB_BLACK : BB_WHITE;

    // Remove the captured piece first. En passant takes it from a different
    // square than the destination.
    if (move.flag == BBM_EN_PASSANT) {
        const int capturedSq = (move.from / 8) * 8 + (move.to % 8);
        togglePiece(state, them, BB_PAWN, capturedSq);
    } else if (move.captured != BB_NONE) {
        togglePiece(state, them, move.captured, move.to);
    }

    if (move.flag == BBM_PROMOTION) {
        togglePiece(state, us, BB_PAWN, move.from);
        togglePiece(state, us, move.promotionType, move.to);
    } else {
        movePiece(state, us, move.moved, move.from, move.to);
    }

    if (move.flag == BBM_CASTLE) {
        // The king has already moved; the rook follows. Which rook is decided
        // by the direction the king went.
        const bool kingSide = (move.to > move.from);
        const int rookFrom = kingSide ? move.from + 3 : move.from - 4;
        const int rookTo   = kingSide ? move.from + 1 : move.from - 1;
        movePiece(state, us, BB_ROOK, rookFrom, rookTo);
    }

    state.castlingRights = castlingRightsAfter(state.castlingRights, move.from, move.to);
    state.enPassantSquare = (move.flag == BBM_DOUBLE_PUSH)
                          ? (move.from + move.to) / 2
                          : -1;
    state.sideToMove = them;
    state.occupancyAll = state.occupancyWhite | state.occupancyBlack;
    return undo;
}

void unmakeBitboardMove(BitboardState& state, const BitboardUndo& undo) {
    const BitboardMove& move = undo.move;
    const BitboardColor us = (state.sideToMove == BB_WHITE) ? BB_BLACK : BB_WHITE;
    const BitboardColor them = state.sideToMove;

    if (move.flag == BBM_CASTLE) {
        const bool kingSide = (move.to > move.from);
        const int rookFrom = kingSide ? move.from + 3 : move.from - 4;
        const int rookTo   = kingSide ? move.from + 1 : move.from - 1;
        movePiece(state, us, BB_ROOK, rookTo, rookFrom);
    }

    if (move.flag == BBM_PROMOTION) {
        togglePiece(state, us, move.promotionType, move.to);
        togglePiece(state, us, BB_PAWN, move.from);
    } else {
        movePiece(state, us, move.moved, move.to, move.from);
    }

    if (move.flag == BBM_EN_PASSANT) {
        const int capturedSq = (move.from / 8) * 8 + (move.to % 8);
        togglePiece(state, them, BB_PAWN, capturedSq);
    } else if (move.captured != BB_NONE) {
        togglePiece(state, them, move.captured, move.to);
    }

    state.castlingRights = undo.castlingBefore;
    state.enPassantSquare = undo.enPassantBefore;
    state.sideToMove = us;
    state.occupancyAll = state.occupancyWhite | state.occupancyBlack;
}
