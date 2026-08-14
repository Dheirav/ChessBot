#include "movegen.hpp"
#include "move_lookup.hpp"
#include "piece.hpp"
#include "legal_move_validator.hpp"
#include "bitboard_attacks.hpp"
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

// The legality filter, shared by both entry points below (PLAN.md 5.5).
//
// It used to make every candidate move, test whether the king was attacked, and
// unmake it. That question is the single largest cost in the search — 80.9
// million isSquareAttacked() calls, about 40% of runtime together with move
// generation (profiled 2026-08-15) — and almost all of it is redundant. Whether
// a move exposes the king depends only on where the king is, which enemy pieces
// give check, and which of our pieces stand between the king and an enemy
// slider. Those are three facts about the *position*, not about each move, so
// they are computed once and every candidate is answered with a bit test.
//
// The three classic ways to get this wrong are avoided rather than solved:
//
//   - **King moves** are tested against an occupancy with the king removed.
//     Otherwise a king fleeing *along* a checking slider's ray looks safe,
//     because its own body still blocks the ray it stands on.
//   - **En passant** keeps the old make/unmake test. It is the notorious case —
//     the captured pawn vacates a square it was never standing on, which can
//     open a rank onto the king — and it is rare enough that testing it the slow
//     way costs nothing measurable. Cleverness here buys no speed and has cost
//     other engines a great deal of correctness.
//   - **Castling** is already fully verified by the pseudo-legal generator,
//     which requires the king's origin, transit and destination squares to be
//     unattacked. It arrives here as an ordinary king move and is re-checked,
//     which is harmless.
//
// Verified against the implementation it replaces over 609 115 positions from
// random games seeded on the four perft positions — kiwipete included, which
// exists to exercise exactly these cases — with zero disagreements, before the
// old path was deleted. `test-perft` guards it permanently.
static void filterLegal(Board& board, PieceColor sideToMove,
                        const MoveList& candidates, MoveList& out) {
    out.clear();
    out.reserve(candidates.size());

    const BitboardState st = toBitboardState(board);
    const BitboardColor us = (sideToMove == COLOR_WHITE) ? BB_WHITE : BB_BLACK;
    const int ksq = kingSquare(st, us);
    if (ksq < 0) {                 // no king: only reachable from a malformed FEN
        for (const Move& m : candidates) out.push_back(m);
        return;
    }

    const Bitboard checkersBB = checkers(st, us);
    const Bitboard pinned     = blockersForKing(st, us);
    const int numCheckers     = popcount(checkersBB);

    // In single check a non-king move must capture the checker or interpose on
    // the line between it and the king. In double check neither helps, and only
    // the king may move.
    Bitboard resolveMask = ~0ULL;
    if (numCheckers == 1) {
        const int checkerSq = lsb(checkersBB);
        resolveMask = checkersBB | betweenSquares(ksq, checkerSq);
    }

    const Bitboard occWithoutKing = st.occupancyAll & ~(1ULL << ksq);
    const Bitboard enemyOcc = (us == BB_WHITE) ? st.occupancyBlack : st.occupancyWhite;
    const PieceColor oppColor = (sideToMove == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;

    for (const Move& m : candidates) {
        bool legal;
        if (m.flag == EN_PASSANT) {
            // Deliberately the slow path — see the note above.
            UndoInfo undo = board.makeMove(m);
            legal = !isSquareAttacked(board, ksq, oppColor);
            board.unmakeMove(undo);
        } else if (m.from == ksq) {
            // attackersTo() reports attackers of both colours; only the enemy's
            // matter. A piece captured on m.to cannot defend m.to, and a blocker
            // standing on m.to never stops a ray from reaching m.to itself, so
            // the occupancy needs no further adjustment.
            legal = (attackersTo(st, m.to, occWithoutKing) & enemyOcc) == 0;
        } else if (numCheckers > 1) {
            legal = false;
        } else {
            legal = testBit(resolveMask, m.to);
            if (legal && testBit(pinned, m.from)) {
                legal = testBit(lineThrough(ksq, m.from), m.to);
            }
        }
        if (legal) out.push_back(m);
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
