#pragma once
#include "board.hpp"
#include "move.hpp"

// Static Exchange Evaluation.
//
// Answers "if I play this capture and both sides then keep recapturing on that
// square with their cheapest available attacker, what do I end up ahead by?" —
// in centipawns, from the moving side's point of view. Negative means the
// capture loses material.
//
// This is what separates a capture that wins a pawn from one that hangs a
// queen, and neither MVV-LVA nor a shallow search reliably tells them apart.
// MVV-LVA sorts QxP above PxQ's refutation because it only looks at the victim;
// SEE plays the whole exchange out.
//
// Two intended uses (both PLAN.md 3.2, both gated on a match):
//   - move ordering: sort captures by SEE rather than by victim value alone
//   - quiescence: skip captures with SEE < 0 instead of searching every one
//
// Exchanges are resolved on the destination square only. That is the standard
// simplification: it ignores intermediate tactics such as pins, deflections and
// in-between moves, and is what makes SEE cheap enough to run at every node.
// X-rays are handled — removing a piece from the exchange re-scans through it,
// so a rook behind a rook, or a queen behind a bishop, joins the sequence.
//
// Values are deliberately SEE's own (see see.cpp) rather than the evaluation's,
// because this measures material on one square, not position.
int see(const Board& board, const Move& move);
