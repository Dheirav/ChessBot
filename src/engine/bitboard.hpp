#pragma once
//
// STATUS: COMPLETE AND VERIFIED, BUT NOT CONNECTED.
//
// This module (bitboard.cpp, bitboard_attacks.cpp, bitboard_move_gen.cpp,
// magic_bitboards.cpp) is a working, perft-correct bitboard implementation.
// Nothing outside it references it yet: the engine's move generation is still
// movegen.cpp. It is finished infrastructure waiting for a consumer, not dead
// code that happens to compile.
//
// Square indexing matches Board: a8 is bit 0, h1 is bit 63, and white moves
// toward LOWER indices. This is the opposite of the near-universal a1 = 0
// convention and is deliberate — the module exists to serve the mailbox engine,
// and two orientations in one codebase is exactly how a bitboard module ends up
// silently generating wrong moves. Everything here assumes it.
//
// What it provides:
//   - magic sliding attacks, validated exhaustively against a direct ray walk
//     for every square and every blocker subset
//   - attackersTo(sq, occupancy), taking a caller-supplied occupancy so x-rays
//     can be discovered by removing pieces (this is the form SEE wants)
//   - checkers() and blockersForKing(), the basis of pin-aware legal move
//     generation
//   - a legal move generator with make/unmake, castling and en passant flags
//
// All of it is verified in tests/bitboard_test.cpp: 107,648 exhaustive magic
// lookups, 765,696 isSquareAttacked comparisons against the mailbox engine over
// 5,982 positions, pin detection against remove-and-test-the-king, and perft
// against the same published counts that guard movegen.cpp.
//
// What it is NOT for: replacing movegen.cpp to make move generation faster.
// That was measured at a ~1.02x ceiling for the whole search, because move
// generation is about 2% of search time (BACKLOG.md 4.0). The value is in the
// consumers — SEE at every node (PLAN.md 3.2), and replacing the make-move
// legality filter, which is the largest item left in the profile (PLAN.md 5.5).
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
