#pragma once
//
// Phase B4 of the bitboard replacement (docs/BITBOARD-REPLACEMENT.md).
//
// The evaluation on a Position. The acceptance test is exact integer equality
// with evaluation.cpp over the corpus, not agreement to within a few
// centipawns, because the whole replacement is judged on reproducing the
// mailbox search tree node for node and the evaluation is what that tree is
// shaped by.
//
// This is where the profile says the time actually goes: evaluate_details is
// 29.5 percent of the search at depth 9 and countPseudoLegalMoves, which is
// mobility called from inside it, is another 15. Mobility is the reason to do
// this at all. On a mailbox board it is a full pseudo-legal move generation per
// side per evaluated node; here it is a popcount of an attack set.
//
#include "bb_position.hpp"

#include "evaluation.hpp"

// The full evaluation, and the breakdown behind it. Same numbers as
// evaluate()/evaluate_details() on the same position, which tests/bbequiv
// checks as exact integer equality rather than as agreement.
int bbEvaluate(const Position& pos);
EvalDetails bbEvaluateDetails(const Position& pos);

// Mobility as evaluation.cpp counts it: pseudo-legal moves, castling excluded,
// promotions counted as four. Exposed separately because it is the term most
// likely to disagree and the one worth checking on its own.
int bbCountPseudoLegal(const Position& pos, BitboardColor color);
