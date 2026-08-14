#pragma once
#include "piece.hpp"
#include <vector>
#include <string>

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

    // Inline for the same reason the Piece accessors are (see piece.hpp): these
    // are member initialisers and field comparisons, and the profile on
    // 2026-08-15 counted 116 million calls to the default constructor and 65
    // million to the other. Out of line in move.cpp, every one of those was a
    // real call.
    Move(int f, int t, Piece m, Piece c = Piece(), MoveFlag fl = NORMAL, Piece p = Piece())
        : from(f), to(t), movedPiece(m), capturedPiece(c), flag(fl), promotionPiece(p) {}
    Move()
        : from(-1), to(-1), movedPiece(), capturedPiece(), flag(NORMAL), promotionPiece() {}

    // Equality ignores the captured piece and compares the promotion piece only
    // for promotions, because it is used to match a move against a generated
    // list — where the caller knows the squares and the flag but not what
    // happens to be standing on the destination.
    bool operator==(const Move& other) const {
        return from == other.from &&
               to == other.to &&
               flag == other.flag &&
               (flag != PROMOTION || promotionPiece.type() == other.promotionPiece.type());
    }

    // Stays out of line: it builds a std::string, so it is neither hot nor
    // cheap, and inlining it would pull <sstream> into every translation unit
    // that touches a Move.
    std::string toString() const;
};

using MoveList = std::vector<Move>;