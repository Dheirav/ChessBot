#pragma once
#include "piece.hpp"
#include <vector>

enum MoveFlag {
    NORMAL,
    CAPTURE,
    PROMOTION,
    EN_PASSANT,
    CASTLING
};

struct Move {
    int from;              // 0-63 (1D index)
    int to;                // 0-63 (1D index)
    Piece movedPiece;      // The piece being moved
    Piece capturedPiece;   // The piece being captured (if any)
    MoveFlag flag;         // Type of move
    Piece promotionPiece;  // For promotions

    Move(int f, int t, Piece m, Piece c = Piece(), MoveFlag fl = NORMAL, Piece p = Piece());
    Move();
};

using MoveList = std::vector<Move>;