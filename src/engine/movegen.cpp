#include "movegen.hpp"
#include "move_lookup.hpp"
#include "piece.hpp"
#include <cctype>
#include <iostream>

// Helper for en passant target index
static int getEnPassantIdx(const Board& board) {
    if (board.enPassantTarget != "-" && board.enPassantTarget.length() == 2) {
        int file = board.enPassantTarget[0] - 'a';
        int rank = board.enPassantTarget[1] - '1';
        return Board::get1DIndex(file, rank);
    }
    return -1;
}


// Optimized: Directly check if a square is attacked by any piece of byColor
static bool isSquareAttacked(const Board& board, int sq, PieceColor byColor) {
    int x = sq % 8, y = sq / 8;
    // Pawn attacks
    if (byColor == COLOR_WHITE) {
        if (y > 0) {
            if (x > 0 && board.squares[(y-1)*8 + (x-1)].type() == PAWN && board.squares[(y-1)*8 + (x-1)].color() == byColor) return true;
            if (x < 7 && board.squares[(y-1)*8 + (x+1)].type() == PAWN && board.squares[(y-1)*8 + (x+1)].color() == byColor) return true;
        }
    } else {
        if (y < 7) {
            if (x > 0 && board.squares[(y+1)*8 + (x-1)].type() == PAWN && board.squares[(y+1)*8 + (x-1)].color() == byColor) return true;
            if (x < 7 && board.squares[(y+1)*8 + (x+1)].type() == PAWN && board.squares[(y+1)*8 + (x+1)].color() == byColor) return true;
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

MoveList generateMoves(const Board& board, PieceColor sideToMove, bool includeCastling) {
    MoveList moves;
    int enPassantIdx = getEnPassantIdx(board);

    int kingSq = -1;
    for (int i = 0; i < 64; ++i) {
        if (board.squares[i].type() == KING && board.squares[i].color() == sideToMove) {
            kingSq = i;
            break;
        }
    }
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
                    bool isPromotion = (sideToMove == COLOR_WHITE && toRank == 7) || (sideToMove == COLOR_BLACK && toRank == 0);
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
                    if ((sideToMove == COLOR_WHITE && board.castlingRights.find('K') != std::string::npos) ||
                        (sideToMove == COLOR_BLACK && board.castlingRights.find('k') != std::string::npos)) {
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
                    if ((sideToMove == COLOR_WHITE && board.castlingRights.find('Q') != std::string::npos) ||
                        (sideToMove == COLOR_BLACK && board.castlingRights.find('q') != std::string::npos)) {
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

    // --- Filter out illegal moves that leave king in check ---
    MoveList legalMoves;
    for (const Move& m : moves) {
        Board boardCopy = board;
        boardCopy.makeMove(m);
        int newKingSq = -1;
        // Check if the move is a king move by looking at the piece type at the source square in the original board
        if (board.squares[m.from].type() == KING) {
            newKingSq = m.to;
        } else {
            newKingSq = kingSq;
            // If the king was captured (shouldn't happen), skip
            if (boardCopy.squares[newKingSq].type() != KING || boardCopy.squares[newKingSq].color() != sideToMove) {
                // Try to find the king (shouldn't be needed)
                for (int i = 0; i < 64; ++i) {
                    if (boardCopy.squares[i].type() == KING && boardCopy.squares[i].color() == sideToMove) {
                        newKingSq = i;
                        break;
                    }
                }
            }
        }
        PieceColor oppColor = (sideToMove == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
        if (!isSquareAttacked(boardCopy, newKingSq, oppColor)) {
            legalMoves.push_back(m);
        }
    }
    return legalMoves;
}

// Overload for default includeCastling=true
MoveList generateMoves(const Board& board, PieceColor sideToMove) {
    return generateMoves(board, sideToMove, true);
}