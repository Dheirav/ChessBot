#include "bb_search.hpp"
#include "bb_evaluation.hpp"
#include "bb_movegen.hpp"
#include "bb_see.hpp"
#include "bb_check.hpp"
#include "search_tuning.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <chrono>
#include <iostream>
#include <memory>

// The bitboard search.
//
// Below the helpers this is search.cpp's own text with the board accessor and
// the move type substituted, for the same reason the evaluation was ported that
// way: a hand-rewritten search is a second search whose every pruning decision
// has to be re-argued, while a substituted one is the same search reading a
// different board, and the node count either matches or names the bug.
//
// The tuned constants, the LMR table and the reduction formula come from
// search_tuning.hpp, shared with search.cpp rather than copied.

namespace {

// BitboardMove stores piece types in the bitboard enum, while the search's
// value tables are indexed by the mailbox PieceType. These three are where that
// conversion happens, and nowhere else.
inline PieceType movedType(const BitboardMove& m) { return toMailboxType(m.moved); }
inline PieceType capturedType(const BitboardMove& m) {
    return (m.captured == BB_NONE) ? NONE : toMailboxType(m.captured);
}
inline PieceType promotedType(const BitboardMove& m) {
    return (m.flag == BBM_PROMOTION) ? toMailboxType(m.promotionType) : NONE;
}

// Material other than pawns and the king, which is what null-move pruning needs
// to know: in an endgame of kings and pawns, passing the turn is often *better*
// than moving, so the null-move assumption fails and the search prunes a
// zugzwang position as if it were winning.
inline bool bbHasNonPawnMaterial(const Position& pos, BitboardColor side) {
    const auto& ours = (side == BB_WHITE) ? pos.white : pos.black;
    return (ours[BB_KNIGHT] | ours[BB_BISHOP] | ours[BB_ROOK] | ours[BB_QUEEN]) != 0;
}

// The packed form the transposition table stores. Sixteen bits, laid out
// exactly as packMove() lays out a mailbox Move, so a move survives a round
// trip through the table as the same move either side reads it.
inline uint16_t packBB(const BitboardMove& m) {
    if (m.from == 0 && m.to == 0) return 0;
    unsigned code;
    switch (m.flag) {
        case BBM_CAPTURE:    code = 1; break;
        case BBM_EN_PASSANT: code = 2; break;
        case BBM_CASTLE:     code = 3; break;
        case BBM_PROMOTION:
            code = 4u + (unsigned(toMailboxType(m.promotionType)) - unsigned(KNIGHT));
            break;
        default:             code = 0; break;
    }
    return uint16_t(unsigned(m.from) | (unsigned(m.to) << 6) | (code << 12));
}

// A table move carries no moved or captured piece, exactly as unpackMove()
// produces none: it exists to be matched against generated moves, never to be
// played directly.
inline BitboardMove unpackBB(uint16_t packed) {
    BitboardMove m{};
    if (packed == 0) return m;
    m.from = uint8_t(packed & 63u);
    m.to   = uint8_t((packed >> 6) & 63u);
    const unsigned code = (packed >> 12) & 7u;
    if (code >= 4u) {
        m.flag = BBM_PROMOTION;
        static constexpr BitboardPieceType PROMO[4] =
            { BB_KNIGHT, BB_BISHOP, BB_ROOK, BB_QUEEN };
        m.promotionType = PROMO[code - 4u];
    } else {
        m.flag = code == 1u ? BBM_CAPTURE
               : code == 2u ? BBM_EN_PASSANT
               : code == 3u ? BBM_CASTLE
                            : BBM_NORMAL;
    }
    return m;
}

// Move::operator== ignores the captured piece and compares the promotion piece
// only for promotions, because it matches a remembered move against a freshly
// generated one. Same rule, so a table move matches the same generated move.
inline bool sameMove(const BitboardMove& a, const BitboardMove& b) {
    if (a.from != b.from || a.to != b.to) return false;
    if (promotedType(a) != promotedType(b)) return false;
    const bool aPromo = (a.flag == BBM_PROMOTION), bPromo = (b.flag == BBM_PROMOTION);
    if (aPromo != bPromo) return false;
    return true;
}

inline bool isNoMove(const BitboardMove& m) { return m.from == 0 && m.to == 0; }

// "A plain quiet move", which is what the mailbox search means by
// `move.flag == NORMAL` at every pruning and reduction guard.
//
// The two flag sets are not in bijection: the mailbox has no double-push flag,
// so a two-square pawn move is NORMAL there and BBM_DOUBLE_PUSH here. Reading
// that as `flag == BBM_NORMAL` silently exempts every double push from late
// move pruning, from move futility and from the reduction guard, which is a
// search that prunes strictly less and cannot be told from a slow one by
// anything except a node count.
inline bool isQuietMove(const BitboardMove& m) {
    return m.flag == BBM_NORMAL || m.flag == BBM_DOUBLE_PUSH;
}

}  // namespace

// the zobrist hash mixed with the game seed: deterministic given both, so one
// position keeps one offset for the whole search and the search stays stable.
static constexpr int EVAL_NOISE_CP = 5;

static inline int evalNoise(const Position& pos) {
    uint64_t h = pos.hash ^ (g_rootSeed * 0x9E3779B97F4A7C15ULL);
    h ^= h >> 33; h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ULL; h ^= h >> 33;
    return (int)(h % (uint64_t)(2 * EVAL_NOISE_CP + 1)) - EVAL_NOISE_CP;
}

static int scoreForSideToMove(const Position& pos) {
    int white = bbEvaluate(pos);
    const int s = (pos.sideToMove == BB_WHITE) ? white : -white;
    return g_searchOptions.evalNoise ? s + evalNoise(pos) : s;
}

// g_searchOptions is defined in search.cpp: one set of toggles, read by both
// cores, so a gate cannot accidentally compare a feature that is on in one
// and off in the other.

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

static inline int corrSide(const Position& pos) {
    return (pos.sideToMove == BB_WHITE) ? 0 : 1;
}

static inline size_t corrSlot(const Position& pos) {
    return (size_t)(pos.pawnHash & (uint64_t)(CORR_SIZE - 1));
}

// Static evaluation with the learned correction applied. Bit-identical to
// scoreForSideToMove() while the toggle is off, which is what keeps the bench
// signature intact.
static int correctedEval(BBSearchContext& ctx, const Position& pos) {
    const int raw = scoreForSideToMove(pos);
    if (!g_searchOptions.corrHist) return raw;
    const int adjusted = raw + ctx.corrHist[corrSide(pos)][corrSlot(pos)] / CORR_GRAIN;
    // A correction must never manufacture a mate score: those are compared
    // against MATE_SCORE thresholds all over the search and a fake one would
    // propagate as a real mate.
    return std::max(-MATE_SCORE + 1000, std::min(MATE_SCORE - 1000, adjusted));
}

// Weighted toward recent observations, and clamped. The weight rises with
// depth because a deeper search's verdict is better evidence about the error
// than a shallow one's.
// Quiescence's static score. Separate from correctedEval() so the quiescence
// half of the correction can be gated on its own -- see SearchOptions.
static inline int quiescenceEval(BBSearchContext& ctx, const Position& pos) {
    return g_searchOptions.corrHistQ ? correctedEval(ctx, pos) : scoreForSideToMove(pos);
}

static void updateCorrHist(BBSearchContext& ctx, const Position& pos, int depth, int diff) {
    int& entry = ctx.corrHist[corrSide(pos)][corrSlot(pos)];
    const int w = std::min(depth + 1, 16);
    const long blended = ((long)entry * (CORR_WEIGHT - w)
                          + (long)diff * CORR_GRAIN * w) / CORR_WEIGHT;
    entry = (int)std::max((long)-CORR_CAP, std::min((long)CORR_CAP, blended));
}

// time control, while the check itself stays invisible in the profile.

// The search stops for two reasons: the GUI asked it to, or it ran out of
// time. Everywhere the search used to test shouldStop it now tests this.
static inline bool searchAborted(BBSearchContext& ctx, const std::atomic<bool>& shouldStop) {
    if (shouldStop.load()) return true;
    if (ctx.extraStop && ctx.extraStop->load(std::memory_order_relaxed)) return true;
    // The node budget is exact rather than sampled: the counter is already in
    // a register's reach at every node, so unlike the clock there is nothing to
    // amortize, and an exactly-enforced budget is what makes a node-limited
    // match reproduce move for move.
    if (g_nodeLimit && ctx.nodes >= g_nodeLimit) return true;
    if (!g_hasDeadline) return false;
    if (g_outOfTime.load(std::memory_order_relaxed)) return true;
    if (ctx.nodes < ctx.nextTimeCheck) return false;
    ctx.nextTimeCheck = ctx.nodes + TIME_CHECK_INTERVAL;
    if (std::chrono::steady_clock::now() >= g_deadline)
        g_outOfTime.store(true, std::memory_order_relaxed);
    return g_outOfTime.load(std::memory_order_relaxed);
}

// Null-move pruning assumes that passing is worse than any real move. That is
// false in zugzwang, which in practice means king-and-pawn endings, so the
// heuristic is only applied while the side to move still has a piece.
// bbHasNonPawnMaterial is defined at the top of this file, as a bitboard test.

// Generates the tactical moves (captures, en passant, promotions) for quiescence
// search. Takes a mutable pos so the legality filter runs in place rather
// than on a copy — this runs at every quiescence node.
static void generateCaptures(const Position& pos, BBMoveList& out) {
    if (g_searchOptions.stagedGen) {
        bbGenerate(pos, out, BB_GEN_CAPTURES);
        return;
    }
    // The unstaged path generates everything and filters, which is what the
    // toggle compares against. tests/bbequiv checks the two produce the same
    // set, so this exists to keep the toggle meaningful rather than because the
    // answer could differ.
    BBMoveList all;
    bbGenerate(pos, all);
    out.clear();
    for (const BitboardMove& m : all)
        if (m.flag == BBM_CAPTURE || m.flag == BBM_EN_PASSANT || m.flag == BBM_PROMOTION)
            out.moves[out.count++] = m;
}

// Quiescence search: avoids the horizon effect by searching captures and
// promotions at the leaves of the main search. Negamax, like the main search.
// `ply` is absolute depth from the root, used for mate scoring. `qDepth` counts
// only how deep *this* quiescence descent has gone, which is what the bound
// below applies to — the two differ because quiescence starts at whatever ply
// the main search stopped at.
// `prevTo` is the square the move that led here landed on, or -1 at the top of
// a quiescence descent. Move-count pruning needs it: a recapture on that square
// is the whole reason quiescence exists and must never be pruned by move count.
static int quiescence(BBSearchContext& ctx,
                      Position& pos, int ply, int qDepth, int alpha, int beta,
                      const std::atomic<bool>& shouldStop, int prevTo = -1) {
    ++ctx.nodes;
    if (searchAborted(ctx, shouldStop)) {
        return 0;
    }

    // Hard horizon. Returning the static evaluation here is an approximation —
    // the position may still be tactically live — but an unbounded search is
    // not an alternative: it is a budget overrun, and on a clock that is a
    // forfeit rather than a bad move.
    if (g_searchOptions.qBound && qDepth >= QS_MAX_DEPTH) {
        return quiescenceEval(ctx, pos);
    }

    BitboardColor side = pos.sideToMove;

    // If not in check, the static evaluation is a valid stand-pat cutoff.
    // If in check, we must search every evasion (stand-pat is illegal).
    bool inCheck = bbInCheck(pos);

    // Kept in scope past this block because delta pruning below needs it: the
    // question "can this capture reach alpha" is asked relative to where the
    // position already stands.
    // Corrected, unlike v1, which applied the offset in the main search only.
    // Quiescence is where most static evaluations actually happen, so leaving it
    // out meant the correction reached a small minority of the evaluations it
    // was learned from. correctedEval() is identical to scoreForSideToMove()
    // while the toggle is off, so this changes no node count when it is.
    int standPat = 0;
    if (!inCheck) {
        standPat = quiescenceEval(ctx, pos);
        if (standPat >= beta) return beta;
        if (standPat > alpha) alpha = standPat;
    }

    BBMoveList moves;
    if (inCheck) bbGenerate(pos, moves); else generateCaptures(pos, moves);

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
        BitboardMove move{};
    };
    static constexpr size_t MAX_TACTICAL = 256;  // legal move count never exceeds 218
    const bool tie = g_searchOptions.orderTieBreak;
    ScoredMove scored[MAX_TACTICAL];
    const size_t count = std::min((size_t)moves.size(), MAX_TACTICAL);

    for (size_t i = 0; i < count; ++i) {
        const BitboardMove& m = moves[i];
        int seeScore = useSee ? bbSee(pos, m) : 0;

        int key;
        if (m.flag == BBM_EN_PASSANT) {
            key = 10 * QS_PIECE_VALUES[PAWN] - QS_PIECE_VALUES[movedType(m)];
        } else if (m.flag == BBM_PROMOTION && m.captured == BB_NONE) {
            key = 10 * QS_PIECE_VALUES[promotedType(m)];
        } else {
            int victim = (m.captured == BB_NONE) ? 0 : QS_PIECE_VALUES[capturedType(m)];
            int attacker = QS_PIECE_VALUES[movedType(m)];
            key = 10 * victim - attacker;
        }
        // See the note in search.cpp: quiescence has never used capture
        // history, and it is where most of the nodes are.
        if (g_searchOptions.qCaptHist)
            key += ctx.orderer.captureHistoryScore(m, pos.sideToMove);


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
              [tie](const ScoredMove& a, const ScoredMove& b) {
                  if (a.key != b.key) return a.key > b.key;
                  return tie && BBMoveOrderer::tieKey(a.move) < BBMoveOrderer::tieKey(b.move);
              });

    // One setup per node, not per move: the squares from which each piece type
    // would check, plus our pieces that block one of our own sliders.
    const bool needCheckInfo = g_searchOptions.qMoveCount && g_searchOptions.qCheckExempt;
    const CheckInfo ci = needCheckInfo ? bbCheckInfo(pos) : CheckInfo{};

    int searched = 0;
    for (size_t i = 0; i < count; ++i) {
        const BitboardMove& move = scored[i].move;

        // Move-count pruning: see the note in search.cpp. Exempts a recapture
        // on the previous move's destination, promotions, mate scores, and --
        // once qCheckExempt is on -- checking moves, which is the exemption
        // Stockfish has and whose absence is why this limit has to sit above
        // the knee.
        if (g_searchOptions.qMoveCount && !inCheck &&
            searched >= QS_MOVE_COUNT_LIMIT &&
            move.flag != BBM_PROMOTION &&
            (int)move.to != prevTo &&
            std::abs(alpha) < MATE_SCORE - 1000 &&
            !(needCheckInfo && bbGivesCheck(pos, ci, move))) {
            continue;
        }

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
        // queen appears on the pos, and pricing them by the captured piece
        // would prune exactly the moves most likely to swing the score. And a
        // mate score for alpha would make every capture look hopeless, which
        // is the one case where the tree must still be searched.
        if (g_searchOptions.deltaPruning && !inCheck &&
            move.flag != BBM_PROMOTION && std::abs(alpha) < MATE_SCORE - 1000) {
            const int victim = (move.flag == BBM_EN_PASSANT)
                             ? QS_PIECE_VALUES[PAWN]
                             : QS_PIECE_VALUES[capturedType(move)];
            if (standPat + victim + QS_DELTA_MARGIN <= alpha) continue;
        }

        if (searchAborted(ctx, shouldStop)) {
            break;
        }

        ++searched;
        PositionUndo undo = pos.makeMove(move);
        int score = -quiescence(ctx, pos, ply + 1, qDepth + 1, -beta, -alpha,
                                shouldStop, (int)move.to);
        pos.unmakeMove(undo);

        if (searchAborted(ctx, shouldStop)) {
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
                        const Position& after) {
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
static int minimaxWithTT(BBSearchContext& ctx,
                        Position& pos, int depth, int ply, int alpha, int beta,
                        const std::atomic<bool>& shouldStop, TranspositionTable& tt,
                        std::vector<uint64_t>& pathHashes,
                        const BitboardMove* prevMove = nullptr,
                        const BitboardMove* excluded = nullptr,
                        bool onPvLine = true) {
    ++ctx.nodes;
    // Check if we should stop searching
    if (searchAborted(ctx, shouldStop)) {
        return 0; // Return neutral score when stopped
    }

    uint64_t hash = pos.hash;

    // Draw detection. All of it runs before the TT probe: a repetition score is
    // path-dependent, and a cached score must not override it.
    if (pos.halfmoveClock >= 100) {
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

    const BitboardColor side = pos.sideToMove;
    const bool inCheck = bbInCheck(pos);

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
    BitboardMove ttMove{};
    int ttScore;

    // Probed after the extension so the entry asked for matches the depth about
    // to be searched; tt.store() below uses the same extended value.
    // An excluded search must not be answered from the table: the entry
    // describes a search that was allowed to play the very move now banned.
    uint16_t ttPacked = 0;
    if (!excluded && tt.probe(hash, depth, ply, alpha, beta, ttScore, ttPacked)) {
        return ttScore;
    }
    ttMove = unpackBB(ttPacked);
    if (excluded) {
        // Still worth the move for ordering, just not the score.
        int ignored = 0;
        tt.probe(hash, -1, ply, -INF, INF, ignored, ttPacked);
        ttMove = unpackBB(ttPacked);
    }

    // <= 0 rather than == 0. Nothing produced a negative depth while the
    // null-move reduction was clamped to depth - 2, but an unclamped reduction
    // does, and a node function that only recognises exactly zero would carry
    // a negative depth straight into move generation and recurse downward
    // until the stack ran out. Stockfish drops into quiescence the same way.
    if (depth <= 0) {
        int score = quiescence(ctx, pos, ply, 0, alpha, beta, shouldStop);
        // Quiescence is fail-hard: a result clipped to the window is only a
        // bound, not an exact score. Never store anything from a stopped
        // search — it returns fake neutral values.
        if (!searchAborted(ctx, shouldStop)) {
            TTEntry::NodeType nodeType;
            if (score <= alpha) {
                nodeType = TTEntry::UPPER_BOUND;
            } else if (score >= beta) {
                nodeType = TTEntry::LOWER_BOUND;
            } else {
                nodeType = TTEntry::EXACT;
            }
            tt.store(hash, 0, ply, score, uint16_t(0), nodeType);
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

    // Move-level futility. Base plus per-ply, both sized against the same
    // measured error as the two margins above rather than against convention.
    static const int MOVE_FUTILITY_BASE  = 300;
    static const int MOVE_FUTILITY_SLOPE = 150;
    static const int MOVE_FUTILITY_MAX_DEPTH = 4;

    // A null-window search (beta - alpha == 1) is a scout, not a principal
    // variation. Pruning inside the PV would change the move actually chosen
    // rather than only how fast it is found.
    // Whether this node is on the principal variation.
    //
    // Derived from the window as `beta - alpha > 1` when interiorPvs is off,
    // which is correct only while every non-reduced move is searched on the
    // full window. Turn scouting on without this and the window stops meaning
    // "PV": isPV reads false almost everywhere, which does not merely disable
    // the PV exemption, it *enables* razoring, reverse futility and late move
    // pruning inside the principal variation. Measured: the scouting itself is
    // worth +1.8% nodes, and the whole -14.1% of interiorPvs came from firing
    // those three rules where they had never fired. They are the eval-margin
    // rules, and this evaluation's margins are 500 centipawns wide.
    const bool isPV = g_searchOptions.interiorPvs ? onPvLine : (beta - alpha > 1);
    const bool nearMate = (std::abs(alpha) >= MATE_SCORE - 1000)
                       || (std::abs(beta) >= MATE_SCORE - 1000);

    // Computed once and shared by the pruning tests below and the correction
    // update at the end of the node. The condition is unchanged while corrHist
    // is off, so the evaluation is called in exactly the same places it was.
    int  staticEval = 0;
    bool haveStatic = false;
    const bool wantStatic =
        !inCheck && (g_searchOptions.improving
                     || g_searchOptions.moveFutility
                     || g_searchOptions.corrHist
                     || (!isPV && !nearMate
                         && (g_searchOptions.revFutility || g_searchOptions.razoring)));
    if (wantStatic) {
        staticEval = correctedEval(ctx, pos);
        haveStatic = true;
    }

    // Record this ply's static score, and decide whether the side to move is
    // better off than on its own previous turn. Default true when there is
    // nothing to compare against -- at the first two plies, or when either node
    // was in check -- because "assume improving" is the conservative choice: it
    // reduces less rather than more.
    bool improving = true;
    if (g_searchOptions.improving) {
        if (ply >= 0 && ply < BBSearchContext::EVAL_STACK_PLIES)
            ctx.evalStack[ply] = haveStatic ? staticEval : BBSearchContext::NO_EVAL;
        if (haveStatic && ply >= 2 && ply < BBSearchContext::EVAL_STACK_PLIES
            && ctx.evalStack[ply - 2] != BBSearchContext::NO_EVAL) {
            improving = staticEval > ctx.evalStack[ply - 2];
        }
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
            const int qScore = quiescence(ctx, pos, ply, 0, alpha, beta, shouldStop);
            if (!searchAborted(ctx, shouldStop) && qScore <= alpha) return qScore;
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
    if (g_searchOptions.nullMove && depth >= 3 && !inCheck && bbHasNonPawnMaterial(pos, side)) {
        int R = 2;
        if (g_searchOptions.nullDepthR) {
            R = NULL_R_BASE + depth / NULL_R_DIV;
            // Leave at least one real ply below the null move, or the reduced
            // search is a static evaluation wearing a search's name.
            if (NULL_R_CLAMP && R > depth - 2) R = depth - 2;
            if (R < 1) R = 1;
        }
        Position::NullUndo nu = pos.makeNullMove();
        // No previous move below a null move: there is no reply to key on.
        int nullScore = -minimaxWithTT(ctx, pos, depth - 1 - R, ply + 1, -beta, -beta + 1,
                                       shouldStop, tt, pathHashes, nullptr, nullptr, false);
        pos.unmakeNullMove(nu);
        if (!searchAborted(ctx, shouldStop) && nullScore >= beta) {
            // Verification: search the position for real at reduced depth, with
            // the same null window. Passing is only evidence that the position
            // is winning if actually moving is too, and in zugzwang it is not.
            if (!g_searchOptions.nullVerify) return beta;
            const int verified = minimaxWithTT(ctx, pos, depth - R, ply, beta - 1, beta,
                                               shouldStop, tt, pathHashes, prevMove,
                                               nullptr, false);
            if (searchAborted(ctx, shouldStop) || verified >= beta) return beta;
        }
    }

    BBMoveList moves;
    bbGenerate(pos, moves);

    if (moves.empty()) {
        // No legal moves: mated if in check, stalemate otherwise.
        int score = inCheck ? -(MATE_SCORE - ply) : 0;
        tt.store(hash, depth, ply, score, uint16_t(0), TTEntry::EXACT);
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
    if (g_searchOptions.iid && isNoMove(ttMove) && depth >= 5 && !inCheck) {
        const int R = 2;
        minimaxWithTT(ctx, pos, depth - R, ply, alpha, beta, shouldStop, tt, pathHashes,
                      prevMove);
        // The shallow search stores its result under this same position, so the
        // move it liked is read back the way any other TT move would be. That
        // is deliberate: it keeps one path into the ordering rather than two.
        int ignored;
        if (!searchAborted(ctx, shouldStop)) {
            tt.probe(hash, 0, ply, -INF, INF, ignored, ttPacked);
            ttMove = unpackBB(ttPacked);
        }
    }

    // Move ordering with killer moves and history heuristic
    ctx.orderer.orderMoves(moves, pos, depth, ttMove, prevMove);

    // Aggressively search TT move first if available
    if (!isNoMove(ttMove)) {
        auto it = std::find_if(moves.begin(), moves.end(),
                               [&](const BitboardMove& m) { return sameMove(m, ttMove); });
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
        depth >= SINGULAR_MIN_DEPTH && !isNoMove(ttMove)) {
        TTEntry entry;
        if (tt.peek(hash, entry) &&
            entry.depth >= depth - SINGULAR_TT_SLACK &&
            entry.nodeType == TTEntry::LOWER_BOUND &&
            std::abs(scoreFromTT(entry.score, ply)) < TT_MATE_THRESHOLD) {
            const int ttValue = scoreFromTT(entry.score, ply);
            const int singularBeta = ttValue - SINGULAR_MARGIN * depth;
            const int probeDepth = depth / 2 - 1;
            if (probeDepth > 0) {
                const int without = minimaxWithTT(ctx, pos, probeDepth, ply,
                                                  singularBeta - 1, singularBeta,
                                                  shouldStop, tt, pathHashes,
                                                  prevMove, &ttMove);
                if (!searchAborted(ctx, shouldStop) && without < singularBeta)
                    singularExtension = 1;
            }
        }
    }

    int bestEval = -INF;
    BitboardMove bestMove{};

    pathHashes.push_back(hash);

    // Quiet moves actually searched at this node, for the history malus. Only
    // searched ones: a move LMP skipped was never tried. Pointers into `moves`,
    // which is not modified during the loop.
    const BitboardMove* quietsTried[64];
    int nQuietsTried = 0;

    int moveIndex = 0;
    for (const BitboardMove& move : moves) {
        // Check stop condition before each move
        if (searchAborted(ctx, shouldStop)) {
            break;
        }
        // The one move a singular probe is pretending does not exist.
        if (excluded && sameMove(move, *excluded)) continue;
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
        // Recomputed inside the loop on purpose when interiorPvs is off: alpha
        // rises as moves are searched, and once it reaches beta - 1 this node
        // stops being a PV node for the remaining moves. Collapsing it into the
        // node-entry isPV changes the shipped tree, which bench catches at once.
        const bool isPv = g_searchOptions.interiorPvs ? isPV : (beta - alpha > 1);

        // Move-level futility. Conditions mirror the LMP guard below, for the
        // same reasons it lists: never in a PV node, never in check, never on a
        // move that is not a plain quiet one, and never once a mate score is in
        // play, because a mate is not a centipawn quantity and the margin
        // arithmetic is meaningless against it.
        //
        // moveIndex > 0 keeps the first move, which is the TT move or the best
        // the ordering could offer. Pruning that on a static estimate would
        // discard the one move the node has real evidence for.
        if (g_searchOptions.moveFutility && !isPv && !inCheck && haveStatic &&
            depth <= MOVE_FUTILITY_MAX_DEPTH &&
            isQuietMove(move) &&
            moveIndex > 0 &&
            bestEval > -MATE_SCORE + 1000 &&
            std::abs(alpha) < MATE_SCORE - 1000 &&
            bbHasNonPawnMaterial(pos, side) &&
            staticEval + MOVE_FUTILITY_BASE + MOVE_FUTILITY_SLOPE * depth <= alpha) {
            continue;
        }

        if (g_searchOptions.lateMovePruning && !isPv && !inCheck &&
            // A ladder, shallowest first: lmpDepth1 wins over lmpShallow,
            // which wins over the original 3. Ordered this way so a gate can
            // turn on the rung it is testing and leave the shipped setting
            // alone on both sides.
            depth <= (g_searchOptions.lmpDeep    ? LMP_DEEP_MAX_DEPTH
                    : g_searchOptions.lmpDepth1  ? 1
                    : g_searchOptions.lmpShallow ? LMP_MAX_DEPTH_SHALLOW
                                                 : LMP_MAX_DEPTH) &&
            isQuietMove(move) &&
            bestEval > -MATE_SCORE + 1000 &&
            bbHasNonPawnMaterial(pos, side) &&
            moveIndex > (g_searchOptions.lmpDeep
                             ? LMP_DEEP_BASE + LMP_DEEP_SLOPE * depth
                             : 3 + depth * depth)) {
            continue;
        }

        if (g_searchOptions.histMalus && isQuietMove(move) &&
            nQuietsTried < (int)(sizeof(quietsTried) / sizeof(quietsTried[0]))) {
            quietsTried[nQuietsTried++] = &move;
        }

        PositionUndo undo = pos.makeMove(move);

        // --- Late move reductions ---
        // The list is ordered by TT move, then captures, killers and history,
        // so a move this far down is unlikely to be best. Search it shallower
        // with a null window first, and only pay for a full-depth re-search if
        // it unexpectedly beats the window. Captures and promotions are never
        // reduced: they are exactly the moves that turn out to matter.
        bool reduce = g_searchOptions.lmr && depth >= 3 && moveIndex > 3 &&
                      !inCheck && isQuietMove(move);
        // The extra ply goes to the move the probe found singular, and to no
        // other. It is never combined with a reduction: a move cannot be both
        // the only one holding the position and unpromising enough to search
        // shallow.
        const int ext = (singularExtension && sameMove(move, ttMove)) ? 1 : 0;

        int eval;
        if (reduce) {
            const int R = lmrReduction(depth, moveIndex, improving,
                                       g_searchOptions.histReduction
                                           ? ctx.orderer.quietHistory(move) : 0);
            eval = -minimaxWithTT(ctx, pos, depth - 1 - R, ply + 1, -alpha - 1, -alpha,
                                  shouldStop, tt, pathHashes, &move, nullptr, false);
            if (!searchAborted(ctx, shouldStop) && eval > alpha) {
                eval = -minimaxWithTT(ctx, pos, depth - 1, ply + 1, -beta, -alpha,
                                      shouldStop, tt, pathHashes, &move, nullptr, isPV);
            }
        } else if (g_searchOptions.interiorPvs && moveIndex > 1) {
            // Principal variation search. Once one move has raised alpha, the
            // rest only have to be shown *not* to beat it, and a null window
            // proves that far sooner than a full one.
            eval = -minimaxWithTT(ctx, pos, depth - 1 + ext, ply + 1, -alpha - 1, -alpha,
                                  shouldStop, tt, pathHashes, &move, nullptr, false);
            if (!searchAborted(ctx, shouldStop) && eval > alpha && eval < beta) {
                eval = -minimaxWithTT(ctx, pos, depth - 1 + ext, ply + 1, -beta, -alpha,
                                      shouldStop, tt, pathHashes, &move, nullptr, isPV);
            }
        } else {
            eval = -minimaxWithTT(ctx, pos, depth - 1 + ext, ply + 1, -beta, -alpha,
                                  shouldStop, tt, pathHashes, &move, nullptr, isPV);
        }
        pos.unmakeMove(undo);

        if (eval > bestEval) {
            bestEval = eval;
            bestMove = move;
        }
        if (bestEval > alpha) alpha = bestEval;
        if (alpha >= beta) {
            // Beta cutoff - update move ordering
            ctx.orderer.updateKillerMove(move, depth);
            ctx.orderer.updateHistory(move, depth, side, prevMove);
            ctx.orderer.updateCaptureHistory(move, depth, side);
            // Everything quiet that was searched before this and did not cut.
            for (int q = 0; q < nQuietsTried; ++q) {
                if (quietsTried[q] != &move)
                    ctx.orderer.penaliseHistory(*quietsTried[q], depth, side, prevMove);
            }
            break;
        }
    }
    
    pathHashes.pop_back();

    // A stopped search leaves bestEval partial (possibly still -INF from an
    // unfinished loop) and its children returned fake neutral scores. Storing
    // that would poison the table for every later search, since the TT
    // persists across moves. Return without storing; callers that see
    // shouldStop discard this value anyway.
    if (searchAborted(ctx, shouldStop)) {
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
        && (isNoMove(bestMove) || bestMove.captured == BB_NONE)) {
        updateCorrHist(ctx, pos, depth, bestEval - staticEval);
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
    if (!excluded) tt.store(hash, depth, ply, bestEval, packBB(bestMove), nodeType);

    return bestEval;
}
// The root, for the bench configuration: one thread, no clock, aspiration on,
// no root randomisation. That is exactly what tests/bench drives the mailbox
// search with, and B6's acceptance test is that the two visit the same nodes,
// so anything the bench configuration does not reach is not ported here.
//
// The threading stagger, the time management, the soft-time cutoff and the
// root tiebreak all sit above this and are representation-independent; they
// stay in search.cpp and get pointed at this core at B7.
// One search thread on the bitboard core.
//
// Deliberately the same shape as search.cpp's searchWorker, because the thread
// pool, the time setup and the node accounting above it are representation-
// independent and are shared rather than duplicated: findBestMoveIterativeDeepening
// spawns whichever of the two this session is configured for, and everything
// outside the worker is the same code either way.
//
// What is not here, and why: the root random tiebreak (off by default, and it
// exists to vary openings rather than to play better) and the aspiration
// volatility tracker behind aspAdaptive (also off). Both are recorded in
// docs/BITBOARD-REPLACEMENT.md as unported rather than dropped.
BitboardMove bbSearchWorker(int threadIndex, Position pos, const SearchLimits& limits,
                            const std::atomic<bool>& shouldStop,
                            TranspositionTable& tt,
                            const std::atomic<bool>* extraStop,
                            uint64_t* nodesOut) {
    const bool isMain = (threadIndex == 0);
    const bool verbose = isMain && !g_searchOptions.quiet;
    const int maxDepth = limits.maxDepth;

    // Heap-allocated for the same reason the mailbox one is: the orderer
    // carries a 2.4MB continuation-history table and a thread stack is not the
    // place for it.
    auto ctxHolder = std::make_unique<BBSearchContext>();
    BBSearchContext& ctx = *ctxHolder;
    ctx.extraStop = extraStop;
    ctx.orderer.clear();
    ctx.nextTimeCheck = TIME_CHECK_INTERVAL;

    // Written on every exit, including the early returns below, so the caller
    // can sum what each thread actually searched.
    struct PublishNodes {
        const BBSearchContext& c; uint64_t* out;
        ~PublishNodes() { if (out) *out = c.nodes; }
    } publishNodes{ctx, nodesOut};

    BBMoveList moves;
    bbGenerate(pos, moves);
    if (moves.empty()) return BitboardMove{};
    if (searchAborted(ctx, shouldStop)) return moves[0];

    BitboardMove bestMove = moves[0];
    const bool whiteToMove = (pos.sideToMove == BB_WHITE);
    int bestScore = -INF;
    bool haveScore = false;

    const auto searchStart = std::chrono::steady_clock::now();
    std::vector<uint64_t> pathHashes;

    // Helpers start one ply deeper on odd thread indices. Without some such
    // stagger every thread walks the same iterations in the same order and
    // mostly re-derives what the others already stored. Thread 0 is never
    // offset, so the single-threaded search is unaffected.
    for (int currentDepth = 1 + (threadIndex % 2); currentDepth <= maxDepth; ++currentDepth) {
        if (searchAborted(ctx, shouldStop)) break;

        const auto depthStart = std::chrono::steady_clock::now();
        const uint64_t depthStartNodes = ctx.nodes;

        const uint64_t hash = pos.hash;
        BitboardMove ttMove{};
        uint16_t ttPacked = 0;
        int ttScore;
        if (tt.probe(hash, currentDepth, 0, -INF, INF, ttScore, ttPacked)) {
            ttMove = unpackBB(ttPacked);
            auto it = std::find_if(moves.begin(), moves.end(),
                                   [&](const BitboardMove& m) { return sameMove(m, ttMove); });
            if (it != moves.end()) std::swap(*moves.begin(), *it);
        }
        ctx.orderer.orderMoves(moves, pos, currentDepth, ttMove);

        int currentBestScore = -INF;
        BitboardMove currentBestMove = moves[0];
        bool completedDepth = true;

        const int INF_LO = -INF;
        const int INF_HI = INF;
        const bool useAspiration = g_searchOptions.aspiration && currentDepth >= 3 &&
                                   haveScore && std::abs(bestScore) < 29000;
        int delta = ASP_BASE_DELTA;
        int windowLo = useAspiration ? bestScore - delta : INF_LO;
        int windowHi = useAspiration ? bestScore + delta : INF_HI;

        while (true) {
            currentBestScore = INF_LO;
            currentBestMove = moves[0];
            completedDepth = true;
            pathHashes.clear();
            pathHashes.push_back(hash);

            int alpha = windowLo;
            int beta = windowHi;

            for (int i = 0; i < moves.size(); ++i) {
                const BitboardMove& move = moves[i];
                if (searchAborted(ctx, shouldStop)) { completedDepth = false; break; }

                const PositionUndo undo = pos.makeMove(move);
                int eval;
                if (i == 0) {
                    eval = -minimaxWithTT(ctx, pos, currentDepth - 1, 1, -beta, -alpha,
                                          shouldStop, tt, pathHashes, &move);
                } else {
                    // Principal variation search: a cheap null-window probe
                    // first, and only a move that beats alpha is re-searched.
                    eval = -minimaxWithTT(ctx, pos, currentDepth - 1, 1, -alpha - 1, -alpha,
                                          shouldStop, tt, pathHashes, &move);
                    if (!searchAborted(ctx, shouldStop) && eval > alpha && eval < beta) {
                        eval = -minimaxWithTT(ctx, pos, currentDepth - 1, 1, -beta, -alpha,
                                              shouldStop, tt, pathHashes, &move);
                    }
                }
                pos.unmakeMove(undo);

                if (!searchAborted(ctx, shouldStop)) {
                    if (eval > currentBestScore) {
                        currentBestScore = eval;
                        currentBestMove = move;
                    }
                    if (eval > alpha) alpha = eval;
                }
            }

            if (!completedDepth || searchAborted(ctx, shouldStop) || !useAspiration) break;

            // Outside the window, so this is only a bound. Widen the failing
            // side and search the depth again.
            if (currentBestScore <= windowLo) {
                delta *= 4;
                windowLo = (bestScore - delta < -29000) ? INF_LO : bestScore - delta;
                continue;
            }
            if (currentBestScore >= windowHi) {
                delta *= 4;
                windowHi = (bestScore + delta > 29000) ? INF_HI : bestScore + delta;
                continue;
            }
            break;
        }

        // Only a completed depth may replace the answer: a partial iteration
        // has searched some root moves against a window the rest never saw.
        if (completedDepth && !searchAborted(ctx, shouldStop)) {
            bestMove = currentBestMove;
            bestScore = currentBestScore;
            haveScore = true;

            if (isMain && g_searchInfo) {
                const auto sinceStart = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - searchStart).count();
                g_searchInfo(currentDepth, bestScore, ctx.nodes, (long)sinceStart,
                             toMailboxMove(pos, bestMove));
            }
            if (verbose) {
                const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - depthStart).count();
                std::cout << "Depth " << currentDepth << " complete in " << ms
                          << "ms. Best move: " << toMailboxMove(pos, bestMove).toString()
                          << " (score: " << (whiteToMove ? bestScore : -bestScore) << ")"
                          << std::endl;
            }
        } else if (verbose) {
            std::cout << "Depth " << currentDepth << " incomplete, using previous result"
                      << std::endl;
        }

        if (budgetSpent(currentDepth, maxDepth, ctx.nodes, depthStartNodes,
                        depthStart, verbose)) {
            break;
        }

        // A found mate ends the search: deeper iterations cannot improve on it
        // and the score is not a centipawn quantity to keep refining. The bound
        // excludes large material scores, which are not mates.
        // See searchWorker: a mate score at depth 1 is a table claim, not a
        // verified mate, and stopping on it is how game rs8QkvCm was drawn from
        // mate in 9. Only stop once the search depth covers the mate.
        if (std::abs(bestScore) > 29000 && std::abs(bestScore) < 31000) {
            const int matePlies = MATE_SCORE - std::abs(bestScore);
            if (currentDepth >= matePlies) {
                if (verbose) std::cout << "Mate in " << matePlies << " plies verified at depth "
                                       << currentDepth << ", stopping search" << std::endl;
                break;
            }
        }
    }

    if (verbose) {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - searchStart).count();
        std::cout << "Iterative deepening search completed in " << ms
                  << "ms. Final best move: " << toMailboxMove(pos, bestMove).toString()
                  << " (score: " << (whiteToMove ? bestScore : -bestScore) << ")" << std::endl;
    }
    return bestMove;
}

BBSearchResult bbSearchRoot(Position& pos, int maxDepth, TranspositionTable& tt,
                            const std::atomic<bool>& shouldStop,
                            std::vector<uint64_t>& pathHashes) {
    BBSearchContext ctx;
    ctx.nextTimeCheck = TIME_CHECK_INTERVAL;
    BBSearchResult result;

    BBMoveList moves;
    bbGenerate(pos, moves);
    if (moves.empty()) return result;

    int bestScore = 0;
    bool haveScore = false;
    BitboardMove bestMove = moves[0];

    for (int currentDepth = 1; currentDepth <= maxDepth; ++currentDepth) {
        if (searchAborted(ctx, shouldStop)) break;

        const uint64_t hash = pos.hash;
        BitboardMove ttMove{};
        uint16_t ttPacked = 0;
        int ttScore;
        if (tt.probe(hash, currentDepth, 0, -INF, INF, ttScore, ttPacked)) {
            ttMove = unpackBB(ttPacked);
            auto it = std::find_if(moves.begin(), moves.end(),
                                   [&](const BitboardMove& m) { return sameMove(m, ttMove); });
            if (it != moves.end()) std::swap(*moves.begin(), *it);
        }
        ctx.orderer.orderMoves(moves, pos, currentDepth, ttMove);

        int currentBestScore = -INF;
        BitboardMove currentBestMove = moves[0];
        bool completedDepth = true;

        const int INF_LO = -INF;
        const int INF_HI = INF;
        // currentDepth >= 3 matters: the depth-1 and depth-2 scores are too
        // unreliable to centre a narrow window on, and a window that fails
        // costs a whole re-search of the iteration.
        const bool useAspiration = g_searchOptions.aspiration && currentDepth >= 3 &&
                                   haveScore && std::abs(bestScore) < 29000;
        int delta = ASP_BASE_DELTA;
        int windowLo = useAspiration ? bestScore - delta : INF_LO;
        int windowHi = useAspiration ? bestScore + delta : INF_HI;

        while (true) {
            currentBestScore = INF_LO;
            currentBestMove = moves[0];
            completedDepth = true;
            pathHashes.clear();
            pathHashes.push_back(hash);

            int alpha = windowLo;
            int beta = windowHi;

            for (int i = 0; i < moves.size(); ++i) {
                const BitboardMove& move = moves[i];
                if (searchAborted(ctx, shouldStop)) { completedDepth = false; break; }

                const PositionUndo undo = pos.makeMove(move);
                int eval;
                if (i == 0) {
                    eval = -minimaxWithTT(ctx, pos, currentDepth - 1, 1, -beta, -alpha,
                                          shouldStop, tt, pathHashes, &move);
                } else {
                    // Principal variation search: a cheap null-window probe
                    // first, and only a move that beats alpha is re-searched.
                    eval = -minimaxWithTT(ctx, pos, currentDepth - 1, 1, -alpha - 1, -alpha,
                                          shouldStop, tt, pathHashes, &move);
                    if (!searchAborted(ctx, shouldStop) && eval > alpha && eval < beta) {
                        eval = -minimaxWithTT(ctx, pos, currentDepth - 1, 1, -beta, -alpha,
                                              shouldStop, tt, pathHashes, &move);
                    }
                }
                pos.unmakeMove(undo);

                if (!searchAborted(ctx, shouldStop)) {
                    if (eval > currentBestScore) {
                        currentBestScore = eval;
                        currentBestMove = move;
                    }
                    if (eval > alpha) alpha = eval;
                }
            }

            if (!completedDepth || searchAborted(ctx, shouldStop) || !useAspiration) break;

            // Outside the window, so this is only a bound. Widen the failing
            // side and search the depth again.
            if (currentBestScore <= windowLo) {
                delta *= 4;
                windowLo = (bestScore - delta < -29000) ? INF_LO : bestScore - delta;
                continue;
            }
            if (currentBestScore >= windowHi) {
                delta *= 4;
                windowHi = (bestScore + delta > 29000) ? INF_HI : bestScore + delta;
                continue;
            }
            break;
        }

        if (completedDepth && !searchAborted(ctx, shouldStop)) {
            bestScore = currentBestScore;
            bestMove = currentBestMove;
            haveScore = true;
        }

        // A found mate ends the search: deeper iterations cannot improve on it
        // and the score is not a centipawn quantity to keep refining. The bound
        // excludes large material scores, which are not mates.
        if (std::abs(bestScore) > 29000 && std::abs(bestScore) < 31000 &&
            currentDepth >= MATE_SCORE - std::abs(bestScore)) break;
    }

    result.best = bestMove;
    result.score = bestScore;
    result.nodes = ctx.nodes;
    return result;
}
