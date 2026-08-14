#pragma once

#include <cstdint>

enum PieceType : uint8_t {
    NONE   = 0,
    KING   = 1,
    PAWN   = 2,
    KNIGHT = 3,
    BISHOP = 4,
    ROOK   = 5,
    QUEEN  = 6,
};

enum PieceColor : uint8_t {
    COLOR_NONE  = 0,
    COLOR_WHITE = 1,
    COLOR_BLACK = 2
};

class Piece {
public:
    // 5-bit representation: CC TTT
    // CC = color (2 bits), TTT = type (3 bits)
    uint8_t value;

    // Defined here rather than in a .cpp, and that is the whole point.
    //
    // Each of these is one instruction. They used to live in piece.cpp, where
    // being in a different translation unit made every use a real function
    // call — and a gprof run on 2026-08-15 counted 1.87 *billion* calls to
    // type(), 381 million to color() and 545 million to the default
    // constructor, together about 21% of the engine's runtime, essentially all
    // of it call overhead rather than work.
    //
    // Anything this small on a path this hot belongs in the header. There is no
    // link-time optimisation in this build to rescue it, and inlining is
    // behaviour-preserving — which the bench signature proves exactly.
    constexpr Piece() : value(0) {}
    constexpr Piece(PieceColor color, PieceType type)
        : value((uint8_t)((static_cast<uint8_t>(color) << 3) |
                          (static_cast<uint8_t>(type) & 0b111))) {}

    constexpr PieceColor color() const { return static_cast<PieceColor>(value >> 3); }
    constexpr PieceType  type()  const { return static_cast<PieceType>(value & 0b111); }
};