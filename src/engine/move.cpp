#include "move.hpp"
#include <sstream>

Move::Move(int f, int t, Piece m, Piece c, MoveFlag fl, Piece p)
    : from(f), to(t), movedPiece(m), capturedPiece(c), flag(fl), promotionPiece(p) {}

Move::Move()
    : from(-1), to(-1), movedPiece(), capturedPiece(), flag(NORMAL), promotionPiece() {}

std::string Move::toString() const {
    if (from == -1 || to == -1) return "invalid";
    
    std::ostringstream oss;
    
    // Convert square indices to algebraic notation
    char fromFile = 'a' + (from % 8);
    char fromRank = '1' + (from / 8);
    char toFile = 'a' + (to % 8);
    char toRank = '1' + (to / 8);
    
    oss << fromFile << fromRank << toFile << toRank;
    
    // Add special move indicators
    if (flag == PROMOTION) {
        oss << "=";
        switch (promotionPiece.type()) {
            case QUEEN: oss << "Q"; break;
            case ROOK: oss << "R"; break;
            case BISHOP: oss << "B"; break;
            case KNIGHT: oss << "N"; break;
            default: break;
        }
    }
    
    return oss.str();
}

bool Move::operator==(const Move& other) const {
    return from == other.from && 
           to == other.to && 
           flag == other.flag &&
           (flag != PROMOTION || promotionPiece.type() == other.promotionPiece.type());
}