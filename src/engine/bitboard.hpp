#pragma once
//
// STATUS: UNFINISHED AND UNUSED — read this before wiring any of it up.
//
// This module (bitboard.cpp, bitboard_move_gen.cpp, magic_bitboards.cpp,
// bitboard_pawn_moves.hpp) compiles and links, but nothing outside these files
// references any of it. The engine's real move generation is movegen.cpp.
// It is kept deliberately; it is not on a path to being switched on as-is.
//
// Four things are wrong with it today:
//
//   1. initMagicBitboards() is never called. The magic attack tables are
//      zero-initialized globals, so getRookAttacks() and getBishopAttacks()
//      return 0 for every query. Wired up as-is, rooks, bishops and queens
//      would generate no moves at all.
//   2. BitboardMove has no castling or en-passant flag. Castling comes out as
//      a bare king move e1->g1, which a make-move routine cannot distinguish
//      from a quiet king move.
//   3. There is no legality filtering, no pin or check handling, and no
//      make/unmake for BitboardState.
//   4. The generation loops are written against the grain of the
//      representation: `for (int to = 0; to < 64; ++to) if (testBit(bb, to))`
//      instead of `while (bb) { sq = lsb(bb); bb &= bb - 1; }`. Pawn
//      generation makes about seven separate 64-iteration scans; the castling
//      isSquareAttacked lambda scans all 64 squares five times. Rewriting
//      these is a prerequisite for the module being faster than what it
//      would replace, not an optimization to do afterwards.
//
// On what it is worth: replacing movegen.cpp with a bitboard generator was
// measured at a ~1.02x ceiling for the whole search, because move generation
// is about 2% of search time (BACKLOG.md 4.0). Do not revive this module for
// that reason. Its plausible value is as *shared* infrastructure that several
// consumers read: attack sets for Static Exchange Evaluation (PLAN.md 3.2),
// pin and checker computation to replace the make-move legality filter
// (19.4% of search time, PLAN.md 5.5), and popcount-based mobility and pawn
// structure in evaluation. That is the case to make before finishing it.
//
#include <cstdint>
#include <string>
#include <array>

using Bitboard = uint64_t;

// Piece types
enum BitboardPieceType {
    BB_PAWN, BB_KNIGHT, BB_BISHOP, BB_ROOK, BB_QUEEN, BB_KING, BB_NONE
};

// Colors
enum BitboardColor {
    BB_WHITE, BB_BLACK
};

// Bitboard structure for all pieces
struct BitboardState {
    // 12 bitboards: [white/black][piece type]
    std::array<Bitboard, 6> white;
    std::array<Bitboard, 6> black;
    Bitboard occupancyWhite = 0ULL;
    Bitboard occupancyBlack = 0ULL;
    Bitboard occupancyAll = 0ULL;

    // Castling rights: bitmask (white kingside=1, white queenside=2, black kingside=4, black queenside=8)
    uint8_t castlingRights = 0;
    // En passant target square (0-63, or -1 if none)
    int enPassantSquare = -1;
    // Side to move
    BitboardColor sideToMove = BB_WHITE;

    BitboardState();
    void clear();
};

// Utility functions
int popcount(Bitboard b);
int lsb(Bitboard b); // least significant bit index
int msb(Bitboard b); // most significant bit index
void setBit(Bitboard& b, int sq);
void clearBit(Bitboard& b, int sq);
bool testBit(Bitboard b, int sq);

// FEN parsing for bitboards
void setBitboardFromFEN(BitboardState& state, const std::string& fen);

// Debug
void printBitboard(Bitboard b);
void printBitboardState(const BitboardState& state);
