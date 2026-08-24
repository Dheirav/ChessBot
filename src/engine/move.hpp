#pragma once
#include "piece.hpp"
#include <cstdint>
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

// A move squeezed into 16 bits, for the transposition table and nothing else.
//
// The table holds millions of these and its entry size divides into
// ENTRIES_PER_MB, so a 20-byte Move in every slot is the difference between a
// table that outlives a search and one that is recycled through it. Measured
// 2026-08-25: the median search was 9.9M nodes against 6.7M slots.
//
// Only the fields `operator==` compares survive the round trip — from, to,
// flag, and the promotion type. That is not a compromise: the sole use of an
// unpacked move is `std::find` against a freshly generated list, which
// supplies the moved and captured pieces itself.
//
// Layout: from in bits 0-5, to in 6-11, and a 3-bit code in 12-14 that folds
// the flag and the promotion type together. It fits in three bits only
// because movegen tags *every* promotion `PROMOTION`, capture or not
// (`movegen.cpp:150`), so the four promotion pieces can take codes 4-7 without
// a separate capture bit.
//
// 0 means "no move". `from == to == 0` is not a legal move, so the sentinel
// cannot collide with a real one.
inline uint16_t packMove(const Move& m) {
    if (m.from < 0 || m.to < 0) return 0;
    unsigned code;
    switch (m.flag) {
        case CAPTURE:    code = 1; break;
        case EN_PASSANT: code = 2; break;
        case CASTLING:   code = 3; break;
        case PROMOTION:  code = 4u + (unsigned(m.promotionPiece.type()) - unsigned(KNIGHT)); break;
        default:         code = 0; break;
    }
    return uint16_t(unsigned(m.from) | (unsigned(m.to) << 6) | (code << 12));
}

inline Move unpackMove(uint16_t packed) {
    if (packed == 0) return Move();
    Move m;
    m.from = int(packed & 63u);
    m.to   = int((packed >> 6) & 63u);
    const unsigned code = (packed >> 12) & 7u;
    if (code >= 4u) {
        m.flag = PROMOTION;
        // The colour is arbitrary: operator== compares promotionPiece.type()
        // and nothing else ever reads an unpacked move's pieces.
        m.promotionPiece = Piece(COLOR_WHITE, PieceType(unsigned(KNIGHT) + code - 4u));
    } else {
        m.flag = code == 1u ? CAPTURE
               : code == 2u ? EN_PASSANT
               : code == 3u ? CASTLING
                            : NORMAL;
    }
    return m;
}

// UCI long algebraic: "e2e4", "e7e8q".
//
// Distinct from Move::toString(), which writes "e7e8=Q" for humans and is baked
// into the bench signature. Kept here because three separate places needed the
// wire format — the UCI layer, the match harness driving external engines, and
// game review — and the third copy was the moment to stop.
std::string toUciMove(const Move& m);