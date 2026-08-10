#include "board.hpp"
#include "fen.hpp"
#include "piece.hpp"
#include "zobrist_hash.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <sstream>
#include <unordered_map>

Board::Board() {
    ZobristHash::initialize();
    currentHash = 0;
    setFromFEN(INITIAL_FEN);
}

// Lightweight constructor: leaves the position uninitialized, so the caller
// must populate it (see copyForSearch). Avoids FEN parsing and hash setup.
Board::Board(SearchCopyTag) : currentHash(0) {}

Board Board::copyForSearch() const {
    Board copy(SearchCopyTag{});
    std::memcpy(copy.squares, squares, sizeof(squares));
    copy.activeColor = activeColor;
    copy.castlingRights = castlingRights;
    copy.enPassantTarget = enPassantTarget;
    copy.halfmoveClock = halfmoveClock;
    copy.fullmoveNumber = fullmoveNumber;
    copy.currentHash = currentHash;
    return copy;
}

bool Board::setFromFEN(const std::string& fen) {
    FENInfo info;
    bool result = parseFEN(fen, *this, info);
    if (result) {
        updateHash();
    }
    return result;
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

UndoInfo Board::makeMove(const Move& move) {
    UndoInfo u;
    u.hashBefore = currentHash;
    u.from = move.from;
    u.to = move.to;
    u.movedPiece = squares[move.from];
    u.castlingBefore = castlingRights;
    u.enPassantBefore = enPassantTarget;
    u.halfmoveBefore = halfmoveClock;
    u.fullmoveBefore = fullmoveNumber;
    u.castlingRookFrom = -1;
    u.castlingRookTo = -1;

    // Bounds check before accessing squares
    if (u.from < 0 || u.from >= BOARD_SIZE * BOARD_SIZE ||
        u.to < 0 || u.to >= BOARD_SIZE * BOARD_SIZE) {
        return u;
    }

    uint64_t h = currentHash;

    // Determine the piece actually captured and where it is removed from
    int capturedSq = -1;
    Piece captured;
    if (move.flag == EN_PASSANT) {
        capturedSq = (u.movedPiece.color() == COLOR_WHITE) ? move.to + 8 : move.to - 8;
        if (capturedSq >= 0 && capturedSq < BOARD_SIZE * BOARD_SIZE) {
            captured = squares[capturedSq];
        }
    } else {
        captured = squares[move.to];
        if (captured.type() != NONE) {
            capturedSq = move.to;
        }
    }
    u.capturedPiece = captured;
    u.capturedSquare = capturedSq;

    // Remove the moved piece from its source square
    h ^= ZobristHash::getPieceSquareHash(move.from, u.movedPiece.type(), u.movedPiece.color());
    squares[move.from] = Piece();

    // Remove the captured piece
    if (captured.type() != NONE && capturedSq >= 0) {
        h ^= ZobristHash::getPieceSquareHash(capturedSq, captured.type(), captured.color());
        squares[capturedSq] = Piece();
    }

    // Place the moved (or promoted) piece on the destination square
    if (move.flag == PROMOTION && move.promotionPiece.type() != NONE) {
        const Piece& promo = move.promotionPiece;
        h ^= ZobristHash::getPieceSquareHash(move.to, promo.type(), promo.color());
        squares[move.to] = promo;
    } else {
        h ^= ZobristHash::getPieceSquareHash(move.to, u.movedPiece.type(), u.movedPiece.color());
        squares[move.to] = u.movedPiece;
    }

    // Handle castling rook move
    if (move.flag == CASTLING) {
        int rookFrom = -1, rookTo = -1;
        if (u.movedPiece.color() == COLOR_WHITE) {
            if (move.from == get1DIndex(4, 7) && move.to == get1DIndex(6, 7)) {
                rookFrom = get1DIndex(7, 7); rookTo = get1DIndex(5, 7);
            } else if (move.from == get1DIndex(4, 7) && move.to == get1DIndex(2, 7)) {
                rookFrom = get1DIndex(0, 7); rookTo = get1DIndex(3, 7);
            }
        } else {
            if (move.from == get1DIndex(4, 0) && move.to == get1DIndex(6, 0)) {
                rookFrom = get1DIndex(7, 0); rookTo = get1DIndex(5, 0);
            } else if (move.from == get1DIndex(4, 0) && move.to == get1DIndex(2, 0)) {
                rookFrom = get1DIndex(0, 0); rookTo = get1DIndex(3, 0);
            }
        }
        if (rookFrom >= 0 && squares[rookFrom].type() == ROOK) {
            const Piece rook = squares[rookFrom];
            h ^= ZobristHash::getPieceSquareHash(rookFrom, rook.type(), rook.color());
            squares[rookFrom] = Piece();
            h ^= ZobristHash::getPieceSquareHash(rookTo, rook.type(), rook.color());
            squares[rookTo] = rook;
            u.castlingRookFrom = rookFrom;
            u.castlingRookTo = rookTo;
        }
    }

    // Update castling rights if the king or a rook moves
    if (u.movedPiece.type() == KING) {
        if (u.movedPiece.color() == COLOR_WHITE) {
            castlingRights.erase(std::remove(castlingRights.begin(), castlingRights.end(), 'K'), castlingRights.end());
            castlingRights.erase(std::remove(castlingRights.begin(), castlingRights.end(), 'Q'), castlingRights.end());
        } else {
            castlingRights.erase(std::remove(castlingRights.begin(), castlingRights.end(), 'k'), castlingRights.end());
            castlingRights.erase(std::remove(castlingRights.begin(), castlingRights.end(), 'q'), castlingRights.end());
        }
    } else if (u.movedPiece.type() == ROOK) {
        if (u.movedPiece.color() == COLOR_WHITE) {
            if (move.from == get1DIndex(0, 7)) // a1
                castlingRights.erase(std::remove(castlingRights.begin(), castlingRights.end(), 'Q'), castlingRights.end());
            if (move.from == get1DIndex(7, 7)) // h1
                castlingRights.erase(std::remove(castlingRights.begin(), castlingRights.end(), 'K'), castlingRights.end());
        } else {
            if (move.from == get1DIndex(0, 0)) // a8
                castlingRights.erase(std::remove(castlingRights.begin(), castlingRights.end(), 'q'), castlingRights.end());
            if (move.from == get1DIndex(7, 0)) // h8
                castlingRights.erase(std::remove(castlingRights.begin(), castlingRights.end(), 'k'), castlingRights.end());
        }
    }

    // Revoke castling rights when a rook is captured on its home square
    if (captured.type() == ROOK) {
        if (capturedSq == get1DIndex(0, 7)) // a1 rook captured
            castlingRights.erase(std::remove(castlingRights.begin(), castlingRights.end(), 'Q'), castlingRights.end());
        if (capturedSq == get1DIndex(7, 7)) // h1 rook captured
            castlingRights.erase(std::remove(castlingRights.begin(), castlingRights.end(), 'K'), castlingRights.end());
        if (capturedSq == get1DIndex(0, 0)) // a8 rook captured
            castlingRights.erase(std::remove(castlingRights.begin(), castlingRights.end(), 'q'), castlingRights.end());
        if (capturedSq == get1DIndex(7, 0)) // h8 rook captured
            castlingRights.erase(std::remove(castlingRights.begin(), castlingRights.end(), 'k'), castlingRights.end());
    }

    // Update en passant target square
    std::string newEp;
    if (u.movedPiece.type() == PAWN) {
        int rankDiff = abs((move.to / 8) - (move.from / 8));
        if (rankDiff == 2) {
            int targetSquare = (move.from + move.to) / 2; // Square between from and to
            int file = targetSquare % 8;
            int rank = targetSquare / 8;
            // Convert to chess notation (a1-h8)
            newEp = static_cast<char>('a' + file);
            newEp += static_cast<char>('1' + (7 - rank));
        }
    }

    // Update hash for castling rights and en passant changes
    h ^= ZobristHash::getCastlingHash(u.castlingBefore) ^ ZobristHash::getCastlingHash(castlingRights);
    h ^= ZobristHash::getEnPassantHash(u.enPassantBefore) ^ ZobristHash::getEnPassantHash(newEp);
    enPassantTarget = newEp;

    // Update halfmove clock and fullmove number
    if (u.movedPiece.type() == PAWN || captured.type() != NONE || move.flag == PROMOTION) {
        halfmoveClock = 0;
    } else {
        halfmoveClock++;
    }

    activeColor = (activeColor == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
    if (activeColor == COLOR_WHITE) {
        fullmoveNumber++;
    }

    // Toggle the side-to-move component of the hash
    h ^= ZobristHash::getSideToMoveHash();

    currentHash = h;
    return u;
}

NullUndo Board::makeNullMove() {
    NullUndo u;
    u.hashBefore = currentHash;
    u.enPassantBefore = enPassantTarget;
    u.halfmoveBefore = halfmoveClock;

    uint64_t h = currentHash;
    // Passing the turn clears any en passant right, exactly as a normal move
    // that is not a double pawn push does.
    h ^= ZobristHash::getEnPassantHash(enPassantTarget) ^ ZobristHash::getEnPassantHash("-");
    enPassantTarget = "-";
    activeColor = (activeColor == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
    h ^= ZobristHash::getSideToMoveHash();
    ++halfmoveClock;
    currentHash = h;
    return u;
}

void Board::unmakeNullMove(const NullUndo& u) {
    activeColor = (activeColor == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
    enPassantTarget = u.enPassantBefore;
    halfmoveClock = u.halfmoveBefore;
    currentHash = u.hashBefore;
}

void Board::unmakeMove(const UndoInfo& u) {
    if (u.from < 0 || u.from >= BOARD_SIZE * BOARD_SIZE ||
        u.to < 0 || u.to >= BOARD_SIZE * BOARD_SIZE) {
        return;
    }

    // Restore the moved piece to its source square
    squares[u.from] = u.movedPiece;

    // Clear the destination square and restore the captured piece (if any).
    // For en passant the captured square differs from the destination square.
    squares[u.to] = Piece();
    if (u.capturedPiece.type() != NONE && u.capturedSquare >= 0 && u.capturedSquare < BOARD_SIZE * BOARD_SIZE) {
        squares[u.capturedSquare] = u.capturedPiece;
    }

    // Restore the castling rook
    if (u.castlingRookFrom >= 0 && u.castlingRookTo >= 0 &&
        u.castlingRookFrom < BOARD_SIZE * BOARD_SIZE && u.castlingRookTo < BOARD_SIZE * BOARD_SIZE) {
        squares[u.castlingRookTo] = Piece();
        squares[u.castlingRookFrom] = Piece(u.movedPiece.color(), ROOK);
    }

    // Restore game state
    activeColor = (activeColor == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
    castlingRights = u.castlingBefore;
    enPassantTarget = u.enPassantBefore;
    halfmoveClock = u.halfmoveBefore;
    fullmoveNumber = u.fullmoveBefore;

    // Restore the hash from before the move (simplest and always correct)
    currentHash = u.hashBefore;
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
    // Defensive: check square and color bounds
    if (square < 0 || square >= BOARD_SIZE * BOARD_SIZE) {
        return false;
    }
    if (byColor != COLOR_WHITE && byColor != COLOR_BLACK) {
        return false;
    }

    int x = square % 8, y = square / 8;

    // Pawn attacks
    if (byColor == COLOR_WHITE) {
        // White pawns attack from (y+1,x-1) and (y+1,x+1)
        if (x > 0 && y < 7) {
            int idx = (y + 1) * 8 + (x - 1);
            if (squares[idx].type() == PAWN && squares[idx].color() == byColor) return true;
        }
        if (x < 7 && y < 7) {
            int idx = (y + 1) * 8 + (x + 1);
            if (squares[idx].type() == PAWN && squares[idx].color() == byColor) return true;
        }
    } else {
        // Black pawns attack from (y-1,x-1) and (y-1,x+1)
        if (x > 0 && y > 0) {
            int idx = (y - 1) * 8 + (x - 1);
            if (squares[idx].type() == PAWN && squares[idx].color() == byColor) return true;
        }
        if (x < 7 && y > 0) {
            int idx = (y - 1) * 8 + (x + 1);
            if (squares[idx].type() == PAWN && squares[idx].color() == byColor) return true;
        }
    }
    // Knight attacks
    const int knightDeltas[8][2] = { {1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,1},{-2,-1},{-1,2} };
    for (int i = 0; i < 8; ++i) {
        int nx = x + knightDeltas[i][0], ny = y + knightDeltas[i][1];
        if (nx >= 0 && nx < 8 && ny >= 0 && ny < 8) {
            int idx = ny * 8 + nx;
            if (squares[idx].type() == KNIGHT && squares[idx].color() == byColor) return true;
        }
    }
    // King attacks
    const int kingDeltas[8][2] = { {1,1},{1,0},{1,-1},{0,1},{0,-1},{-1,1},{-1,0},{-1,-1} };
    for (int i = 0; i < 8; ++i) {
        int nx = x + kingDeltas[i][0], ny = y + kingDeltas[i][1];
        if (nx >= 0 && nx < 8 && ny >= 0 && ny < 8) {
            int idx = ny * 8 + nx;
            if (squares[idx].type() == KING && squares[idx].color() == byColor) return true;
        }
    }
    // Sliding pieces (rook/queen)
    const int rookDirs[4][2] = { {0,1},{1,0},{0,-1},{-1,0} };
    for (int d = 0; d < 4; ++d) {
        int nx = x, ny = y;
        while (true) {
            nx += rookDirs[d][0]; ny += rookDirs[d][1];
            if (nx < 0 || nx >= 8 || ny < 0 || ny >= 8) break;
            int idx = ny * 8 + nx;
            if (squares[idx].type() != NONE) {
                if ((squares[idx].type() == ROOK || squares[idx].type() == QUEEN) && squares[idx].color() == byColor) return true;
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
            if (squares[idx].type() != NONE) {
                if ((squares[idx].type() == BISHOP || squares[idx].type() == QUEEN) && squares[idx].color() == byColor) return true;
                break;
            }
        }
    }
    return false;
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

uint64_t Board::computeHash() const {
    uint64_t hash = 0;
    
    // Hash pieces on squares
    for (int square = 0; square < 64; ++square) {
        const Piece& piece = squares[square];
        if (piece.type() != NONE) {
            hash ^= ZobristHash::getPieceSquareHash(square, piece.type(), piece.color());
        }
    }
    
    // Hash side to move
    if (activeColor == COLOR_BLACK) {
        hash ^= ZobristHash::getSideToMoveHash();
    }
    
    // Hash castling rights
    hash ^= ZobristHash::getCastlingHash(castlingRights);
    
    // Hash en passant target
    hash ^= ZobristHash::getEnPassantHash(enPassantTarget);
    
    return hash;
}

void Board::updateHash() {
    currentHash = computeHash();
}