#pragma once
//
// Phase B1 of the bitboard replacement (docs/BITBOARD-REPLACEMENT.md).
//
// A legal move generator that never makes a move to find out whether it is
// legal. The existing generateBitboardLegal does exactly that: it makes every
// pseudo-legal move, tests the king square, and unmakes. That is the same
// expensive thing movegen.cpp does, and it is the single largest reason the
// mailbox generator is slow, so carrying it into the replacement would leave
// the main prize on the table.
//
// Instead legality is decided from three masks computed once per node:
//
//   check mask   the squares a non-king move must land on to answer a check.
//                Empty under double check, since only the king can move.
//   pin mask     a pinned piece may move only along the line through its own
//                king, which lineThrough() gives directly.
//   king danger  the king may not step onto a square the enemy attacks with
//                the king itself removed from the occupancy, which is what
//                stops it walking backwards along a checking ray.
//
// En passant is the one case the masks cannot express, because the capture
// removes a pawn from a square that is neither the origin nor the destination
// and can expose a rank. It gets an explicit occupancy test, which is cheap
// because there is at most one en passant target in a position.
//
#include "bb_position.hpp"

// A fixed-size list. The old generator returned std::vector, which means a heap
// allocation per node, and the search calls this millions of times per second.
// 256 is above the highest known legal move count (218) with room to spare.
struct BBMoveList {
    static constexpr int MAX = 256;
    BitboardMove moves[MAX];
    int count = 0;

    void clear() { count = 0; }
    int size() const { return count; }
    bool empty() const { return count == 0; }
    BitboardMove& operator[](int i) { return moves[i]; }
    const BitboardMove& operator[](int i) const { return moves[i]; }
    BitboardMove* begin() { return moves; }
    BitboardMove* end() { return moves + count; }
    const BitboardMove* begin() const { return moves; }
    const BitboardMove* end() const { return moves + count; }
};

enum BBGenType {
    BB_GEN_ALL,       // every legal move
    BB_GEN_CAPTURES,  // captures, en passant and promotions: the quiescence set
    BB_GEN_QUIETS,    // everything BB_GEN_CAPTURES does not emit
};

void bbGenerate(const Position& pos, BBMoveList& out, BBGenType type = BB_GEN_ALL);

// True when the side to move is in check. Free once checkers() has been called,
// but generation does not always need to be run just to ask.
bool bbInCheck(const Position& pos);
