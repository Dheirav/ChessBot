#pragma once
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
