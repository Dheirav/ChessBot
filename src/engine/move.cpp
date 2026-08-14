#include "move.hpp"
#include <sstream>

std::string Move::toString() const {
    if (from == -1 || to == -1) return "invalid";
    
    std::ostringstream oss;
    
    // Convert square indices to algebraic notation
    char fromFile = 'a' + (from % 8);
    char fromRank = '8' - (from / 8);
    char toFile = 'a' + (to % 8);
    char toRank = '8' - (to / 8);
    
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

std::string toUciMove(const Move& m) {
    if (m.from < 0 || m.to < 0) return "0000";
    std::string s;
    s += (char)('a' + (m.from % 8));
    s += (char)('8' - (m.from / 8));
    s += (char)('a' + (m.to % 8));
    s += (char)('8' - (m.to / 8));
    if (m.flag == PROMOTION) {
        switch (m.promotionPiece.type()) {
            case QUEEN:  s += 'q'; break;
            case ROOK:   s += 'r'; break;
            case BISHOP: s += 'b'; break;
            case KNIGHT: s += 'n'; break;
            default: break;
        }
    }
    return s;
}
