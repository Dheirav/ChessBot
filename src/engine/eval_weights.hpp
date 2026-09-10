#pragma once
//
// The evaluation's constants: piece values, hand-tuned weights and the
// piece-square tables.
//
// Lifted out of evaluation.cpp so that the bitboard evaluation being built
// beside it (docs/BITBOARD-REPLACEMENT.md) reads the same objects rather than
// its own copies. The acceptance test for that work is exact integer equality
// between the two evaluations, and two copies of a tuned table are exactly the
// thing that would pass the test today and quietly diverge the first time
// someone tunes one of them.
//
// This is a move, not a change. The values, the tuning switch and the comments
// are unaltered; test-evalref and test-bench prove nothing moved.
//
#include "evaluation.hpp"
#include <cstdint>


// There is no king-danger term here -- no charge for enemy pieces attacking
// the squares around the king -- and like the hanging-piece penalty below, the
// absence is measured rather than an oversight. See ROADMAP.md 6.4.
//
// The defect is real and still true of the code: king safety is a placement
// term plus a pawn shield capped at 24 centipawns that switches off the moment
// the king leaves the back rank, so a queen, rook and knight swarming the king
// score the same as an empty board. It was found by reviewing the three games
// this engine lost to 2300+ opposition, in all of which it walked its king up
// the board while counting the material it had been paid to do it.
//
// One was built anyway: attackers into a nine-square king zone, weighted by
// piece (N/B 20, R 40, Q 80), charged on a saturating curve in the number of
// *distinct* attackers ({0,0,50,75,88,94,97,99}%), faded out by game phase. It
// was mirror-symmetric, it moved the tree by -1.4% nodes, and on the position
// that lost the CookieCompote28 game it re-scored a mating attack by -135
// centipawns where the shipped evaluation read +4. Every reason to expect it to
// work.
//
//   king danger on, 1x            +1.3 Elo  [ -7.9,   +10.6]   3 360 games
//   legacy centralisation off     +2.2 Elo  [ -6.8,   +11.1]   3 360 games
//   both together                -11.0 Elo  [-20.4,    -1.6]   3 360 games
//   king danger on, 8x          -216.9 Elo  [-241.9, -193.8]     960 games
//
// Monotone in magnitude with no peak above zero, which is 6.2's threat-term
// scan in mirror image and licenses the same conclusion: the best charge is
// none. The combination is the one interval clear of zero and it is negative,
// most likely because PST_KING_MG already prices middlegame king placement and
// a danger term charges a second time for the same exposure.
//
// It was also not free when switched off. The king-zone test ran per attacked
// square in the attack loop below, which is the hottest loop in the evaluation:
// bench 6 went 2007 -> 2114 ms, 5% slower, for a term contributing nothing.
// That is what settled deleting it over keeping it behind its toggle the way
// seepruning and deltapruning are kept -- those cost nothing when off.
//
// What the gates cannot rule out: self-play may be structurally unable to see
// this. A king-safety term pays against opponents who attack kings, and in
// self-play both sides share this engine's disinclination to. The losses that
// prompted it were against engines rated 2567 to 3042. Settling that needs a
// gauntlet against a stronger attacking opponent, which the harness cannot yet
// run -- not another arm of the same experiment.

// Threat bonus - bonus for threatening to capture valuable pieces.
// The KING slot is 0 and must stay in place to keep the PieceType indexing:
// the threat loop skips king targets entirely (a king can never be captured),
// so any value here would be unreachable.
const int threatBonus[] = { 0, 0, 10, 25, 30, 50, 100 };

// ---------------------------------------------------------------------------
// Term weights, named so they can be addressed.
//
// Every one of these was an inline literal until 2026-08-25 -- `50` inside an
// if, `-10 *` in the pawn-structure block, `* 5` at the end of a king-activity
// expression. A Texel tune has to *name* what it optimises, so this exists to
// make the weights reachable before anything tries to move them.
//
// **No value changed when they were extracted.** The bench signature is the
// proof and it must read 793,823; if it moves, this refactor is wrong and not
// the tune. Nothing here has been tuned yet.
//
// They are `constexpr` rather than mutable on purpose. Making them runtime
// variables so a tuner could perturb them in-process would cost the constant
// folding this evaluation depends on, and speed here is worth more than the
// convenience -- inlining the Piece accessors alone was 1.87x (PLAN 5.6). The
// tuner gets its own build instead; these stay constants in the engine that
// ships.
//
// `-DEVAL_TUNING` is that other build, used only by `tools/tune`: it drops the
// constexpr so the weights become ordinary mutable globals the tuner can
// perturb between passes. Nothing links both. If you find yourself wanting
// EVAL_TUNING in a shipped binary, the answer is to write the tuned numbers
// back here as constants and rebuild.
// ---------------------------------------------------------------------------
#ifdef EVAL_TUNING
  #define EVAL_WEIGHT int
  // Piece-square tables lose both `static` and `const` so tools/tune can bind
  // to them by name. In the shipped build they stay exactly as they were.
  #define EVAL_TABLE
#else
  #define EVAL_WEIGHT constexpr int
  #define EVAL_TABLE static const
#endif

namespace EvalWeights {

// Pawn structure
EVAL_WEIGHT DOUBLED_PAWN    = -10;
EVAL_WEIGHT ISOLATED_PAWN   = -10;
EVAL_WEIGHT BACKWARD_PAWN   =  -8;
EVAL_WEIGHT CONNECTED_PAWN  =   5;
EVAL_WEIGHT PASSED_PAWN     =  20;
EVAL_WEIGHT PAWN_CHAIN      =   5;

// Pieces
EVAL_WEIGHT BISHOP_PAIR     =  50;
EVAL_WEIGHT MOBILITY        =   2;
EVAL_WEIGHT ROOK_OPEN_FILE  =  10;
EVAL_WEIGHT ROOK_SEMI_OPEN  =   5;
EVAL_WEIGHT ROOK_ON_7TH     =  10;
EVAL_WEIGHT OUTPOST         =  10;
EVAL_WEIGHT TRAPPED_PIECE   =   5;
EVAL_WEIGHT UNDEFENDED      =   5;

// King and squares
EVAL_WEIGHT CENTRE_CONTROL  =   5;
EVAL_WEIGHT KING_CENTRE_DIST =  4;   // charged per square from the centre
EVAL_WEIGHT KING_PAWN_SHIELD =  8;   // per shield pawn, back rank only
EVAL_WEIGHT KING_ACTIVITY    =  5;   // endgame only

}  // namespace EvalWeights

// There is no penalty here for a piece the opponent can win material on, and
// the absence is measured rather than an oversight.
//
// The term used to charge the *full piece value* of anything attacked and not
// defended -- material counted twice, since the material term was still
// counting the piece. Rebuilding it on see() and charging half was worth
// +121.2 Elo (2026-08-15), which looked like the answer. It was not: gating the
// divisor found the score still climbing as the charge shrank, all the way to
// charging nothing at all.
//
//   divisor 1  -177.7    divisor 3  +66.8    divisor 6  +105.2
//   divisor 2   0 (ref)  divisor 4  +98.1    none       +152.0
//
// Monotone, with no peak between 6 and infinity, so no larger divisor can beat
// removing it. The gain never came from pricing threats accurately; it came
// from this term saying less. A static score cannot know whether a threatened
// piece will be saved -- that is what the search is for, and the search already
// does it a ply later, for real, instead of guessing.
//
// Deleting it also returns the ~13% per node that the see() call cost.
// See ROADMAP.md 6.2.

// Piece-square tables (centipawns), white perspective, index 0 = a8 (matches board indexing).
// Standard simplified evaluation tables. Black uses the vertically mirrored table (sq ^ 56).

// Pawn
EVAL_TABLE int PST_PAWN[64] = {
      0,  0,  0,  0,  0,  0,  0,  0,
     50, 50, 50, 50, 50, 50, 50, 50,
     10, 10, 20, 30, 30, 20, 10, 10,
      5,  5, 10, 25, 25, 10,  5,  5,
      0,  0,  0, 20, 20,  0,  0,  0,
      5, -5,-10,  0,  0,-10, -5,  5,
      5, 10, 10,-20,-20, 10, 10,  5,
      0,  0,  0,  0,  0,  0,  0,  0
};

// Knight
EVAL_TABLE int PST_KNIGHT[64] = {
    -50,-40,-30,-30,-30,-30,-40,-50,
    -40,-20,  0,  0,  0,  0,-20,-40,
    -30,  0, 10, 15, 15, 10,  0,-30,
    -30,  5, 15, 20, 20, 15,  5,-30,
    -30,  0, 15, 20, 20, 15,  0,-30,
    -30,  5, 10, 15, 15, 10,  5,-30,
    -40,-20,  0,  5,  5,  0,-20,-40,
    -50,-40,-30,-30,-30,-30,-40,-50
};

// Bishop
EVAL_TABLE int PST_BISHOP[64] = {
    -20,-10,-10,-10,-10,-10,-10,-20,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -10,  0,  5, 10, 10,  5,  0,-10,
    -10,  5,  5, 10, 10,  5,  5,-10,
    -10,  0, 10, 10, 10, 10,  0,-10,
    -10, 10, 10, 10, 10, 10, 10,-10,
    -10,  5,  0,  0,  0,  0,  5,-10,
    -20,-10,-10,-10,-10,-10,-10,-20
};

// Rook
EVAL_TABLE int PST_ROOK[64] = {
      0,  0,  0,  0,  0,  0,  0,  0,
      5, 10, 10, 10, 10, 10, 10,  5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
      0,  0,  0,  5,  5,  0,  0,  0
};

// Queen
EVAL_TABLE int PST_QUEEN[64] = {
    -20,-10,-10, -5, -5,-10,-10,-20,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -10,  0,  5,  5,  5,  5,  0,-10,
     -5,  0,  5,  5,  5,  5,  0, -5,
      0,  0,  5,  5,  5,  5,  0, -5,
    -10,  5,  5,  5,  5,  5,  0,-10,
    -10,  0,  5,  0,  0,  0,  0,-10,
    -20,-10,-10, -5, -5,-10,-10,-20
};

// King (middlegame)
EVAL_TABLE int PST_KING_MG[64] = {
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -20,-30,-30,-40,-40,-30,-30,-20,
    -10,-20,-20,-20,-20,-20,-20,-10,
     20, 20,  0,  0,  0,  0, 20, 20,
     20, 30, 10,  0,  0, 10, 30, 20
};

// King (endgame)
EVAL_TABLE int PST_KING_EG[64] = {
    -50,-40,-30,-20,-20,-30,-40,-50,
    -30,-20,-10,  0,  0,-10,-20,-30,
    -30,-10, 20, 30, 30, 20,-10,-30,
    -30,-10, 30, 40, 40, 30,-10,-30,
    -30,-10, 30, 40, 40, 30,-10,-30,
    -30,-10, 20, 30, 30, 20,-10,-30,
    -30,-30,  0,  0,  0,  0,-30,-30,
    -50,-30,-30,-30,-30,-30,-30,-50
};

// Piece-square value for non-king pieces, signed for color (positive = good for white)
