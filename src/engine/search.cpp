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
#include <atomic>
#include <iostream>
#include <chrono>

// Piece values for MVV-LVA ordering in the quiescence search
static const int QS_PIECE_VALUES[7] = { 0, 20000, 100, 320, 330, 500, 900 };

// Sorts a losing capture below every sound one while leaving MVV-LVA to order
// within each group. Larger than any MVV-LVA key (10 × queen = 9000), so the
// two groups can never interleave.
static constexpr int SEE_LOSING_CAPTURE_BAND = 100000;

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
    {"nullmove",    "nullmove", &SearchOptions::nullMove},
    {"lmr",         "lmr",      &SearchOptions::lmr},
    {"aspiration",  "asp",      &SearchOptions::aspiration},
    {"seeordering", "seeord",   &SearchOptions::seeOrdering},
    {"seepruning",  "seeprune", &SearchOptions::seePruning},
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

// Checking the clock costs far more than a node does, so it is checked once
// every few thousand nodes instead of at every one. At the measured ~165k
// nodes/second this bounds overshoot to roughly 12ms, well inside any real
// time control, while the check itself stays invisible in the profile.
static constexpr uint64_t TIME_CHECK_INTERVAL = 2048;

// The search stops for two reasons: the GUI asked it to, or it ran out of
// time. Everywhere the search used to test shouldStop it now tests this.
static inline bool searchAborted(const std::atomic<bool>& shouldStop) {
    if (shouldStop.load()) return true;
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
static int quiescence(Board& board, int ply, int alpha, int beta,
                      const std::atomic<bool>& shouldStop) {
    ++g_searchNodes;
    if (searchAborted(shouldStop)) {
        return 0;
    }

    PieceColor side = board.activeColor;

    // If not in check, the static evaluation is a valid stand-pat cutoff.
    // If in check, we must search every evasion (stand-pat is illegal).
    bool inCheck = LegalMoveValidator::isInCheck(board, side);

    if (!inCheck) {
        int standPat = scoreForSideToMove(board);
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

        if (searchAborted(shouldStop)) {
            break;
        }

        UndoInfo undo = board.makeMove(move);
        int score = -quiescence(board, ply + 1, -beta, -alpha, shouldStop);
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

// Minimax with transposition table support. `pathHashes` holds the zobrist
// keys of the positions on the current search path (root to parent) and is
// used to score in-search repetitions as draws.
static int minimaxWithTT(Board& board, int depth, int ply, int alpha, int beta,
                        const std::atomic<bool>& shouldStop, TranspositionTable& tt,
                        std::vector<uint64_t>& pathHashes) {
    ++g_searchNodes;
    // Check if we should stop searching
    if (searchAborted(shouldStop)) {
        return 0; // Return neutral score when stopped
    }

    uint64_t hash = board.getHash();

    // Draw detection. Both checks run before the TT probe: a repetition
    // score is path-dependent, and a cached score must not override it.
    if (board.halfmoveClock >= 100) {
        return 0; // Fifty-move rule
    }
    if (ply > 0 && std::find(pathHashes.begin(), pathHashes.end(), hash) != pathHashes.end()) {
        return 0; // Repetition within the search line: treat as a draw
    }

    int originalAlpha = alpha;
    int originalBeta = beta;
    Move ttMove;
    int ttScore;

    // Probe transposition table
    if (tt.probe(hash, depth, ply, alpha, beta, ttScore, ttMove)) {
        return ttScore;
    }

    if (depth == 0) {
        int score = quiescence(board, ply, alpha, beta, shouldStop);
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

    PieceColor side = board.activeColor;
    bool inCheck = LegalMoveValidator::isInCheck(board, side);

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
        int nullScore = -minimaxWithTT(board, depth - 1 - R, ply + 1, -beta, -beta + 1,
                                       shouldStop, tt, pathHashes);
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
    
    // Move ordering with killer moves and history heuristic
    g_moveOrderer.orderMoves(moves, board, depth, ttMove);

    // Aggressively search TT move first if available
    if (ttMove.from != -1) {
        auto it = std::find(moves.begin(), moves.end(), ttMove);
        if (it != moves.end() && it != moves.begin()) {
            std::iter_swap(moves.begin(), it);
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
        ++moveIndex;

        UndoInfo undo = board.makeMove(move);

        // --- Late move reductions ---
        // The list is ordered by TT move, then captures, killers and history,
        // so a move this far down is unlikely to be best. Search it shallower
        // with a null window first, and only pay for a full-depth re-search if
        // it unexpectedly beats the window. Captures and promotions are never
        // reduced: they are exactly the moves that turn out to matter.
        bool reduce = g_searchOptions.lmr && depth >= 3 && moveIndex > 3 &&
                      !inCheck && move.flag == NORMAL;
        int eval;
        if (reduce) {
            const int R = 1;
            eval = -minimaxWithTT(board, depth - 1 - R, ply + 1, -alpha - 1, -alpha,
                                  shouldStop, tt, pathHashes);
            if (!searchAborted(shouldStop) && eval > alpha) {
                eval = -minimaxWithTT(board, depth - 1, ply + 1, -beta, -alpha,
                                      shouldStop, tt, pathHashes);
            }
        } else {
            eval = -minimaxWithTT(board, depth - 1, ply + 1, -beta, -alpha,
                                  shouldStop, tt, pathHashes);
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
            g_moveOrderer.updateHistory(move, depth);
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

    tt.store(hash, depth, ply, bestEval, bestMove, nodeType);

    return bestEval;
}

// Iterative deepening. Each iteration reuses the previous one's transposition
// entries and move ordering, so the extra cost of starting shallow is far less
// than the ordering it buys — and it is what makes a time limit usable at all,
// since there is always a completed result to fall back on.
Move findBestMoveIterativeDeepening(Board& board, const SearchLimits& limits,
                                   const std::atomic<bool>& shouldStop,
                                   TranspositionTable& tt) {
    const int maxDepth = limits.maxDepth;

    // Clear move ordering data for new search
    g_moveOrderer.clear();
    g_searchNodes = 0;

    // Arm the deadline. With no budget the search is depth-limited and every
    // clock check short-circuits, which is what keeps tests/bench reproducible.
    g_hasDeadline = (limits.moveTimeMs > 0);
    g_outOfTime = false;
    g_nextTimeCheck = TIME_CHECK_INTERVAL;
    if (g_hasDeadline) {
        g_deadline = std::chrono::steady_clock::now() +
                     std::chrono::milliseconds(limits.moveTimeMs);
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
        int delta = 50;
        int windowLo = useAspiration ? bestScore - delta : INF_LO;
        int windowHi = useAspiration ? bestScore + delta : INF_HI;

        std::vector<uint64_t> pathHashes;

        while (true) {
            currentBestScore = INF_LO;
            currentBestMove = moves[0];
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
                                          shouldStop, tt, pathHashes);
                } else {
                    // Principal variation search: later root moves get a cheap
                    // null-window probe first, and only a move that beats alpha
                    // is re-searched with the full window.
                    eval = -minimaxWithTT(board, currentDepth - 1, 1, -alpha - 1, -alpha,
                                          shouldStop, tt, pathHashes);
                    if (!searchAborted(shouldStop) && eval > alpha && eval < beta) {
                        eval = -minimaxWithTT(board, currentDepth - 1, 1, -beta, -alpha,
                                              shouldStop, tt, pathHashes);
                    }
                }
                board.unmakeMove(undo);

                if (!searchAborted(shouldStop)) {
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
        if (g_hasDeadline && currentDepth < maxDepth) {
            auto now = std::chrono::steady_clock::now();
            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                g_deadline - now).count();
            auto lastIteration = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - depthStart).count();
            const double BRANCHING = 2.3;
            if (remaining <= 0 || (double)lastIteration * BRANCHING > (double)remaining) {
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
                                   TranspositionTable& tt) {
    return findBestMoveIterativeDeepening(board, SearchLimits(maxDepth), shouldStop, tt);
}
