#pragma once
#include "bitboard.hpp"
#include "board.hpp"

// Attack sets over bitboards.
//
// This is the layer worth having. A bitboard *move generator* was measured at a
// ~1.02x ceiling for the whole search, because move generation is only about 2%
// of search time (BACKLOG.md 4.0) — but that measurement is about replacing
// movegen.cpp, not about bitboards. What bitboards actually buy is shared
// infrastructure that several expensive things read:
//
//   - attackersTo() is the natural form of Static Exchange Evaluation, which is
//     about to run at every node. The mailbox version ray-scans eight
//     directions per query.
//   - checkers() and blockersForKing() are what pin-aware legal move generation
//     is built on. The current legality filter — make every candidate move and
//     test the king square — is the single largest item in the profile
//     (PLAN.md 5.5).
//   - popcount over an attack set is mobility, and pawn structure is a handful
//     of shifts.
//
// Square indexing matches Board exactly: index = y * 8 + x, with y = 0 the
// eighth rank. White therefore moves toward *lower* indices. Sliders do not
// care about orientation, but pawns do, and getting that backwards is the
// classic way this module goes silently wrong.
//
// Every function here is verified against the mailbox implementation in
// tests/bitboard_test.cpp, on the principle that a second implementation is
// only worth having if it is checked against the first.

// Fills the leaper tables and the magic tables. Must be called before anything
// else here. Cheap, idempotent.
void initBitboardAttacks();

// Leapers. Precomputed, so these are a single array read.
Bitboard knightAttacks(int sq);
Bitboard kingAttacks(int sq);
Bitboard pawnAttacks(BitboardColor side, int sq);

// Sliders. Occupancy is every piece on the board, of either colour; the
// returned set includes the first blocker in each direction, which is what
// makes it usable for both captures and defence.
Bitboard rookAttacks(int sq, Bitboard occupancy);
Bitboard bishopAttacks(int sq, Bitboard occupancy);
Bitboard queenAttacks(int sq, Bitboard occupancy);

// Every piece of either colour that attacks `sq`, given an occupancy. Passing
// an occupancy that differs from the state's own is deliberate and is what SEE
// needs: it asks "who attacks this square once these pieces have been removed",
// which is how x-rays are discovered.
Bitboard attackersTo(const BitboardState& state, int sq, Bitboard occupancy);

// The same, restricted to one side.
Bitboard attackersTo(const BitboardState& state, int sq, Bitboard occupancy,
                     BitboardColor bySide);

// Is `sq` attacked by `bySide`? Uses the state's own occupancy.
bool isSquareAttackedBB(const BitboardState& state, int sq, BitboardColor bySide);

// The square of `side`'s king, or -1 if it has none (test positions may not).
int kingSquare(const BitboardState& state, BitboardColor side);

// The enemy pieces giving check to `side` right now. Empty means not in check;
// more than one bit means only a king move can be legal.
Bitboard checkers(const BitboardState& state, BitboardColor side);

// Pieces of `side` standing between their own king and an enemy slider — the
// pinned pieces. A pinned piece may only move along the pin ray.
//
// `pinners`, if given, receives the enemy sliders doing the pinning, in the
// same order sense: the pinner on a pinned piece's ray is the one that shares a
// line with the king.
Bitboard blockersForKing(const BitboardState& state, BitboardColor side,
                         Bitboard* pinners = nullptr);

// Squares on the line strictly between two squares, empty if they do not share
// a rank, file or diagonal. Used to answer "does this move stay on the pin ray"
// and "can this piece block the check".
Bitboard betweenSquares(int a, int b);

// The full line through two squares, including both ends, empty if they are not
// aligned. A pinned piece may move anywhere on this line.
Bitboard lineThrough(int a, int b);

// Build a BitboardState from the engine's mailbox Board. The two
// representations are kept in step this way rather than by duplicating FEN
// parsing, so there is one place where a position is interpreted.
BitboardState toBitboardState(const Board& board);
