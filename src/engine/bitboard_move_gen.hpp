#pragma once
#include "bitboard.hpp"
#include <vector>

struct BitboardMove {
    int from; // 0-63
    int to;   // 0-63
    bool isCapture;
    bool isPromotion;
    BitboardPieceType promotionType;
};

using BitboardMoveList = std::vector<BitboardMove>;

// Generate all pawn moves for the given color
void generatePawnMoves(const BitboardState& state, BitboardColor color, BitboardMoveList& moves);

// Updated king move generation with castling
void generateKingMoves(const BitboardState& state, BitboardColor color, BitboardMoveList& moves);

// Generate all knight moves for the given color
void generateKnightMoves(const BitboardState& state, BitboardColor color, BitboardMoveList& moves);

// Generate all bishop moves for the given color
void generateBishopMoves(const BitboardState& state, BitboardColor color, BitboardMoveList& moves);

// Generate all rook moves for the given color
void generateRookMoves(const BitboardState& state, BitboardColor color, BitboardMoveList& moves);

// Generate all queen moves for the given color
void generateQueenMoves(const BitboardState& state, BitboardColor color, BitboardMoveList& moves);

// Generate all king moves for the given color
void generateKingMoves(const BitboardState& state, BitboardColor color, BitboardMoveList& moves);
