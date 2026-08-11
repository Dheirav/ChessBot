#include "pgn.hpp"

#include "legal_move_validator.hpp"
#include "movegen.hpp"

#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

namespace {

std::string squareName(int idx) {
    if (idx < 0 || idx > 63) return "??";
    std::string s;
    s += (char)('a' + (idx % 8));
    s += (char)('8' - (idx / 8));
    return s;
}

char pieceLetter(PieceType type) {
    switch (type) {
        case KNIGHT: return 'N';
        case BISHOP: return 'B';
        case ROOK:   return 'R';
        case QUEEN:  return 'Q';
        case KING:   return 'K';
        default:     return '\0';   // pawns are not named in SAN
    }
}

// "+" if the move gives check, "#" if it mates, "" otherwise. Both need the
// position *after* the move, so this is the one place a copy is made.
std::string checkSuffix(const Board& board, const Move& move) {
    Board after = board.copyForSearch();
    after.makeMove(move);
    const PieceColor opponent = after.activeColor;
    if (!LegalMoveValidator::isInCheck(after, opponent)) return "";
    return generateLegalMoves(after, opponent).empty() ? "#" : "+";
}

// SAN names the piece, and then only as much of its origin square as it takes
// to tell it apart from every other piece of the same kind that could legally
// play to the same destination: file if that is enough, else rank, else both.
std::string disambiguation(const Board& board, const Move& move) {
    const PieceType type = move.movedPiece.type();
    if (type == PAWN || type == KING) return "";

    MoveList legal = generateLegalMoves(board, board.activeColor);
    bool ambiguous = false, sameFile = false, sameRank = false;
    for (const Move& other : legal) {
        if (other.from == move.from || other.to != move.to) continue;
        if (other.movedPiece.type() != type) continue;
        ambiguous = true;
        if (other.from % 8 == move.from % 8) sameFile = true;
        if (other.from / 8 == move.from / 8) sameRank = true;
    }

    if (!ambiguous) return "";
    const std::string origin = squareName(move.from);
    if (!sameFile) return origin.substr(0, 1);   // file alone is enough
    if (!sameRank) return origin.substr(1, 1);   // file clashes, rank does not
    return origin;                               // two on the same file and rank
}

}  // namespace

std::string pgnToday() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d.%02d.%02d",
                  local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
    return buf;
}

std::string toSan(const Board& board, const Move& move) {
    // Castling is named for the rook it moves, not the squares involved.
    if (move.flag == CASTLING) {
        const bool kingside = (move.to % 8) > (move.from % 8);
        return (kingside ? "O-O" : "O-O-O") + checkSuffix(board, move);
    }

    const bool isCapture = move.capturedPiece.type() != NONE || move.flag == EN_PASSANT;
    std::string san;

    if (move.movedPiece.type() == PAWN) {
        // A capturing pawn is named by the file it left; a pushing pawn is not
        // named at all.
        if (isCapture) {
            san += (char)('a' + (move.from % 8));
            san += 'x';
        }
        san += squareName(move.to);
        if (move.flag == PROMOTION) {
            san += '=';
            san += pieceLetter(move.promotionPiece.type());
        }
    } else {
        san += pieceLetter(move.movedPiece.type());
        san += disambiguation(board, move);
        if (isCapture) san += 'x';
        san += squareName(move.to);
    }

    return san + checkSuffix(board, move);
}

std::string toPgn(const std::vector<Move>& moves, const PgnTags& tags) {
    std::ostringstream out;

    // The seven-tag roster, in the order the standard requires it.
    out << "[Event \""  << tags.event  << "\"]\n"
        << "[Site \""   << tags.site   << "\"]\n"
        << "[Date \""   << (tags.date.empty() ? "????.??.??" : tags.date) << "\"]\n"
        << "[Round \""  << tags.round  << "\"]\n"
        << "[White \""  << tags.white  << "\"]\n"
        << "[Black \""  << tags.black  << "\"]\n"
        << "[Result \"" << tags.result << "\"]\n";
    // A game that did not start from the opening position is unreadable without
    // these two, and they must appear together.
    if (!tags.startFen.empty()) {
        out << "[SetUp \"1\"]\n[FEN \"" << tags.startFen << "\"]\n";
    }
    out << "\n";

    Board board;
    board.setFromFEN(tags.startFen.empty() ? Board::INITIAL_FEN : tags.startFen);

    // Move numbers follow the board, not the loop counter: a game set up from a
    // FEN can begin at move 23, and can begin with Black to play.
    std::ostringstream movetext;
    bool first = true;
    for (const Move& move : moves) {
        const bool whiteToMove = (board.activeColor == COLOR_WHITE);
        if (whiteToMove) {
            movetext << (first ? "" : " ") << board.fullmoveNumber << ".";
        } else if (first) {
            // Black to move at the start of the movetext needs the ellipsis, or
            // the first move reads as White's.
            movetext << board.fullmoveNumber << "...";
        }
        movetext << " " << toSan(board, move);
        board.makeMove(move);
        first = false;
    }
    movetext << (first ? "" : " ") << tags.result;

    // Wrapped at 80 columns, never mid-token: PGN readers are line-based.
    const std::string text = movetext.str();
    std::istringstream tokens(text);
    std::string token, line;
    while (tokens >> token) {
        if (line.empty()) {
            line = token;
        } else if (line.size() + 1 + token.size() <= 80) {
            line += " " + token;
        } else {
            out << line << "\n";
            line = token;
        }
    }
    if (!line.empty()) out << line << "\n";

    return out.str();
}

bool writePgn(const std::string& path, const std::vector<Move>& moves,
              const PgnTags& tags) {
    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << toPgn(moves, tags);
    return file.good();
}
