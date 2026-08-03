#include "bitboard_move_gen.hpp"
#include "magic_bitboards.hpp"
#include <cassert>
#include <bitset>

// Helper: shift bitboard north/south
constexpr Bitboard north(Bitboard b) { return b << 8; }
constexpr Bitboard south(Bitboard b) { return b >> 8; }
constexpr Bitboard east(Bitboard b)  { return (b & 0xfefefefefefefefeULL) << 1; }
constexpr Bitboard west(Bitboard b)  { return (b & 0x7f7f7f7f7f7f7f7fULL) >> 1; }

// Helper: mask for rank 2, 7, 8, 1
constexpr Bitboard rank1 = 0x00000000000000FFULL;
constexpr Bitboard rank2 = 0x000000000000FF00ULL;
constexpr Bitboard rank7 = 0x00FF000000000000ULL;
constexpr Bitboard rank8 = 0xFF00000000000000ULL;

void generatePawnMoves(const BitboardState& state, BitboardColor color, BitboardMoveList& moves) {
    Bitboard pawns = (color == BB_WHITE) ? state.white[BB_PAWN] : state.black[BB_PAWN];
    Bitboard enemy = (color == BB_WHITE) ? state.occupancyBlack : state.occupancyWhite;
    Bitboard empty = ~state.occupancyAll;

    if (color == BB_WHITE) {
        // Single pushes
        Bitboard singlePush = north(pawns) & empty;
        Bitboard promotionPush = singlePush & rank8;
        Bitboard quietPush = singlePush & ~rank8;
        // Double pushes
        Bitboard doublePush = north(quietPush & rank2) & empty;
        doublePush = north(doublePush) & empty;
        // Captures
        Bitboard eastCap = north(east(pawns)) & enemy;
        Bitboard westCap = north(west(pawns)) & enemy;
        Bitboard promoEastCap = eastCap & rank8;
        Bitboard promoWestCap = westCap & rank8;
        Bitboard normalEastCap = eastCap & ~rank8;
        Bitboard normalWestCap = westCap & ~rank8;
        // Generate moves
        // Quiet single pushes
        for (int to = 0; to < 64; ++to) {
            if (testBit(quietPush, to)) {
                moves.push_back({to - 8, to, false, false, BB_NONE});
            }
        }
        // Double pushes
        for (int to = 0; to < 64; ++to) {
            if (testBit(doublePush, to)) {
                moves.push_back({to - 16, to, false, false, BB_NONE});
            }
        }
        // Promotions (quiet)
        for (int to = 0; to < 64; ++to) {
            if (testBit(promotionPush, to)) {
                for (BitboardPieceType pt : {BB_QUEEN, BB_ROOK, BB_BISHOP, BB_KNIGHT}) {
                    moves.push_back({to - 8, to, false, true, pt});
                }
            }
        }
        // Captures
        for (int to = 0; to < 64; ++to) {
            if (testBit(normalEastCap, to)) {
                moves.push_back({to - 9, to, true, false, BB_NONE});
            }
            if (testBit(normalWestCap, to)) {
                moves.push_back({to - 7, to, true, false, BB_NONE});
            }
        }
        // Promotion captures
        for (int to = 0; to < 64; ++to) {
            if (testBit(promoEastCap, to)) {
                for (BitboardPieceType pt : {BB_QUEEN, BB_ROOK, BB_BISHOP, BB_KNIGHT}) {
                    moves.push_back({to - 9, to, true, true, pt});
                }
            }
            if (testBit(promoWestCap, to)) {
                for (BitboardPieceType pt : {BB_QUEEN, BB_ROOK, BB_BISHOP, BB_KNIGHT}) {
                    moves.push_back({to - 7, to, true, true, pt});
                }
            }
        }
        // En passant
        if (state.enPassantSquare >= 0 && state.enPassantSquare < 64) {
            Bitboard epSquare = 1ULL << state.enPassantSquare;
            Bitboard epCapture = (color == BB_WHITE) ? (epSquare >> 8) : (epSquare << 8);
            // Check if pawns can capture en passant
            Bitboard pawnsEast = east(pawns);
            Bitboard pawnsWest = west(pawns);
            if (testBit(epCapture, state.enPassantSquare)) {
                if (testBit(pawnsEast, state.enPassantSquare)) {
                    moves.push_back({state.enPassantSquare, state.enPassantSquare + ((color == BB_WHITE) ? 8 : -8), true, false, BB_NONE});
                }
                if (testBit(pawnsWest, state.enPassantSquare)) {
                    moves.push_back({state.enPassantSquare, state.enPassantSquare + ((color == BB_WHITE) ? 8 : -8), true, false, BB_NONE});
                }
            }
        }
    } else {
        // Black pawns
        Bitboard singlePush = south(pawns) & empty;
        Bitboard promotionPush = singlePush & rank1;
        Bitboard quietPush = singlePush & ~rank1;
        // Double pushes
        Bitboard doublePush = south(quietPush & rank7) & empty;
        doublePush = south(doublePush) & empty;
        // Captures
        Bitboard eastCap = south(east(pawns)) & enemy;
        Bitboard westCap = south(west(pawns)) & enemy;
        Bitboard promoEastCap = eastCap & rank1;
        Bitboard promoWestCap = westCap & rank1;
        Bitboard normalEastCap = eastCap & ~rank1;
        Bitboard normalWestCap = westCap & ~rank1;
        // Generate moves
        // Quiet single pushes
        for (int to = 0; to < 64; ++to) {
            if (testBit(quietPush, to)) {
                moves.push_back({to + 8, to, false, false, BB_NONE});
            }
        }
        // Double pushes
        for (int to = 0; to < 64; ++to) {
            if (testBit(doublePush, to)) {
                moves.push_back({to + 16, to, false, false, BB_NONE});
            }
        }
        // Promotions (quiet)
        for (int to = 0; to < 64; ++to) {
            if (testBit(promotionPush, to)) {
                for (BitboardPieceType pt : {BB_QUEEN, BB_ROOK, BB_BISHOP, BB_KNIGHT}) {
                    moves.push_back({to + 8, to, false, true, pt});
                }
            }
        }
        // Captures
        for (int to = 0; to < 64; ++to) {
            if (testBit(normalEastCap, to)) {
                moves.push_back({to + 9, to, true, false, BB_NONE});
            }
            if (testBit(normalWestCap, to)) {
                moves.push_back({to + 7, to, true, false, BB_NONE});
            }
        }
        // Promotion captures
        for (int to = 0; to < 64; ++to) {
            if (testBit(promoEastCap, to)) {
                for (BitboardPieceType pt : {BB_QUEEN, BB_ROOK, BB_BISHOP, BB_KNIGHT}) {
                    moves.push_back({to + 9, to, true, true, pt});
                }
            }
            if (testBit(promoWestCap, to)) {
                for (BitboardPieceType pt : {BB_QUEEN, BB_ROOK, BB_BISHOP, BB_KNIGHT}) {
                    moves.push_back({to + 7, to, true, true, pt});
                }
            }
        }
        // En passant
        if (state.enPassantSquare >= 0 && state.enPassantSquare < 64) {
            Bitboard epSquare = 1ULL << state.enPassantSquare;
            Bitboard epCapture = (color == BB_WHITE) ? (epSquare >> 8) : (epSquare << 8);
            // Check if pawns can capture en passant
            Bitboard pawnsEast = east(pawns);
            Bitboard pawnsWest = west(pawns);
            if (testBit(epCapture, state.enPassantSquare)) {
                if (testBit(pawnsEast, state.enPassantSquare)) {
                    moves.push_back({state.enPassantSquare, state.enPassantSquare + ((color == BB_WHITE) ? 8 : -8), true, false, BB_NONE});
                }
                if (testBit(pawnsWest, state.enPassantSquare)) {
                    moves.push_back({state.enPassantSquare, state.enPassantSquare + ((color == BB_WHITE) ? 8 : -8), true, false, BB_NONE});
                }
            }
        }
    }
}
// Knight move generation (stub)
void generateKnightMoves(const BitboardState& state, BitboardColor color, BitboardMoveList& moves) {
    Bitboard knights = (color == BB_WHITE) ? state.white[BB_KNIGHT] : state.black[BB_KNIGHT];
    Bitboard own = (color == BB_WHITE) ? state.occupancyWhite : state.occupancyBlack;
    Bitboard enemy = (color == BB_WHITE) ? state.occupancyBlack : state.occupancyWhite;
    constexpr int offsets[8] = {17, 15, 10, 6, -17, -15, -10, -6};
    for (int sq = 0; sq < 64; ++sq) {
        if (testBit(knights, sq)) {
            int rank = sq / 8;
            int file = sq % 8;
            for (int i = 0; i < 8; ++i) {
                int to = sq + offsets[i];
                if (to < 0 || to >= 64) continue;
                int toRank = to / 8;
                int toFile = to % 8;
                // Knight moves must be within 2 ranks/files
                if (abs(toRank - rank) + abs(toFile - file) == 3 && abs(toRank - rank) <= 2 && abs(toFile - file) <= 2) {
                    if (!testBit(own, to)) {
                        bool isCapture = testBit(enemy, to);
                        moves.push_back({sq, to, isCapture, false, BB_NONE});
                    }
                }
            }
        }
    }
}

// Bishop move generation (stub)
void generateBishopMoves(const BitboardState& state, BitboardColor color, BitboardMoveList& moves) {
    Bitboard bishops = (color == BB_WHITE) ? state.white[BB_BISHOP] : state.black[BB_BISHOP];
    Bitboard own = (color == BB_WHITE) ? state.occupancyWhite : state.occupancyBlack;
    Bitboard enemy = (color == BB_WHITE) ? state.occupancyBlack : state.occupancyWhite;
    Bitboard occupancy = state.occupancyAll;
    for (int sq = 0; sq < 64; ++sq) {
        if (testBit(bishops, sq)) {
            uint64_t attacks = getBishopAttacks(sq, occupancy) & ~own;
            for (int to = 0; to < 64; ++to) {
                if (testBit(attacks, to)) {
                    bool isCapture = testBit(enemy, to);
                    moves.push_back({sq, to, isCapture, false, BB_NONE});
                }
            }
        }
    }
}

// Rook move generation (stub)
void generateRookMoves(const BitboardState& state, BitboardColor color, BitboardMoveList& moves) {
    Bitboard rooks = (color == BB_WHITE) ? state.white[BB_ROOK] : state.black[BB_ROOK];
    Bitboard own = (color == BB_WHITE) ? state.occupancyWhite : state.occupancyBlack;
    Bitboard enemy = (color == BB_WHITE) ? state.occupancyBlack : state.occupancyWhite;
    Bitboard occupancy = state.occupancyAll;
    Bitboard temp = rooks;
    while (temp) {
        int sq = lsb(temp);
        uint64_t attacks = getRookAttacks(sq, occupancy) & ~own;
        for (int to = 0; to < 64; ++to) {
            if (testBit(attacks, to)) {
                bool isCapture = testBit(enemy, to);
                moves.push_back({sq, to, isCapture, false, BB_NONE});
            }
        }
        clearBit(temp, sq);
    }
}

// Queen move generation (stub)
void generateQueenMoves(const BitboardState& state, BitboardColor color, BitboardMoveList& moves) {
    Bitboard queens = (color == BB_WHITE) ? state.white[BB_QUEEN] : state.black[BB_QUEEN];
    Bitboard own = (color == BB_WHITE) ? state.occupancyWhite : state.occupancyBlack;
    Bitboard enemy = (color == BB_WHITE) ? state.occupancyBlack : state.occupancyWhite;
    Bitboard occupancy = state.occupancyAll;
    for (int sq = 0; sq < 64; ++sq) {
        if (testBit(queens, sq)) {
            uint64_t attacks = (getRookAttacks(sq, occupancy) | getBishopAttacks(sq, occupancy)) & ~own;
            for (int to = 0; to < 64; ++to) {
                if (testBit(attacks, to)) {
                    bool isCapture = testBit(enemy, to);
                    moves.push_back({sq, to, isCapture, false, BB_NONE});
                }
            }
        }
    }
}

// King move generation (stub)
void generateKingMoves(const BitboardState& state, BitboardColor color, BitboardMoveList& moves) {
    Bitboard king = (color == BB_WHITE) ? state.white[BB_KING] : state.black[BB_KING];
    Bitboard own = (color == BB_WHITE) ? state.occupancyWhite : state.occupancyBlack;
    Bitboard enemy = (color == BB_WHITE) ? state.occupancyBlack : state.occupancyWhite;

    // King moves: 8 directions
    constexpr int directions[8] = {8, -8, 1, -1, 9, -9, 7, -7};
    for (int sq = 0; sq < 64; ++sq) {
        if (testBit(king, sq)) {
            int rank = sq / 8;
            int file = sq % 8;
            for (int d = 0; d < 8; ++d) {
                int to = sq + directions[d];
                int toRank = to / 8;
                int toFile = to % 8;
                // Stay on board and not wrap around
                if (to >= 0 && to < 64 && abs(toRank - rank) <= 1 && abs(toFile - file) <= 1) {
                    if (!testBit(own, to)) {
                        bool isCapture = testBit(enemy, to);
                        moves.push_back({sq, to, isCapture, false, BB_NONE});
                    }
                }
            }
        }
    }
    // Castling logic
    auto isSquareAttacked = [&](int sq) {
        // Check if square is attacked by any enemy piece
        Bitboard occ = state.occupancyAll;
        // Pawn attacks
        Bitboard pawnAttacks = 0ULL;
        if (color == BB_WHITE) {
            pawnAttacks |= south(east(state.black[BB_PAWN]));
            pawnAttacks |= south(west(state.black[BB_PAWN]));
        } else {
            pawnAttacks |= north(east(state.white[BB_PAWN]));
            pawnAttacks |= north(west(state.white[BB_PAWN]));
        }
        if (testBit(pawnAttacks, sq)) return true;
        // Knight attacks
        constexpr int knightOffsets[8] = {17, 15, 10, 6, -17, -15, -10, -6};
        Bitboard knights = (color == BB_WHITE) ? state.black[BB_KNIGHT] : state.white[BB_KNIGHT];
        for (int ksq = 0; ksq < 64; ++ksq) {
            if (testBit(knights, ksq)) {
                for (int i = 0; i < 8; ++i) {
                    int to = ksq + knightOffsets[i];
                    int toRank = to / 8, toFile = to % 8;
                    int kRank = ksq / 8, kFile = ksq % 8;
                    if (to >= 0 && to < 64 && abs(toRank - kRank) + abs(toFile - kFile) == 3 && abs(toRank - kRank) <= 2 && abs(toFile - kFile) <= 2) {
                        if (to == sq) return true;
                    }
                }
            }
        }
        // Bishop/Queen attacks
        Bitboard bishops = (color == BB_WHITE) ? state.black[BB_BISHOP] : state.white[BB_BISHOP];
        Bitboard queens = (color == BB_WHITE) ? state.black[BB_QUEEN] : state.white[BB_QUEEN];
        Bitboard bq = bishops | queens;
        for (int bqSq = 0; bqSq < 64; ++bqSq) {
            if (testBit(bq, bqSq)) {
                uint64_t attacks = getBishopAttacks(bqSq, occ);
                if (testBit(attacks, sq)) return true;
            }
        }
        // Rook/Queen attacks
        Bitboard rooks = (color == BB_WHITE) ? state.black[BB_ROOK] : state.white[BB_ROOK];
        Bitboard rq = rooks | queens;
        for (int rqSq = 0; rqSq < 64; ++rqSq) {
            if (testBit(rq, rqSq)) {
                uint64_t attacks = getRookAttacks(rqSq, occ);
                if (testBit(attacks, sq)) return true;
            }
        }
        // King attacks
        Bitboard enemyKing = (color == BB_WHITE) ? state.black[BB_KING] : state.white[BB_KING];
        for (int ksq = 0; ksq < 64; ++ksq) {
            if (testBit(enemyKing, ksq)) {
                int kRank = ksq / 8, kFile = ksq % 8;
                for (int d = 0; d < 8; ++d) {
                    int to = ksq + directions[d];
                    int toRank = to / 8, toFile = to % 8;
                    if (to >= 0 && to < 64 && abs(toRank - kRank) <= 1 && abs(toFile - kFile) <= 1) {
                        if (to == sq) return true;
                    }
                }
            }
        }
        return false;
    };
    if (color == BB_WHITE) {
        // Kingside: e1 (4) to g1 (6), rook h1 (7)
        if ((state.castlingRights & 1) &&
            !testBit(state.occupancyAll, 5) && !testBit(state.occupancyAll, 6)) {
            // Check squares e1, f1, g1 not attacked
            if (!isSquareAttacked(4) && !isSquareAttacked(5) && !isSquareAttacked(6)) {
                moves.push_back({4, 6, false, false, BB_NONE});
            }
        }
        // Queenside: e1 (4) to c1 (2), rook a1 (0)
        if ((state.castlingRights & 2) &&
            !testBit(state.occupancyAll, 3) && !testBit(state.occupancyAll, 2) && !testBit(state.occupancyAll, 1)) {
            // Check squares e1, d1, c1 not attacked
            if (!isSquareAttacked(4) && !isSquareAttacked(3) && !isSquareAttacked(2)) {
                moves.push_back({4, 2, false, false, BB_NONE});
            }
        }
    } else {
        // Kingside: e8 (60) to g8 (62), rook h8 (63)
        if ((state.castlingRights & 4) &&
            !testBit(state.occupancyAll, 61) && !testBit(state.occupancyAll, 62)) {
            // Check squares e8, f8, g8 not attacked
            if (!isSquareAttacked(60) && !isSquareAttacked(61) && !isSquareAttacked(62)) {
                moves.push_back({60, 62, false, false, BB_NONE});
            }
        }
        // Queenside: e8 (60) to c8 (58), rook a8 (56)
        if ((state.castlingRights & 8) &&
            !testBit(state.occupancyAll, 59) && !testBit(state.occupancyAll, 58) && !testBit(state.occupancyAll, 57)) {
            // Check squares e8, d8, c8 not attacked
            if (!isSquareAttacked(60) && !isSquareAttacked(59) && !isSquareAttacked(58)) {
                moves.push_back({60, 58, false, false, BB_NONE});
            }
        }
    }
}
