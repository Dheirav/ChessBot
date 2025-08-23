
#include "search.hpp"
#include "movegen.hpp"
#include "board.hpp"
#include "evaluation.hpp"
#include <limits>
#include <algorithm>

// Minimax with alpha-beta pruning
static int minimax(Board& board, int depth, int alpha, int beta, bool maximizingPlayer) {
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
        Board copy = board;
        copy.makeMove(move);
        copy.activeColor = (side == COLOR_WHITE ? COLOR_BLACK : COLOR_WHITE);
        int eval = minimax(copy, depth - 1, alpha, beta, !maximizingPlayer);
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

Move findBestMove(const Board& board, int depth) {
    MoveList moves = generateMoves(board, board.activeColor);
    if (moves.empty()) return Move();

    // MVV-LVA: Most Valuable Victim, Least Valuable Attacker for captures
    extern const int pieceValues[];
    std::sort(moves.begin(), moves.end(), [&](const Move& a, const Move& b) {
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
        Board copy = board;
        copy.makeMove(move);
        copy.activeColor = (board.activeColor == COLOR_WHITE ? COLOR_BLACK : COLOR_WHITE);
        int eval = minimax(copy, depth - 1, std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), false);
        if (eval > bestEval) {
            bestEval = eval;
            bestMove = move;
        }
    }
    return bestMove;
}
