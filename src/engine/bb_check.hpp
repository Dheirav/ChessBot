#pragma once
//
// "Does this move give check", answered from precomputed masks.
//
// The engine has never been able to ask this, and several things want it:
//
//   - Stockfish exempts checking moves from quiescence move-count pruning and
//     from capture futility. Without the exemption a tight limit prunes exactly
//     the forcing moves that decide positions, which is why this engine's
//     quiescence limit has to sit at 3 or 4 where Stockfish uses 2.
//   - a quiet move that gives check is worth a large ordering bonus.
//   - several of Stockfish's other pruning rules are only safe with it.
//
// The cost is one setup per node, not per move. From the enemy king square,
// the squares a piece of each type would have to stand on to check it are just
// that piece type's attack set *from the king square*, because every piece
// except the pawn attacks symmetrically. Then a move gives direct check if its
// destination is in the mask for the piece that arrives there.
//
// Discovered check is the other half: a piece of ours that stands between an
// enemy king and one of our own sliders gives check by moving away, unless it
// moves along the same line it was blocking.
//
#include "bb_position.hpp"

struct CheckInfo {
    // Squares from which a piece of each type would check the enemy king.
    // Indexed by BitboardPieceType; the king entry is unused, since a king
    // cannot give check.
    Bitboard checkSquares[6];
    // Our pieces that currently block a slider of ours from the enemy king.
    // Moving one gives discovered check unless it stays on the line.
    Bitboard blockers;
    int theirKing;
};

CheckInfo bbCheckInfo(const Position& pos);
bool bbGivesCheck(const Position& pos, const CheckInfo& ci, const BitboardMove& move);
