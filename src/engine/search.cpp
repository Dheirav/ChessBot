#include "search.hpp"
#include "movegen.hpp"
#include "board.hpp"
#include "evaluation.hpp"
#include "transposition_table.hpp"
#include "move_ordering.hpp"
#include "legal_move_validator.hpp"
#include <limits>
#include <algorithm>
#include <atomic>
#include <iostream>
#include <chrono>

// Piece values for MVV-LVA ordering in the quiescence search
static const int QS_PIECE_VALUES[7] = { 0, 20000, 100, 320, 330, 500, 900 };

// Score used for checkmate (positive = white mates black, negative = black mates white).
// Actual mate scores are MATE_SCORE - ply so that nearer mates score higher,
// which makes the engine converge on the fastest mate instead of shuffling
// between equally "mating" lines forever. Stalemate is scored 0.
static constexpr int MATE_SCORE = 30000;

static int mateScore(bool whiteToMove, int ply) {
    return whiteToMove ? -(MATE_SCORE - ply) : (MATE_SCORE - ply);
}

// Generates the tactical moves (captures, en passant, promotions) for quiescence search
static MoveList generateCaptures(const Board& board, PieceColor side) {
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

// Quiescence search: avoids the horizon effect by searching captures and promotions
// at the leaves of the main search. Uses the same white-perspective scoring as minimax.
static int quiescence(Board& board, int ply, int alpha, int beta,
                      const std::atomic<bool>& shouldStop) {
    if (shouldStop.load()) {
        return 0;
    }

    PieceColor side = board.activeColor;

    // The evaluation is always white-perspective, so the side to move maximizes
    // when it is white and minimizes when it is black.
    bool whiteToMove = (side == COLOR_WHITE);

    // If not in check, the static evaluation is a valid stand-pat cutoff.
    // If in check, we must search every evasion (stand-pat is illegal).
    bool inCheck = LegalMoveValidator::isInCheck(board, side);

    if (!inCheck) {
        int standPat = evaluate(board);
        if (whiteToMove) {
            if (standPat >= beta) return beta;
            if (standPat > alpha) alpha = standPat;
        } else {
            if (standPat <= alpha) return alpha;
            if (standPat < beta) beta = standPat;
        }
    }

    MoveList moves = inCheck ? generateLegalMoves(board, side) : generateCaptures(board, side);

    // No captures (or evasions) available. In check with no evasions this is
    // checkmate; otherwise the stand-pat value above already folded into alpha/beta.
    if (moves.empty()) {
        return inCheck ? mateScore(whiteToMove, ply) : (whiteToMove ? alpha : beta);
    }

    // Order captures by MVV-LVA (most valuable victim, least valuable attacker)
    std::sort(moves.begin(), moves.end(), [](const Move& a, const Move& b) {
        auto score = [](const Move& m) {
            if (m.flag == EN_PASSANT) {
                return 10 * QS_PIECE_VALUES[PAWN] - QS_PIECE_VALUES[m.movedPiece.type()];
            }
            if (m.flag == PROMOTION && m.capturedPiece.type() == NONE) {
                return 10 * QS_PIECE_VALUES[m.promotionPiece.type()];
            }
            int victim = (m.capturedPiece.type() == NONE) ? 0 : QS_PIECE_VALUES[m.capturedPiece.type()];
            int attacker = QS_PIECE_VALUES[m.movedPiece.type()];
            return 10 * victim - attacker;
        };
        return score(a) > score(b);
    });

    for (const Move& move : moves) {
        if (shouldStop.load()) {
            break;
        }

        UndoInfo undo = board.makeMove(move);
        int score = quiescence(board, ply + 1, alpha, beta, shouldStop);
        board.unmakeMove(undo);

        if (shouldStop.load()) {
            break;
        }

        if (whiteToMove) {
            if (score > alpha) {
                alpha = score;
                if (alpha >= beta) return beta;
            }
        } else {
            if (score < beta) {
                beta = score;
                if (alpha >= beta) return alpha;
            }
        }
    }

    return whiteToMove ? alpha : beta;
}

// Minimax with alpha-beta pruning and stop condition
static int minimax(Board& board, int depth, int ply, int alpha, int beta,
                  const std::atomic<bool>& shouldStop) {
    // Check if we should stop searching
    if (shouldStop.load()) {
        return 0; // Return neutral score when stopped
    }

    // Fifty-move rule: draw regardless of material
    if (board.halfmoveClock >= 100) {
        return 0;
    }

    if (depth == 0) {
        return quiescence(board, ply, alpha, beta, shouldStop);
    }

    // The side to move is always board.activeColor. Because evaluate() is
    // white-perspective, white maximizes and black minimizes.
    PieceColor side = board.activeColor;
    bool whiteToMove = (side == COLOR_WHITE);
    MoveList moves = generateLegalMoves(board, side);
    if (moves.empty()) {
        if (LegalMoveValidator::isInCheck(board, side)) {
            return mateScore(whiteToMove, ply);
        }
        return 0;
    }
    int bestEval = whiteToMove ? std::numeric_limits<int>::min() : std::numeric_limits<int>::max();
    for (const Move& move : moves) {
        // Check stop condition before each move
        if (shouldStop.load()) {
            break;
        }
        
        UndoInfo undo = board.makeMove(move);
        int eval = minimax(board, depth - 1, ply + 1, alpha, beta, shouldStop);
        board.unmakeMove(undo);

        if (whiteToMove) {
            if (eval > bestEval) bestEval = eval;
            if (bestEval > alpha) alpha = bestEval;
            if (beta <= alpha) break;
        } else {
            if (eval < bestEval) bestEval = eval;
            if (bestEval < beta) beta = bestEval;
            if (beta <= alpha) break;
        }
    }
    return bestEval;
}

// Minimax with transposition table support. `pathHashes` holds the zobrist
// keys of the positions on the current search path (root to parent) and is
// used to score in-search repetitions as draws.
static int minimaxWithTT(Board& board, int depth, int ply, int alpha, int beta,
                        const std::atomic<bool>& shouldStop, TranspositionTable& tt,
                        std::vector<uint64_t>& pathHashes) {
    // Check if we should stop searching
    if (shouldStop.load()) {
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
        if (!shouldStop.load()) {
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

    // The side to move is always board.activeColor. Because evaluate() is
    // white-perspective, white maximizes and black minimizes.
    PieceColor side = board.activeColor;
    bool whiteToMove = (side == COLOR_WHITE);
    MoveList moves = generateLegalMoves(board, side);
    
    if (moves.empty()) {
        int score;
        if (LegalMoveValidator::isInCheck(board, side)) {
            score = mateScore(whiteToMove, ply);
        } else {
            score = 0;
        }
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

    int bestEval = whiteToMove ? std::numeric_limits<int>::min() : std::numeric_limits<int>::max();
    Move bestMove;

    pathHashes.push_back(hash);

    for (const Move& move : moves) {
        // Check stop condition before each move
        if (shouldStop.load()) {
            break;
        }

        UndoInfo undo = board.makeMove(move);
        int eval = minimaxWithTT(board, depth - 1, ply + 1, alpha, beta, shouldStop, tt, pathHashes);
        board.unmakeMove(undo);
        
        if (whiteToMove) {
            if (eval > bestEval) {
                bestEval = eval;
                bestMove = move;
            }
            if (bestEval > alpha) alpha = bestEval;
            if (beta <= alpha) {
                // Beta cutoff - update move ordering
                g_moveOrderer.updateKillerMove(move, depth);
                g_moveOrderer.updateHistory(move, depth);
                break;
            }
        } else {
            if (eval < bestEval) {
                bestEval = eval;
                bestMove = move;
            }
            if (bestEval < beta) beta = bestEval;
            if (beta <= alpha) {
                // Beta cutoff - update move ordering
                g_moveOrderer.updateKillerMove(move, depth);
                g_moveOrderer.updateHistory(move, depth);
                break;
            }
        }
    }
    
    pathHashes.pop_back();

    // A stopped search leaves bestEval partial (possibly still ±INT limits
    // from an unfinished loop) and its children returned fake neutral scores.
    // Storing that would poison the table for every later search, since the
    // TT persists across moves. Return without storing; callers that see
    // shouldStop discard this value anyway.
    if (shouldStop.load()) {
        return bestEval;
    }

    // Store in transposition table
    TTEntry::NodeType nodeType;
    if (bestEval <= originalAlpha) {
        nodeType = TTEntry::UPPER_BOUND;
    } else if (bestEval >= beta) {
        nodeType = TTEntry::LOWER_BOUND;
    } else {
        nodeType = TTEntry::EXACT;
    }

    tt.store(hash, depth, ply, bestEval, bestMove, nodeType);

    return bestEval;
}

Move findBestMove(const Board& board, int depth) {
    std::atomic<bool> dummyStop{false};
    Board searchBoard = board.copyForSearch();
    return findBestMoveWithStop(searchBoard, depth, dummyStop);
}

Move findBestMoveWithStop(Board& board, int depth, const std::atomic<bool>& shouldStop) {
    MoveList moves = generateLegalMoves(board, board.activeColor);
    if (moves.empty()) return Move();

    // Early stop check
    if (shouldStop.load()) {
        return moves[0];
    }

    // Move ordering for the root
    g_moveOrderer.orderMoves(moves, board, depth, Move());

    bool whiteToMove = (board.activeColor == COLOR_WHITE);
    int bestEval = whiteToMove ? std::numeric_limits<int>::min() : std::numeric_limits<int>::max();
    Move bestMove = moves[0];
    
    for (const Move& move : moves) {
        // Check stop condition before evaluating each move
        if (shouldStop.load()) {
            std::cout << "Search stopped during move evaluation" << std::endl;
            break;
        }
        
        UndoInfo undo = board.makeMove(move);
        int eval = minimax(board, depth - 1, 1, std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), shouldStop);
        board.unmakeMove(undo);
        
        if (!shouldStop.load()) {
            if (whiteToMove) {
                if (eval > bestEval) {
                    bestEval = eval;
                    bestMove = move;
                }
            } else {
                if (eval < bestEval) {
                    bestEval = eval;
                    bestMove = move;
                }
            }
        }
    }
    
    return bestMove;
}

Move findBestMoveWithTT(Board& board, int depth, const std::atomic<bool>& shouldStop, 
                       TranspositionTable& tt) {
    // Clear move ordering data for new search
    g_moveOrderer.clear();
    
    MoveList moves = generateLegalMoves(board, board.activeColor);
    if (moves.empty()) return Move();

    // Early stop check
    if (shouldStop.load()) {
        return moves[0];
    }

    // Try to get best move from transposition table first
    uint64_t hash = board.getHash();
    Move ttMove;
    int ttScore;
    if (tt.probe(hash, depth, 0, std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), ttScore, ttMove)) {
        // Verify the TT move is legal
        auto it = std::find(moves.begin(), moves.end(), ttMove);
        if (it != moves.end()) {
            return ttMove;
        }
    }

    // Use move ordering for root moves
    g_moveOrderer.orderMoves(moves, board, depth, ttMove);

    bool whiteToMove = (board.activeColor == COLOR_WHITE);
    int bestEval = whiteToMove ? std::numeric_limits<int>::min() : std::numeric_limits<int>::max();
    Move bestMove = moves[0];

    // Narrowing alpha/beta window across root moves (see iterative deepening).
    int alpha = std::numeric_limits<int>::min();
    int beta = std::numeric_limits<int>::max();

    std::vector<uint64_t> pathHashes;
    pathHashes.push_back(hash);

    for (size_t i = 0; i < moves.size(); ++i) {
        const Move& move = moves[i];
        // Check stop condition before evaluating each move
        if (shouldStop.load()) {
            std::cout << "Search stopped during move evaluation (TT version)" << std::endl;
            break;
        }

        UndoInfo undo = board.makeMove(move);
        int eval;
        if (i == 0) {
            eval = minimaxWithTT(board, depth - 1, 1, alpha, beta, shouldStop, tt, pathHashes);
        } else if (whiteToMove) {
            eval = minimaxWithTT(board, depth - 1, 1, alpha, alpha + 1, shouldStop, tt, pathHashes);
            if (!shouldStop.load() && eval > alpha && eval < beta) {
                eval = minimaxWithTT(board, depth - 1, 1, alpha, beta, shouldStop, tt, pathHashes);
            }
        } else {
            eval = minimaxWithTT(board, depth - 1, 1, beta - 1, beta, shouldStop, tt, pathHashes);
            if (!shouldStop.load() && eval < beta && eval > alpha) {
                eval = minimaxWithTT(board, depth - 1, 1, alpha, beta, shouldStop, tt, pathHashes);
            }
        }
        board.unmakeMove(undo);

        if (!shouldStop.load()) {
            if (whiteToMove) {
                if (eval > bestEval) {
                    bestEval = eval;
                    bestMove = move;
                }
                if (eval > alpha) alpha = eval;
            } else {
                if (eval < bestEval) {
                    bestEval = eval;
                    bestMove = move;
                }
                if (eval < beta) beta = eval;
            }
        }
    }
    
    return bestMove;
}

// Iterative deepening search with time management and better move ordering
Move findBestMoveIterativeDeepening(Board& board, int maxDepth, 
                                   const std::atomic<bool>& shouldStop, 
                                   TranspositionTable& tt) {
    // Clear move ordering data for new search
    g_moveOrderer.clear();
    
    MoveList moves = generateLegalMoves(board, board.activeColor);
    if (moves.empty()) return Move();

    // Early stop check
    if (shouldStop.load()) {
        return moves[0];
    }

    Move bestMove = moves[0];
    bool whiteToMove = (board.activeColor == COLOR_WHITE);
    int bestScore = whiteToMove ? std::numeric_limits<int>::min() : std::numeric_limits<int>::max();
    
    auto searchStart = std::chrono::steady_clock::now();
    std::cout << "Starting iterative deepening search up to depth " << maxDepth << std::endl;
    
    // Iterative deepening loop
    for (int currentDepth = 1; currentDepth <= maxDepth; ++currentDepth) {
        if (shouldStop.load()) {
            std::cout << "Search stopped at depth " << (currentDepth - 1) << std::endl;
            break;
        }
        
        auto depthStart = std::chrono::steady_clock::now();
        std::cout << "Searching depth " << currentDepth << "..." << std::endl;
        
        // Try to get best move from transposition table for move ordering
        uint64_t hash = board.getHash();
        Move ttMove;
        int ttScore;
        if (tt.probe(hash, currentDepth, 0, std::numeric_limits<int>::min(),
                    std::numeric_limits<int>::max(), ttScore, ttMove)) {
            // Verify the TT move is legal and use it for ordering
            auto it = std::find(moves.begin(), moves.end(), ttMove);
            if (it != moves.end()) {
                // Move TT move to front for better ordering
                std::swap(*moves.begin(), *it);
            }
        }
        
        // Order moves using previous iteration knowledge
        g_moveOrderer.orderMoves(moves, board, currentDepth, ttMove);
        
        int currentBestScore = whiteToMove ? std::numeric_limits<int>::min() : std::numeric_limits<int>::max();
        Move currentBestMove = moves[0];
        bool completedDepth = true;

        // Narrowing alpha/beta window across root moves: later moves are pruned
        // against the current best, and non-first moves get a cheap null-window
        // search first (principal variation search). Alpha-beta is exact, so the
        // chosen move is unchanged.
        int alpha = std::numeric_limits<int>::min();
        int beta = std::numeric_limits<int>::max();

        std::vector<uint64_t> pathHashes;
        pathHashes.push_back(hash);

        for (size_t i = 0; i < moves.size(); ++i) {
            const Move& move = moves[i];
            // Check stop condition before evaluating each move
            if (shouldStop.load()) {
                std::cout << "Search interrupted during depth " << currentDepth << std::endl;
                completedDepth = false;
                break;
            }

            UndoInfo undo = board.makeMove(move);
            int eval;
            if (i == 0) {
                eval = minimaxWithTT(board, currentDepth - 1, 1, alpha, beta, shouldStop, tt, pathHashes);
            } else if (whiteToMove) {
                eval = minimaxWithTT(board, currentDepth - 1, 1, alpha, alpha + 1, shouldStop, tt, pathHashes);
                if (!shouldStop.load() && eval > alpha && eval < beta) {
                    eval = minimaxWithTT(board, currentDepth - 1, 1, alpha, beta, shouldStop, tt, pathHashes);
                }
            } else {
                eval = minimaxWithTT(board, currentDepth - 1, 1, beta - 1, beta, shouldStop, tt, pathHashes);
                if (!shouldStop.load() && eval < beta && eval > alpha) {
                    eval = minimaxWithTT(board, currentDepth - 1, 1, alpha, beta, shouldStop, tt, pathHashes);
                }
            }
            board.unmakeMove(undo);

            if (!shouldStop.load()) {
                if (whiteToMove) {
                    if (eval > currentBestScore) {
                        currentBestScore = eval;
                        currentBestMove = move;
                    }
                    if (eval > alpha) alpha = eval;
                } else {
                    if (eval < currentBestScore) {
                        currentBestScore = eval;
                        currentBestMove = move;
                    }
                    if (eval < beta) beta = eval;
                }
            }
        }
        
        // Only update best move if we completed the full depth
        if (completedDepth && !shouldStop.load()) {
            bestMove = currentBestMove;
            bestScore = currentBestScore;
            auto depthEnd = std::chrono::steady_clock::now();
            auto depthDuration = std::chrono::duration_cast<std::chrono::milliseconds>(depthEnd - depthStart);
            std::cout << "Depth " << currentDepth << " complete in " << depthDuration.count() 
                     << "ms. Best move: " << bestMove.toString() << " (score: " << bestScore << ")" << std::endl;
        } else {
            std::cout << "Depth " << currentDepth << " incomplete, using previous result" << std::endl;
            break;
        }
        
        // Optional: Check for mate scores and stop early if mate is found
        // Only stop if we detect an actual mate score (near ±MATE_SCORE which is around ±30000)
        // Do NOT stop for large evaluation scores from material imbalances
        if (abs(bestScore) > 29000 && abs(bestScore) < 31000) {
            std::cout << "Mate detected at depth " << currentDepth << ", stopping search" << std::endl;
            break;
        }
    }
    
    auto searchEnd = std::chrono::steady_clock::now();
    auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(searchEnd - searchStart);
    std::cout << "Iterative deepening search completed in " << totalDuration.count() 
             << "ms. Final best move: " << bestMove.toString() << " (score: " << bestScore << ")" << std::endl;
    
    return bestMove;
}
