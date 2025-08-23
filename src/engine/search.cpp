
#include "search.hpp"
#include "movegen.hpp"
#include "board.hpp"
#include "evaluation.hpp"
#include <limits>
#include <algorithm>
#include <atomic>
#include <iostream>

// Minimax with alpha-beta pruning and stop condition
static int minimax(Board& board, int depth, int alpha, int beta, bool maximizingPlayer, 
                  const std::atomic<bool>& shouldStop) {
    // Check if we should stop searching
    if (shouldStop.load()) {
        return 0; // Return neutral score when stopped
    }
    
    if (depth == 0) {
        return evaluate(board);
    }
    PieceColor side = maximizingPlayer ? board.activeColor : (board.activeColor == COLOR_WHITE ? COLOR_BLACK : COLOR_WHITE);
    MoveList moves = generateMoves(board, side);
    if (moves.empty()) {
        // Checkmate or stalemate
        return evaluate(board);
    }
    int bestEval = maximizingPlayer ? std::numeric_limits<int>::min() : std::numeric_limits<int>::max();
    for (const Move& move : moves) {
        // Check stop condition before each move
        if (shouldStop.load()) {
            break;
        }
        
        Board copy = board;
        copy.makeMove(move);
        copy.activeColor = (side == COLOR_WHITE ? COLOR_BLACK : COLOR_WHITE);
        int eval = minimax(copy, depth - 1, alpha, beta, !maximizingPlayer, shouldStop);
        if (maximizingPlayer) {
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

// Original minimax without stop condition (for compatibility)
static int minimaxOriginal(Board& board, int depth, int alpha, int beta, bool maximizingPlayer) {
    std::atomic<bool> dummyStop{false};
    return minimax(board, depth, alpha, beta, maximizingPlayer, dummyStop);
}

Move findBestMove(const Board& board, int depth) {
    std::atomic<bool> dummyStop{false};
    return findBestMoveWithStop(board, depth, dummyStop);
}

Move findBestMoveWithStop(const Board& board, int depth, const std::atomic<bool>& shouldStop) {
    MoveList moves = generateMoves(board, board.activeColor);
    if (moves.empty()) return Move();

    // Early stop check
    if (shouldStop.load()) {
        return moves.empty() ? Move() : moves[0];
    }

    // MVV-LVA: Most Valuable Victim, Least Valuable Attacker for captures
    extern const int pieceValues[];
    std::sort(moves.begin(), moves.end(), [&](const Move& a, const Move& b) {
        // Check stop condition during sorting
        if (shouldStop.load()) return false;
        
        auto moveType = [&](const Move& m) -> int {
            Board copy = board;
            copy.makeMove(m);
            PieceColor oppColor = (board.activeColor == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
            MoveList oppMoves = generateMoves(copy, oppColor);
            if (oppMoves.empty()) {
                int kingSq = -1;
                for (int i = 0; i < 64; ++i) {
                    if (copy.squares[i].type() == KING && copy.squares[i].color() == oppColor) {
                        kingSq = i;
                        break;
                    }
                }
                if (kingSq != -1 && copy.isSquareAttacked(kingSq, board.activeColor)) {
                    return 30000; // Checkmate
                } else {
                    return 0; // Stalemate
                }
            }
            int kingSq = -1;
            for (int i = 0; i < 64; ++i) {
                if (copy.squares[i].type() == KING && copy.squares[i].color() == oppColor) {
                    kingSq = i;
                    break;
                }
            }
            if (kingSq != -1 && copy.isSquareAttacked(kingSq, board.activeColor)) {
                return 20000; // Check
            }
            if (m.flag == CAPTURE) {
                // MVV-LVA: victim value * 100 - attacker value
                int victim = pieceValues[m.capturedPiece.type()];
                int attacker = pieceValues[m.movedPiece.type()];
                return 1000 + victim * 100 - attacker;
            }
            return 0;
        };
        return moveType(a) > moveType(b);
    });

    int bestEval = std::numeric_limits<int>::min();
    Move bestMove = moves[0];
    
    for (const Move& move : moves) {
        // Check stop condition before evaluating each move
        if (shouldStop.load()) {
            std::cout << "Search stopped during move evaluation" << std::endl;
            break;
        }
        
        Board copy = board;
        copy.makeMove(move);
        copy.activeColor = (board.activeColor == COLOR_WHITE ? COLOR_BLACK : COLOR_WHITE);
        int eval = minimax(copy, depth - 1, std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), false, shouldStop);
        
        if (!shouldStop.load() && eval > bestEval) {
            bestEval = eval;
            bestMove = move;
        }
    }
    
    return bestMove;
}
