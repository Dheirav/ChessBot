#include "fen.hpp"
#include <sstream>
#include <cctype>

bool parseFEN(const std::string& fen, Board& board, FENInfo& info) {
    // Clear the board
    for (int i = 0; i < 64; ++i)
        board.squares[i] = Piece();

    std::istringstream iss(fen);
    std::string piecePlacement, castling, enPassant, halfmoveStr, fullmoveStr;
    char activeColor;

    if (!(iss >> piecePlacement >> activeColor >> castling >> enPassant >> halfmoveStr >> fullmoveStr))
        return false;

    info.piecePlacement = piecePlacement;
    info.activeColor = activeColor;
    info.castlingRights = castling;
    info.enPassant = enPassant;
    info.halfmoveClock = std::stoi(halfmoveStr);
    info.fullmoveNumber = std::stoi(fullmoveStr);

    // Parse piece placement
    int rank = 0, file = 0;
    for (char c : piecePlacement) {
        if (c == '/') {
            ++rank;
            file = 0;
        } else if (isdigit(c)) {
            file += c - '0';
        } else {
            PieceColor color = isupper(c) ? COLOR_WHITE : COLOR_BLACK;
            PieceType type;
            switch (tolower(c)) {
                case 'p': type = PAWN; break;
                case 'n': type = KNIGHT; break;
                case 'b': type = BISHOP; break;
                case 'r': type = ROOK; break;
                case 'q': type = QUEEN; break;
                case 'k': type = KING; break;
                default:  type = NONE; break;
            }
            int idx = Board::get1DIndex(file, rank);
            board.squares[idx] = Piece(color, type);
            ++file;
        }
    }

    // Set board state fields
    board.activeColor = (activeColor == 'w' ? COLOR_WHITE : COLOR_BLACK);
    board.castlingRights = castling;
    board.enPassantTarget = enPassant;
    board.halfmoveClock = info.halfmoveClock;
    board.fullmoveNumber = info.fullmoveNumber;

    return true;
}