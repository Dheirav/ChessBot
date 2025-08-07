#include "board.hpp"
#include "fen.hpp"
#include "piece.hpp"
#include <algorithm>
#include <sstream>
#include <unordered_map>

// Memoization cache (not thread-safe, but fine for single-threaded GUI)
static std::unordered_map<AttackMemoKey, bool> attackMemo;

Board::Board() {
    setFromFEN(INITIAL_FEN);
}

bool Board::setFromFEN(const std::string& fen) {
    FENInfo info;
    return parseFEN(fen, *this, info);
}

std::string Board::getFEN() const {
    std::ostringstream fen;
    for (int rank = 0; rank < 8; rank++) {
        int empty = 0;
        for (int file = 0; file < 8; ++file) {
            int idx = get1DIndex(file, rank);
            const Piece& piece = squares[idx];
            if (piece.type() == NONE) {
                ++empty;
            } else {
                if (empty > 0) {
                    fen << empty;
                    empty = 0;
                }
                char c;
                switch (piece.type()) {
                    case PAWN:   c = 'p'; break;
                    case KNIGHT: c = 'n'; break;
                    case BISHOP: c = 'b'; break;
                    case ROOK:   c = 'r'; break;
                    case QUEEN:  c = 'q'; break;
                    case KING:   c = 'k'; break;
                    default:     c = '?'; break;
                }
                if (piece.color() == COLOR_WHITE)
                    c = toupper(c);
                fen << c;
            }
        }
        if (empty > 0)
            fen << empty;
        if (rank < 8 - 1)
            fen << '/';
    }
    // Add other FEN fields
    fen << ' ' << (activeColor == COLOR_WHITE ? 'w' : 'b');
    fen << ' ' << (castlingRights.empty() ? "-" : castlingRights);
    fen << ' ' << (enPassantTarget.empty() ? "-" : enPassantTarget);
    fen << ' ' << halfmoveClock;
    fen << ' ' << fullmoveNumber;
    return fen.str();
}

void Board::makeMove(const Move& move) {

    int fromIdx = move.from;
    int toIdx = move.to;

    // Bounds check before accessing squares
    if (fromIdx < 0 || fromIdx >= BOARD_SIZE * BOARD_SIZE) {
        return;
    }
    if (toIdx < 0 || toIdx >= BOARD_SIZE * BOARD_SIZE) {
        return;
    }

    // Move the piece
    // If capturing, clear the destination square first (important for king captures)
    if (move.capturedPiece.type() != NONE) {
        squares[toIdx] = Piece();
    }
    squares[toIdx] = squares[fromIdx];
    squares[fromIdx] = Piece(); // Clear the moved square

    // Defensive: don't move rook if from/to are out of bounds and only if rook exists
    auto safe_move_rook = [&](int from, int to) {
        if (from < 0 || from >= BOARD_SIZE * BOARD_SIZE) {
            return;
        }
        if (to < 0 || to >= BOARD_SIZE * BOARD_SIZE) {
            return;
        }
        if (squares[from].type() != ROOK) {
            return;
        }
        squares[to] = squares[from];
        squares[from] = Piece();
    };

    // Handle castling rook move
    if (move.flag == CASTLING) {
        // White king-side
        if (move.from == get1DIndex(4, 7) && move.to == get1DIndex(6, 7)) {
            safe_move_rook(get1DIndex(7, 7), get1DIndex(5, 7));
        }
        // White queen-side
        else if (move.from == get1DIndex(4, 7) && move.to == get1DIndex(2, 7)) {
            safe_move_rook(get1DIndex(0, 7), get1DIndex(3, 7));
        }
        // Black king-side
        else if (move.from == get1DIndex(4, 0) && move.to == get1DIndex(6, 0)) {
            safe_move_rook(get1DIndex(7, 0), get1DIndex(5, 0));
        }
        // Black queen-side
        else if (move.from == get1DIndex(4, 0) && move.to == get1DIndex(2, 0)) {
            safe_move_rook(get1DIndex(0, 0), get1DIndex(3, 0));
        }
    }

    // Handle captured piece
    if (move.capturedPiece.type() != NONE) {
        // Normally we would handle captured pieces here, e.g. remove from game state
    }

    // Update active color
    activeColor = (activeColor == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;

    // Update halfmove clock and fullmove number
    halfmoveClock++;
    if (activeColor == COLOR_WHITE) {
        fullmoveNumber++;
    }

    // Update castling rights if king or rook moves
    // White king moves
    if (move.movedPiece.type() == KING && move.movedPiece.color() == COLOR_WHITE) {
        castlingRights.erase(std::remove(castlingRights.begin(), castlingRights.end(), 'K'), castlingRights.end());
        castlingRights.erase(std::remove(castlingRights.begin(), castlingRights.end(), 'Q'), castlingRights.end());
    }
    // Black king moves
    if (move.movedPiece.type() == KING && move.movedPiece.color() == COLOR_BLACK) {
        castlingRights.erase(std::remove(castlingRights.begin(), castlingRights.end(), 'k'), castlingRights.end());
        castlingRights.erase(std::remove(castlingRights.begin(), castlingRights.end(), 'q'), castlingRights.end());
    }
    // White rook moves
    if (move.movedPiece.type() == ROOK && move.movedPiece.color() == COLOR_WHITE) {
        if (move.from == get1DIndex(0, 7)) // a1
            castlingRights.erase(std::remove(castlingRights.begin(), castlingRights.end(), 'Q'), castlingRights.end());
        if (move.from == get1DIndex(7, 7)) // h1
            castlingRights.erase(std::remove(castlingRights.begin(), castlingRights.end(), 'K'), castlingRights.end());
    }
    // Black rook moves
    if (move.movedPiece.type() == ROOK && move.movedPiece.color() == COLOR_BLACK) {
        if (move.from == get1DIndex(0, 0)) // a8
            castlingRights.erase(std::remove(castlingRights.begin(), castlingRights.end(), 'q'), castlingRights.end());
        if (move.from == get1DIndex(7, 0)) // h8
            castlingRights.erase(std::remove(castlingRights.begin(), castlingRights.end(), 'k'), castlingRights.end());
    }

    // Optionally print FEN after each move for debugging or logging
    // std::cout << getFEN() << std::endl;
}

void Board::saveStateForUndo() {
    undoStack.push_back(getFEN());
    redoStack.clear(); // Clear redo stack on new move
}

void Board::undoMove() {
    if (!undoStack.empty()) {
        redoStack.push_back(getFEN());
        std::string prevFEN = undoStack.back();
        undoStack.pop_back();
        setFromFEN(prevFEN);
    }
}

void Board::redoMove() {
    if (!redoStack.empty()) {
        undoStack.push_back(getFEN());
        std::string nextFEN = redoStack.back();
        redoStack.pop_back();
        setFromFEN(nextFEN);
    }
}

bool Board::isSquareAttacked(int square, int byColor) const {
    static thread_local int callDepth = 0;
    ++callDepth;
    static thread_local bool topLevel = true;
    if (topLevel) attackMemo.clear();
    topLevel = false;

    // Defensive: check square and color bounds
    if (square < 0 || square >= 64) {
        if (topLevel) topLevel = true;
        --callDepth;
        return false;
    }
    if (byColor != COLOR_WHITE && byColor != COLOR_BLACK) {
        if (topLevel) topLevel = true;
        --callDepth;
        return false;
    }
    // Defensive: check squares array size (assume squares is std::vector<Piece> or fixed array of 64)
    #ifdef __cpp_lib_span
    if (squares.size() < 64) {
        if (topLevel) topLevel = true;
        --callDepth;
        return false;
    }
    #endif

    AttackMemoKey key{square, byColor};
    auto it = attackMemo.find(key);
    if (it != attackMemo.end()) {
        if (topLevel) topLevel = true;
        --callDepth;
        return it->second;
    }
    bool attacked = false;

    int x = square % 8, y = square / 8;

    // Pawn attacks (fixed logic)
    if (byColor == COLOR_WHITE) {
        // White pawns attack from (y+1,x-1) and (y+1,x+1)
        if (x > 0 && y < 7) {
            int idx = (y + 1) * 8 + (x - 1);
            if (idx >= 0 && idx < 64 && squares[idx].type() == PAWN && squares[idx].color() == byColor) attacked = true;
        }
        if (x < 7 && y < 7) {
            int idx = (y + 1) * 8 + (x + 1);
            if (idx >= 0 && idx < 64 && squares[idx].type() == PAWN && squares[idx].color() == byColor) attacked = true;
        }
    } else if (byColor == COLOR_BLACK) {
        // Black pawns attack from (y-1,x-1) and (y-1,x+1)
        if (x > 0 && y > 0) {
            int idx = (y - 1) * 8 + (x - 1);
            if (idx >= 0 && idx < 64 && squares[idx].type() == PAWN && squares[idx].color() == byColor) attacked = true;
        }
        if (x < 7 && y > 0) {
            int idx = (y - 1) * 8 + (x + 1);
            if (idx >= 0 && idx < 64 && squares[idx].type() == PAWN && squares[idx].color() == byColor) attacked = true;
        }
    }
    // Knight attacks
    const int knightDeltas[8][2] = { {1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,1},{-2,-1},{-1,2} };
    for (int i = 0; i < 8; ++i) {
        int nx = x + knightDeltas[i][0], ny = y + knightDeltas[i][1];
        int idx = ny * 8 + nx;
        if (nx >= 0 && nx < 8 && ny >= 0 && ny < 8 && idx >= 0 && idx < 64 && squares[idx].type() == KNIGHT && squares[idx].color() == byColor) attacked = true;
    }
    // King attacks
    const int kingDeltas[8][2] = { {1,1},{1,0},{1,-1},{0,1},{0,-1},{-1,1},{-1,0},{-1,-1} };
    for (int i = 0; i < 8; ++i) {
        int nx = x + kingDeltas[i][0], ny = y + kingDeltas[i][1];
        int idx = ny * 8 + nx;
        if (nx >= 0 && nx < 8 && ny >= 0 && ny < 8 && idx >= 0 && idx < 64 && squares[idx].type() == KING && squares[idx].color() == byColor) attacked = true;
    }
    // Sliding pieces (rook/queen)
    const int rookDirs[4][2] = { {0,1},{1,0},{0,-1},{-1,0} };
    for (int d = 0; d < 4; ++d) {
        int nx = x, ny = y;
        while (true) {
            nx += rookDirs[d][0]; ny += rookDirs[d][1];
            if (nx < 0 || nx >= 8 || ny < 0 || ny >= 8) break;
            int idx = ny * 8 + nx;
            if (idx < 0 || idx >= 64) break;
            if (squares[idx].type() != NONE) {
                if ((squares[idx].type() == ROOK || squares[idx].type() == QUEEN) && squares[idx].color() == byColor) attacked = true;
                break;
            }
        }
    }
    // Sliding pieces (bishop/queen)
    const int bishopDirs[4][2] = { {1,1},{1,-1},{-1,-1},{-1,1} };
    for (int d = 0; d < 4; ++d) {
        int nx = x, ny = y;
        while (true) {
            nx += bishopDirs[d][0]; ny += bishopDirs[d][1];
            if (nx < 0 || nx >= 8 || ny < 0 || ny >= 8) break;
            int idx = ny * 8 + nx;
            if (idx < 0 || idx >= 64) break;
            if (squares[idx].type() != NONE) {
                if ((squares[idx].type() == BISHOP || squares[idx].type() == QUEEN) && squares[idx].color() == byColor) attacked = true;
                break;
            }
        }
    }
    attackMemo[key] = attacked;
    if (topLevel) topLevel = true;
    --callDepth;
    return attacked;
}

char Board::pieceToChar(Piece piece) const {
    switch (piece.type()) {
        case PAWN:   return piece.color() == COLOR_WHITE ? 'P' : 'p';
        case KNIGHT: return piece.color() == COLOR_WHITE ? 'N' : 'n';
        case BISHOP: return piece.color() == COLOR_WHITE ? 'B' : 'b';
        case ROOK:   return piece.color() == COLOR_WHITE ? 'R' : 'r';
        case QUEEN:  return piece.color() == COLOR_WHITE ? 'Q' : 'q';
        case KING:   return piece.color() == COLOR_WHITE ? 'K' : 'k';
        case NONE:   return '.';
        default:     return '?';
    }
}

void Board::printBoardState(const std::string& context) const {
}

void Board::printCastlingRights() const {
}

void Board::printKingPositions() const {
}

void Board::printActiveColor() const {
}

void Board::printEnPassantTarget() const {
}

void Board::printHalfmoveClock() const {
}

void Board::printFullmoveNumber() const {
}

int Board::get1DIndex(int file, int rank) {
    return rank * BOARD_SIZE + file;
}
int Board::get2DIndex(int index) {
    return index / BOARD_SIZE * BOARD_SIZE + index % BOARD_SIZE;
}
int Board::getRank(int index) {
    return index / BOARD_SIZE;
}
int Board::getFile(int index) {
    return index % BOARD_SIZE;
}