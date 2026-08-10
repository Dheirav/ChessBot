#include "piece.hpp"

Piece::Piece() : value(0) {}

Piece::Piece(PieceColor color, PieceType type) {
    value = (static_cast<uint8_t>(color) << 3) | (static_cast<uint8_t>(type) & 0b111);
}

PieceColor Piece::color() const {
    return static_cast<PieceColor>(value >> 3);
}

PieceType Piece::type() const {
    return static_cast<PieceType>(value & 0b111);
}