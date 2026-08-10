#include "movegen.hpp"
#include "move_lookup.hpp"
#include "piece.hpp"
#include "legal_move_validator.hpp"
#include <cctype>
#include <iostream>

// The en passant target is stored as a board index directly; -1 means none.
static int getEnPassantIdx(const Board& board) {
    return board.enPassantSquare;
}


// Optimized: Directly check if a square is attacked by any piece of byColor
static bool isSquareAttacked(const Board& board, int sq, PieceColor byColor) {
    int x = sq % 8, y = sq / 8;
    // Pawn attacks
    if (byColor == COLOR_WHITE) {
        // White pawns move toward rank 8 (decreasing y); a white pawn attacking sq sits below it.
        if (y < 7) {
            if (x > 0 && board.squares[(y+1)*8 + (x-1)].type() == PAWN && board.squares[(y+1)*8 + (x-1)].color() == byColor) return true;
            if (x < 7 && board.squares[(y+1)*8 + (x+1)].type() == PAWN && board.squares[(y+1)*8 + (x+1)].color() == byColor) return true;
        }
    } else {
        // Black pawns move toward rank 1 (increasing y); a black pawn attacking sq sits above it.
        if (y > 0) {
            if (x > 0 && board.squares[(y-1)*8 + (x-1)].type() == PAWN && board.squares[(y-1)*8 + (x-1)].color() == byColor) return true;
            if (x < 7 && board.squares[(y-1)*8 + (x+1)].type() == PAWN && board.squares[(y-1)*8 + (x+1)].color() == byColor) return true;
        }
    }
    // Knight attacks
    const int knightMoves[8][2] = { {1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,-1},{-2,1},{-1,2} };
    for (int i = 0; i < 8; ++i) {
        int nx = x + knightMoves[i][0], ny = y + knightMoves[i][1];
        if (nx >= 0 && nx < 8 && ny >= 0 && ny < 8) {
            int idx = ny*8 + nx;
            if (board.squares[idx].type() == KNIGHT && board.squares[idx].color() == byColor) return true;
        }
    }
    // Sliding pieces: rook/queen (orthogonal)
    const int rookDirs[4][2] = { {0,1},{1,0},{0,-1},{-1,0} };
    for (int d = 0; d < 4; ++d) {
        int nx = x, ny = y;
        while (true) {
            nx += rookDirs[d][0]; ny += rookDirs[d][1];
            if (nx < 0 || nx >= 8 || ny < 0 || ny >= 8) break;
            int idx = ny*8 + nx;
            if (board.squares[idx].type() != NONE) {
                if ((board.squares[idx].type() == ROOK || board.squares[idx].type() == QUEEN) && board.squares[idx].color() == byColor) return true;
                break;
            }
        }
    }
    // Sliding pieces: bishop/queen (diagonal)
    const int bishopDirs[4][2] = { {1,1},{1,-1},{-1,-1},{-1,1} };
    for (int d = 0; d < 4; ++d) {
        int nx = x, ny = y;
        while (true) {
            nx += bishopDirs[d][0]; ny += bishopDirs[d][1];
            if (nx < 0 || nx >= 8 || ny < 0 || ny >= 8) break;
            int idx = ny*8 + nx;
            if (board.squares[idx].type() != NONE) {
                if ((board.squares[idx].type() == BISHOP || board.squares[idx].type() == QUEEN) && board.squares[idx].color() == byColor) return true;
                break;
            }
        }
    }
    // King attacks
    const int kingMoves[8][2] = { {1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1},{1,-1} };
    for (int i = 0; i < 8; ++i) {
        int nx = x + kingMoves[i][0], ny = y + kingMoves[i][1];
        if (nx >= 0 && nx < 8 && ny >= 0 && ny < 8) {
            int idx = ny*8 + nx;
            if (board.squares[idx].type() == KING && board.squares[idx].color() == byColor) return true;
        }
    }
    return false;
}

void generatePseudoLegalMoves(const Board& board, PieceColor sideToMove, bool includeCastling, MoveList& moves) {
    moves.clear();
    int enPassantIdx = getEnPassantIdx(board);

    for (int sq = 0; sq < 64; ++sq) {
        const Piece& piece = board.squares[sq];
        if (piece.type() == NONE || piece.color() != sideToMove)
            continue;

        switch (piece.type()) {
            case PAWN: {
                const auto& pawnMoves = (sideToMove == COLOR_WHITE) ? whitePawnMovesFrom[sq] : blackPawnMovesFrom[sq];
                for (int to : pawnMoves) {
                    int dx = (to % 8) - (sq % 8);
                    int toRank = to / 8;
                    bool isPromotion = (sideToMove == COLOR_WHITE && toRank == 0) || (sideToMove == COLOR_BLACK && toRank == 7);
                    bool isCapture = board.squares[to].type() != NONE && board.squares[to].color() != sideToMove;
                    bool isEnPassant = (to == enPassantIdx);
                    bool isDiagonal = (dx != 0);

                    // Detect two-step forward move
                    bool isDoubleStep = false;
                    int fromRank = sq / 8;
                    if (!isDiagonal && board.squares[to].type() == NONE) {
                        if (sideToMove == COLOR_WHITE && fromRank == 6 && toRank == 4) {
                            int oneStepSq = sq - 8;
                           
                            if (board.squares[oneStepSq].type() != NONE) {
                                isDoubleStep = true;
                            }
                        } else if (sideToMove == COLOR_BLACK && fromRank == 1 && toRank == 3) {
                            int oneStepSq = sq + 8;
                            if (board.squares[oneStepSq].type() != NONE) {
                                isDoubleStep = true;
                            }
                        }
                    }

                    // If double-step is true, skip this move (invalid)
                    if (isDoubleStep) {
                        continue;
                    }
                    // Promotion
                    if (isPromotion) {
                        for (PieceType promo : {QUEEN, ROOK, BISHOP, KNIGHT}) {
                            // Only allow promotion captures if there is a piece to capture
                            if (isDiagonal && (isCapture || isEnPassant))
                                moves.emplace_back(sq, to, piece, board.squares[to], PROMOTION, Piece(sideToMove, promo));
                            // Only allow promotion quiet moves if not diagonal and not a capture
                            else if (!isDiagonal && board.squares[to].type() == NONE)
                                moves.emplace_back(sq, to, piece, board.squares[to], PROMOTION, Piece(sideToMove, promo));
                        }
                    }
                    // En passant
                    else if (isEnPassant) {
                        moves.emplace_back(sq, to, piece, Piece(sideToMove == COLOR_WHITE ? COLOR_BLACK : COLOR_WHITE, PAWN), EN_PASSANT);
                    }
                    // Capture
                    else if (isDiagonal && isCapture) {
                        moves.emplace_back(sq, to, piece, board.squares[to], CAPTURE);
                    }
                    // Quiet move
                    else if (!isDiagonal && board.squares[to].type() == NONE) {
                        moves.emplace_back(sq, to, piece);
                    }
                }
                break;
            }
            case KNIGHT: {
                for (int to : knightMovesFrom[sq]) {
                    if (board.squares[to].type() == NONE || board.squares[to].color() != sideToMove)
                        moves.emplace_back(sq, to, piece, board.squares[to], board.squares[to].type() != NONE ? CAPTURE : NORMAL);
                }
                break;
            }
            case BISHOP: {
                // Bishop directions: {NE, NW, SE, SW}
                const int bishopDirs[4][2] = { {1, 1}, {-1, 1}, {1, -1}, {-1, -1} };
                int x = sq % 8, y = sq / 8;
                for (int dir = 0; dir < 4; ++dir) {
                    int dx = bishopDirs[dir][0], dy = bishopDirs[dir][1];
                    int nx = x + dx, ny = y + dy;
                    while (nx >= 0 && nx < 8 && ny >= 0 && ny < 8) {
                        int to = ny * 8 + nx;
                        if (board.squares[to].type() == NONE) {
                            moves.emplace_back(sq, to, piece);
                        } else {
                            if (board.squares[to].color() != sideToMove)
                                moves.emplace_back(sq, to, piece, board.squares[to], CAPTURE);
                            break;
                        }
                        nx += dx;
                        ny += dy;
                    }
                }
                break;
            }
            case ROOK: {
                // Rook directions: {N, S, E, W}
                const int rookDirs[4][2] = { {0, 1}, {0, -1}, {1, 0}, {-1, 0} };
                int x = sq % 8, y = sq / 8;
                for (int dir = 0; dir < 4; ++dir) {
                    int dx = rookDirs[dir][0], dy = rookDirs[dir][1];
                    int nx = x + dx, ny = y + dy;
                    while (nx >= 0 && nx < 8 && ny >= 0 && ny < 8) {
                        int to = ny * 8 + nx;
                        if (board.squares[to].type() == NONE) {
                            moves.emplace_back(sq, to, piece);
                        } else {
                            if (board.squares[to].color() != sideToMove)
                                moves.emplace_back(sq, to, piece, board.squares[to], CAPTURE);
                            break;
                        }
                        nx += dx;
                        ny += dy;
                    }
                }
                break;
            }
            case QUEEN: {
                // Queen = rook + bishop directions
                const int queenDirs[8][2] = { {0, 1}, {0, -1}, {1, 0}, {-1, 0}, {1, 1}, {-1, 1}, {1, -1}, {-1, -1} };
                int x = sq % 8, y = sq / 8;
                for (int dir = 0; dir < 8; ++dir) {
                    int dx = queenDirs[dir][0], dy = queenDirs[dir][1];
                    int nx = x + dx, ny = y + dy;
                    while (nx >= 0 && nx < 8 && ny >= 0 && ny < 8) {
                        int to = ny * 8 + nx;
                        if (board.squares[to].type() == NONE) {
                            moves.emplace_back(sq, to, piece);
                        } else {
                            if (board.squares[to].color() != sideToMove)
                                moves.emplace_back(sq, to, piece, board.squares[to], CAPTURE);
                            break;
                        }
                        nx += dx;
                        ny += dy;
                    }
                }
                break;
            }
            case KING: {
                PieceColor oppColor = (sideToMove == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
                for (int to : kingMovesFrom[sq]) {
                    if (board.squares[to].type() == NONE || board.squares[to].color() != sideToMove) {
                        // If capturing, set capturedPiece
                        if (board.squares[to].type() != NONE && board.squares[to].color() != sideToMove) {
                            moves.emplace_back(sq, to, piece, board.squares[to], CAPTURE);
                        } else {
                            moves.emplace_back(sq, to, piece); // No capture
                        }
                    }
                }
                // FIX: Use correct rank for castling (White: 7, Black: 0)
                int rank = (sideToMove == COLOR_WHITE) ? 7 : 0;
                if (includeCastling && sq == Board::get1DIndex(4, rank)) {
                    // oppColor already declared above
                    // King-side castling
                    if (board.castlingRights &
                        ((sideToMove == COLOR_WHITE) ? CASTLE_WK : CASTLE_BK)) {
                        if (board.squares[Board::get1DIndex(5, rank)].type() == NONE &&
                            board.squares[Board::get1DIndex(6, rank)].type() == NONE) {
                            if (board.squares[Board::get1DIndex(4, rank)].type() == KING &&
                                board.squares[Board::get1DIndex(4, rank)].color() == sideToMove &&
                                board.squares[Board::get1DIndex(7, rank)].type() == ROOK &&
                                board.squares[Board::get1DIndex(7, rank)].color() == sideToMove) {
                                if (!isSquareAttacked(board, Board::get1DIndex(4, rank), oppColor) &&
                                    !isSquareAttacked(board, Board::get1DIndex(5, rank), oppColor) &&
                                    !isSquareAttacked(board, Board::get1DIndex(6, rank), oppColor)) {
                                    moves.emplace_back(sq, Board::get1DIndex(6, rank), piece, Piece(), CASTLING);
                                }
                            }
                        }
                    }
                    // Queen-side castling
                    if (board.castlingRights &
                        ((sideToMove == COLOR_WHITE) ? CASTLE_WQ : CASTLE_BQ)) {
                        if (board.squares[Board::get1DIndex(1, rank)].type() == NONE &&
                            board.squares[Board::get1DIndex(2, rank)].type() == NONE &&
                            board.squares[Board::get1DIndex(3, rank)].type() == NONE) {
                            if (board.squares[Board::get1DIndex(4, rank)].type() == KING &&
                                board.squares[Board::get1DIndex(4, rank)].color() == sideToMove &&
                                board.squares[Board::get1DIndex(0, rank)].type() == ROOK &&
                                board.squares[Board::get1DIndex(0, rank)].color() == sideToMove) {
                                if (!isSquareAttacked(board, Board::get1DIndex(4, rank), oppColor) &&
                                    !isSquareAttacked(board, Board::get1DIndex(3, rank), oppColor) &&
                                    !isSquareAttacked(board, Board::get1DIndex(2, rank), oppColor)) {
                                    moves.emplace_back(sq, Board::get1DIndex(2, rank), piece, Piece(), CASTLING);
                                }
                            }
                        }
                    }
                }
                break;
            }
            default: break;
        }
    }

}

// The legality filter, shared by both entry points below. `board` is mutated
// and restored: every candidate move is made, the king square tested, and the
// move unmade. unmakeMove restores the position exactly — the search relies on
// that invariant at every node it visits — so on return the board is
// bit-identical to what came in.
static void filterLegal(Board& board, PieceColor sideToMove,
                        const MoveList& candidates, MoveList& out) {
    out.clear();
    out.reserve(candidates.size());

    int kingSq = -1;
    for (int i = 0; i < 64; ++i) {
        if (board.squares[i].type() == KING && board.squares[i].color() == sideToMove) {
            kingSq = i;
            break;
        }
    }

    const PieceColor oppColor = (sideToMove == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
    for (const Move& m : candidates) {
        const bool kingMove = (board.squares[m.from].type() == KING);
        UndoInfo undo = board.makeMove(m);

        int newKingSq = kingMove ? m.to : kingSq;
        // Defensive: if the king is not where it was expected, find it. This
        // should never fire — a king can never be captured in a legal search.
        if (newKingSq < 0 || board.squares[newKingSq].type() != KING ||
            board.squares[newKingSq].color() != sideToMove) {
            newKingSq = -1;
            for (int i = 0; i < 64; ++i) {
                if (board.squares[i].type() == KING && board.squares[i].color() == sideToMove) {
                    newKingSq = i;
                    break;
                }
            }
        }

        if (newKingSq >= 0 && !isSquareAttacked(board, newKingSq, oppColor)) {
            out.push_back(m);
        }
        board.unmakeMove(undo);
    }
}

// Pseudo-legal scratch buffer. generateLegalMoves does not recurse, so a single
// buffer per thread is safe, and reusing it keeps its capacity — which stops
// the per-node heap allocation that generating into a fresh MoveList caused.
static thread_local MoveList pseudoScratch;

void generateLegalMoves(Board& board, PieceColor sideToMove, bool includeCastling,
                        MoveList& out) {
    generatePseudoLegalMoves(board, sideToMove, includeCastling, pseudoScratch);
    filterLegal(board, sideToMove, pseudoScratch, out);
}

MoveList generateLegalMoves(Board& board, PieceColor sideToMove, bool includeCastling) {
    MoveList legal;
    generateLegalMoves(board, sideToMove, includeCastling, legal);
    return legal;
}

// const overload, for callers that do not own a mutable board (the GUI, the
// evaluation's in-check mobility path). It pays for one board copy; the
// mutable overload above, which the search uses at every node, does not.
MoveList generateLegalMoves(const Board& board, PieceColor sideToMove, bool includeCastling) {
    Board scratch = board.copyForSearch();
    MoveList legal;
    generateLegalMoves(scratch, sideToMove, includeCastling, legal);
    return legal;
}
