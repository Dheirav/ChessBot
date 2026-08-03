#include "fen.hpp"
#include <sstream>
#include <cctype>

namespace {
// Parses a non-negative integer from digits only; never throws.
bool parseNonNegativeInt(const std::string& s, int& out) {
    if (s.empty()) return false;
    long long value = 0;
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        value = value * 10 + (c - '0');
        if (value > 1000000000) return false;
    }
    out = static_cast<int>(value);
    return true;
}
}

bool parseFEN(const std::string& fen, Board& board, FENInfo& info) {
    // Clear the board
    for (int i = 0; i < 64; ++i)
        board.squares[i] = Piece();

    std::istringstream iss(fen);
    std::string piecePlacement, castling, enPassant, halfmoveStr, fullmoveStr;
    char activeColor;

    if (!(iss >> piecePlacement >> activeColor >> castling >> enPassant >> halfmoveStr >> fullmoveStr))
        return false;

    if (activeColor != 'w' && activeColor != 'b')
        return false;

    int halfmoveClock, fullmoveNumber;
    if (!parseNonNegativeInt(halfmoveStr, halfmoveClock)) return false;
    if (!parseNonNegativeInt(fullmoveStr, fullmoveNumber) || fullmoveNumber == 0) return false;

    if (castling != "-" && !castling.empty()) {
        for (char c : castling) {
            if (c != 'K' && c != 'Q' && c != 'k' && c != 'q') return false;
        }
    }

    if (enPassant != "-" && !enPassant.empty()) {
        if (enPassant.size() != 2) return false;
        if (enPassant[0] < 'a' || enPassant[0] > 'h') return false;
        if (enPassant[1] < '1' || enPassant[1] > '8') return false;
    }

    // Parse piece placement: exactly 8 ranks, each filling exactly 8 files.
    int rank = 0, file = 0;
    int whiteKings = 0, blackKings = 0;
    for (char c : piecePlacement) {
        if (c == '/') {
            if (file != 8 || rank >= 7) return false;
            ++rank;
            file = 0;
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            int n = c - '0';
            if (n == 0) return false;
            file += n;
            if (file > 8) return false;
        } else {
            if (file >= 8) return false;
            PieceColor color = std::isupper(static_cast<unsigned char>(c)) ? COLOR_WHITE : COLOR_BLACK;
            PieceType type;
            switch (std::tolower(static_cast<unsigned char>(c))) {
                case 'p': type = PAWN; break;
                case 'n': type = KNIGHT; break;
                case 'b': type = BISHOP; break;
                case 'r': type = ROOK; break;
                case 'q': type = QUEEN; break;
                case 'k': type = KING; break;
                default:  return false;
            }
            if (type == KING) {
                if (color == COLOR_WHITE) ++whiteKings;
                else ++blackKings;
            }
            int idx = Board::get1DIndex(file, rank);
            board.squares[idx] = Piece(color, type);
            ++file;
        }
    }
    if (rank != 7 || file != 8) return false;
    if (whiteKings != 1 || blackKings != 1) return false;

    // Set board state fields
    info.piecePlacement = piecePlacement;
    info.activeColor = activeColor;
    info.castlingRights = castling;
    info.enPassant = enPassant;
    info.halfmoveClock = halfmoveClock;
    info.fullmoveNumber = fullmoveNumber;

    board.activeColor = (activeColor == 'w' ? COLOR_WHITE : COLOR_BLACK);
    board.castlingRights = castling;
    board.enPassantTarget = enPassant;
    board.halfmoveClock = info.halfmoveClock;
    board.fullmoveNumber = info.fullmoveNumber;

    return true;
}
