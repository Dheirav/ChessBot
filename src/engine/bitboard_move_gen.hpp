#pragma once
#include "bitboard.hpp"
#include <vector>

// Bitboard move generation.
//
// Square indexing matches Board: a8 is bit 0, h1 is bit 63, white moves toward
// lower indices. See bitboard.cpp for why this module does not use the usual
// a1 = 0 convention.
//
// Correctness here is established by perft: tests/bitboard_test.cpp counts the
// leaf nodes of the legal move tree through this generator and compares against
// the same published reference values that guard movegen.cpp. A move generator
// that has not been perft-checked is not a move generator, it is a guess.

enum BitboardMoveFlag : uint8_t {
    BBM_NORMAL,
    BBM_CAPTURE,
    BBM_DOUBLE_PUSH,   // sets an en passant target
    BBM_EN_PASSANT,    // the captured pawn is not on the destination square
    BBM_CASTLE,        // the rook moves too; a bare king move cannot express it
    BBM_PROMOTION,     // may also be a capture; promotionType says which piece
};

struct BitboardMove {
    uint8_t from = 0;
    uint8_t to = 0;
    BitboardMoveFlag flag = BBM_NORMAL;
    BitboardPieceType moved = BB_NONE;
    BitboardPieceType captured = BB_NONE;      // BB_NONE when not a capture
    BitboardPieceType promotionType = BB_NONE; // only when flag is BBM_PROMOTION
    bool isCapture = false;                    // true for promotion-captures too
};

using BitboardMoveList = std::vector<BitboardMove>;

// Everything needed to reverse a move. Small and trivially copyable, so the
// search can keep one per ply on the stack.
struct BitboardUndo {
    BitboardMove move;
    uint8_t castlingBefore = 0;
    int enPassantBefore = -1;
};

// Pseudo-legal moves: may leave the mover's own king attacked.
void generateBitboardPseudoLegal(const BitboardState& state, BitboardColor color,
                                 BitboardMoveList& moves);

// Fully legal moves. Castling legality (the king may not start in, pass
// through, or land on an attacked square) is resolved during generation; the
// rest is resolved by making each move and testing the king square.
void generateBitboardLegal(BitboardState& state, BitboardColor color,
                           BitboardMoveList& moves);

// Apply and reverse. unmake restores the state exactly, including castling
// rights, the en passant target and all occupancy.
BitboardUndo makeBitboardMove(BitboardState& state, const BitboardMove& move);
void unmakeBitboardMove(BitboardState& state, const BitboardUndo& undo);
