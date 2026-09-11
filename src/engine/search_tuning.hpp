#pragma once
//
// The search's tuned constants, the LMR reduction table and the reduction
// formula.
//
// Lifted out of search.cpp so the bitboard search being built beside it
// (docs/BITBOARD-REPLACEMENT.md) reads the same numbers rather than its own
// copies. The acceptance test for that work is an exact node count against this
// search, and two copies of a tuned constant are exactly what would pass the
// test today and diverge the first time one of them is retuned.
//
// Everything here is internally linked or inline, so each translation unit gets
// its own copy of the values and no copy of the decisions.
//
// This is a move, not a change. Every value, table and comment is unaltered,
// and the bench signature proves it.
//
#include "search.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <atomic>
#include <chrono>

// Piece values for MVV-LVA ordering in the quiescence search
static const int QS_PIECE_VALUES[7] = { 0, 20000, 100, 320, 330, 500, 900 };

// Sorts a losing capture below every sound one while leaving MVV-LVA to order
// within each group. Larger than any MVV-LVA key (10 × queen = 9000), so the
// two groups can never interleave.
static constexpr int SEE_LOSING_CAPTURE_BAND = 100000;

// How far past the main search's horizon quiescence may recurse (PLAN.md 3.1).
//
// Eight plies is enough to resolve any exchange sequence that occurs in a real
// game — a capture chain longer than that needs eight defenders of one square —
// while bounding the pathological case that motivated this: in check,
// quiescence searches every legal evasion rather than captures only, so a long
// forcing sequence of checks had no limit at all.
static constexpr int QS_MAX_DEPTH = 8;

// Late move pruning fires only at shallow remaining depth. Deep nodes are
// where a late quiet move can still change the result, and they are also the
// nodes worth spending on -- pruning there trades the search's judgement for
// its speed at exactly the wrong end.
static constexpr int LMP_MAX_DEPTH = 3;

// The alternative `lmpShallow` selects. One ply shallower prunes less and keeps
// more of the search's judgement; whether that is worth the nodes it gives back
// is a gate's question, not a comment's.
static constexpr int LMP_MAX_DEPTH_SHALLOW = 2;

// Late move reduction table for SearchOptions::lmrTable, indexed by remaining
// depth and by how many moves have already been tried at this node.
//
// R = BASE + ln(depth) * ln(moveCount) / DIVISOR, which is the conventional
// shape: the reduction grows slowly with depth and slowly with move number, and
// their product is what makes a late move in a deep node cheap to dismiss.
// Worked through, against the fixed R = 1 it replaces:
//     depth  3, move  4 -> 1     (unchanged, where the old constant was right)
//     depth  8, move 12 -> 2
//     depth 12, move 30 -> 4
//
// The constants are the conventional starting values and are a first guess
// rather than a tuned result. `BUGS.md` 18 and PLAN 3.1 are the standing
// warning about that: 3.1 lost 50 Elo by moving a single pruning constant
// inside the evaluation's own error, so this ships behind a toggle and a gate
// like everything else rather than on the strength of the formula being
// standard elsewhere.
static constexpr double LMR_BASE    = 0.77;
static constexpr double LMR_DIVISOR = 2.36;
static constexpr int LMR_MAX_DEPTH_IDX = 64;
static constexpr int LMR_MAX_MOVE_IDX  = 64;

static const std::array<std::array<uint8_t, LMR_MAX_MOVE_IDX>, LMR_MAX_DEPTH_IDX>
LMR_TABLE = [] {
    std::array<std::array<uint8_t, LMR_MAX_MOVE_IDX>, LMR_MAX_DEPTH_IDX> t{};
    for (int d = 1; d < LMR_MAX_DEPTH_IDX; ++d)
        for (int m = 1; m < LMR_MAX_MOVE_IDX; ++m)
            t[d][m] = (uint8_t)(LMR_BASE + std::log((double)d) * std::log((double)m)
                                           / LMR_DIVISOR);
    return t;
}();

// The reduction this node should apply. Clamped so the reduced search keeps at
// least one ply: dropping to depth 0 hands the move straight to quiescence,
// which is a different and much more aggressive decision than reducing it.
// How much of the reduction a fully-rewarded quiet move gets back. Two plies,
// chosen to be the same order as the table's own spread rather than tuned: at
// depth 12 move 30 the table gives 4, so this can return a good move to 2.
static constexpr int HIST_RED_MAX = 2;

// Null-move reduction when SearchOptions::nullDepthR is on: R = base + depth/div.
// Deliberately structural, with Stockfish's eval-dependent bonus term left out
// of the first version, because every eval-reading heuristic gated here has
// failed and the depth term reads nothing.
static constexpr int NULL_R_BASE = 2;
static constexpr int NULL_R_DIV  = 4;

// Late move pruning under SearchOptions::lmpDeep: keep base + slope*depth quiet
// moves, at any depth up to the cap. Linear rather than the shipped quadratic,
// because 3 + depth*depth passes the number of legal moves in a typical
// position by depth 6 and stops pruning anything at all.
static constexpr int LMP_DEEP_MAX_DEPTH = 10;
static constexpr int LMP_DEEP_BASE      = 3;
static constexpr int LMP_DEEP_SLOPE     = 3;

// Tactical moves searched per quiescence node under SearchOptions::qMoveCount,
// before the exemptions. Stockfish uses 2; this starts at 4 because it cannot
// yet exempt checking moves and a prune that guesses wrong loses the move
// rather than costing a re-search.
static constexpr int QS_MOVE_COUNT_LIMIT = 4;

static inline int lmrReduction(int depth, int moveIndex, bool improving, int history) {
    // Without the table the reduction is the old fixed ply, still nudged by
    // improving so the two toggles compose rather than one silencing the other.
    if (!g_searchOptions.lmrTable) return improving ? 1 : 2;
    const int d = depth     < LMR_MAX_DEPTH_IDX ? depth     : LMR_MAX_DEPTH_IDX - 1;
    const int m = moveIndex < LMR_MAX_MOVE_IDX  ? moveIndex : LMR_MAX_MOVE_IDX  - 1;
    int r = LMR_TABLE[d][m];
    // One ply more when the side to move is drifting. Its late quiet moves are
    // demonstrably not turning the position around, so they are worth less.
    if (!improving) ++r;
    // A move this history table has rewarded is one the search has found useful
    // before, so take some of the reduction back. Scaled against HISTORY_MAX so
    // the adjustment tracks the table's own range as it ages.
    // Two-sided, which it could not be until histMalus existed. A well-rewarded
    // quiet move gets reduction back; a punished one is reduced *further*, and
    // the second half is where the node savings are. The `history > 0` guard
    // that used to be here dated from a table that only ever added, and it
    // silently discarded exactly the half that pays.
    if (g_searchOptions.histReduction && history > 0) {
        r -= (history * HIST_RED_MAX) / MoveOrderer::HISTORY_MAX;
    }
    if (r < 1) r = 1;
    if (r > depth - 2) r = depth - 2;   // leave >= 1 ply of real search
    return r < 1 ? 1 : r;
}

// Singular extensions. The probe is a search in its own right, so it only runs
// where a spare ply is worth paying for: deep enough that one more matters, and
// against a table entry deep enough to be worth testing.
// 10 rather than the more usual 8, chosen by measuring the probe's price at a
// realistic depth rather than by convention. On one middlegame position at
// `go depth 11`, against the same search without it:
//     MIN_DEPTH  8   +49.8% nodes
//     MIN_DEPTH 10   +11.1%
//     MIN_DEPTH 12    +0.0%  (never fires at depth 11)
// The probe is itself a search, so its cost is paid at every qualifying node
// whether or not the extension is granted; 8 pays it far too often.
//
// Note that **bench cannot see this feature at all** -- its deepest interior
// node at `bench 8` is depth 7, so the signature is identical on and off. The
// tree check for this one has to be a real search at depth 11 or more.
static constexpr int SINGULAR_MIN_DEPTH = 10;  // no probe shallower than this
static constexpr int SINGULAR_TT_SLACK   = 3;  // entry may be this much shallower
static constexpr int SINGULAR_MARGIN     = 2;  // beta drop, per ply of depth

// How far below the best a root move may score and still be considered for the
// random tiebreak. Ten centipawns is deliberately small: the aim is opening
// variety against the same opponent, not to play a worse move on purpose.
static constexpr int ROOT_RANDOM_MARGIN = 10;

// Randomise only in the opening, by fullmove number.
//
// This is where the defect lives -- `BUGS.md` 6 is about *repeated openings*
// against the same opponent, and two games that diverge by move 12 are already
// decorrelated. It is also where the cost is affordable. The tiebreak needs a
// true score for every root move, which means no alpha cutoffs at the root,
// which costs +234% nodes at bench 6. Paying that for the whole game would lose
// far more than the variety is worth; paying it for twelve moves, in positions
// the engine finds nearly equal anyway, is cheap.
static constexpr int ROOT_RANDOM_MAX_MOVE = 12;

// Aspiration window sizing. 50 is the shipped fixed width and stays the floor,
// so an adaptive window can only ever be wider -- narrower would trade misses
// for cutoffs in the direction that already works.
static constexpr int ASP_BASE_DELTA = 50;
static constexpr int ASP_MAX_DELTA  = 400;   // past this it is barely a window

extern uint64_t g_rootSeed;   // defined in search.cpp

// xorshift64*, seeded per search from g_rootSeed and the root position. Not a
// good general-purpose generator and does not need to be: it chooses among a
// handful of moves, and being cheap and dependency-free matters more.

// +6.1 with the interval spanning zero, which is to say "not demonstrably
// better than the broken version". A tight margin asks that evaluation to be
// right about positions it has never been shown to judge well.
//
// At 900 the same rule cuts 8.3% of nodes rather than 37.5%, and prunes only
// what almost no evaluation error could rescue.
static constexpr int QS_DELTA_MARGIN = 900;

// The search is negamax: every score is from the point of view of the side to
// move, and a child's score is negated on the way back up. evaluate() is
// white-perspective, so the single conversion happens in scoreForSideToMove()
// below and nowhere else.
//
// This replaced a white-perspective minimax that branched on whiteToMove at
// every decision — stand-pat, the move loop, null move, LMR, the alpha-beta
// update — and so carried two mirrored copies of each. The duplication was not
// only bulk: it is what produced the TT bound-classification bug fixed earlier
// (the black branch shrinks beta, so comparing against the shrunk value
// misfiled every black PV node as a lower bound).

// Score used for checkmate, from the perspective of the side to move: being
// mated is -(MATE_SCORE - ply). Subtracting ply makes nearer mates score
// higher, so the engine converges on the fastest mate instead of shuffling
// between equally "mating" lines forever. Stalemate is scored 0.
static constexpr int MATE_SCORE = SEARCH_MATE_SCORE;

// Window infinities. Deliberately not std::numeric_limits<int>::min(): negamax
// negates the window on every recursion, and -INT_MIN is undefined behaviour.
// Any value comfortably above the largest representable mate score works;
// scoreToTT() can push a mate to MATE_SCORE + ply, so this leaves headroom.
static constexpr int INF = 32000;

// Time and node budget, shared with the bitboard search so that a node-limited
// run stops at the same node in both. These are state rather than tuning: they
// are written once when a search starts and read on the hot path, and they live
// in search.cpp.
extern bool g_hasDeadline;
extern std::atomic<bool> g_outOfTime;
extern std::chrono::steady_clock::time_point g_deadline;
extern uint64_t g_nodeLimit;
extern std::chrono::steady_clock::time_point g_softDeadline;

// Whether the budget has enough left to start another iteration. Defined in
// search.cpp and shared, because it decides nothing about the position and two
// copies of a deadline rule is how one of them overruns.
bool budgetSpent(int currentDepth, int maxDepth, uint64_t nodesUsed,
                 uint64_t depthStartNodes,
                 std::chrono::steady_clock::time_point depthStart, bool verbose);
static constexpr uint64_t TIME_CHECK_INTERVAL = 2048;
