#include "move.hpp"

Move::Move(int f, int t, Piece m, Piece c, MoveFlag fl, Piece p)
    : from(f), to(t), movedPiece(m), capturedPiece(c), flag(fl), promotionPiece(p) {}

Move::Move()
    : from(-1), to(-1), movedPiece(), capturedPiece(), flag(NORMAL), promotionPiece() {}