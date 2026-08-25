

#include "evaluation.hpp"
#include "board.hpp"
#include "movegen.hpp" // For generateLegalMoves
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// Piece values indexed by PieceType (NONE=0, KING=1, PAWN=2, KNIGHT=3, BISHOP=4, ROOK=5, QUEEN=6)
const int pieceValues[] = { 0, 20000, 100, 325, 335, 525, 950 };

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
#else
  #define EVAL_WEIGHT constexpr int
#endif

namespace EvalWeights {

// Pawn structure
EVAL_WEIGHT DOUBLED_PAWN    = -14;
EVAL_WEIGHT ISOLATED_PAWN   = -2;
EVAL_WEIGHT BACKWARD_PAWN   =  -11;
EVAL_WEIGHT CONNECTED_PAWN  =   3;
EVAL_WEIGHT PASSED_PAWN     =  33;
EVAL_WEIGHT PAWN_CHAIN      =   6;

// Pieces
EVAL_WEIGHT BISHOP_PAIR     =  38;
EVAL_WEIGHT MOBILITY        =   4;
EVAL_WEIGHT ROOK_OPEN_FILE  =  19;
EVAL_WEIGHT ROOK_SEMI_OPEN  =   9;
EVAL_WEIGHT ROOK_ON_7TH     =  1;
EVAL_WEIGHT OUTPOST         =  11;
EVAL_WEIGHT TRAPPED_PIECE   =   3;
EVAL_WEIGHT UNDEFENDED      =   8;

// King and squares
EVAL_WEIGHT CENTRE_CONTROL  =   -11;
EVAL_WEIGHT KING_CENTRE_DIST =  7;   // charged per square from the centre
EVAL_WEIGHT KING_PAWN_SHIELD =  5;   // per shield pawn, back rank only
EVAL_WEIGHT KING_ACTIVITY    =  -5;   // endgame only

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
static const int PST_PAWN[64] = {
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
static const int PST_KNIGHT[64] = {
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
static const int PST_BISHOP[64] = {
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
static const int PST_ROOK[64] = {
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
static const int PST_QUEEN[64] = {
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
static const int PST_KING_MG[64] = {
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
static const int PST_KING_EG[64] = {
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
static int getPST(int pieceType, int color, int sq) {
    const int* table = nullptr;
    switch (pieceType) {
        case PAWN:   table = PST_PAWN; break;
        case KNIGHT: table = PST_KNIGHT; break;
        case BISHOP: table = PST_BISHOP; break;
        case ROOK:   table = PST_ROOK; break;
        case QUEEN:  table = PST_QUEEN; break;
        default:     return 0;
    }
    if (color == COLOR_WHITE) return table[sq];
    return -table[sq ^ 56];
}

// Extern/static declarations for feature extraction
static bool isCenter(int idx);

// Helper: is square in center
static bool isCenter(int idx) {
    return idx == 27 || idx == 28 || idx == 35 || idx == 36;
}

// Calls fn(sq) for every square the piece on 'from' attacks, using exactly the
// geometry of Board::isSquareAttacked(): sliders stop at and include the first
// occupied square, pawns hit their two forward diagonals, knights and kings use
// fixed offsets. Never yields 'from' itself.
//
// This replaces two patterns that dominated the evaluation: a 64x64 sweep that
// ran a geometry test on every ordered pair of squares, and two full
// isSquareAttacked() ray-scans per piece (~52 scans per evaluation).
template <typename F>
static inline void forEachAttackedSquare(const Board& board, int from, F fn) {
    static const int KNIGHT_D[8][2] = { {1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,1},{-2,-1},{-1,2} };
    static const int KING_D[8][2]   = { {1,1},{1,0},{1,-1},{0,1},{0,-1},{-1,1},{-1,0},{-1,-1} };
    static const int ROOK_D[4][2]   = { {0,1},{1,0},{0,-1},{-1,0} };
    static const int BISHOP_D[4][2] = { {1,1},{1,-1},{-1,-1},{-1,1} };

    const Piece& a = board.squares[from];
    const int x = from % 8, y = from / 8;

    switch (a.type()) {
        case PAWN: {
            int ny = y + ((a.color() == COLOR_WHITE) ? -1 : 1);
            if (ny >= 0 && ny < 8) {
                if (x > 0) fn(ny * 8 + x - 1);
                if (x < 7) fn(ny * 8 + x + 1);
            }
            break;
        }
        case KNIGHT:
        case KING: {
            const int (*d)[2] = (a.type() == KNIGHT) ? KNIGHT_D : KING_D;
            for (int i = 0; i < 8; ++i) {
                int nx = x + d[i][0], ny = y + d[i][1];
                if (nx >= 0 && nx < 8 && ny >= 0 && ny < 8) fn(ny * 8 + nx);
            }
            break;
        }
        case ROOK:
        case BISHOP:
        case QUEEN: {
            for (int pass = 0; pass < 2; ++pass) {
                if (pass == 0 && a.type() == BISHOP) continue;
                if (pass == 1 && a.type() == ROOK) continue;
                const int (*d)[2] = (pass == 0) ? ROOK_D : BISHOP_D;
                for (int dir = 0; dir < 4; ++dir) {
                    int nx = x, ny = y;
                    while (true) {
                        nx += d[dir][0]; ny += d[dir][1];
                        if (nx < 0 || nx >= 8 || ny < 0 || ny >= 8) break;
                        int idx = ny * 8 + nx;
                        fn(idx);
                        if (board.squares[idx].type() != NONE) break;
                    }
                }
            }
            break;
        }
        default: break;
    }
}

// Mobility is counted over pseudo-legal moves rather than legal ones.
// generateLegalMoves() filters for legality by copying the whole board, making the
// move and testing the king square for every candidate - roughly 35 board
// copies per call, paid twice per evaluated node, to produce two integers.
// Pseudo-legal counts differ from legal counts only when pieces are pinned or
// the king is in check, which is the standard trade engines make here.
// Castling is excluded: it is not mobility, and each castling test costs three
// more isSquareAttacked() scans.
// kingSq is the square of 'color's king, or -1 if it has none.
static int countMobility(const Board& board, PieceColor color, int kingSq) {
    // In check the pseudo-legal count is not an approximation but simply
    // wrong: nearly every generated move is illegal, so the side in check
    // would be credited with mobility it does not have (measured at up to
    // 108cp, more than a pawn). Checks occur in only ~3% of positions, so
    // paying for the real legality filter here costs almost nothing and
    // removes the one large distortion.
    PieceColor opp = (color == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
    if (kingSq >= 0 && board.isSquareAttacked(kingSq, (int)opp))
        return (int)generateLegalMoves(board, color, true).size();

    // Counted, not collected: this wants one integer, and materialising ~35
    // Move objects to call .size() on them was about 14% of the engine's
    // runtime. countPseudoLegalMoves() shares the generator body, so the number
    // is by construction the same one the old call produced.
    return countPseudoLegalMoves(board, color, /*includeCastling=*/false);
}

// Returns a detailed breakdown of evaluation for logging
// How far a square is from the centre of the board, measured symmetrically.
//
// The obvious |x - 3| is wrong, and wrong in a way that survived every test in
// this repo: a board has eight files and eight ranks, so its centre lies
// *between* 3 and 4. |x - 3| charges 4 at one edge and 3 at the other, which
// makes it asymmetric under the reflection that chess itself is symmetric
// under. King safety used it, so White's king on rank 7 was penalised one more
// than Black's identical king on rank 0 — the −4 in the mirror-symmetric
// starting position, present in every position the engine ever evaluated.
//
// Measuring to the nearer of the two central coordinates restores the symmetry
// and keeps the same units. Range is 0..6 rather than the old 0..7, so king
// safety is slightly smaller in magnitude than before; the multiplier is a
// tuning question and a separate, gated one.
// --- King danger -----------------------------------------------------------
//
// Enemy pieces bearing on the squares around the king, weighted by piece and
// squared, which is the standard shape and the one ROADMAP.md 6.4 built and
// rejected: +1.3, +2.2, -11.0, and -216.9 at 8x magnitude over 10 080 games.
//
// Rebuilt here because 6.4 judged it on self-play alone, and self-play is the
// instrument that cannot see this term: both sides get it, both sides share
// this engine's disinclination to attack, and a term worth something against
// an attacker then prices at zero. That was recorded as 6.4's own caveat. Two
// instruments it lacked now exist — tests/evalerror scores an evaluation
// against Stockfish in a second, and tests/gauntlet.sh plays something other
// than ourselves — so this is a different experiment on the same feature
// rather than a rerun of the one that failed.
//
// The position that demands it is BUGS.md 13's:
//
//   r2r4/pN3pkp/Qb6/3qn1p1/3Pn3/4BP2/PP2P1PP/R3KB1R w KQ - 3 20
//
// White takes a rook with his king on e1 and Black's queen, two knights and a
// bishop pointing at it, and scores it +3.81 where the truth is -1.49. The
// king-exposure term below does not fire there at all: the castling rights are
// still present and every file at the king has a pawn on it. Attackers are
// what is left.
//
// Off by default (KING_DANGER_SCALE = 0), as an unmeasured term must be.
static const int KING_DANGER_WEIGHT[7] = { 0, 0, 1, 3, 3, 4, 6 };  // by PieceType
static const int KING_DANGER_SCALE = 0;   // percent; 0 is off, 100 is as written

static int kingDanger(const Board& board, int kingSq, PieceColor attacker) {
    if (KING_DANGER_SCALE == 0 || kingSq < 0) return 0;
    uint64_t zone = 0;
    const int kf = kingSq % 8, kr = kingSq / 8;
    for (int df = -1; df <= 1; ++df) {
        for (int dr = -1; dr <= 1; ++dr) {
            const int f = kf + df, r = kr + dr;
            if (f < 0 || f > 7 || r < 0 || r > 7) continue;
            zone |= 1ULL << (r * 8 + f);
        }
    }
    int danger = 0;
    for (int i = 0; i < 64; ++i) {
        const Piece& p = board.squares[i];
        if (p.type() == NONE || p.type() == KING || p.color() != attacker) continue;
        int hits = 0;
        forEachAttackedSquare(board, i, [&](int sq) { if ((zone >> sq) & 1ULL) ++hits; });
        if (hits > 0) danger += KING_DANGER_WEIGHT[p.type()] * hits;
    }
    // Squared, because two attackers are worth more than twice one — that is
    // the whole reason a count is not enough. The /8 sets the units: a danger
    // of 40, which is roughly a queen and two minor pieces bearing on the
    // zone, comes to 200cp at 100%. Getting this divisor wrong is quiet — an
    // earlier version divided by 40 000 and produced a term that measured
    // exactly nothing at every scale, which reads as "the feature is
    // worthless" rather than "the constant is wrong".
    return danger * danger * KING_DANGER_SCALE / 100 / 8;
}

// --- King exposure ---------------------------------------------------------
//
// The compensation the evaluation could not price (BUGS.md 13). The term above
// is `-distFromCenter * 4` plus a pawn shield capped at 24 centipawns, so the
// most it can ever say about a king is a quarter of a pawn. In `gtB9qan7` the
// engine was +3 in material with its king stuck on d1, both rooks still at
// home and the enemy bishop pair bearing down, and scored the position +1.65
// where the truth was -3.09. A term whose entire range is 24cp cannot express
// that, whatever its sign.
//
// Deliberately narrow. ROADMAP.md 6.4 built the general form — count the
// attackers around the king, weight them by piece — and gated it four times
// over 10 080 games: +1.3, +2.2, -11.0, and -216.9 at 8x magnitude. Repeating
// that shape and expecting a different number is not a plan. This charges
// three specific and cheap facts instead:
//
//   * a king that has lost the right to castle and is still on the centre files
//   * files at the king carrying no pawn of its own
//   * both, only in proportion to the heavy pieces the enemy has left to use
//
// The last one is what keeps it out of endgames, where a central king is
// correct play and kingActivityBonus is already paying for it.
//
// Off by default, as an unmeasured term must be. KING_EXPOSURE_SCALE is the
// single knob: 0 disables it exactly, and the shipped default stays 0 until a
// gate says otherwise.
static const int KING_EXPOSURE_STRANDED = 60;  // no castling rights, still on d/e
static const int KING_EXPOSURE_OPEN     = 25;  // a file at the king with no pawn at all
static const int KING_EXPOSURE_SEMI     = 12;  // ... or none of ours
static const int KING_EXPOSURE_HEAVY    = 4;   // queen + two rooks = fully armed
static const int KING_EXPOSURE_SCALE    = 0;   // percent; 0 is off, 100 is as written

static int kingExposure(int kingFile, int kingRank, int backRank, bool canCastle,
                        const uint8_t* ownPawnFile, const uint8_t* theirPawnFile,
                        int enemyHeavy) {
    if (KING_EXPOSURE_SCALE == 0 || kingFile < 0 || enemyHeavy <= 0) return 0;
    int penalty = 0;
    const bool central = (kingFile >= 3 && kingFile <= 4);
    const int  forward = (backRank == 7) ? -1 : 1;
    const bool athome  = (kingRank == backRank || kingRank == backRank + forward);
    if (!canCastle && central && athome) penalty += KING_EXPOSURE_STRANDED;
    for (int df = -1; df <= 1; ++df) {
        const int f = kingFile + df;
        if (f < 0 || f > 7) continue;
        if (ownPawnFile[f] == 0)
            penalty += (theirPawnFile[f] == 0) ? KING_EXPOSURE_OPEN : KING_EXPOSURE_SEMI;
    }
    if (enemyHeavy > KING_EXPOSURE_HEAVY) enemyHeavy = KING_EXPOSURE_HEAVY;
    return penalty * enemyHeavy / KING_EXPOSURE_HEAVY * KING_EXPOSURE_SCALE / 100;
}

static int centreDistance(int file, int rank) {
    return std::min(std::abs(file - 3), std::abs(file - 4)) +
           std::min(std::abs(rank - 3), std::abs(rank - 4));
}

EvalDetails evaluate_details(const Board& board) {
    EvalDetails e{};
    // Copy feature extraction logic from evaluate()
    int materialScore = 0;
    int mobilityScore = 0;
    int kingSafetyScore = 0;
    int centerControlScore = 0;
    int bishopPairBonus = 0;
    int doubledPawnPenalty = 0, isolatedPawnPenalty = 0, passedPawnBonus = 0, backwardPawnPenalty = 0, connectedPawnBonus = 0, pawnChainBonus = 0;
    int rooksOpenFileBonus = 0, rooksSemiOpenFileBonus = 0, rooks7thRankBonus = 0;
    int pstScore = 0;
    int outpostBonus = 0;
    int trappedPiecePenalty = 0;
    int kingActivityBonus = 0;
    // Blends the king's midgame and endgame piece-square tables below. It is a
    // property of the position as a whole, not of either side, so it belongs in
    // a weight and never in the score — see the note on e.total.
    float gamePhaseFactor = 1.0f;
    int threatScore = 0, undefendedPenalty = 0;
    int spaceScore = 0;

    int whiteMaterial = 0, blackMaterial = 0;
    int whitePawns = 0, blackPawns = 0;
    int whiteMobility = 0, blackMobility = 0;
    int whiteKingSafety = 0, blackKingSafety = 0;
    int whiteCenterControl = 0, blackCenterControl = 0;
    int whitePassedPawns = 0, blackPassedPawns = 0;
    int whiteDoubledPawns = 0, blackDoubledPawns = 0;
    int whiteIsolatedPawns = 0, blackIsolatedPawns = 0;
    int whiteBackwardPawns = 0, blackBackwardPawns = 0;
    int whiteConnectedPawns = 0, blackConnectedPawns = 0;
    int whitePawnChains = 0, blackPawnChains = 0;
    int whiteRooksOpenFile = 0, blackRooksOpenFile = 0;
    int whiteRooksSemiOpenFile = 0, blackRooksSemiOpenFile = 0;
    int whiteRooks7th = 0, blackRooks7th = 0;
    int whiteBishopPair = 0, blackBishopPair = 0;
    int whiteKingFile = -1, blackKingFile = -1;
    int whiteKingRank = -1, blackKingRank = -1;
    int whiteThreats = 0, blackThreats = 0;
    int whiteUndefended = 0, blackUndefended = 0;
    int whiteSpace = 0, blackSpace = 0;
    int whiteDrawish = 0, blackDrawish = 0;
    int whiteBishopCount = 0, blackBishopCount = 0;
    // Rooks and queens still on the board, as "how much is there to attack
    // with": the king-exposure charge is proportional to it, which is what
    // keeps the term out of endgames.
    int whiteHeavy = 0, blackHeavy = 0;

    // Per-file pawn masks: bit r set means a pawn of that colour stands on
    // rank r of that file. The passed/doubled/isolated/backward tests and the
    // rook open-file test each used to walk a file or a rank range square by
    // square; with these they become constant-time bit tests.
    uint8_t whitePawnFile[8] = {}, blackPawnFile[8] = {};
    for (int i = 0; i < 64; ++i) {
        const Piece& p = board.squares[i];
        if (p.type() != PAWN) continue;
        if (p.color() == COLOR_WHITE) whitePawnFile[i % 8] |= (uint8_t)(1u << (i / 8));
        else                          blackPawnFile[i % 8] |= (uint8_t)(1u << (i / 8));
    }
    // Union of a file and its two neighbours; and of the two neighbours alone.
    auto span3 = [](const uint8_t m[8], int f) -> uint8_t {
        uint8_t r = m[f];
        if (f > 0) r |= m[f - 1];
        if (f < 7) r |= m[f + 1];
        return r;
    };
    auto adj2 = [](const uint8_t m[8], int f) -> uint8_t {
        uint8_t r = 0;
        if (f > 0) r |= m[f - 1];
        if (f < 7) r |= m[f + 1];
        return r;
    };
    auto ranksBelow = [](int rank) -> uint8_t { return (uint8_t)((1u << rank) - 1u); };
    auto ranksAbove = [](int rank) -> uint8_t { return (uint8_t)(~((1u << (rank + 1)) - 1u)); };

    // --- Feature extraction logic (copied from evaluate) ---
    for (int i = 0; i < 64; ++i) {
        const Piece& p = board.squares[i];
        if (p.type() == NONE) continue;
        int file = i % 8, rank = i / 8;
        int color = p.color();
        materialScore += (color == COLOR_WHITE ? 1 : -1) * pieceValues[p.type()];
        if (p.type() != KING) {
            pstScore += getPST(p.type(), color, i);
        }
        if (isCenter(i)) {
            if (color == COLOR_WHITE) whiteCenterControl++;
            else blackCenterControl++;
        }
        if (color == COLOR_WHITE) {
            whiteMaterial += pieceValues[p.type()];
            if (p.type() == PAWN) {
                whitePawns++;
                if (file > 0 && board.squares[i-1].type() == PAWN && board.squares[i-1].color() == COLOR_WHITE) whiteConnectedPawns++;
                if (file < 7 && board.squares[i+1].type() == PAWN && board.squares[i+1].color() == COLOR_WHITE) whiteConnectedPawns++;
                // No black pawn on this or an adjacent file, ahead of us
                // (white advances toward rank 0).
                if ((span3(blackPawnFile, file) & ranksBelow(rank)) == 0) whitePassedPawns++;
                // Every friendly pawn behind us on the same file.
                whiteDoubledPawns += __builtin_popcount((unsigned)(whitePawnFile[file] & ranksAbove(rank)));
                if (adj2(whitePawnFile, file) == 0) whiteIsolatedPawns++;
                if ((span3(whitePawnFile, file) & ranksAbove(rank)) == 0) whiteBackwardPawns++;
                if ((file > 0 && rank < 7 && board.squares[(rank+1)*8+file-1].type() == PAWN && board.squares[(rank+1)*8+file-1].color() == COLOR_WHITE) ||
                    (file < 7 && rank < 7 && board.squares[(rank+1)*8+file+1].type() == PAWN && board.squares[(rank+1)*8+file+1].color() == COLOR_WHITE))
                    whitePawnChains++;
            }
            if (p.type() == KING) {
                whiteKingFile = file;
                whiteKingRank = rank;
            }
            if (p.type() == ROOK) {
                bool openFile = (whitePawnFile[file] | blackPawnFile[file]) == 0;
                bool semiOpen = whitePawnFile[file] == 0;
                if (openFile) whiteRooksOpenFile++;
                else if (semiOpen) whiteRooksSemiOpenFile++;
                if (rank == 1) whiteRooks7th++;
            }
            if (p.type() == BISHOP) whiteBishopCount++;
            if (p.type() == ROOK) whiteHeavy += 1;
            if (p.type() == QUEEN) whiteHeavy += 2;
        } else {
            blackMaterial += pieceValues[p.type()];
            if (p.type() == PAWN) {
                blackPawns++;
                if (file > 0 && board.squares[i-1].type() == PAWN && board.squares[i-1].color() == COLOR_BLACK) blackConnectedPawns++;
                if (file < 7 && board.squares[i+1].type() == PAWN && board.squares[i+1].color() == COLOR_BLACK) blackConnectedPawns++;
                // Mirror of the white case: black advances toward rank 7.
                if ((span3(whitePawnFile, file) & ranksAbove(rank)) == 0) blackPassedPawns++;
                blackDoubledPawns += __builtin_popcount((unsigned)(blackPawnFile[file] & ranksBelow(rank)));
                if (adj2(blackPawnFile, file) == 0) blackIsolatedPawns++;
                if ((span3(blackPawnFile, file) & ranksBelow(rank)) == 0) blackBackwardPawns++;
                if ((file > 0 && rank > 0 && board.squares[(rank-1)*8+file-1].type() == PAWN && board.squares[(rank-1)*8+file-1].color() == COLOR_BLACK) ||
                    (file < 7 && rank > 0 && board.squares[(rank-1)*8+file+1].type() == PAWN && board.squares[(rank-1)*8+file+1].color() == COLOR_BLACK))
                    blackPawnChains++;
            }
            if (p.type() == KING) {
                blackKingFile = file;
                blackKingRank = rank;
            }
            if (p.type() == ROOK) {
                bool openFile = (whitePawnFile[file] | blackPawnFile[file]) == 0;
                bool semiOpen = blackPawnFile[file] == 0;
                if (openFile) blackRooksOpenFile++;
                else if (semiOpen) blackRooksSemiOpenFile++;
                if (rank == 6) blackRooks7th++;
            }
            if (p.type() == BISHOP) blackBishopCount++;
            if (p.type() == ROOK) blackHeavy += 1;
            if (p.type() == QUEEN) blackHeavy += 2;
        }
    }

    // Bishop pair
    if (whiteBishopCount >= 2) {
        whiteBishopPair = 1;
        bishopPairBonus += EvalWeights::BISHOP_PAIR;
    }
    if (blackBishopCount >= 2) {
        blackBishopPair = 1;
        bishopPairBonus -= EvalWeights::BISHOP_PAIR;
    }

    // Pawn structure: doubled, isolated, backward, connected, passed pawns and pawn chains
    doubledPawnPenalty = EvalWeights::DOUBLED_PAWN * (whiteDoubledPawns - blackDoubledPawns);
    isolatedPawnPenalty = EvalWeights::ISOLATED_PAWN * (whiteIsolatedPawns - blackIsolatedPawns);
    backwardPawnPenalty = EvalWeights::BACKWARD_PAWN * (whiteBackwardPawns - blackBackwardPawns);
    connectedPawnBonus = EvalWeights::CONNECTED_PAWN * (whiteConnectedPawns - blackConnectedPawns);
    passedPawnBonus = EvalWeights::PASSED_PAWN * (whitePassedPawns - blackPassedPawns);
    pawnChainBonus = EvalWeights::PAWN_CHAIN * (whitePawnChains - blackPawnChains);

    // Mobility
    // King squares were located during the piece scan above (index = rank*8 + file).
    int whiteKingSq = (whiteKingFile >= 0) ? whiteKingRank * 8 + whiteKingFile : -1;
    int blackKingSq = (blackKingFile >= 0) ? blackKingRank * 8 + blackKingFile : -1;
    whiteMobility = countMobility(board, COLOR_WHITE, whiteKingSq);
    blackMobility = countMobility(board, COLOR_BLACK, blackKingSq);
    mobilityScore = EvalWeights::MOBILITY * (whiteMobility - blackMobility);

    // King safety.
    //
    // `-distFromCenter * 4` is a *centralisation* term wearing king safety's
    // name, and it applies in every phase — so it pays the king to walk toward
    // the middle of the board in a middlegame, against PST_KING_MG, which is
    // simultaneously paying it not to. Fading it out with the game phase
    // instead was gated on 2026-08-16: +2.2 Elo, 95% CI [-6.8, +11.1] over
    // 3 360 games. No difference demonstrated, so it stays as it is; see
    // ROADMAP.md 6.4 and the note at the top of this file.
    if (whiteKingFile != -1 && whiteKingRank != -1) {
        int distFromCenter = centreDistance(whiteKingFile, whiteKingRank);
        whiteKingSafety = -distFromCenter * EvalWeights::KING_CENTRE_DIST;
        if (whiteKingRank == 7) {
            for (int df = -1; df <= 1; ++df) {
                int f = whiteKingFile + df;
                if (f >= 0 && f < 8) {
                    int idx = 6 * 8 + f;
                    if (board.squares[idx].type() == PAWN && board.squares[idx].color() == COLOR_WHITE) whiteKingSafety += EvalWeights::KING_PAWN_SHIELD;
                }
            }
        }
    }
    if (blackKingFile != -1 && blackKingRank != -1) {
        int distFromCenter = centreDistance(blackKingFile, blackKingRank);
        blackKingSafety = -distFromCenter * EvalWeights::KING_CENTRE_DIST;
        if (blackKingRank == 0) {
            for (int df = -1; df <= 1; ++df) {
                int f = blackKingFile + df;
                if (f >= 0 && f < 8) {
                    int idx = 1 * 8 + f;
                    if (board.squares[idx].type() == PAWN && board.squares[idx].color() == COLOR_BLACK) blackKingSafety += EvalWeights::KING_PAWN_SHIELD;
                }
            }
        }
    }
    kingSafetyScore = whiteKingSafety - blackKingSafety;

    // Center control
    int centerSquares[4] = { 27, 28, 35, 36 };
    for (int i = 0; i < 4; ++i) {
        const Piece& p = board.squares[centerSquares[i]];
        if (p.type() != NONE) {
            if (p.color() == COLOR_WHITE) centerControlScore += EvalWeights::CENTRE_CONTROL;
            else centerControlScore -= EvalWeights::CENTRE_CONTROL;
        }
    }

    // Rooks on open/semi-open files and 7th rank, from the per-rook counters
    // computed in the piece scan. (The previous version used a single net
    // pawn count per file, which scored a file with one pawn of each color
    // as fully open.)
    rooksOpenFileBonus = EvalWeights::ROOK_OPEN_FILE * (whiteRooksOpenFile - blackRooksOpenFile);
    rooksSemiOpenFileBonus = EvalWeights::ROOK_SEMI_OPEN * (whiteRooksSemiOpenFile - blackRooksSemiOpenFile);
    rooks7thRankBonus = EvalWeights::ROOK_ON_7TH * (whiteRooks7th - blackRooks7th);

    // Outposts
    for (int i = 0; i < 64; ++i) {
        const Piece& p = board.squares[i];
        if (p.type() == KNIGHT || p.type() == BISHOP) {
            int rank = i / 8, file = i % 8;
            if (p.color() == COLOR_WHITE && rank <= 3) {
                if ((file > 0 && board.squares[i+7].type() == PAWN && board.squares[i+7].color() == COLOR_WHITE) ||
                    (file < 7 && board.squares[i+9].type() == PAWN && board.squares[i+9].color() == COLOR_WHITE))
                    outpostBonus += EvalWeights::OUTPOST;
            }
            if (p.color() == COLOR_BLACK && rank >= 4) {
                if ((file > 0 && board.squares[i-9].type() == PAWN && board.squares[i-9].color() == COLOR_BLACK) ||
                    (file < 7 && board.squares[i-7].type() == PAWN && board.squares[i-7].color() == COLOR_BLACK))
                    outpostBonus -= EvalWeights::OUTPOST;
            }
        }
    }

    // Trapped pieces
    for (int i = 0; i < 64; ++i) {
        const Piece& p = board.squares[i];
        if ((p.type() == ROOK || p.type() == BISHOP) && (i % 8 == 0 || i % 8 == 7 || i / 8 == 0 || i / 8 == 7)) {
            if (p.color() == COLOR_WHITE) trappedPiecePenalty -= EvalWeights::TRAPPED_PIECE;
            else trappedPiecePenalty += EvalWeights::TRAPPED_PIECE;
        }
    }

    // King activity
    int totalMaterial = whiteMaterial + blackMaterial - pieceValues[KING]*2;
    if (totalMaterial < 2000) {
        if (whiteKingFile != -1 && whiteKingRank != -1) kingActivityBonus += (4 - std::abs(whiteKingFile - 3.5) - std::abs(whiteKingRank - 3.5)) * EvalWeights::KING_ACTIVITY;
        if (blackKingFile != -1 && blackKingRank != -1) kingActivityBonus -= (4 - std::abs(blackKingFile - 3.5) - std::abs(blackKingRank - 3.5)) * EvalWeights::KING_ACTIVITY;
    }

    // Game phase scaling
    gamePhaseFactor = std::min(1.0f, totalMaterial / 3200.0f);

    // King exposure, folded into kingSafety rather than given a term of its
    // own: it is king safety, and evaltrace's kSafe column is where a reader
    // already looks for it. It has to be applied *here*, after the phase is
    // known — gamePhaseFactor is 1.0 until this line, so scaling it any
    // earlier silently scales by nothing.
    // Attackers around each king. Same placement rule as the exposure term:
    // after gamePhaseFactor exists, or the phase scaling silently does nothing.
    if (whiteKingFile != -1 && blackKingFile != -1) {
        const int wKingSq = Board::get1DIndex(whiteKingFile, whiteKingRank);
        const int bKingSq = Board::get1DIndex(blackKingFile, blackKingRank);
        kingSafetyScore -= (int)((kingDanger(board, wKingSq, COLOR_BLACK)
                                  - kingDanger(board, bKingSq, COLOR_WHITE))
                                 * gamePhaseFactor);
    }
    kingSafetyScore -= (int)((kingExposure(whiteKingFile, whiteKingRank, 7,
                                           (board.castlingRights & (CASTLE_WK | CASTLE_WQ)) != 0,
                                           whitePawnFile, blackPawnFile, blackHeavy)
                              - kingExposure(blackKingFile, blackKingRank, 0,
                                             (board.castlingRights & (CASTLE_BK | CASTLE_BQ)) != 0,
                                             blackPawnFile, whitePawnFile, whiteHeavy))
                             * gamePhaseFactor);

    // King piece-square tables, blended between middlegame and endgame by game phase
    if (whiteKingFile != -1 && whiteKingRank != -1) {
        int ks = Board::get1DIndex(whiteKingFile, whiteKingRank);
        pstScore += (int)(PST_KING_MG[ks] * gamePhaseFactor + PST_KING_EG[ks] * (1.0f - gamePhaseFactor));
    }
    if (blackKingFile != -1 && blackKingRank != -1) {
        int ks = Board::get1DIndex(blackKingFile, blackKingRank) ^ 56;
        pstScore -= (int)(PST_KING_MG[ks] * gamePhaseFactor + PST_KING_EG[ks] * (1.0f - gamePhaseFactor));
    }

    // Enhanced Threats and Captures - much more aggressive evaluation
    int captureIncentive = 0;

    // One walk over each piece's attack set does both jobs at once: it
    // accumulates the threat terms (previously a 64x64 sweep with a geometry
    // test per ordered pair) and fills the attack maps used by the hanging
    // test below (previously two isSquareAttacked() ray-scans per piece).
    // Indexed by PieceColor, so slot 1 is white and slot 2 is black.
    bool attackedBy[3][64] = {};

    for (int i = 0; i < 64; ++i) {
        const Piece& attacker = board.squares[i];
        if (attacker.type() == NONE) continue;
        const PieceColor ac = attacker.color();
        const int attackerValue = pieceValues[attacker.type()];

        forEachAttackedSquare(board, i, [&](int j) {
            attackedBy[ac][j] = true;

            const Piece& target = board.squares[j];
            if (target.type() == NONE || target.color() == ac) return;

            // The king is handled by the search's mate scores, not the static threats.
            if (target.type() == KING) return;

            int threatValue = ::threatBonus[target.type()];
            if (ac == COLOR_WHITE) whiteThreats += threatValue;
            else                   blackThreats += threatValue;

            // Extra bonus for attacking more valuable pieces with less valuable pieces
            if (pieceValues[target.type()] > attackerValue) {
                int valueGap = pieceValues[target.type()] - attackerValue;
                if (ac == COLOR_WHITE) captureIncentive += valueGap / 10; // 10% of value difference
                else                   captureIncentive -= valueGap / 10;
            }
        });
    }

    threatScore = (whiteThreats - blackThreats) + captureIncentive;

    // Undefended pieces.
    //
    // "Defended" means a friendly piece actually attacks the square, which is
    // what attackedBy[own] already says — it was built above for the threat
    // term and costs nothing to reuse.
    //
    // It used to mean "a friendly piece stands on one of the eight neighbouring
    // squares", which is a different property entirely and not the one the term
    // is named after. It scored a knight beside its own rook as defended when
    // neither could recapture on the other's square, and scored a rook defended
    // down an open file as undefended because the defender was five squares
    // away. What it measured was how clumped the pieces were.
    for (int i = 0; i < 64; ++i) {
        const Piece& p = board.squares[i];
        if (p.type() == NONE) continue;
        if (attackedBy[p.color()][i]) continue;
        if (p.color() == COLOR_WHITE) whiteUndefended++;
        else blackUndefended++;
    }
    undefendedPenalty = -EvalWeights::UNDEFENDED * whiteUndefended + EvalWeights::UNDEFENDED * blackUndefended;

    // Space advantage
    for (int i = 0; i < 64; ++i) {
        int rank = i / 8;
        const Piece& p = board.squares[i];
        if (p.type() == NONE) continue;
        if (p.color() == COLOR_WHITE && rank < 4) whiteSpace++;
        if (p.color() == COLOR_BLACK && rank > 3) blackSpace++;
    }
    spaceScore = whiteSpace - blackSpace;

    // Drawishness
    if (whiteMaterial == 0 && blackMaterial == 0 && whitePawns == 0 && blackPawns == 0) {
        whiteDrawish = 1;
        blackDrawish = 1;
    }

    // Assign to EvalDetails
    // Every term here is colour-relative: positive favours white, and mirroring
    // the position must negate it. Two addends used to violate that and were
    // removed on 2026-08-14 (BUGS.md 2):
    //
    //   (int)(gamePhaseFactor * 1.5f)  — the game phase is a property of the
    //       position, identical for both sides, so adding it handed white a
    //       centipawn in every position with roughly a full opening's material.
    //       It is still used, correctly, as a weight for the king PST blend.
    //
    //   tempoBonus                     — declared `float 0.01f`, which promoted
    //       this whole sum to float and truncated it toward zero, turning −5
    //       into −4. It was also a constant rather than a bonus to the side to
    //       move, so it was not measuring tempo at all. An honest tempo bonus is
    //       a real idea and a separate one: it changes evaluation, so it needs
    //       its own gate rather than a free ride on a bug fix.
    //
    // The sum is int throughout. Keep it that way — a single float addend
    // silently truncates every score the engine produces.
    e.total = materialScore + mobilityScore + kingSafetyScore + centerControlScore + bishopPairBonus + doubledPawnPenalty + isolatedPawnPenalty + passedPawnBonus + backwardPawnPenalty + connectedPawnBonus + pawnChainBonus + rooksOpenFileBonus + rooksSemiOpenFileBonus + rooks7thRankBonus + pstScore + outpostBonus + trappedPiecePenalty + kingActivityBonus + threatScore + undefendedPenalty + spaceScore;
    e.material = materialScore;
    e.mobility = mobilityScore;
    e.kingSafety = kingSafetyScore;
    e.centerControl = centerControlScore;
    e.bishopPair = bishopPairBonus;
    e.doubledPawn = doubledPawnPenalty;
    e.isolatedPawn = isolatedPawnPenalty;
    e.passedPawn = passedPawnBonus;
    e.backwardPawn = backwardPawnPenalty;
    e.connectedPawn = connectedPawnBonus;
    e.pawnChain = pawnChainBonus;
    e.rooksOpenFile = rooksOpenFileBonus;
    e.rooksSemiOpenFile = rooksSemiOpenFileBonus;
    e.rooks7thRank = rooks7thRankBonus;
    e.pst = pstScore;
    e.outpost = outpostBonus;
    e.trapped = trappedPiecePenalty;
    e.kingActivity = kingActivityBonus;
    e.threats = threatScore;
    e.undefended = undefendedPenalty;
    e.space = spaceScore;
    // Reported for diagnostics only — deliberately not part of e.total.
    e.drawish = 30 * (whiteDrawish - blackDrawish) + 30 * (whiteBishopPair - blackBishopPair);
    return e;
}

// --- Evaluation cache ---
//
// Evaluation is the largest single item in the profile (34.1% of search time,
// BACKLOG.md section 7), and measured over the bench positions at depth 6,
// 46.1% of evaluate() calls are for a position that has already been evaluated
// — 1,077,632 calls covering 580,993 distinct positions. Those repeats are free
// to serve from a table.
//
// The key is the zobrist hash. evaluate_details() reads board.squares[] and
// nothing else — no side to move, castling rights, en passant target or move
// clocks — so the hash covers strictly more state than the evaluation depends
// on. That direction is the safe one: two positions sharing a hash have the
// same pieces and therefore the same evaluation, while two positions differing
// only in, say, en passant target hash differently and merely miss the cache.
//
// Entries never need invalidating. The evaluation of a position is a pure
// function of that position, so an entry stays correct across moves, searches
// and whole games.
namespace {

struct EvalCacheEntry {
    uint64_t lock = 0;   // hash XOR score, for torn-read detection
    int32_t score = 0;
};

// 512K entries, 8 MB. Direct-mapped: a collision simply overwrites, which costs
// a recomputation and never a wrong answer.
constexpr size_t EVAL_CACHE_ENTRIES = 1u << 19;
EvalCacheEntry g_evalCache[EVAL_CACHE_ENTRIES];

// The GUI thread can call evaluatePosition() while the search thread is
// running, so reads and writes here race. Rather than lock the hot path, the
// entry stores hash XOR score: a torn read pairs one entry's lock with
// another's score, the verification fails, and the caller recomputes. Wrong
// answers are impossible; the only cost of a race is a cache miss.
inline uint64_t encodeLock(uint64_t hash, int32_t score) {
    return hash ^ (uint64_t)(uint32_t)score;
}

}  // namespace

int evaluate(const Board& board) {
    const uint64_t hash = board.getHash();
    EvalCacheEntry& entry = g_evalCache[hash & (EVAL_CACHE_ENTRIES - 1)];

    const uint64_t lock = entry.lock;
    const int32_t cached = entry.score;
    if (encodeLock(lock, cached) == hash) {
        // lock == hash ^ score, so lock ^ score == hash confirms both halves
        // belong together and describe this position.
        return cached;
    }

    const int score = evaluate_details(board).total;
    entry.score = (int32_t)score;
    entry.lock = encodeLock(hash, (int32_t)score);
    return score;
}
