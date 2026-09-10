#include "bb_evaluation.hpp"
#include "bb_movegen.hpp"
#include "bitboard_attacks.hpp"
#include "eval_weights.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// The bitboard evaluation.
//
// Everything below the helpers is evaluation.cpp's own text with the board
// accessor substituted, which is deliberate and is the reason Position carries
// a Piece squares[64]. A hand-rewritten evaluation would be a second
// evaluation that has to be re-tuned and re-argued; this one is the same
// evaluation reading a different board, so the acceptance test -- exact integer
// equality over the corpus -- is a test of the port and not of anybody's
// judgement about what a term should be worth.
//
// The terms get converted to bitboard operations one at a time afterwards, with
// that equality test still standing over each one. Mobility is already
// converted, because it is 15 percent of the search on its own.
//
// Weights, piece values and the piece-square tables come from eval_weights.hpp,
// shared with evaluation.cpp rather than copied, so the two cannot drift.

namespace {

constexpr Bitboard FILE_A = 0x0101010101010101ULL;
constexpr Bitboard FILE_H = 0x8080808080808080ULL;
constexpr Bitboard RANK_8 = 0x00000000000000FFULL;  // white promotes here
constexpr Bitboard RANK_7 = 0x000000000000FF00ULL;  // black pawns start
constexpr Bitboard RANK_2 = 0x00FF000000000000ULL;  // white pawns start
constexpr Bitboard RANK_1 = 0xFF00000000000000ULL;  // black promotes here

} // namespace

int bbCountPseudoLegal(const Position& pos, BitboardColor color) {
    const bool white = (color == BB_WHITE);
    const auto& ours = white ? pos.white : pos.black;
    const Bitboard own   = white ? pos.occupancyWhite : pos.occupancyBlack;
    const Bitboard theirs = white ? pos.occupancyBlack : pos.occupancyWhite;
    const Bitboard occ = pos.occupancyAll;
    const Bitboard empty = ~occ;
    int n = 0;

    // Pieces: every reachable square that is not our own is one move, which is
    // what the mailbox generator emits whether the square is empty or holds an
    // enemy. A slider stops at the first occupied square either way, and that
    // is exactly what the magic tables encode.
    Bitboard b = ours[BB_KNIGHT];
    while (b) { n += popcount(knightAttacks(lsb(b)) & ~own); b &= b - 1; }
    b = ours[BB_BISHOP];
    while (b) { n += popcount(bishopAttacks(lsb(b), occ) & ~own); b &= b - 1; }
    b = ours[BB_ROOK];
    while (b) { n += popcount(rookAttacks(lsb(b), occ) & ~own); b &= b - 1; }
    b = ours[BB_QUEEN];
    while (b) { n += popcount(queenAttacks(lsb(b), occ) & ~own); b &= b - 1; }
    b = ours[BB_KING];
    while (b) { n += popcount(kingAttacks(lsb(b)) & ~own); b &= b - 1; }

    // Pawns. A promotion is four moves here, because the mailbox generator
    // emits one per promotion piece and mobility counts what it emits.
    const Bitboard pawns = ours[BB_PAWN];
    const Bitboard promoRank = white ? RANK_8 : RANK_1;
    const Bitboard startRank = white ? RANK_2 : RANK_7;
    auto push = [&](Bitboard x) { return white ? (x >> 8) : (x << 8); };

    const Bitboard single = push(pawns) & empty;
    n += popcount(single & ~promoRank);
    n += 4 * popcount(single & promoRank);
    n += popcount(push(push(pawns & startRank) & empty) & empty);

    const Bitboard capL = white ? ((pawns & ~FILE_A) >> 9) : ((pawns & ~FILE_A) << 7);
    const Bitboard capR = white ? ((pawns & ~FILE_H) >> 7) : ((pawns & ~FILE_H) << 9);
    const Bitboard caps = (capL | capR) & theirs;
    // Two pawns may capture onto the same square, so the two directions are
    // counted separately rather than unioned.
    for (Bitboard c : { capL & theirs, capR & theirs }) {
        n += popcount(c & ~promoRank);
        n += 4 * popcount(c & promoRank);
    }
    (void)caps;

    // En passant, deliberately not restricted to the side to move.
    //
    // movegen.cpp tests `to == board.enPassantSquare` without asking whose turn
    // it is, so when White has just played a double push, Black's mobility is
    // credited with the real en passant capture and White's is credited with a
    // phantom one for any pawn of its own that happens to attack the same
    // square. That is a defect in the evaluation, worth its own line in
    // BUGS.md, but it is reproduced exactly here: this phase is measuring
    // whether the bitboard core computes the same numbers, and fixing the
    // numbers at the same time would mean neither question gets answered.
    if (pos.enPassantSquare >= 0) {
        const BitboardColor them = white ? BB_BLACK : BB_WHITE;
        n += popcount(pawnAttacks(them, pos.enPassantSquare) & pawns);
    }
    return n;
}

namespace bbeval {
namespace {

// The mailbox evaluation asks the board whether a square is attacked; here the
// same question is a bitboard lookup. Same answer -- bitboard_test checks that
// over 765,696 comparisons -- so it substitutes cleanly.
inline bool bbAttacked(const Position& pos, int sq, PieceColor by) {
    return isSquareAttackedBB(pos, sq, (by == COLOR_WHITE) ? BB_WHITE : BB_BLACK);
}

}  // namespace

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
static inline void forEachAttackedSquareAs(const Position& pos, int from,
                                           PieceType type, PieceColor color, F fn) {
    // The mailbox version walked rays square by square and called fn on the
    // blocker before stopping, which is exactly the set a magic lookup returns:
    // reachable squares up to and including the first occupied one. So this is
    // the same set delivered in a different order, and every caller here counts
    // or maxes over it rather than depending on the order.
    Bitboard attacks = 0;
    switch (type) {
        case PAWN:   attacks = pawnAttacks(color == COLOR_WHITE ? BB_WHITE : BB_BLACK, from); break;
        case KNIGHT: attacks = knightAttacks(from); break;
        case KING:   attacks = kingAttacks(from); break;
        case ROOK:   attacks = rookAttacks(from, pos.occupancyAll); break;
        case BISHOP: attacks = bishopAttacks(from, pos.occupancyAll); break;
        case QUEEN:  attacks = queenAttacks(from, pos.occupancyAll); break;
        default: break;
    }
    while (attacks) {
        fn(lsb(attacks));
        attacks &= attacks - 1;
    }
}

// The ordinary form: ask what the piece actually standing on `from` attacks.
template <typename F>
static inline void forEachAttackedSquare(const Position& pos, int from, F fn) {
    const Piece& a = pos.squares[from];
    forEachAttackedSquareAs(pos, from, a.type(), a.color(), fn);
}

// Mobility is counted over pseudo-legal moves rather than legal ones.
// generateLegalMoves() filters for legality by copying the whole pos, making the
// move and testing the king square for every candidate - roughly 35 board
// copies per call, paid twice per evaluated node, to produce two integers.
// Pseudo-legal counts differ from legal counts only when pieces are pinned or
// the king is in check, which is the standard trade engines make here.
// Castling is excluded: it is not mobility, and each castling test costs three
// more isSquareAttacked() scans.
// kingSq is the square of 'color's king, or -1 if it has none.
static int countMobility(const Position& pos, PieceColor color, int kingSq) {
    const BitboardColor us = (color == COLOR_WHITE) ? BB_WHITE : BB_BLACK;
    const BitboardColor them = (us == BB_WHITE) ? BB_BLACK : BB_WHITE;

    // In check the pseudo-legal count is not an approximation but simply
    // wrong: nearly every generated move is illegal, so the side in check
    // would be credited with mobility it does not have (measured at up to
    // 108cp, more than a pawn). Checks occur in only ~3% of positions, so
    // paying for the real legality filter here costs almost nothing.
    if (kingSq >= 0 && isSquareAttackedBB(pos, kingSq, them)) {
        // Generated for `color`, which need not be the side to move, because
        // the evaluation asks the question of both sides at every node.
        Position flipped = pos;
        flipped.sideToMove = us;
        BBMoveList legal;
        bbGenerate(flipped, legal);
        return legal.size();
    }
    return bbCountPseudoLegal(pos, us);
}

// Returns a detailed breakdown of evaluation for logging
// How far a square is from the centre of the pos, measured symmetrically.
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
// Percent; 0 is off, 100 is as written. Overridable at build time so variants
// can be compared without editing the file, which matters because an evaluation
// change cannot be A/B'd inside one process: g_evalCache is keyed on position
// alone, so both sides of a --optA/--optB match would share cached scores
// (BUGS.md 8). Comparing this needs two binaries.
#ifndef KING_DANGER_SCALE_PCT
#define KING_DANGER_SCALE_PCT 0
#endif
static const int KING_DANGER_SCALE = KING_DANGER_SCALE_PCT;
#ifndef KING_DANGER_MIN_ATTACKERS_N
#define KING_DANGER_MIN_ATTACKERS_N 2
#endif
static const int KING_DANGER_MIN_ATTACKERS = KING_DANGER_MIN_ATTACKERS_N;
#ifndef KING_DANGER_OFFSET_N
#define KING_DANGER_OFFSET_N 0
#endif
static const int KING_DANGER_OFFSET = KING_DANGER_OFFSET_N;
#ifndef KING_DANGER_DEFENDER_W_N
#define KING_DANGER_DEFENDER_W_N 0
#endif
static const int KING_DANGER_DEFENDER_W = KING_DANGER_DEFENDER_W_N;
#ifndef KING_DANGER_WEAK_W_N
#define KING_DANGER_WEAK_W_N 0
#endif
static const int KING_DANGER_WEAK_W = KING_DANGER_WEAK_W_N;
#ifndef KING_DANGER_CHECK_W_N
#define KING_DANGER_CHECK_W_N 0
#endif
static const int KING_DANGER_CHECK_W = KING_DANGER_CHECK_W_N;
#ifndef KING_DANGER_NO_QUEEN_CUT_N
#define KING_DANGER_NO_QUEEN_CUT_N 0
#endif
static const int KING_DANGER_NO_QUEEN_CUT = KING_DANGER_NO_QUEEN_CUT_N;

// Attack information needed by king safety, and by nothing else.
//
// Built once per evaluation and **only when the term is live**, because it walks
// every piece's attack set a second time. With KING_DANGER_SCALE at 0 the
// shipped engine never constructs it, which is why bench is unchanged.
struct KingSafetyAttacks {
    uint8_t  count[3][64] = {};   // how many times each colour attacks each square
    uint64_t byType[3][7] = {};   // squares attacked, per colour per piece type
    uint64_t any[3]       = {};   // squares attacked at all, per colour
};

static void buildKingSafetyAttacks(const Position& pos, KingSafetyAttacks& a) {
    for (Bitboard scan_ = pos.occupancyAll; scan_; scan_ &= scan_ - 1) {
        const int i = lsb(scan_);
        const Piece& p = pos.squares[i];
        if (p.type() == NONE) continue;
        const int col = (int)p.color();
        const int t   = (int)p.type();
        forEachAttackedSquare(pos, i, [&](int sq) {
            if (a.count[col][sq] < 255) ++a.count[col][sq];
            a.byType[col][t] |= 1ULL << sq;
            a.any[col]       |= 1ULL << sq;
        });
    }
}

// Friendly pawns and minor pieces standing in the king's own zone.
//
// Ethereal subtracts KingDefenders[count] from its safety score, and the
// absence of any such term is the specific reason ours damages ordinary
// positions: it charges for enemy pieces being *near* a king without asking
// whether anything is guarding it. In a normal middlegame a castled king has
// three pawns and often a minor in its zone, and those should cancel most of
// the proximity charge.
static int kingDefenders(const Position& pos, int kingSq, PieceColor defender) {
    if (kingSq < 0) return 0;
    const int kf = kingSq % 8, kr = kingSq / 8;
    int n = 0;
    for (int df = -1; df <= 1; ++df)
        for (int dr = -1; dr <= 1; ++dr) {
            const int f = kf + df, r = kr + dr;
            if (f < 0 || f > 7 || r < 0 || r > 7) continue;
            const Piece& p = pos.squares[r * 8 + f];
            if (p.color() != defender) continue;
            const PieceType t = p.type();
            if (t == PAWN || t == KNIGHT || t == BISHOP) ++n;
        }
    return n;
}

static int kingDanger(const Position& pos, int kingSq, PieceColor attacker,
                      const KingSafetyAttacks& atk) {
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
    int attackers = 0;      // distinct pieces bearing on the zone
    for (Bitboard scan_ = (attacker == COLOR_WHITE ? pos.occupancyWhite : pos.occupancyBlack); scan_; scan_ &= scan_ - 1) {
        const int i = lsb(scan_);
        const Piece& p = pos.squares[i];
        if (p.type() == NONE || p.type() == KING || p.color() != attacker) continue;
        int hits = 0;
        forEachAttackedSquare(pos, i, [&](int sq) { if ((zone >> sq) & 1ULL) ++hits; });
        if (hits > 0) { danger += KING_DANGER_WEIGHT[p.type()] * hits; ++attackers; }
    }

    // Nothing below KING_DANGER_MIN_ATTACKERS *distinct pieces*, and this is
    // the correction that six failed gates were missing.
    //
    // The curve above is quadratic in a weighted square count starting from
    // zero, so it charged something in almost every position: a lone queen with
    // four squares in the zone cost 160cp at scale 500, a rook touching three
    // cost 50. Those are ordinary positions. A queen on an open diagonal near a
    // king is piece activity, not an attack, and taxing it applies at every
    // node in the tree rather than only where an attack exists.
    //
    // The damage is measured, not inferred. At scale 500 the old curve moved
    // `comp` error 543.7 -> 504.5 while moving `ctl` error 181.9 -> **189.3**,
    // so it improved the rare case by degrading the common one, and a 1 680
    // game gauntlet against a fixed external attacker then had it scoring
    // 62.11% -> 59.79%.
    //
    // One attacker is not an attack. ROADMAP 6.4's own candidate charged on a
    // saturating curve in distinct attacker count ({0,0,50,75,88,94,97,99}%),
    // which is zero for one attacker; that property is what this restores.
    if (attackers < KING_DANGER_MIN_ATTACKERS) return 0;

    // Subtract an offset before charging anything, which is the mechanism
    // Ethereal uses and this term lacked. Its safety score carries
    // SafetyAdjustment = S(-74, -26) and is then clamped by MAX(0, mg), so
    // small amounts of attack produce *no* penalty at all rather than a small
    // one. A threshold on attacker *count* is the wrong axis and was measured
    // as such: raising it moved comp and ctl error back toward baseline
    // together instead of separating them.
    const PieceColor defender = (attacker == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
    const int att = (int)attacker, def = (int)defender;

    // Weak squares: in the king's own zone, attacked by the enemy and defended
    // at most once by us. Ethereal charges SafetyWeakSquares = S(42, 41) per
    // one. A square the defender covers twice is not a hole; a square covered
    // once or not at all is where an attack actually lands.
    if (KING_DANGER_WEAK_W) {
        int weak = 0;
        for (int sq = 0; sq < 64; ++sq) {
            if (!((zone >> sq) & 1ULL)) continue;
            if (atk.count[att][sq] > 0 && atk.count[def][sq] <= 1) ++weak;
        }
        danger += KING_DANGER_WEAK_W * weak;
    }

    // Safe checks: squares from which an enemy piece could check this king, that
    // the enemy can actually reach, and that we do not defend.
    //
    // "Where could a knight check from" is knight-attacks-*from the king square*,
    // which is why forEachAttackedSquareAs takes an explicit type: it is asked
    // about a hypothetical piece standing where the king stands. For sliders it
    // respects occupancy, so a blocked line yields no check square.
    //
    // This is Ethereal's dominant term -- SafetySafeKnightCheck = 112 against
    // SafetyAttackValue = 45, so one unanswerable check outweighs two attacked
    // squares -- and the thing our version had no concept of at all.
    if (KING_DANGER_CHECK_W) {
        int checks = 0;
        const PieceType kinds[4] = { KNIGHT, BISHOP, ROOK, QUEEN };
        for (int k = 0; k < 4; ++k) {
            uint64_t from = 0;
            forEachAttackedSquareAs(pos, kingSq, kinds[k], defender,
                                    [&](int sq) { from |= 1ULL << sq; });
            const uint64_t safe = from & atk.byType[att][(int)kinds[k]] & ~atk.any[def];
            checks += __builtin_popcountll(safe);
        }
        danger += KING_DANGER_CHECK_W * checks;
    }

    // Without a queen an attack rarely converts. Ethereal charges
    // SafetyNoEnemyQueens = S(-237, -259), which effectively switches the term
    // off; a flat reduction is the same idea at this granularity.
    if (KING_DANGER_NO_QUEEN_CUT) {
        bool hasQueen = false;
        for (int i = 0; i < 64 && !hasQueen; ++i) {
            const Piece& p = pos.squares[i];
            if (p.type() == QUEEN && p.color() == attacker) hasQueen = true;
        }
        if (!hasQueen) danger -= KING_DANGER_NO_QUEEN_CUT;
    }

    // Subtract what is guarding the king before charging for what attacks it.
    danger -= KING_DANGER_DEFENDER_W * kingDefenders(pos, kingSq, defender);

    danger -= KING_DANGER_OFFSET;
    if (danger <= 0) return 0;
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

EvalDetails evaluate_details(const Position& pos) {
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
    // Rooks and queens still on the pos, as "how much is there to attack
    // with": the king-exposure charge is proportional to it, which is what
    // keeps the term out of endgames.
    int whiteHeavy = 0, blackHeavy = 0;

    // The nine full-board scans this evaluation used to make are now walks of
    // the relevant bitboard. lsb() returns the lowest set bit, so each visits
    // exactly the squares its guard accepted, in the same ascending order, and
    // the loop bodies are unchanged. In a middlegame that is about half the
    // iterations, and for the narrower boards far fewer.

    // Per-file pawn masks: bit r set means a pawn of that colour stands on
    // rank r of that file. The passed/doubled/isolated/backward tests and the
    // rook open-file test each used to walk a file or a rank range square by
    // square; with these they become constant-time bit tests.
    uint8_t whitePawnFile[8] = {}, blackPawnFile[8] = {};
    for (Bitboard scan_ = pos.white[BB_PAWN] | pos.black[BB_PAWN]; scan_; scan_ &= scan_ - 1) {
        const int i = lsb(scan_);
        const Piece& p = pos.squares[i];
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
    for (Bitboard scan_ = pos.occupancyAll; scan_; scan_ &= scan_ - 1) {
        const int i = lsb(scan_);
        const Piece& p = pos.squares[i];
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
                if (file > 0 && pos.squares[i-1].type() == PAWN && pos.squares[i-1].color() == COLOR_WHITE) whiteConnectedPawns++;
                if (file < 7 && pos.squares[i+1].type() == PAWN && pos.squares[i+1].color() == COLOR_WHITE) whiteConnectedPawns++;
                // No black pawn on this or an adjacent file, ahead of us
                // (white advances toward rank 0).
                if ((span3(blackPawnFile, file) & ranksBelow(rank)) == 0) whitePassedPawns++;
                // Every friendly pawn behind us on the same file.
                whiteDoubledPawns += __builtin_popcount((unsigned)(whitePawnFile[file] & ranksAbove(rank)));
                if (adj2(whitePawnFile, file) == 0) whiteIsolatedPawns++;
                if ((span3(whitePawnFile, file) & ranksAbove(rank)) == 0) whiteBackwardPawns++;
                if ((file > 0 && rank < 7 && pos.squares[(rank+1)*8+file-1].type() == PAWN && pos.squares[(rank+1)*8+file-1].color() == COLOR_WHITE) ||
                    (file < 7 && rank < 7 && pos.squares[(rank+1)*8+file+1].type() == PAWN && pos.squares[(rank+1)*8+file+1].color() == COLOR_WHITE))
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
                if (file > 0 && pos.squares[i-1].type() == PAWN && pos.squares[i-1].color() == COLOR_BLACK) blackConnectedPawns++;
                if (file < 7 && pos.squares[i+1].type() == PAWN && pos.squares[i+1].color() == COLOR_BLACK) blackConnectedPawns++;
                // Mirror of the white case: black advances toward rank 7.
                if ((span3(whitePawnFile, file) & ranksAbove(rank)) == 0) blackPassedPawns++;
                blackDoubledPawns += __builtin_popcount((unsigned)(blackPawnFile[file] & ranksBelow(rank)));
                if (adj2(blackPawnFile, file) == 0) blackIsolatedPawns++;
                if ((span3(blackPawnFile, file) & ranksBelow(rank)) == 0) blackBackwardPawns++;
                if ((file > 0 && rank > 0 && pos.squares[(rank-1)*8+file-1].type() == PAWN && pos.squares[(rank-1)*8+file-1].color() == COLOR_BLACK) ||
                    (file < 7 && rank > 0 && pos.squares[(rank-1)*8+file+1].type() == PAWN && pos.squares[(rank-1)*8+file+1].color() == COLOR_BLACK))
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
    whiteMobility = countMobility(pos, COLOR_WHITE, whiteKingSq);
    blackMobility = countMobility(pos, COLOR_BLACK, blackKingSq);
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
                    if (pos.squares[idx].type() == PAWN && pos.squares[idx].color() == COLOR_WHITE) whiteKingSafety += EvalWeights::KING_PAWN_SHIELD;
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
                    if (pos.squares[idx].type() == PAWN && pos.squares[idx].color() == COLOR_BLACK) blackKingSafety += EvalWeights::KING_PAWN_SHIELD;
                }
            }
        }
    }
    kingSafetyScore = whiteKingSafety - blackKingSafety;

    // Center control
    int centerSquares[4] = { 27, 28, 35, 36 };
    for (int i = 0; i < 4; ++i) {
        const Piece& p = pos.squares[centerSquares[i]];
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
    for (Bitboard scan_ = pos.white[BB_KNIGHT] | pos.black[BB_KNIGHT] | pos.white[BB_BISHOP] | pos.black[BB_BISHOP]; scan_; scan_ &= scan_ - 1) {
        const int i = lsb(scan_);
        const Piece& p = pos.squares[i];
        if (p.type() == KNIGHT || p.type() == BISHOP) {
            int rank = i / 8, file = i % 8;
            if (p.color() == COLOR_WHITE && rank <= 3) {
                if ((file > 0 && pos.squares[i+7].type() == PAWN && pos.squares[i+7].color() == COLOR_WHITE) ||
                    (file < 7 && pos.squares[i+9].type() == PAWN && pos.squares[i+9].color() == COLOR_WHITE))
                    outpostBonus += EvalWeights::OUTPOST;
            }
            if (p.color() == COLOR_BLACK && rank >= 4) {
                if ((file > 0 && pos.squares[i-9].type() == PAWN && pos.squares[i-9].color() == COLOR_BLACK) ||
                    (file < 7 && pos.squares[i-7].type() == PAWN && pos.squares[i-7].color() == COLOR_BLACK))
                    outpostBonus -= EvalWeights::OUTPOST;
            }
        }
    }

    // Trapped pieces
    for (Bitboard scan_ = pos.white[BB_ROOK] | pos.black[BB_ROOK] | pos.white[BB_BISHOP] | pos.black[BB_BISHOP]; scan_; scan_ &= scan_ - 1) {
        const int i = lsb(scan_);
        const Piece& p = pos.squares[i];
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
        // Built here rather than at file scope because it walks every attack set
        // a second time. At KING_DANGER_SCALE 0 this block never runs, so the
        // shipped engine pays nothing for a term it does not use.
        KingSafetyAttacks ksAtk;
        if (KING_DANGER_SCALE != 0) buildKingSafetyAttacks(pos, ksAtk);
        kingSafetyScore -= (int)((kingDanger(pos, wKingSq, COLOR_BLACK, ksAtk)
                                  - kingDanger(pos, bKingSq, COLOR_WHITE, ksAtk))
                                 * gamePhaseFactor);
    }
    kingSafetyScore -= (int)((kingExposure(whiteKingFile, whiteKingRank, 7,
                                           (pos.castlingRights & (CASTLE_WK | CASTLE_WQ)) != 0,
                                           whitePawnFile, blackPawnFile, blackHeavy)
                              - kingExposure(blackKingFile, blackKingRank, 0,
                                             (pos.castlingRights & (CASTLE_BK | CASTLE_BQ)) != 0,
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

    for (Bitboard scan_ = pos.occupancyAll; scan_; scan_ &= scan_ - 1) {
        const int i = lsb(scan_);
        const Piece& attacker = pos.squares[i];
        if (attacker.type() == NONE) continue;
        const PieceColor ac = attacker.color();
        const int attackerValue = pieceValues[attacker.type()];

        forEachAttackedSquare(pos, i, [&](int j) {
            attackedBy[ac][j] = true;

            const Piece& target = pos.squares[j];
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
    for (Bitboard scan_ = pos.occupancyAll; scan_; scan_ &= scan_ - 1) {
        const int i = lsb(scan_);
        const Piece& p = pos.squares[i];
        if (p.type() == NONE) continue;
        if (attackedBy[p.color()][i]) continue;
        if (p.color() == COLOR_WHITE) whiteUndefended++;
        else blackUndefended++;
    }
    undefendedPenalty = -EvalWeights::UNDEFENDED * whiteUndefended + EvalWeights::UNDEFENDED * blackUndefended;

    // Space advantage
    for (Bitboard scan_ = pos.occupancyAll; scan_; scan_ &= scan_ - 1) {
        const int i = lsb(scan_);
        int rank = i / 8;
        const Piece& p = pos.squares[i];
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
// The key is the zobrist hash. evaluate_details() reads pos.squares[] and
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

}  // namespace bbeval

EvalDetails bbEvaluateDetails(const Position& pos) {
    return bbeval::evaluate_details(pos);
}

namespace {

// The same direct-mapped cache evaluation.cpp uses, and deliberately its own
// rather than shared: the two evaluations must be able to disagree during
// verification, and a shared cache would hand one of them the other's answer
// and hide exactly the bug this phase exists to find.
struct BBEvalCacheEntry { uint64_t lock = 0; int32_t score = 0; };
constexpr size_t BB_EVAL_CACHE_ENTRIES = 1u << 19;
BBEvalCacheEntry g_bbEvalCache[BB_EVAL_CACHE_ENTRIES];
inline uint64_t encodeLock(uint64_t hash, int32_t score) {
    return hash ^ (uint64_t)(uint32_t)score;
}

}  // namespace

int bbEvaluate(const Position& pos) {
    const uint64_t hash = pos.hash;
    BBEvalCacheEntry& entry = g_bbEvalCache[hash & (BB_EVAL_CACHE_ENTRIES - 1)];
    const uint64_t lock = entry.lock;
    const int32_t cached = entry.score;
    if (encodeLock(lock, cached) == hash) return cached;

    const int score = bbEvaluateDetails(pos).total;
    entry.score = (int32_t)score;
    entry.lock = encodeLock(hash, (int32_t)score);
    return score;
}
