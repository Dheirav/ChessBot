#include "see.hpp"

#include <algorithm>
#include <cstdint>

namespace {

// SEE's own piece values, indexed by PieceType (NONE, KING, PAWN, KNIGHT,
// BISHOP, ROOK, QUEEN). Rounder than the evaluation's, and deliberately so:
// this is counting material on a single square, not judging a position, and
// small positional adjustments to the evaluation should not silently change
// which captures the search prunes.
//
// The king's value is not a real value. It exists so that "capture with the
// king" is only ever chosen when nothing else can, and so that a king capture
// followed by a recapture scores as catastrophic — which is exactly what it
// would be, and why the rules forbid it.
constexpr int SEE_VALUE[7] = {
    0,      // NONE
    20000,  // KING
    100,    // PAWN
    320,    // KNIGHT
    330,    // BISHOP
    500,    // ROOK
    900,    // QUEEN
};

// A square is "gone" once its piece has joined the exchange. Squares are marked
// here rather than mutated on the board, so nothing is ever unmade and the
// caller's position is untouched.
using Removed = uint64_t;
inline bool isGone(Removed r, int sq)   { return (r >> sq) & 1ULL; }
inline Removed markGone(Removed r, int sq) { return r | (1ULL << sq); }

inline bool occupied(const Board& b, Removed removed, int sq) {
    return b.squares[sq].type() != NONE && !isGone(removed, sq);
}

// Walks outward from `sq` along one direction and returns the first square
// still holding a piece, or -1. Because removed squares are treated as empty,
// this is what makes x-rays work: once the bishop in front is spent, the same
// scan finds the queen behind it.
int firstOccupiedAlong(const Board& board, Removed removed, int sq, int dx, int dy) {
    int x = sq % 8, y = sq / 8;
    while (true) {
        x += dx;
        y += dy;
        if (x < 0 || x > 7 || y < 0 || y > 7) return -1;
        int idx = y * 8 + x;
        if (occupied(board, removed, idx)) return idx;
    }
}

// The cheapest piece of `side` that can capture on `sq`, as a square index, or
// -1 if there is none. Checked in increasing value so the exchange always
// recaptures with the least it can afford to lose.
int leastValuableAttacker(const Board& board, Removed removed, int sq, PieceColor side) {
    const int x = sq % 8, y = sq / 8;

    auto pieceAt = [&](int idx, PieceType type) {
        return occupied(board, removed, idx) &&
               board.squares[idx].type() == type &&
               board.squares[idx].color() == side;
    };

    // Pawns. A white pawn attacking sq sits one rank below it on the board's
    // indexing (white moves toward decreasing y); a black pawn one above.
    // This mirrors isSquareAttacked() in movegen.cpp exactly.
    const int pawnY = (side == COLOR_WHITE) ? y + 1 : y - 1;
    if (pawnY >= 0 && pawnY <= 7) {
        if (x > 0 && pieceAt(pawnY * 8 + x - 1, PAWN)) return pawnY * 8 + x - 1;
        if (x < 7 && pieceAt(pawnY * 8 + x + 1, PAWN)) return pawnY * 8 + x + 1;
    }

    static const int KNIGHT_D[8][2] = { {1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,-1},{-2,1},{-1,2} };
    for (const auto& d : KNIGHT_D) {
        int nx = x + d[0], ny = y + d[1];
        if (nx < 0 || nx > 7 || ny < 0 || ny > 7) continue;
        if (pieceAt(ny * 8 + nx, KNIGHT)) return ny * 8 + nx;
    }

    static const int BISHOP_D[4][2] = { {1,1},{1,-1},{-1,1},{-1,-1} };
    static const int ROOK_D[4][2]   = { {0,1},{0,-1},{1,0},{-1,0} };

    // Bishops before rooks before queens, so the cheapest slider goes first.
    int queenSquare = -1;
    for (const auto& d : BISHOP_D) {
        int idx = firstOccupiedAlong(board, removed, sq, d[0], d[1]);
        if (idx < 0 || board.squares[idx].color() != side) continue;
        PieceType t = board.squares[idx].type();
        if (t == BISHOP) return idx;
        if (t == QUEEN && queenSquare < 0) queenSquare = idx;
    }
    for (const auto& d : ROOK_D) {
        int idx = firstOccupiedAlong(board, removed, sq, d[0], d[1]);
        if (idx < 0 || board.squares[idx].color() != side) continue;
        PieceType t = board.squares[idx].type();
        if (t == ROOK) return idx;
        if (t == QUEEN && queenSquare < 0) queenSquare = idx;
    }
    if (queenSquare >= 0) return queenSquare;

    static const int KING_D[8][2] = { {1,1},{1,0},{1,-1},{0,1},{0,-1},{-1,1},{-1,0},{-1,-1} };
    for (const auto& d : KING_D) {
        int nx = x + d[0], ny = y + d[1];
        if (nx < 0 || nx > 7 || ny < 0 || ny > 7) continue;
        if (pieceAt(ny * 8 + nx, KING)) return ny * 8 + nx;
    }

    return -1;
}

}  // namespace

int see(const Board& board, const Move& move) {
    const int target = move.to;
    Removed removed = 0;

    // What the first capture wins.
    int captured;
    if (move.flag == EN_PASSANT) {
        // The captured pawn is not on the destination square; it sits beside
        // the moving pawn, on the destination's file at the mover's rank.
        int capturedSq = (move.from / 8) * 8 + (move.to % 8);
        removed = markGone(removed, capturedSq);
        captured = SEE_VALUE[PAWN];
    } else {
        captured = SEE_VALUE[board.squares[target].type()];
    }

    // The piece that ends up standing on the target square, which is what the
    // opponent stands to win by recapturing. For a promotion that is the new
    // piece, and the promotion itself gains the difference.
    PieceType standing = move.movedPiece.type();
    if (move.flag == PROMOTION) {
        standing = move.promotionPiece.type();
        captured += SEE_VALUE[standing] - SEE_VALUE[PAWN];
    }

    removed = markGone(removed, move.from);

    // gain[d] is the score for the side to move at depth d, assuming the
    // exchange continues. Built forwards, then resolved backwards, because each
    // side may decline to recapture — which is what the max() below encodes.
    int gain[36];
    int d = 0;
    gain[0] = captured;

    PieceColor side = (move.movedPiece.color() == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;

    while (true) {
        int fromSq = leastValuableAttacker(board, removed, target, side);
        if (fromSq < 0) break;

        ++d;
        gain[d] = SEE_VALUE[standing] - gain[d - 1];

        // Neither side will continue an exchange that is losing for them even
        // in the best case, so once both the running score and its refutation
        // are negative the rest cannot matter.
        if (std::max(-gain[d - 1], gain[d]) < 0) break;

        standing = board.squares[fromSq].type();
        removed = markGone(removed, fromSq);
        side = (side == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;

        if (d >= 33) break;  // far beyond any real exchange; guards the array
    }

    // Resolve backwards: at each step the side to move takes the exchange only
    // if it beats standing pat. This must run down to and including d == 1 —
    // that step is the single recapture, which is the whole answer for the
    // commonest case of all (capture a defended piece, get taken back).
    while (d > 0) {
        gain[d - 1] = -std::max(-gain[d - 1], gain[d]);
        --d;
    }
    return gain[0];
}
