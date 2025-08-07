#include "search.hpp"
#include "movegen.hpp"
#include "board.hpp"
#include "evaluation.hpp"
#include <limits>

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
