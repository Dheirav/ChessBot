#include "bb_position.hpp"
#include "bitboard_attacks.hpp"
#include "zobrist_hash.hpp"
#include <sstream>
#include <cctype>

namespace {

inline uint64_t pieceKey(int sq, BitboardPieceType t, BitboardColor c) {
    return ZobristHash::getPieceSquareHash(sq, toMailboxType(t), toMailboxColor(c));
}

// Castling rights are hashed as a whole-mask lookup rather than per-right, so a
// change is an XOR of the old value out and the new one in. Same for the en
// passant file, where "no target" hashes to zero and cancels itself.
inline uint64_t stateKeyDelta(uint8_t castleBefore, uint8_t castleAfter,
                              int epBefore, int epAfter) {
    return ZobristHash::getCastlingHash(castleBefore)
         ^ ZobristHash::getCastlingHash(castleAfter)
         ^ ZobristHash::getEnPassantHash(epBefore)
         ^ ZobristHash::getEnPassantHash(epAfter)
         ^ ZobristHash::getSideToMoveHash();
}

} // namespace

BitboardPieceType Position::pieceAt(int sq, BitboardColor& colorOut) const {
    const Bitboard bit = 1ULL << sq;
    for (int t = 0; t < 6; ++t) {
        if (white[t] & bit) { colorOut = BB_WHITE; return (BitboardPieceType)t; }
        if (black[t] & bit) { colorOut = BB_BLACK; return (BitboardPieceType)t; }
    }
    colorOut = BB_WHITE;
    return BB_NONE;
}

uint64_t Position::computeHash() const {
    uint64_t h = 0;
    for (int sq = 0; sq < 64; ++sq) {
        BitboardColor c;
        const BitboardPieceType t = pieceAt(sq, c);
        if (t != BB_NONE) h ^= pieceKey(sq, t, c);
    }
    if (sideToMove == BB_BLACK) h ^= ZobristHash::getSideToMoveHash();
    h ^= ZobristHash::getCastlingHash(castlingRights);
    h ^= ZobristHash::getEnPassantHash(enPassantSquare);
    return h;
}

uint64_t Position::computePawnHash() const {
    uint64_t h = 0;
    Bitboard b = white[BB_PAWN];
    while (b) { const int sq = lsb(b); b &= b - 1; h ^= pieceKey(sq, BB_PAWN, BB_WHITE); }
    b = black[BB_PAWN];
    while (b) { const int sq = lsb(b); b &= b - 1; h ^= pieceKey(sq, BB_PAWN, BB_BLACK); }
    return h;
}

void Position::syncSquares() {
    for (int sq = 0; sq < 64; ++sq) {
        BitboardColor c;
        const BitboardPieceType t = pieceAt(sq, c);
        squares[sq] = (t == BB_NONE) ? Piece()
                                     : Piece(toMailboxColor(c), toMailboxType(t));
    }
}

bool Position::consistent() const {
    Bitboard w = 0, b = 0;
    for (int t = 0; t < 6; ++t) { w |= white[t]; b |= black[t]; }
    if (w != occupancyWhite || b != occupancyBlack) return false;
    if ((occupancyWhite | occupancyBlack) != occupancyAll) return false;
    if (occupancyWhite & occupancyBlack) return false;
    for (int sq = 0; sq < 64; ++sq) {
        BitboardColor c;
        const BitboardPieceType t = pieceAt(sq, c);
        const Piece want = (t == BB_NONE) ? Piece()
                                          : Piece(toMailboxColor(c), toMailboxType(t));
        if (squares[sq].value != want.value) return false;
    }
    return hash == computeHash() && pawnHash == computePawnHash();
}

void Position::setFromFEN(const std::string& fen) {
    setBitboardFromFEN(*this, fen);

    // setBitboardFromFEN stops after the en passant field, so the two clocks
    // are parsed here. A missing clock field is a legal abbreviated FEN and
    // means "unknown", which the usual reading treats as zero moves played.
    halfmoveClock = 0;
    fullmoveNumber = 1;
    std::istringstream in(fen);
    std::string tok;
    for (int field = 0; in >> tok; ++field) {
        if (field == 4) halfmoveClock = std::atoi(tok.c_str());
        else if (field == 5) fullmoveNumber = std::atoi(tok.c_str());
    }

    syncSquares();
    hash = computeHash();
    pawnHash = computePawnHash();
    history.clear();
}

std::string Position::toFEN() const {
    static const char* GLYPH = "pnbrqk";
    std::string out;
    for (int rank = 0; rank < 8; ++rank) {
        int empty = 0;
        for (int file = 0; file < 8; ++file) {
            BitboardColor c;
            const BitboardPieceType t = pieceAt(rank * 8 + file, c);
            if (t == BB_NONE) { ++empty; continue; }
            if (empty) { out += char('0' + empty); empty = 0; }
            const char g = GLYPH[t];
            out += (c == BB_WHITE) ? char(std::toupper(g)) : g;
        }
        if (empty) out += char('0' + empty);
        if (rank != 7) out += '/';
    }
    out += (sideToMove == BB_WHITE) ? " w " : " b ";
    out += castlingRightsToString(castlingRights);
    out += ' ';
    if (enPassantSquare < 0) {
        out += '-';
    } else {
        out += char('a' + enPassantSquare % 8);
        out += char('1' + (7 - enPassantSquare / 8));
    }
    out += ' ' + std::to_string(halfmoveClock);
    out += ' ' + std::to_string(fullmoveNumber);
    return out;
}

PositionUndo Position::makeMove(const BitboardMove& move) {
    PositionUndo undo;
    undo.hashBefore = hash;
    undo.pawnHashBefore = pawnHash;
    undo.halfmoveBefore = halfmoveClock;
    undo.fullmoveBefore = fullmoveNumber;

    const BitboardColor us = sideToMove;
    const BitboardColor them = (us == BB_WHITE) ? BB_BLACK : BB_WHITE;
    const uint8_t castleBefore = castlingRights;
    const int epBefore = enPassantSquare;

    // The key delta is built from the move rather than from the resulting
    // board, so it has to mirror makeBitboardMove's piece moves exactly. Where
    // the two disagree the position stays right and the key goes wrong, which
    // is the failure mode that only shows up as TT collisions much later.
    uint64_t dh = 0, dp = 0;

    if (move.flag == BBM_EN_PASSANT) {
        const int capSq = (move.from / 8) * 8 + (move.to % 8);
        const uint64_t k = pieceKey(capSq, BB_PAWN, them);
        dh ^= k; dp ^= k;
    } else if (move.captured != BB_NONE) {
        dh ^= pieceKey(move.to, move.captured, them);
        if (move.captured == BB_PAWN) dp ^= pieceKey(move.to, BB_PAWN, them);
    }

    if (move.flag == BBM_PROMOTION) {
        const uint64_t from = pieceKey(move.from, BB_PAWN, us);
        dh ^= from ^ pieceKey(move.to, move.promotionType, us);
        dp ^= from;
    } else {
        dh ^= pieceKey(move.from, move.moved, us) ^ pieceKey(move.to, move.moved, us);
        if (move.moved == BB_PAWN) {
            dp ^= pieceKey(move.from, BB_PAWN, us) ^ pieceKey(move.to, BB_PAWN, us);
        }
    }

    if (move.flag == BBM_CASTLE) {
        const bool kingSide = (move.to > move.from);
        const int rookFrom = kingSide ? move.from + 3 : move.from - 4;
        const int rookTo   = kingSide ? move.from + 1 : move.from - 1;
        dh ^= pieceKey(rookFrom, BB_ROOK, us) ^ pieceKey(rookTo, BB_ROOK, us);
    }

    history.push_back(hash);
    undo.bb = makeBitboardMove(*this, move);

    // The square array follows the same four piece movements makeBitboardMove
    // performs. consistent() is what stops these two descriptions drifting.
    if (move.flag == BBM_EN_PASSANT)
        squares[(move.from / 8) * 8 + (move.to % 8)] = Piece();
    squares[move.from] = Piece();
    squares[move.to] = (move.flag == BBM_PROMOTION)
        ? Piece(toMailboxColor(us), toMailboxType(move.promotionType))
        : Piece(toMailboxColor(us), toMailboxType(move.moved));
    if (move.flag == BBM_CASTLE) {
        const bool kingSide = (move.to > move.from);
        squares[kingSide ? move.from + 3 : move.from - 4] = Piece();
        squares[kingSide ? move.from + 1 : move.from - 1] =
            Piece(toMailboxColor(us), ROOK);
    }

    hash ^= dh ^ stateKeyDelta(castleBefore, castlingRights, epBefore, enPassantSquare);
    pawnHash ^= dp;

    // Reset on a pawn move or a capture, which is what makes the fifty-move
    // rule and the repetition scan bounded.
    halfmoveClock = (move.moved == BB_PAWN || move.isCapture) ? 0 : halfmoveClock + 1;
    if (us == BB_BLACK) ++fullmoveNumber;
    return undo;
}

void Position::unmakeMove(const PositionUndo& undo) {
    const BitboardMove& move = undo.bb.move;
    const BitboardColor us = (sideToMove == BB_WHITE) ? BB_BLACK : BB_WHITE;
    const BitboardColor them = sideToMove;

    unmakeBitboardMove(*this, undo.bb);

    squares[move.from] = Piece(toMailboxColor(us), toMailboxType(move.moved));
    if (move.flag == BBM_EN_PASSANT) {
        squares[move.to] = Piece();
        squares[(move.from / 8) * 8 + (move.to % 8)] = Piece(toMailboxColor(them), PAWN);
    } else {
        squares[move.to] = (move.captured == BB_NONE)
            ? Piece()
            : Piece(toMailboxColor(them), toMailboxType(move.captured));
    }
    if (move.flag == BBM_CASTLE) {
        const bool kingSide = (move.to > move.from);
        squares[kingSide ? move.from + 1 : move.from - 1] = Piece();
        squares[kingSide ? move.from + 3 : move.from - 4] =
            Piece(toMailboxColor(us), ROOK);
    }
    hash = undo.hashBefore;
    pawnHash = undo.pawnHashBefore;
    halfmoveClock = undo.halfmoveBefore;
    fullmoveNumber = undo.fullmoveBefore;
    if (!history.empty()) history.pop_back();
}

Position::NullUndo Position::makeNullMove() {
    NullUndo u;
    u.hashBefore = hash;
    u.enPassantBefore = enPassantSquare;
    u.halfmoveBefore = halfmoveClock;

    hash ^= ZobristHash::getEnPassantHash(enPassantSquare)
          ^ ZobristHash::getEnPassantHash(-1);
    enPassantSquare = -1;
    sideToMove = (sideToMove == BB_WHITE) ? BB_BLACK : BB_WHITE;
    hash ^= ZobristHash::getSideToMoveHash();
    ++halfmoveClock;
    return u;
}

void Position::unmakeNullMove(const NullUndo& u) {
    sideToMove = (sideToMove == BB_WHITE) ? BB_BLACK : BB_WHITE;
    enPassantSquare = u.enPassantBefore;
    halfmoveClock = u.halfmoveBefore;
    hash = u.hashBefore;
}

bool Position::isRepetition(int count) const {
    // Only positions since the last irreversible move can repeat, and they
    // alternate side to move, so every second entry is the one to compare.
    int seen = 1;
    const int limit = (int)history.size();
    const int stop = limit - halfmoveClock;
    for (int i = limit - 2; i >= 0 && i >= stop; i -= 2) {
        if (history[i] == hash && ++seen >= count) return true;
    }
    return false;
}

Move toMailboxMove(const Position& pos, const BitboardMove& m) {
    const PieceColor us = toMailboxColor(pos.sideToMove);
    const PieceColor them = (us == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
    const Piece moved(us, toMailboxType(m.moved));
    const Piece captured = (m.captured == BB_NONE)
        ? Piece() : Piece(them, toMailboxType(m.captured));
    MoveFlag flag = NORMAL;
    switch (m.flag) {
        case BBM_CAPTURE:    flag = CAPTURE; break;
        case BBM_PROMOTION:  flag = PROMOTION; break;
        case BBM_EN_PASSANT: flag = EN_PASSANT; break;
        case BBM_CASTLE:     flag = CASTLING; break;
        default:             flag = NORMAL; break;   // BBM_DOUBLE_PUSH has no mailbox flag
    }
    const Piece promo = (m.flag == BBM_PROMOTION)
        ? Piece(us, toMailboxType(m.promotionType)) : Piece();
    return Move(m.from, m.to, moved, captured, flag, promo);
}

Position toPosition(const Board& board) {
    Position pos;
    static_cast<BitboardState&>(pos) = toBitboardState(board);
    pos.syncSquares();
    pos.halfmoveClock = board.halfmoveClock;
    pos.fullmoveNumber = board.fullmoveNumber;
    pos.hash = pos.computeHash();
    pos.pawnHash = pos.computePawnHash();
    return pos;
}
