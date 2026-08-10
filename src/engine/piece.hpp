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

    Piece();
    Piece(PieceColor color, PieceType type);

    PieceColor color() const;
    PieceType type() const;
};