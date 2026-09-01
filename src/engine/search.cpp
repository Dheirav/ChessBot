#include "search.hpp"
#include "movegen.hpp"
#include "board.hpp"
#include "evaluation.hpp"
#include "transposition_table.hpp"
#include "move_ordering.hpp"
#include "legal_move_validator.hpp"
#include "see.hpp"
#include <limits>
#include <algorithm>
#include <cstring>
#include <atomic>
#include <iostream>
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

uint64_t g_rootSeed = 0;

// xorshift64*, seeded per search from g_rootSeed and the root position. Not a
// good general-purpose generator and does not need to be: it chooses among a
// handful of moves, and being cheap and dependency-free matters more.
static uint64_t rootRand(uint64_t& state) {
    state ^= state >> 12; state ^= state << 25; state ^= state >> 27;
    return state * 0x2545F4914F6CDD1DULL;
}

// Delta pruning margin: how much a capture is allowed to be behind alpha before
// it is dismissed as unable to catch up. A capture that cannot reach alpha even
// after winning its victim plus this much positional compensation is not going
// to change the score of the node.
//
// A queen, as PLAN.md 3.1 specifies. It was first implemented at 200, which is
// the conventional figure and four and a half times more aggressive, and a
// 3 360-game gate rejected it at **-50.0 Elo, 95% CI [-60.3, -39.7]** — all
// twelve shards negative, -36 to -69. That is not a marginal result and it is
// worth understanding rather than just retuning past.
//
// Delta pruning is a bet on the static evaluation: it discards a subtree
// because stand-pat says the capture cannot bridge the gap to alpha. That bet
// is only as good as the evaluation making it, and this engine's evaluation is
// hand-written piece-square tables that carried three correctness bugs until
// 2026-08-14 and whose quality is still unmeasured — the Phase 4 gate returned
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

// evaluate() is white-perspective; the search is not.
static int scoreForSideToMove(const Board& board) {
    int white = evaluate(board);
    return (board.activeColor == COLOR_WHITE) ? white : -white;
}

SearchOptions g_searchOptions;

// --- Correction history (SearchOptions::corrHist) ---
//
// The search already measures this evaluation's error thousands of times a
// second and discards it: every node has a static score and, once searched, the
// value that search actually returned. The gap between them is the error, for
// free. This keeps a running average of it, keyed on pawn structure, and adds
// it back as an offset next time.
//
// Keyed on pawns because the key has to be *coarser* than the position or it
// never repeats -- a full-hash table would learn nothing -- and because pawn
// structure is both persistent across moves and the thing a material-dominated
// evaluation misprices most. Board::pawnHash exists for this and is maintained
// beside the main hash without being folded into it.
static constexpr int CORR_SIZE   = 16384;   // power of two: indexed by mask
static constexpr int CORR_GRAIN  = 256;     // fixed point, so small errors survive averaging
static constexpr int CORR_CAP    = 96 * CORR_GRAIN;  // a correction, not a second evaluation
static constexpr int CORR_WEIGHT = 128;     // denominator of the running average
static int g_corrHist[2][CORR_SIZE];

static inline int corrSide(const Board& board) {
    return (board.activeColor == COLOR_WHITE) ? 0 : 1;
}

static inline size_t corrSlot(const Board& board) {
    return (size_t)(board.getPawnHash() & (uint64_t)(CORR_SIZE - 1));
}

// Static evaluation with the learned correction applied. Bit-identical to
// scoreForSideToMove() while the toggle is off, which is what keeps the bench
// signature intact.
static int correctedEval(const Board& board) {
    const int raw = scoreForSideToMove(board);
    if (!g_searchOptions.corrHist) return raw;
    const int adjusted = raw + g_corrHist[corrSide(board)][corrSlot(board)] / CORR_GRAIN;
    // A correction must never manufacture a mate score: those are compared
    // against MATE_SCORE thresholds all over the search and a fake one would
    // propagate as a real mate.
    return std::max(-MATE_SCORE + 1000, std::min(MATE_SCORE - 1000, adjusted));
}

// Weighted toward recent observations, and clamped. The weight rises with
// depth because a deeper search's verdict is better evidence about the error
// than a shallow one's.
static void updateCorrHist(const Board& board, int depth, int diff) {
    int& entry = g_corrHist[corrSide(board)][corrSlot(board)];
    const int w = std::min(depth + 1, 16);
    const long blended = ((long)entry * (CORR_WEIGHT - w)
                          + (long)diff * CORR_GRAIN * w) / CORR_WEIGHT;
    entry = (int)std::max((long)-CORR_CAP, std::min((long)CORR_CAP, blended));
}

// The one place a search feature is named.
//
// Setting an option by name and describing which options are set are the same
// knowledge, so they read the same table. Keeping them as two separate lists
// cost a match: a run gated on `seepruning` printed a header that named only
// the three old heuristics, because the describe() in tests/match.cpp had never
// heard of SEE. A label that silently omits the variable under test is worse
// than no label — it reads as confirmation that the right thing was compared.
//
// Adding a feature means adding one line here, and the harness header, the UCI
// option list and the match log all learn about it at once.
const SearchOptionEntry SEARCH_OPTIONS[] = {
    {"nullmove",    "nullmove", "NullMove",    &SearchOptions::nullMove},
    {"lmr",         "lmr",      "LMR",         &SearchOptions::lmr},
    {"aspiration",  "asp",      "Aspiration",  &SearchOptions::aspiration},
    {"seeordering", "seeord",   "SeeOrdering", &SearchOptions::seeOrdering},
    {"seepruning",  "seeprune", "SeePruning",  &SearchOptions::seePruning},
    {"ttaging",     "ttage",    "TtAging",     &SearchOptions::ttAging},
    {"qbound",      "qbound",   "QBound",      &SearchOptions::qBound},
    {"deltapruning","delta",    "DeltaPruning",&SearchOptions::deltaPruning},
    {"checkext",    "checkext", "CheckExt",    &SearchOptions::checkExtension},
    {"softtime",    "softtime", "SoftTime",    &SearchOptions::softTime},
    {"iid",         "iid",      "Iid",         &SearchOptions::iid},
    {"timealloc",   "timealloc","TimeAlloc",   &SearchOptions::timeAlloc},
    {"revfutility", "revfut",   "RevFutility", &SearchOptions::revFutility},
    {"razoring",    "razor",    "Razoring",    &SearchOptions::razoring},
    {"lmp",         "lmp",      "Lmp",         &SearchOptions::lateMovePruning},
    {"lmpshallow",  "lmpsh",    "LmpShallow",  &SearchOptions::lmpShallow},
    {"lmpdepth1",   "lmpd1",    "LmpDepth1",   &SearchOptions::lmpDepth1},
    {"singularext", "singext",  "SingularExt", &SearchOptions::singularExt},
    {"razortight",  "razortt",  "RazorTight",  &SearchOptions::razorTight},
    {"rootrandom",  "rootrnd",  "RootRandom",  &SearchOptions::rootRandom},
    {"aspadaptive", "aspadapt", "AspAdaptive", &SearchOptions::aspAdaptive},
    {"conthist",    "conthist", "ContHist",    &SearchOptions::contHist},
    {"capthist",    "capthist", "CaptHist",    &SearchOptions::captHist},
    {"corrhist",    "corrhist", "CorrHist",    &SearchOptions::corrHist},
};
const size_t SEARCH_OPTION_COUNT = sizeof(SEARCH_OPTIONS) / sizeof(SEARCH_OPTIONS[0]);

bool setSearchOption(SearchOptions& opts, const std::string& name, bool value) {
    for (size_t i = 0; i < SEARCH_OPTION_COUNT; ++i) {
        if (name == SEARCH_OPTIONS[i].name) {
            opts.*(SEARCH_OPTIONS[i].field) = value;
            return true;
        }
    }
    return false;
}

std::string describeSearchOptions(const SearchOptions& opts) {
    std::string out;
    for (size_t i = 0; i < SEARCH_OPTION_COUNT; ++i) {
        if (!(opts.*(SEARCH_OPTIONS[i].field))) continue;
        if (!out.empty()) out += "+";
        out += SEARCH_OPTIONS[i].shortName;
    }
    return out.empty() ? std::string("plain-alphabeta") : out;
}
uint64_t g_searchNodes = 0;
SearchInfoFn g_searchInfo = nullptr;

// --- Time control ---
//
// The deadline lives in file scope rather than being threaded through every
// recursive call, because it is read at nearly every node and written exactly
// once per search. When no budget is set, searchAborted() short-circuits on
// g_hasDeadline and the search behaves exactly as it did before time control
// existed — which is what keeps tests/bench reproducible.
static bool g_hasDeadline = false;
static bool g_outOfTime = false;
static std::chrono::steady_clock::time_point g_deadline;
static uint64_t g_nextTimeCheck = 0;

// A *soft* deadline, separate from the hard one above (BUGS.md 11).
//
// One deadline has to serve two different questions and answers the second one
// badly. "May I still be searching?" wants the hard limit -- passing it on a
// clock is a forfeit. "Should I begin another iteration?" wants a smaller one,
// because an iteration that cannot finish is discarded and its time buys
// nothing.
//
// Using the hard limit for both means the search stops as soon as the *whole*
// predicted next iteration no longer fits, and the prediction is 2.3x the last
// one -- so it routinely abandons a large tail of its budget. Measured on
// 2026-08-16 over five positions at a 90s+1s clock: the engine used **75%** of
// what parseGo had allocated it, and the two effects together left a real
// 900+10 game finished with 535 of 1 500 available seconds unspent.
//
// Split, the soft limit governs starting an iteration and the hard limit
// governs abandoning one already running. The search may now begin an iteration
// it is not certain to finish, and keep the result if it lands, which is where
// the unspent quarter goes.
//
// g_softDeadline == g_deadline reproduces the old behaviour exactly, which is
// what SearchLimits leaves it at unless a caller asks otherwise.
static std::chrono::steady_clock::time_point g_softDeadline;
// Node budget, in the same place and for the same reason. 0 = unlimited, which
// is the only state tests/bench and tests/perft ever see.
static uint64_t g_nodeLimit = 0;

// Checking the clock costs far more than a node does, so it is checked once
// every few thousand nodes instead of at every one. At the measured ~165k
// nodes/second this bounds overshoot to roughly 12ms, well inside any real
// time control, while the check itself stays invisible in the profile.
static constexpr uint64_t TIME_CHECK_INTERVAL = 2048;

// The search stops for two reasons: the GUI asked it to, or it ran out of
// time. Everywhere the search used to test shouldStop it now tests this.
static inline bool searchAborted(const std::atomic<bool>& shouldStop) {
    if (shouldStop.load()) return true;
    // The node budget is exact rather than sampled: the counter is already in
    // a register's reach at every node, so unlike the clock there is nothing to
    // amortize, and an exactly-enforced budget is what makes a node-limited
    // match reproduce move for move.
    if (g_nodeLimit && g_searchNodes >= g_nodeLimit) return true;
    if (!g_hasDeadline) return false;
    if (g_outOfTime) return true;
    if (g_searchNodes < g_nextTimeCheck) return false;
    g_nextTimeCheck = g_searchNodes + TIME_CHECK_INTERVAL;
    if (std::chrono::steady_clock::now() >= g_deadline) g_outOfTime = true;
    return g_outOfTime;
}

// Null-move pruning assumes that passing is worse than any real move. That is
// false in zugzwang, which in practice means king-and-pawn endings, so the
// heuristic is only applied while the side to move still has a piece.
static bool hasNonPawnMaterial(const Board& board, PieceColor side) {
    for (int i = 0; i < 64; ++i) {
        const Piece& p = board.squares[i];
        if (p.color() != side) continue;
        PieceType t = p.type();
        if (t == KNIGHT || t == BISHOP || t == ROOK || t == QUEEN) return true;
    }
    return false;
}

// Generates the tactical moves (captures, en passant, promotions) for quiescence
// search. Takes a mutable board so the legality filter runs in place rather
// than on a copy — this runs at every quiescence node.
static MoveList generateCaptures(Board& board, PieceColor side) {
    MoveList all = generateLegalMoves(board, side);
    MoveList tactical;
    tactical.reserve(all.size());
    for (const Move& m : all) {
        if (m.flag == CAPTURE || m.flag == EN_PASSANT || m.flag == PROMOTION) {
            tactical.push_back(m);
        }
    }
    return tactical;
}

// Quiescence search: avoids the horizon effect by searching captures and
// promotions at the leaves of the main search. Negamax, like the main search.
// `ply` is absolute depth from the root, used for mate scoring. `qDepth` counts
// only how deep *this* quiescence descent has gone, which is what the bound
// below applies to — the two differ because quiescence starts at whatever ply
// the main search stopped at.
static int quiescence(Board& board, int ply, int qDepth, int alpha, int beta,
                      const std::atomic<bool>& shouldStop) {
    ++g_searchNodes;
    if (searchAborted(shouldStop)) {
        return 0;
    }

    // Hard horizon. Returning the static evaluation here is an approximation —
    // the position may still be tactically live — but an unbounded search is
    // not an alternative: it is a budget overrun, and on a clock that is a
    // forfeit rather than a bad move.
    if (g_searchOptions.qBound && qDepth >= QS_MAX_DEPTH) {
        return scoreForSideToMove(board);
    }

    PieceColor side = board.activeColor;

    // If not in check, the static evaluation is a valid stand-pat cutoff.
    // If in check, we must search every evasion (stand-pat is illegal).
    bool inCheck = LegalMoveValidator::isInCheck(board, side);

    // Kept in scope past this block because delta pruning below needs it: the
    // question "can this capture reach alpha" is asked relative to where the
    // position already stands.
    int standPat = 0;
    if (!inCheck) {
        standPat = scoreForSideToMove(board);
        if (standPat >= beta) return beta;
        if (standPat > alpha) alpha = standPat;
    }

    MoveList moves = inCheck ? generateLegalMoves(board, side) : generateCaptures(board, side);

    // No captures (or evasions) available. In check with no evasions this is
    // checkmate; otherwise the stand-pat value above already folded into alpha.
    if (moves.empty()) {
        return inCheck ? -(MATE_SCORE - ply) : alpha;
    }

    // Order the tactical moves, and decide which to skip, in a single pass.
    //
    // Both SEE features want the same number for the same move, and resolving
    // an exchange is not free, so it is computed at most once per move here
    // rather than once per use — and, critically, not inside a sort comparator,
    // which would evaluate it O(n log n) times instead of O(n).
    //
    // While in check this is evasion search, not capture search: every move
    // must be searched, so neither SEE feature applies.
    const bool useSee = !inCheck && (g_searchOptions.seeOrdering || g_searchOptions.seePruning);

    struct ScoredMove {
        int key;        // sort key, descending
        int seeScore;   // exchange result; only meaningful when useSee
        Move move;
    };
    static constexpr size_t MAX_TACTICAL = 256;  // legal move count never exceeds 218
    ScoredMove scored[MAX_TACTICAL];
    const size_t count = std::min(moves.size(), MAX_TACTICAL);

    for (size_t i = 0; i < count; ++i) {
        const Move& m = moves[i];
        int seeScore = useSee ? see(board, m) : 0;

        int key;
        if (m.flag == EN_PASSANT) {
            key = 10 * QS_PIECE_VALUES[PAWN] - QS_PIECE_VALUES[m.movedPiece.type()];
        } else if (m.flag == PROMOTION && m.capturedPiece.type() == NONE) {
            key = 10 * QS_PIECE_VALUES[m.promotionPiece.type()];
        } else {
            int victim = (m.capturedPiece.type() == NONE) ? 0 : QS_PIECE_VALUES[m.capturedPiece.type()];
            int attacker = QS_PIECE_VALUES[m.movedPiece.type()];
            key = 10 * victim - attacker;
        }

        // SEE separates the winning captures from the losing ones; MVV-LVA
        // still orders within each group.
        //
        // Using the exchange result as the sort key directly is the obvious
        // thing and it is worse: most sound captures resolve to 0, so it
        // collapses QxQ, RxR and PxP into one indistinguishable block and
        // throws away exactly the victim-value information that produces early
        // cutoffs. SEE knows which captures are sound; it does not know which
        // to try first.
        if (g_searchOptions.seeOrdering && !inCheck && seeScore < 0) {
            key -= SEE_LOSING_CAPTURE_BAND;
        }

        scored[i] = ScoredMove{key, seeScore, m};
    }

    std::sort(scored, scored + count,
              [](const ScoredMove& a, const ScoredMove& b) { return a.key > b.key; });

    for (size_t i = 0; i < count; ++i) {
        const Move& move = scored[i].move;

        // Skip captures that lose material outright. Quiescence exists to
        // resolve tactics, and a capture the opponent simply recaptures for
        // profit resolves nothing — it only grows the tree. Promotions are
        // covered too: SEE already credits the promotion gain, so an underpaid
        // promotion is pruned and a sound one is not.
        if (g_searchOptions.seePruning && !inCheck && scored[i].seeScore < 0) {
            continue;
        }

        // Delta pruning: a capture that cannot reach alpha even after winning
        // its victim outright, plus a margin for whatever positional
        // compensation the exchange might bring, cannot change this node's
        // score. Skipping it removes a subtree that was only ever going to
        // confirm what stand-pat already said.
        //
        // Three exclusions, each load-bearing. In check there is no valid
        // stand-pat to measure against, so the arithmetic is meaningless.
        // Promotions are excluded because the victim is not what they gain — a
        // queen appears on the board, and pricing them by the captured piece
        // would prune exactly the moves most likely to swing the score. And a
        // mate score for alpha would make every capture look hopeless, which
        // is the one case where the tree must still be searched.
        if (g_searchOptions.deltaPruning && !inCheck &&
            move.flag != PROMOTION && std::abs(alpha) < MATE_SCORE - 1000) {
            const int victim = (move.flag == EN_PASSANT)
                             ? QS_PIECE_VALUES[PAWN]
                             : QS_PIECE_VALUES[move.capturedPiece.type()];
            if (standPat + victim + QS_DELTA_MARGIN <= alpha) continue;
        }

        if (searchAborted(shouldStop)) {
            break;
        }

        UndoInfo undo = board.makeMove(move);
        int score = -quiescence(board, ply + 1, qDepth + 1, -beta, -alpha, shouldStop);
        board.unmakeMove(undo);

        if (searchAborted(shouldStop)) {
            break;
        }

        if (score > alpha) {
            alpha = score;
            if (alpha >= beta) return beta;
        }
    }

    return alpha;
}

// Positions the game reached before the root, back to the last irreversible
// move. Set once per search from the caller's history.
//
// A file-static rather than a parameter threaded through every recursive call,
// for the same reason g_searchNodes is: one search runs at a time, this is read
// once per node in the hottest loop in the engine, and widening the recursive
// signature to carry a value that never changes during a search buys nothing.
// If the search is ever made concurrent, this becomes shared read-only state,
// which is what it already is in practice.
static std::vector<uint64_t> g_gameHistory;

void recordGamePosition(std::vector<uint64_t>& history, uint64_t hashBefore,
                        const Board& after) {
    history.push_back(hashBefore);
    // An irreversible move partitions the game: nothing before it can recur,
    // including the position just recorded.
    if (after.halfmoveClock == 0) history.clear();
}

// Minimax with transposition table support. `pathHashes` holds the zobrist
// keys of the positions on the current search path (root to parent) and is
// used to score in-search repetitions as draws.
// `prevMove` is the move that led to this node -- nullptr at the root and below
// a null move -- and exists for continuation history, which orders replies by
// what they are answering.
// `excluded` is the singular-extension probe's one intrusion into the search:
// the move it must pretend does not exist. A node searched with an excluded
// move is answering "how good is this position *without* that move", which is a
// different question from the one the table stores -- so such a node neither
// reads nor writes the table, and never extends again.
static int minimaxWithTT(Board& board, int depth, int ply, int alpha, int beta,
                        const std::atomic<bool>& shouldStop, TranspositionTable& tt,
                        std::vector<uint64_t>& pathHashes,
                        const Move* prevMove = nullptr,
                        const Move* excluded = nullptr) {
    ++g_searchNodes;
    // Check if we should stop searching
    if (searchAborted(shouldStop)) {
        return 0; // Return neutral score when stopped
    }

    uint64_t hash = board.getHash();

    // Draw detection. All of it runs before the TT probe: a repetition score is
    // path-dependent, and a cached score must not override it.
    if (board.halfmoveClock >= 100) {
        return 0; // Fifty-move rule
    }
    if (ply > 0) {
        // Two rules, because a repetition inside the tree and a repetition of
        // something the game already played are not equally conclusive.
        //
        // Inside the tree, one match is enough. Both sides are choosing moves
        // here, so a line that can reach the same position twice can normally
        // reach it a third time, and treating the second occurrence as a draw
        // is the standard, and much cheaper, approximation.
        if (std::find(pathHashes.begin(), pathHashes.end(), hash) != pathHashes.end()) {
            return 0;
        }
        // Against the actual game, one match is *not* enough: it makes this
        // only the second occurrence, and a second occurrence is not a draw.
        // Scoring it 0 would have the engine decline winning lines and claim
        // draws that do not exist. Two prior occurrences make this the third,
        // which is the one that ends the game — and is exactly the position
        // the engine used to walk into while a rook up.
        if (std::count(g_gameHistory.begin(), g_gameHistory.end(), hash) >= 2) {
            return 0;
        }
    }

    const PieceColor side = board.activeColor;
    const bool inCheck = LegalMoveValidator::isInCheck(board, side);

    // --- Check extension (PLAN.md 3.3) ---
    //
    // Being in check is not a quiet position, and the reply is usually forced.
    // Spending one more ply there is cheap — few evasions exist — and it is
    // where the horizon effect does the most damage: a search that stops while
    // in check evaluates a position whose material is about to change.
    //
    // This has to happen *before* the depth == 0 drop below, not inside the
    // move loop. A check that arrives exactly at the horizon is the case worth
    // extending, and by the time depth has hit zero the node has already been
    // handed to quiescence — which searches evasions but cannot search the
    // quiet consolidating move that follows them.
    //
    // The ply ceiling is insurance rather than the real bound. A run of checks
    // repeats positions, and the repetition rule above scores that 0, so a
    // perpetual cannot extend forever on its own. The ceiling only catches a
    // long forcing sequence that never repeats.
    if (g_searchOptions.checkExtension && inCheck && ply < 64) {
        ++depth;
    }

    int originalAlpha = alpha;
    int originalBeta = beta;
    Move ttMove;
    int ttScore;

    // Probed after the extension so the entry asked for matches the depth about
    // to be searched; tt.store() below uses the same extended value.
    // An excluded search must not be answered from the table: the entry
    // describes a search that was allowed to play the very move now banned.
    if (!excluded && tt.probe(hash, depth, ply, alpha, beta, ttScore, ttMove)) {
        return ttScore;
    }
    if (excluded) {
        // Still worth the move for ordering, just not the score.
        int ignored = 0;
        tt.probe(hash, -1, ply, -INF, INF, ignored, ttMove);
    }

    if (depth == 0) {
        int score = quiescence(board, ply, 0, alpha, beta, shouldStop);
        // Quiescence is fail-hard: a result clipped to the window is only a
        // bound, not an exact score. Never store anything from a stopped
        // search — it returns fake neutral values.
        if (!searchAborted(shouldStop)) {
            TTEntry::NodeType nodeType;
            if (score <= alpha) {
                nodeType = TTEntry::UPPER_BOUND;
            } else if (score >= beta) {
                nodeType = TTEntry::LOWER_BOUND;
            } else {
                nodeType = TTEntry::EXACT;
            }
            tt.store(hash, 0, ply, score, Move(), nodeType);
        }
        return score;
    }

    // --- Shallow-depth futility, both halves (PLAN.md 3.4) ---
    //
    // Margins are sized off the *measured* error of this engine's evaluation,
    // not off textbook values — see the toggles in search.hpp. The short
    // version: over 688 ordinary positions the static evaluation differs from
    // Stockfish at depth 16 by a median of 125cp and a 90th percentile of
    // 407cp, so a 100–150cp-per-ply margin would be pruning on noise. 3.1 made
    // that mistake with delta pruning and it cost 50 Elo.
    //
    // A node that is *already* this far above beta, or this far below alpha, is
    // one the evaluation is confident about by its own error bars.
    static const int REV_FUTILITY_MARGIN = 300;   // per ply, ~90th percentile
    static const int REV_FUTILITY_MAX_DEPTH = 3;
    // 500 is the shipped value, ~95th percentile of the evaluation's measured
    // error. `razorTight` asks whether 350 does better -- see the toggle.
    const int RAZOR_MARGIN = g_searchOptions.razorTight ? 350 : 500;
    static const int RAZOR_MAX_DEPTH = 2;

    // A null-window search (beta - alpha == 1) is a scout, not a principal
    // variation. Pruning inside the PV would change the move actually chosen
    // rather than only how fast it is found.
    const bool isPV = (beta - alpha > 1);
    const bool nearMate = (std::abs(alpha) >= MATE_SCORE - 1000)
                       || (std::abs(beta) >= MATE_SCORE - 1000);

    // Computed once and shared by the pruning tests below and the correction
    // update at the end of the node. The condition is unchanged while corrHist
    // is off, so the evaluation is called in exactly the same places it was.
    int  staticEval = 0;
    bool haveStatic = false;
    const bool wantStatic =
        !inCheck && (g_searchOptions.corrHist
                     || (!isPV && !nearMate
                         && (g_searchOptions.revFutility || g_searchOptions.razoring)));
    if (wantStatic) {
        staticEval = correctedEval(board);
        haveStatic = true;
    }

    if (!isPV && !inCheck && !nearMate
        && (g_searchOptions.revFutility || g_searchOptions.razoring)) {

        // Reverse futility: so far above beta that giving the opponent the best
        // reply this evaluation can imagine still would not bring it below.
        if (g_searchOptions.revFutility && depth <= REV_FUTILITY_MAX_DEPTH
            && staticEval - REV_FUTILITY_MARGIN * depth >= beta) {
            return staticEval - REV_FUTILITY_MARGIN * depth;
        }

        // Razoring: so far below alpha that only a capture sequence could save
        // it — so ask quiescence, which searches exactly those, and believe it
        // only when it agrees. Falling through on disagreement is what keeps
        // this from being the -50 Elo version of the bet.
        if (g_searchOptions.razoring && depth <= RAZOR_MAX_DEPTH
            && staticEval + RAZOR_MARGIN <= alpha) {
            const int qScore = quiescence(board, ply, 0, alpha, beta, shouldStop);
            if (!searchAborted(shouldStop) && qScore <= alpha) return qScore;
        }
    }

    // --- Null-move pruning ---
    // Hand the opponent a free move. If the position still fails high even
    // after that, the real position almost certainly does too, so the entire
    // subtree can be skipped without searching it. The verification search runs
    // at reduced depth with a null window, so it is cheap.
    //
    // Conditions: enough depth left to pay for the reduced search; not in check
    // (passing while in check is meaningless); and the side to move still has a
    // piece, since the "passing cannot help" assumption fails in zugzwang.
    if (g_searchOptions.nullMove && depth >= 3 && !inCheck && hasNonPawnMaterial(board, side)) {
        const int R = 2;
        NullUndo nu = board.makeNullMove();
        // No previous move below a null move: there is no reply to key on.
        int nullScore = -minimaxWithTT(board, depth - 1 - R, ply + 1, -beta, -beta + 1,
                                       shouldStop, tt, pathHashes, nullptr);
        board.unmakeNullMove(nu);
        if (!searchAborted(shouldStop) && nullScore >= beta) return beta;
    }

    MoveList moves = generateLegalMoves(board, side);

    if (moves.empty()) {
        // No legal moves: mated if in check, stalemate otherwise.
        int score = inCheck ? -(MATE_SCORE - ply) : 0;
        tt.store(hash, depth, ply, score, Move(), TTEntry::EXACT);
        return score;
    }

    // --- Internal iterative deepening (PLAN.md 3.5) ---
    //
    // Alpha-beta's whole efficiency rests on searching the best move first, and
    // the transposition table is what usually supplies it. When the table has
    // nothing for this position -- a node reached for the first time, or one
    // whose entry was displaced -- the ordering falls back to killers and
    // history, which know nothing about *this* position.
    //
    // So search it shallowly first and use whatever that returns. The cost is a
    // subtree a few plies smaller; at a branching factor near 2.3 that is a
    // small fraction of the full-depth search, and it is repaid whenever it
    // moves the best move to the front.
    //
    // Only at depth: below the threshold the reduced search is nearly as
    // expensive as the real one, so there is nothing left to win. Not in check,
    // because evasions are few and already forcing -- there is little ordering
    // work to do and the shallow search would mostly rediscover it.
    if (g_searchOptions.iid && ttMove.from == -1 && depth >= 5 && !inCheck) {
        const int R = 2;
        minimaxWithTT(board, depth - R, ply, alpha, beta, shouldStop, tt, pathHashes,
                      prevMove);
        // The shallow search stores its result under this same position, so the
        // move it liked is read back the way any other TT move would be. That
        // is deliberate: it keeps one path into the ordering rather than two.
        int ignored;
        if (!searchAborted(shouldStop))
            tt.probe(hash, 0, ply, -INF, INF, ignored, ttMove);
    }

    // Move ordering with killer moves and history heuristic
    g_moveOrderer.orderMoves(moves, board, depth, ttMove, prevMove);

    // Aggressively search TT move first if available
    if (ttMove.from != -1) {
        auto it = std::find(moves.begin(), moves.end(), ttMove);
        if (it != moves.end() && it != moves.begin()) {
            std::iter_swap(moves.begin(), it);
        }
    }

    // --- Singular extension probe ---
    // Ask whether the table's move is the *only* move holding this position:
    // search everything else at reduced depth against a window just under the
    // score the table already claims. If nothing else reaches that window, the
    // position hangs on one move, and one more ply there is worth more than a
    // ply anywhere else in the tree.
    //
    // Conditions, each of which is about not paying for the probe unless it can
    // pay back: deep enough that an extra ply matters, a table entry deep
    // enough to be worth testing, and a *lower bound* -- an entry that failed
    // high is a claim that the move is at least this good, which is what makes
    // the comparison meaningful. Mate scores are excluded because the margin
    // arithmetic is meaningless against them.
    //
    // Never inside an excluded search: that would recurse, and the inner node
    // is already answering a different question.
    int singularExtension = 0;
    if (g_searchOptions.singularExt && !excluded && !inCheck &&
        depth >= SINGULAR_MIN_DEPTH && ttMove.from != -1) {
        TTEntry entry;
        if (tt.peek(hash, entry) &&
            entry.depth >= depth - SINGULAR_TT_SLACK &&
            entry.nodeType == TTEntry::LOWER_BOUND &&
            std::abs(scoreFromTT(entry.score, ply)) < TT_MATE_THRESHOLD) {
            const int ttValue = scoreFromTT(entry.score, ply);
            const int singularBeta = ttValue - SINGULAR_MARGIN * depth;
            const int probeDepth = depth / 2 - 1;
            if (probeDepth > 0) {
                const int without = minimaxWithTT(board, probeDepth, ply,
                                                  singularBeta - 1, singularBeta,
                                                  shouldStop, tt, pathHashes,
                                                  prevMove, &ttMove);
                if (!searchAborted(shouldStop) && without < singularBeta)
                    singularExtension = 1;
            }
        }
    }

    int bestEval = -INF;
    Move bestMove;

    pathHashes.push_back(hash);

    int moveIndex = 0;
    for (const Move& move : moves) {
        // Check stop condition before each move
        if (searchAborted(shouldStop)) {
            break;
        }
        // The one move a singular probe is pretending does not exist.
        if (excluded && move == *excluded) continue;
        ++moveIndex;

        // --- Late move pruning ---
        // The list is ordered, so a quiet move this far down is very unlikely
        // to be best. Where LMR searches such a move shallower, this skips it
        // without searching it at all -- which is why the guards matter more
        // here than anywhere else in this file. A reduction that guesses wrong
        // costs a re-search; a prune that guesses wrong loses the move for
        // good, and the score returned is a bound built on a move list the
        // search never finished.
        //
        // Exempt, each for its own reason:
        //   PV nodes      -- the principal variation is the line being claimed
        //                    as best; pruning inside it prunes the answer.
        //   in check      -- the move list is evasions, and there is no such
        //                    thing as a late evasion worth skipping.
        //   captures and promotions -- exactly the moves that overturn a
        //                    position, and the reason LMR exempts them too.
        //   the first moves -- bestEval is still -INF until one move has been
        //                    searched, and a node that prunes every move
        //                    returns a score for nothing. The mate test below
        //                    covers this for free: -INF is -32000, which is
        //                    already below the -29000 bound.
        //   being mated    -- when every line loses, the escape may well be the
        //                    twentieth quiet move, and pruning it turns a long
        //                    mate into a short one.
        //   pawn endings   -- the same zugzwang guard null-move pruning uses at
        //                    line 557. Kept for the reason it is kept there:
        //                    with only pawns left, the move that holds the
        //                    position is routinely a quiet one far down an
        //                    ordered list, which is exactly what this prunes.
        //
        //                    It does **not** rescue the `zugzwang` bench
        //                    position, and the comment says so rather than
        //                    implying otherwise: White has a rook there, so the
        //                    guard never fires, and that position still answers
        //                    e1e5 without LMP and e1f1 with it. Eight of the
        //                    nine bench positions are unchanged. That one is a
        //                    known cost to weigh against whatever the gate
        //                    returns, not a bug to be tuned away in advance.
        //
        // The threshold grows with depth because the deeper the remaining
        // search, the more a late move can still turn out to matter. 3 + d*d
        // is the conventional shape: 4 moves at depth 1, 7 at 2, 12 at 3.
        const bool isPv = (beta - alpha > 1);
        if (g_searchOptions.lateMovePruning && !isPv && !inCheck &&
            // A ladder, shallowest first: lmpDepth1 wins over lmpShallow,
            // which wins over the original 3. Ordered this way so a gate can
            // turn on the rung it is testing and leave the shipped setting
            // alone on both sides.
            depth <= (g_searchOptions.lmpDepth1  ? 1
                    : g_searchOptions.lmpShallow ? LMP_MAX_DEPTH_SHALLOW
                                                 : LMP_MAX_DEPTH) &&
            move.flag == NORMAL &&
            bestEval > -MATE_SCORE + 1000 &&
            hasNonPawnMaterial(board, side) &&
            moveIndex > 3 + depth * depth) {
            continue;
        }

        UndoInfo undo = board.makeMove(move);

        // --- Late move reductions ---
        // The list is ordered by TT move, then captures, killers and history,
        // so a move this far down is unlikely to be best. Search it shallower
        // with a null window first, and only pay for a full-depth re-search if
        // it unexpectedly beats the window. Captures and promotions are never
        // reduced: they are exactly the moves that turn out to matter.
        bool reduce = g_searchOptions.lmr && depth >= 3 && moveIndex > 3 &&
                      !inCheck && move.flag == NORMAL;
        // The extra ply goes to the move the probe found singular, and to no
        // other. It is never combined with a reduction: a move cannot be both
        // the only one holding the position and unpromising enough to search
        // shallow.
        const int ext = (singularExtension && move == ttMove) ? 1 : 0;

        int eval;
        if (reduce) {
            const int R = 1;
            eval = -minimaxWithTT(board, depth - 1 - R, ply + 1, -alpha - 1, -alpha,
                                  shouldStop, tt, pathHashes, &move);
            if (!searchAborted(shouldStop) && eval > alpha) {
                eval = -minimaxWithTT(board, depth - 1, ply + 1, -beta, -alpha,
                                      shouldStop, tt, pathHashes, &move);
            }
        } else {
            eval = -minimaxWithTT(board, depth - 1 + ext, ply + 1, -beta, -alpha,
                                  shouldStop, tt, pathHashes, &move);
        }
        board.unmakeMove(undo);

        if (eval > bestEval) {
            bestEval = eval;
            bestMove = move;
        }
        if (bestEval > alpha) alpha = bestEval;
        if (alpha >= beta) {
            // Beta cutoff - update move ordering
            g_moveOrderer.updateKillerMove(move, depth);
            g_moveOrderer.updateHistory(move, depth, prevMove);
            g_moveOrderer.updateCaptureHistory(move, depth);
            break;
        }
    }
    
    pathHashes.pop_back();

    // A stopped search leaves bestEval partial (possibly still -INF from an
    // unfinished loop) and its children returned fake neutral scores. Storing
    // that would poison the table for every later search, since the TT
    // persists across moves. Return without storing; callers that see
    // shouldStop discard this value anyway.
    if (searchAborted(shouldStop)) {
        return bestEval;
    }

    // Record how wrong the static evaluation turned out to be here.
    //
    // Excluded nodes are skipped for the same reason they do not touch the
    // table: they answer a different question. Nodes whose best move is a
    // capture are skipped because the gap there is tactics resolving, not a
    // standing evaluation error -- exactly what this table must not learn. And
    // mate scores are skipped because the difference is then unbounded and
    // says nothing about the evaluation.
    if (g_searchOptions.corrHist && !excluded && haveStatic && depth > 0
        && std::abs(bestEval) < MATE_SCORE - 1000
        && (bestMove.from == -1 || bestMove.capturedPiece.type() == NONE)) {
        updateCorrHist(board, depth, bestEval - staticEval);
    }

    // Store in transposition table. Bound classification compares against the
    // ORIGINAL window, not the current one: the loop above raises `alpha` to
    // bestEval, so comparing against the raised value would file every PV node
    // as an upper bound.
    TTEntry::NodeType nodeType;
    if (bestEval <= originalAlpha) {
        nodeType = TTEntry::UPPER_BOUND;
    } else if (bestEval >= originalBeta) {
        nodeType = TTEntry::LOWER_BOUND;
    } else {
        nodeType = TTEntry::EXACT;
    }

    // An excluded search answers "how good without that move", which is not
    // what this hash means to anyone else. Storing it would poison the entry
    // for every later probe.
    if (!excluded) tt.store(hash, depth, ply, bestEval, bestMove, nodeType);

    return bestEval;
}

// Iterative deepening. Each iteration reuses the previous one's transposition
// entries and move ordering, so the extra cost of starting shallow is far less
// than the ordering it buys — and it is what makes a time limit usable at all,
// since there is always a completed result to fall back on.
Move findBestMoveIterativeDeepening(Board& board, const SearchLimits& limits,
                                   const std::atomic<bool>& shouldStop,
                                   TranspositionTable& tt,
                                   const std::vector<uint64_t>& gameHistory) {
    const int maxDepth = limits.maxDepth;

    // Whether this search randomises among near-equal root moves: the toggle,
    // and only while still in the opening. Computed once so every use agrees.
    const bool randomisingHere = g_searchOptions.rootRandom
                              && board.fullmoveNumber <= ROOT_RANDOM_MAX_MOVE;

    // How far the root score has been moving between iterations, as a decaying
    // sum and a sample count. Decaying rather than a flat mean because the
    // early iterations of a search swing wildly and say little about what the
    // deep ones will do; halving on each new sample keeps roughly the last few
    // iterations in view.
    long scoreSwing = 0;
    int scoreSwingSamples = 0;

    // Copied, not referenced: the caller owns its history and may edit it the
    // moment this returns, and a search reading a half-updated list would score
    // draws that are not there. It is at most fifty-odd entries by
    // construction, so the copy is not worth avoiding.
    g_gameHistory = gameHistory;

    // Clear move ordering data for new search
    g_moveOrderer.clear();
    // Cleared with the ordering tables rather than persisted across moves. The
    // corrections are learned from one search's own error and the position has
    // moved on by the next one; carrying them would apply a stale offset to a
    // structure that may no longer be there.
    std::memset(g_corrHist, 0, sizeof(g_corrHist));
    g_searchNodes = 0;
    // Age the table: entries this search does not reuse are now displaceable.
    if (g_searchOptions.ttAging) tt.newSearch();

    // Arm the deadline. With no budget the search is depth-limited and every
    // clock check short-circuits, which is what keeps tests/bench reproducible.
    g_hasDeadline = (limits.moveTimeMs > 0);
    g_outOfTime = false;
    g_nextTimeCheck = TIME_CHECK_INTERVAL;
    g_nodeLimit = limits.maxNodes;
    if (g_hasDeadline) {
        const auto now = std::chrono::steady_clock::now();
        g_softDeadline = now + std::chrono::milliseconds(limits.moveTimeMs);
        // hardTimeMs of 0 means "no separate hard limit", i.e. the two coincide
        // and the search behaves exactly as it did before the split existed.
        // A hard limit below the soft one would be nonsense, so it is clamped
        // rather than trusted.
        const long hard = (limits.hardTimeMs > limits.moveTimeMs)
                              ? limits.hardTimeMs : limits.moveTimeMs;
        g_deadline = now + std::chrono::milliseconds(hard);
    }
    
    MoveList moves = generateLegalMoves(board, board.activeColor);
    if (moves.empty()) return Move();

    // Early stop check
    if (searchAborted(shouldStop)) {
        return moves[0];
    }

    Move bestMove = moves[0];
    // Negamax: bestScore is from the root side's point of view throughout, so
    // it is directly comparable across iterations. Converted back to
    // white-perspective only for the human-readable log below.
    const bool whiteToMove = (board.activeColor == COLOR_WHITE);
    int bestScore = -INF;
    // Whether bestScore holds a real completed-depth result yet. The aspiration
    // window needs a previous score to centre on.
    bool haveScore = false;

    auto searchStart = std::chrono::steady_clock::now();
    if (!g_searchOptions.quiet) {
        std::cout << "Starting iterative deepening search up to depth " << maxDepth;
        if (g_hasDeadline) std::cout << " within " << limits.moveTimeMs << "ms";
        std::cout << std::endl;
    }

    // Iterative deepening loop
    for (int currentDepth = 1; currentDepth <= maxDepth; ++currentDepth) {
        if (searchAborted(shouldStop)) {
            if (!g_searchOptions.quiet) std::cout << "Search stopped at depth " << (currentDepth - 1) << std::endl;
            break;
        }

        auto depthStart = std::chrono::steady_clock::now();
        const uint64_t depthStartNodes = g_searchNodes;
        if (!g_searchOptions.quiet) std::cout << "Searching depth " << currentDepth << "..." << std::endl;
        
        // Try to get best move from transposition table for move ordering
        uint64_t hash = board.getHash();
        Move ttMove;
        int ttScore;
        if (tt.probe(hash, currentDepth, 0, -INF, INF, ttScore, ttMove)) {
            // Verify the TT move is legal and use it for ordering
            auto it = std::find(moves.begin(), moves.end(), ttMove);
            if (it != moves.end()) {
                // Move TT move to front for better ordering
                std::swap(*moves.begin(), *it);
            }
        }
        
        // Order moves using previous iteration knowledge
        g_moveOrderer.orderMoves(moves, board, currentDepth, ttMove);
        
        int currentBestScore = -INF;
        Move currentBestMove = moves[0];
        bool completedDepth = true;

        // --- Aspiration window ---
        // Iterative deepening already knows roughly what the score should be,
        // and it rarely moves far between iterations. Searching a narrow window
        // around the previous score makes alpha-beta cut off much sooner
        // everywhere in the tree. The risk is that the true score falls outside
        // the window: the search then "fails" low or high and must be redone
        // with a wider one, which is why the window grows on each retry instead
        // of jumping straight back to infinity.
        const int INF_LO = -INF;
        const int INF_HI = INF;
        bool useAspiration = g_searchOptions.aspiration && currentDepth >= 3 &&
                             haveScore && std::abs(bestScore) < 29000;
        // The window's starting width.
        //
        // Fixed at 50 by default. `aspAdaptive` instead sizes it from how far
        // the score has actually been moving between iterations, tracked in
        // `scoreSwing` below as a decaying mean of |score(d) - score(d-1)|.
        // A window twice the recent swing, floored at the old 50 and capped so
        // it cannot degenerate into no window at all, should miss about as
        // often whatever the evaluation's scale -- which is the property the
        // fixed one lacks.
        int delta = ASP_BASE_DELTA;
        if (g_searchOptions.aspAdaptive && scoreSwingSamples > 0) {
            const int swing = (int)(scoreSwing / scoreSwingSamples);
            delta = std::max(ASP_BASE_DELTA, std::min(ASP_MAX_DELTA, 2 * swing));
        }
        int windowLo = useAspiration ? bestScore - delta : INF_LO;
        int windowHi = useAspiration ? bestScore + delta : INF_HI;

        std::vector<uint64_t> pathHashes;

        // Root moves with an *exact* score this iteration, for the tiebreak.
        //
        // Only moves that raise alpha get re-searched with a full window and
        // therefore an exact score; the rest return an upper bound and we know
        // only that they are no better. So this list under-samples the truly
        // near-equal moves rather than guessing at them -- which is the safe
        // direction, since a move is only ever chosen when it is *known* to be
        // within the margin.
        std::vector<std::pair<Move, int>> exactRootScores;

        while (true) {
            currentBestScore = INF_LO;
            currentBestMove = moves[0];
            exactRootScores.clear();
            completedDepth = true;
            pathHashes.clear();
            pathHashes.push_back(hash);

            // Narrowing alpha/beta window across root moves: later moves are pruned
            // against the current best, and non-first moves get a cheap null-window
            // search first (principal variation search). Alpha-beta is exact, so the
            // chosen move is unchanged. These are per-attempt copies so that a
            // widened retry starts from the fresh window.
            int alpha = windowLo;
            int beta = windowHi;
            // Held for the rootRandom path, which must not see alpha rise.
            const int windowLoFixed = windowLo;

            for (size_t i = 0; i < moves.size(); ++i) {
                const Move& move = moves[i];
                // Check stop condition before evaluating each move
                if (searchAborted(shouldStop)) {
                    if (!g_searchOptions.quiet) std::cout << "Search interrupted during depth " << currentDepth << std::endl;
                    completedDepth = false;
                    break;
                }

                UndoInfo undo = board.makeMove(move);
                int eval;
                if (i == 0) {
                    eval = -minimaxWithTT(board, currentDepth - 1, 1, -beta, -alpha,
                                          shouldStop, tt, pathHashes, &move);
                } else if (randomisingHere) {
                    // Every root move searched against the *original* window,
                    // never a raised alpha. This is the only way to get a true
                    // score for each of them, and the tiebreak is meaningless
                    // without one.
                    //
                    // Two earlier versions of this were wrong, both silently.
                    // Using PVS scores never fired at all, because with good
                    // ordering only the first move raises alpha and there is
                    // nothing to choose between. Using a full window but a
                    // rising alpha fired constantly and chose junk -- a move
                    // below alpha still returns a *bound*, so f2f3 at a true
                    // -71 was accepted against a best of +51, well outside the
                    // ten-centipawn margin it was supposed to respect.
                    //
                    // The price is no alpha cutoffs at the root. It is confined
                    // to the root ply and this path is off for gates and bench,
                    // so nothing measured pays for it.
                    eval = -minimaxWithTT(board, currentDepth - 1, 1, -beta, -windowLoFixed,
                                          shouldStop, tt, pathHashes, &move);
                } else {
                    // Principal variation search: later root moves get a cheap
                    // null-window probe first, and only a move that beats alpha
                    // is re-searched with the full window.
                    eval = -minimaxWithTT(board, currentDepth - 1, 1, -alpha - 1, -alpha,
                                          shouldStop, tt, pathHashes, &move);
                    if (!searchAborted(shouldStop) && eval > alpha && eval < beta) {
                        eval = -minimaxWithTT(board, currentDepth - 1, 1, -beta, -alpha,
                                              shouldStop, tt, pathHashes, &move);
                    }
                }
                board.unmakeMove(undo);

                if (!searchAborted(shouldStop)) {
                    // Exact iff this move raised alpha (or is the first, which
                    // is searched on the full window). Anything else is a bound.
                    if (i == 0 || randomisingHere || eval > alpha)
                        exactRootScores.emplace_back(move, eval);
                    if (eval > currentBestScore) {
                        currentBestScore = eval;
                        currentBestMove = move;
                    }
                    if (eval > alpha) alpha = eval;
                }
            }

            if (!completedDepth || searchAborted(shouldStop) || !useAspiration) break;

            // The score landed outside the window, so this result is only a
            // bound. Widen on the failing side and search the depth again.
            if (currentBestScore <= windowLo) {
                delta *= 4;
                windowLo = (bestScore - delta < -29000) ? INF_LO : bestScore - delta;
                if (!g_searchOptions.quiet) std::cout << "  aspiration fail low, widening" << std::endl;
                continue;
            }
            if (currentBestScore >= windowHi) {
                delta *= 4;
                windowHi = (bestScore + delta > 29000) ? INF_HI : bestScore + delta;
                if (!g_searchOptions.quiet) std::cout << "  aspiration fail high, widening" << std::endl;
                continue;
            }
            break;
        }


        // --- Random tiebreak among near-equal root moves (BUGS.md 6) ---
        // Applied per iteration so the reported best move and the played move
        // never disagree. Skipped entirely when off, which is why bench is
        // unchanged and gates stay reproducible.
        if (randomisingHere && completedDepth && !searchAborted(shouldStop)
            && exactRootScores.size() > 1 && std::abs(currentBestScore) < 29000) {
            std::vector<Move> tied;
            for (const auto& ms : exactRootScores)
                if (ms.second >= currentBestScore - ROOT_RANDOM_MARGIN) tied.push_back(ms.first);
            if (tied.size() > 1) {
                // Seeded from the run seed and the position, so a whole game
                // replays identically given the same seed, while different
                // games diverge.
                uint64_t st = g_rootSeed ^ (hash + 0x9E3779B97F4A7C15ULL * (uint64_t)currentDepth);
                if (st == 0) st = 0x9E3779B97F4A7C15ULL;
                currentBestMove = tied[rootRand(st) % tied.size()];
            }
        }

        // Feed the volatility tracker before bestScore is overwritten, so the
        // measurement is genuinely |this depth - previous depth|.
        if (completedDepth && !searchAborted(shouldStop) && haveScore
            && std::abs(currentBestScore) < 29000 && std::abs(bestScore) < 29000) {
            scoreSwing = scoreSwing / 2 + std::abs(currentBestScore - bestScore);
            scoreSwingSamples = scoreSwingSamples / 2 + 1;
        }

        // Only update best move if we completed the full depth
        if (completedDepth && !searchAborted(shouldStop)) {
            bestMove = currentBestMove;
            bestScore = currentBestScore;
            haveScore = true;
            auto depthEnd = std::chrono::steady_clock::now();
            auto depthDuration = std::chrono::duration_cast<std::chrono::milliseconds>(depthEnd - depthStart);
            if (g_searchInfo) {
                auto sinceStart = std::chrono::duration_cast<std::chrono::milliseconds>(
                    depthEnd - searchStart).count();
                g_searchInfo(currentDepth, bestScore, g_searchNodes,
                             (long)sinceStart, bestMove);
            }
            if (!g_searchOptions.quiet) {
                std::cout << "Depth " << currentDepth << " complete in " << depthDuration.count()
                         << "ms. Best move: " << bestMove.toString()
                         << " (score: " << (whiteToMove ? bestScore : -bestScore) << ")" << std::endl;
            }
        } else {
            if (!g_searchOptions.quiet) std::cout << "Depth " << currentDepth << " incomplete, using previous result" << std::endl;
            break;
        }
        
        // --- Is there time to start the next iteration? ---
        // Starting an iteration that cannot finish wastes the whole of it: a
        // partial iteration's result is discarded, so the time buys nothing.
        // The effective branching factor with these heuristics enabled was
        // measured at ~2.3x per ply and is stable from depth 6 (BACKLOG.md 7),
        // so predict the next iteration at 2.3x the last one and only start it
        // if it fits in what is left.
        // The same prediction against the node budget. Without it the last
        // iteration of every move is one that could never finish, so a fixed
        // share of the budget is spent on results that are thrown away — and
        // that share is spent differently by the two sides of an A/B, which is
        // exactly the sort of difference a gate must not invent.
        if (g_nodeLimit && currentDepth < maxDepth) {
            const uint64_t used = g_searchNodes;
            const uint64_t lastIteration = used - depthStartNodes;
            const double BRANCHING = 2.3;
            if (used >= g_nodeLimit ||
                (double)lastIteration * BRANCHING > (double)(g_nodeLimit - used)) {
                if (!g_searchOptions.quiet) {
                    std::cout << "Stopping at depth " << currentDepth << ": next iteration needs ~"
                              << (uint64_t)((double)lastIteration * BRANCHING) << " nodes, "
                              << (g_nodeLimit - std::min(used, g_nodeLimit)) << " left" << std::endl;
                }
                break;
            }
        }

        if (g_hasDeadline && currentDepth < maxDepth) {
            auto now = std::chrono::steady_clock::now();
            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                g_softDeadline - now).count();
            auto lastIteration = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - depthStart).count();
            const double BRANCHING = 2.3;

            // Two rules, and which one applies is the whole of BUGS.md 11's
            // second half.
            //
            // The prediction rule refuses to begin an iteration unless the
            // *entire* predicted iteration fits in what is left. It never
            // wastes time on an iteration that cannot finish — and it pays for
            // that by abandoning, on average, most of a predicted iteration's
            // worth of budget on every move. Measured: 75% of the allocation
            // used, over five positions at a 90s+1s clock.
            //
            // The elapsed rule begins an iteration whenever the target has not
            // yet passed, and relies on the hard deadline to end one that runs
            // long. It spends the budget; the price is that an iteration which
            // does not finish is discarded, because this search never uses a
            // partial result.
            //
            // Which trade is better is not decidable from here — unused time
            // and wasted time are both losses and only a game says which costs
            // more. That is what the `--tc` gate is for, and until it rules,
            // the prediction rule is what ships.
            const bool stop = g_searchOptions.softTime
                                  ? (remaining <= 0)
                                  : (remaining <= 0 ||
                                     (double)lastIteration * BRANCHING > (double)remaining);
            if (stop) {
                if (!g_searchOptions.quiet) {
                    std::cout << "Stopping at depth " << currentDepth << ": next iteration needs ~"
                              << (long)((double)lastIteration * BRANCHING) << "ms, "
                              << remaining << "ms left" << std::endl;
                }
                break;
            }
        }

        // Optional: Check for mate scores and stop early if mate is found
        // Only stop if we detect an actual mate score (near ±MATE_SCORE which is around ±30000)
        // Do NOT stop for large evaluation scores from material imbalances
        if (abs(bestScore) > 29000 && abs(bestScore) < 31000) {
            if (!g_searchOptions.quiet) std::cout << "Mate detected at depth " << currentDepth << ", stopping search" << std::endl;
            break;
        }
    }
    
    auto searchEnd = std::chrono::steady_clock::now();
    auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(searchEnd - searchStart);
    if (!g_searchOptions.quiet) {
        std::cout << "Iterative deepening search completed in " << totalDuration.count()
                 << "ms. Final best move: " << bestMove.toString()
                 << " (score: " << (whiteToMove ? bestScore : -bestScore) << ")" << std::endl;
    }
    
    return bestMove;
}

// Depth-only convenience overload: no clock, exactly the behaviour that
// existed before time control.
Move findBestMoveIterativeDeepening(Board& board, int maxDepth,
                                   const std::atomic<bool>& shouldStop,
                                   TranspositionTable& tt,
                                   const std::vector<uint64_t>& gameHistory) {
    return findBestMoveIterativeDeepening(board, SearchLimits(maxDepth), shouldStop, tt,
                                          gameHistory);
}
