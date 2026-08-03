

#include "evaluation.hpp"
#include "board.hpp"
#include "movegen.hpp" // For generateMoves
#include <algorithm>
#include <cmath>
#include <vector>

// Piece values indexed by PieceType (NONE=0, KING=1, PAWN=2, KNIGHT=3, BISHOP=4, ROOK=5, QUEEN=6)
const int pieceValues[] = { 0, 20000, 100, 325, 335, 525, 950 };

// Capture bonus values - extra points for capturing these pieces
const int captureBonus[] = { 0, 10000, 50, 100, 110, 200, 400 };

// Threat bonus - bonus for threatening to capture valuable pieces
const int threatBonus[] = { 0, 500, 10, 25, 30, 50, 100 };

// Piece-square tables (centipawns), white perspective, index 0 = a8 (matches board indexing).
// Standard simplified evaluation tables. Black uses the vertically mirrored table (sq ^ 56).

// Pawn
static const int PST_PAWN[64] = {
      0,  0,  0,  0,  0,  0,  0,  0,
     50, 50, 50, 50, 50, 50, 50, 50,
     10, 10, 20, 30, 30, 20, 10, 10,
      5,  5, 10, 25, 25, 10,  5,  5,
      0,  0,  0, 20, 20,  0,  0,  0,
      5, -5,-10,  0,  0,-10, -5,  5,
      5, 10, 10,-20,-20, 10, 10,  5,
      0,  0,  0,  0,  0,  0,  0,  0
};

// Knight
static const int PST_KNIGHT[64] = {
    -50,-40,-30,-30,-30,-30,-40,-50,
    -40,-20,  0,  0,  0,  0,-20,-40,
    -30,  0, 10, 15, 15, 10,  0,-30,
    -30,  5, 15, 20, 20, 15,  5,-30,
    -30,  0, 15, 20, 20, 15,  0,-30,
    -30,  5, 10, 15, 15, 10,  5,-30,
    -40,-20,  0,  5,  5,  0,-20,-40,
    -50,-40,-30,-30,-30,-30,-40,-50
};

// Bishop
static const int PST_BISHOP[64] = {
    -20,-10,-10,-10,-10,-10,-10,-20,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -10,  0,  5, 10, 10,  5,  0,-10,
    -10,  5,  5, 10, 10,  5,  5,-10,
    -10,  0, 10, 10, 10, 10,  0,-10,
    -10, 10, 10, 10, 10, 10, 10,-10,
    -10,  5,  0,  0,  0,  0,  5,-10,
    -20,-10,-10,-10,-10,-10,-10,-20
};

// Rook
static const int PST_ROOK[64] = {
      0,  0,  0,  0,  0,  0,  0,  0,
      5, 10, 10, 10, 10, 10, 10,  5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
      0,  0,  0,  5,  5,  0,  0,  0
};

// Queen
static const int PST_QUEEN[64] = {
    -20,-10,-10, -5, -5,-10,-10,-20,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -10,  0,  5,  5,  5,  5,  0,-10,
     -5,  0,  5,  5,  5,  5,  0, -5,
      0,  0,  5,  5,  5,  5,  0, -5,
    -10,  5,  5,  5,  5,  5,  0,-10,
    -10,  0,  5,  0,  0,  0,  0,-10,
    -20,-10,-10, -5, -5,-10,-10,-20
};

// King (middlegame)
static const int PST_KING_MG[64] = {
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -20,-30,-30,-40,-40,-30,-30,-20,
    -10,-20,-20,-20,-20,-20,-20,-10,
     20, 20,  0,  0,  0,  0, 20, 20,
     20, 30, 10,  0,  0, 10, 30, 20
};

// King (endgame)
static const int PST_KING_EG[64] = {
    -50,-40,-30,-20,-20,-30,-40,-50,
    -30,-20,-10,  0,  0,-10,-20,-30,
    -30,-10, 20, 30, 30, 20,-10,-30,
    -30,-10, 30, 40, 40, 30,-10,-30,
    -30,-10, 30, 40, 40, 30,-10,-30,
    -30,-10, 20, 30, 30, 20,-10,-30,
    -30,-30,  0,  0,  0,  0,-30,-30,
    -50,-30,-30,-30,-30,-30,-30,-50
};

// Piece-square value for non-king pieces, signed for color (positive = good for white)
static int getPST(int pieceType, int color, int sq) {
    const int* table = nullptr;
    switch (pieceType) {
        case PAWN:   table = PST_PAWN; break;
        case KNIGHT: table = PST_KNIGHT; break;
        case BISHOP: table = PST_BISHOP; break;
        case ROOK:   table = PST_ROOK; break;
        case QUEEN:  table = PST_QUEEN; break;
        default:     return 0;
    }
    if (color == COLOR_WHITE) return table[sq];
    return -table[sq ^ 56];
}

// Extern/static declarations for feature extraction
static bool isCenter(int idx);

// Helper: is square in center
static bool isCenter(int idx) {
    return idx == 27 || idx == 28 || idx == 35 || idx == 36;
}

// Returns a detailed breakdown of evaluation for logging
EvalDetails evaluate_details(const Board& board) {
    EvalDetails e{};
    // Copy feature extraction logic from evaluate()
    int materialScore = 0;
    int mobilityScore = 0;
    int kingSafetyScore = 0;
    int centerControlScore = 0;
    int bishopPairBonus = 0;
    int doubledPawnPenalty = 0, isolatedPawnPenalty = 0, passedPawnBonus = 0, backwardPawnPenalty = 0, connectedPawnBonus = 0, pawnChainBonus = 0;
    int rooksOpenFileBonus = 0, rooksSemiOpenFileBonus = 0, rooks7thRankBonus = 0;
    int pstScore = 0;
    int outpostBonus = 0;
    int trappedPiecePenalty = 0;
    int coordinationBonus = 0;
    int kingActivityBonus = 0;
    float gamePhaseFactor = 1.0f;
    float tempoBonus = 0.01f; 
    int threatScore = 0, undefendedPenalty = 0;
    int spaceScore = 0;
    int nnueEvalScore = 0;
    int repetitionPenalty = 0;
    int drawishPenalty = 0;

    int whiteMaterial = 0, blackMaterial = 0;
    int whitePawns = 0, blackPawns = 0;
    int whiteMobility = 0, blackMobility = 0;
    int whiteKingSafety = 0, blackKingSafety = 0;
    int whiteCenterControl = 0, blackCenterControl = 0;
    int whitePassedPawns = 0, blackPassedPawns = 0;
    int whiteDoubledPawns = 0, blackDoubledPawns = 0;
    int whiteIsolatedPawns = 0, blackIsolatedPawns = 0;
    int whiteBackwardPawns = 0, blackBackwardPawns = 0;
    int whiteConnectedPawns = 0, blackConnectedPawns = 0;
    int whitePawnChains = 0, blackPawnChains = 0;
    int whiteRooksOpenFile = 0, blackRooksOpenFile = 0;
    int whiteRooksSemiOpenFile = 0, blackRooksSemiOpenFile = 0;
    int whiteRooks7th = 0, blackRooks7th = 0;
    int whiteBishopPair = 0, blackBishopPair = 0;
    int whiteKingFile = -1, blackKingFile = -1;
    int whiteKingRank = -1, blackKingRank = -1;
    int pawnFiles[8] = {0};
    int whiteOutposts = 0, blackOutposts = 0;
    int whiteTrapped = 0, blackTrapped = 0;
    int whiteCoord = 0, blackCoord = 0;
    int whiteKingActivity = 0, blackKingActivity = 0;
    int whiteThreats = 0, blackThreats = 0;
    int whiteUndefended = 0, blackUndefended = 0;
    int whiteSpace = 0, blackSpace = 0;
    int whiteDrawish = 0, blackDrawish = 0;
    int whiteBishopCount = 0, blackBishopCount = 0;

    // --- Feature extraction logic (copied from evaluate) ---
    for (int i = 0; i < 64; ++i) {
        const Piece& p = board.squares[i];
        if (p.type() == NONE) continue;
        int file = i % 8, rank = i / 8;
        int color = p.color();
        materialScore += (color == COLOR_WHITE ? 1 : -1) * pieceValues[p.type()];
        if (p.type() != KING) {
            pstScore += getPST(p.type(), color, i);
        }
        if (isCenter(i)) {
            if (color == COLOR_WHITE) whiteCenterControl++;
            else blackCenterControl++;
        }
        if (color == COLOR_WHITE) {
            whiteMaterial += pieceValues[p.type()];
            if (p.type() == PAWN) {
                whitePawns++;
                pawnFiles[file]++;
                if (file > 0 && board.squares[i-1].type() == PAWN && board.squares[i-1].color() == COLOR_WHITE) whiteConnectedPawns++;
                if (file < 7 && board.squares[i+1].type() == PAWN && board.squares[i+1].color() == COLOR_WHITE) whiteConnectedPawns++;
                bool passed = true;
                for (int r = rank - 1; r >= 0; --r) {
                    for (int df = -1; df <= 1; ++df) {
                        int f2 = file + df;
                        if (f2 < 0 || f2 > 7) continue;
                        int idx = r * 8 + f2;
                        if (board.squares[idx].type() == PAWN && board.squares[idx].color() == COLOR_BLACK) passed = false;
                    }
                }
                if (passed) whitePassedPawns++;
                for (int r = rank + 1; r < 8; ++r) {
                    int idx = r * 8 + file;
                    if (board.squares[idx].type() == PAWN && board.squares[idx].color() == COLOR_WHITE) whiteDoubledPawns++;
                }
                bool isolated = true;
                for (int df = -1; df <= 1; ++df) {
                    if (df == 0) continue;
                    int f2 = file + df;
                    if (f2 < 0 || f2 > 7) continue;
                    for (int r = 0; r < 8; ++r) {
                        int idx = r * 8 + f2;
                        if (board.squares[idx].type() == PAWN && board.squares[idx].color() == COLOR_WHITE) isolated = false;
                    }
                }
                if (isolated) whiteIsolatedPawns++;
                bool backward = true;
                for (int df = -1; df <= 1; ++df) {
                    int f2 = file + df;
                    if (f2 < 0 || f2 > 7) continue;
                    for (int r = rank + 1; r < 8; ++r) {
                        int idx = r * 8 + f2;
                        if (board.squares[idx].type() == PAWN && board.squares[idx].color() == COLOR_WHITE) backward = false;
                    }
                }
                if (backward) whiteBackwardPawns++;
                if ((file > 0 && rank < 7 && board.squares[(rank+1)*8+file-1].type() == PAWN && board.squares[(rank+1)*8+file-1].color() == COLOR_WHITE) ||
                    (file < 7 && rank < 7 && board.squares[(rank+1)*8+file+1].type() == PAWN && board.squares[(rank+1)*8+file+1].color() == COLOR_WHITE))
                    whitePawnChains++;
            }
            if (p.type() == KING) {
                whiteKingFile = file;
                whiteKingRank = rank;
            }
            if ((p.type() == KNIGHT || p.type() == BISHOP) && rank <= 3) {
                bool protectedByPawn = false;
                if ((file > 0 && rank < 7 && board.squares[(rank+1)*8+file-1].type() == PAWN && board.squares[(rank+1)*8+file-1].color() == COLOR_WHITE) ||
                    (file < 7 && rank < 7 && board.squares[(rank+1)*8+file+1].type() == PAWN && board.squares[(rank+1)*8+file+1].color() == COLOR_WHITE))
                    protectedByPawn = true;
                if (protectedByPawn) whiteOutposts++;
            }
            if ((p.type() == ROOK || p.type() == BISHOP) && (file == 0 || file == 7 || rank == 0 || rank == 7)) whiteTrapped++;
            if (p.type() == ROOK) {
                bool openFile = true, semiOpen = true;
                for (int r = 0; r < 8; ++r) {
                    int idx = r * 8 + file;
                    if (board.squares[idx].type() == PAWN) {
                        openFile = false;
                        if (board.squares[idx].color() == COLOR_WHITE) semiOpen = false;
                    }
                }
                if (openFile) whiteRooksOpenFile++;
                else if (semiOpen) whiteRooksSemiOpenFile++;
                if (rank == 1) whiteRooks7th++;
            }
            if (p.type() == BISHOP) whiteBishopCount++;
            for (int j = 0; j < 64; ++j) {
                if (i == j) continue;
                if (board.squares[j].type() != NONE && board.squares[j].color() == COLOR_WHITE) {
                    if (std::abs((i%8)-(j%8)) <= 1 && std::abs((i/8)-(j/8)) <= 1) whiteCoord++;
                }
            }
            if (p.type() == KING && whiteMaterial + blackMaterial - pieceValues[KING]*2 < 2000) {
                whiteKingActivity += (int)(4 - std::abs(file - 3.5) - std::abs(rank - 3.5));
            }
        } else {
            blackMaterial += pieceValues[p.type()];
            if (p.type() == PAWN) {
                blackPawns++;
                pawnFiles[file]--;
                if (file > 0 && board.squares[i-1].type() == PAWN && board.squares[i-1].color() == COLOR_BLACK) blackConnectedPawns++;
                if (file < 7 && board.squares[i+1].type() == PAWN && board.squares[i+1].color() == COLOR_BLACK) blackConnectedPawns++;
                bool passed = true;
                for (int r = rank + 1; r < 8; ++r) {
                    for (int df = -1; df <= 1; ++df) {
                        int f2 = file + df;
                        if (f2 < 0 || f2 > 7) continue;
                        int idx = r * 8 + f2;
                        if (board.squares[idx].type() == PAWN && board.squares[idx].color() == COLOR_WHITE) passed = false;
                    }
                }
                if (passed) blackPassedPawns++;
                for (int r = rank - 1; r >= 0; --r) {
                    int idx = r * 8 + file;
                    if (board.squares[idx].type() == PAWN && board.squares[idx].color() == COLOR_BLACK) blackDoubledPawns++;
                }
                bool isolated = true;
                for (int df = -1; df <= 1; ++df) {
                    if (df == 0) continue;
                    int f2 = file + df;
                    if (f2 < 0 || f2 > 7) continue;
                    for (int r = 0; r < 8; ++r) {
                        int idx = r * 8 + f2;
                        if (board.squares[idx].type() == PAWN && board.squares[idx].color() == COLOR_BLACK) isolated = false;
                    }
                }
                if (isolated) blackIsolatedPawns++;
                bool backward = true;
                for (int df = -1; df <= 1; ++df) {
                    int f2 = file + df;
                    if (f2 < 0 || f2 > 7) continue;
                    for (int r = rank - 1; r >= 0; --r) {
                        int idx = r * 8 + file;
                        if (board.squares[idx].type() == PAWN && board.squares[idx].color() == COLOR_BLACK) backward = false;
                    }
                }
                if (backward) blackBackwardPawns++;
                if ((file > 0 && rank > 0 && board.squares[(rank-1)*8+file-1].type() == PAWN && board.squares[(rank-1)*8+file-1].color() == COLOR_BLACK) ||
                    (file < 7 && rank > 0 && board.squares[(rank-1)*8+file+1].type() == PAWN && board.squares[(rank-1)*8+file+1].color() == COLOR_BLACK))
                    blackPawnChains++;
            }
            if (p.type() == KING) {
                blackKingFile = file;
                blackKingRank = rank;
            }
            if ((p.type() == KNIGHT || p.type() == BISHOP) && rank >= 4) {
                bool protectedByPawn = false;
                if ((file > 0 && rank > 0 && board.squares[(rank-1)*8+file-1].type() == PAWN && board.squares[(rank-1)*8+file-1].color() == COLOR_BLACK) ||
                    (file < 7 && rank > 0 && board.squares[(rank-1)*8+file+1].type() == PAWN && board.squares[(rank-1)*8+file+1].color() == COLOR_BLACK))
                    protectedByPawn = true;
                if (protectedByPawn) blackOutposts++;
            }
            if ((p.type() == ROOK || p.type() == BISHOP) && (file == 0 || file == 7 || rank == 0 || rank == 7)) blackTrapped++;
            if (p.type() == ROOK) {
                bool openFile = true, semiOpen = true;
                for (int r = 0; r < 8; ++r) {
                    int idx = r * 8 + file;
                    if (board.squares[idx].type() == PAWN) {
                        openFile = false;
                        if (board.squares[idx].color() == COLOR_BLACK) semiOpen = false;
                    }
                }
                if (openFile) blackRooksOpenFile++;
                else if (semiOpen) blackRooksSemiOpenFile++;
                if (rank == 6) blackRooks7th++;
            }
            if (p.type() == BISHOP) blackBishopCount++;
            for (int j = 0; j < 64; ++j) {
                if (i == j) continue;
                if (board.squares[j].type() != NONE && board.squares[j].color() == COLOR_BLACK) {
                    if (std::abs((i%8)-(j%8)) <= 1 && std::abs((i/8)-(j/8)) <= 1) blackCoord++;
                }
            }
            if (p.type() == KING && whiteMaterial + blackMaterial - pieceValues[KING]*2 < 2000) {
                blackKingActivity += (int)(4 - std::abs(file - 3.5) - std::abs(rank - 3.5));
            }
        }
    }

    // Bishop pair
    if (whiteBishopCount >= 2) {
        whiteBishopPair = 1;
        bishopPairBonus += 50;
    }
    if (blackBishopCount >= 2) {
        blackBishopPair = 1;
        bishopPairBonus -= 50;
    }

    // Pawn structure: doubled, isolated, backward, connected, passed pawns and pawn chains
    doubledPawnPenalty = -10 * (whiteDoubledPawns - blackDoubledPawns);
    isolatedPawnPenalty = -10 * (whiteIsolatedPawns - blackIsolatedPawns);
    backwardPawnPenalty = -8 * (whiteBackwardPawns - blackBackwardPawns);
    connectedPawnBonus = 5 * (whiteConnectedPawns - blackConnectedPawns);
    passedPawnBonus = 20 * (whitePassedPawns - blackPassedPawns);
    pawnChainBonus = 5 * (whitePawnChains - blackPawnChains);

    // Mobility
    whiteMobility = (int)generateMoves(board, COLOR_WHITE).size();
    blackMobility = (int)generateMoves(board, COLOR_BLACK).size();
    mobilityScore = 2 * (whiteMobility - blackMobility);

    // King safety
    if (whiteKingFile != -1 && whiteKingRank != -1) {
        int distFromCenter = std::abs(whiteKingFile - 3) + std::abs(whiteKingRank - 3);
        whiteKingSafety = -distFromCenter * 4; // Reduced from 5 to 4
        if (whiteKingRank == 7) {
            for (int df = -1; df <= 1; ++df) {
                int f = whiteKingFile + df;
                if (f >= 0 && f < 8) {
                    int idx = 6 * 8 + f;
                    if (board.squares[idx].type() == PAWN && board.squares[idx].color() == COLOR_WHITE) whiteKingSafety += 8; // Reduced from 10 to 8
                }
            }
        }
    }
    if (blackKingFile != -1 && blackKingRank != -1) {
        int distFromCenter = std::abs(blackKingFile - 3) + std::abs(blackKingRank - 3);
        blackKingSafety = -distFromCenter * 4; // Reduced from 5 to 4
        if (blackKingRank == 0) {
            for (int df = -1; df <= 1; ++df) {
                int f = blackKingFile + df;
                if (f >= 0 && f < 8) {
                    int idx = 1 * 8 + f;
                    if (board.squares[idx].type() == PAWN && board.squares[idx].color() == COLOR_BLACK) blackKingSafety += 8; // Reduced from 10 to 8
                }
            }
        }
    }
    kingSafetyScore = whiteKingSafety - blackKingSafety;

    // Center control
    int centerSquares[4] = { 27, 28, 35, 36 };
    for (int i = 0; i < 4; ++i) {
        const Piece& p = board.squares[centerSquares[i]];
        if (p.type() != NONE) {
            if (p.color() == COLOR_WHITE) centerControlScore += 5;
            else centerControlScore -= 5;
        }
    }

    // Rooks on open/semi-open files and 7th rank
    for (int f = 0; f < 8; ++f) {
        bool openFile = (pawnFiles[f] == 0);
        bool semiOpenWhite = (pawnFiles[f] >= 0);
        bool semiOpenBlack = (pawnFiles[f] <= 0);
        for (int r = 0; r < 8; ++r) {
            int idx = r * 8 + f;
            const Piece& p = board.squares[idx];
            if (p.type() == ROOK) {
                if (p.color() == COLOR_WHITE) {
                    if (openFile) rooksOpenFileBonus += 10;
                    else if (semiOpenWhite) rooksSemiOpenFileBonus += 5;
                    if (r == 1) rooks7thRankBonus += 10;
                } else {
                    if (openFile) rooksOpenFileBonus -= 10;
                    else if (semiOpenBlack) rooksSemiOpenFileBonus -= 5;
                    if (r == 6) rooks7thRankBonus -= 10;
                }
            }
        }
    }

    // Outposts
    for (int i = 0; i < 64; ++i) {
        const Piece& p = board.squares[i];
        if (p.type() == KNIGHT || p.type() == BISHOP) {
            int rank = i / 8, file = i % 8;
            if (p.color() == COLOR_WHITE && rank <= 3) {
                if ((file > 0 && board.squares[i+7].type() == PAWN && board.squares[i+7].color() == COLOR_WHITE) ||
                    (file < 7 && board.squares[i+9].type() == PAWN && board.squares[i+9].color() == COLOR_WHITE))
                    outpostBonus += 10;
            }
            if (p.color() == COLOR_BLACK && rank >= 4) {
                if ((file > 0 && board.squares[i-9].type() == PAWN && board.squares[i-9].color() == COLOR_BLACK) ||
                    (file < 7 && board.squares[i-7].type() == PAWN && board.squares[i-7].color() == COLOR_BLACK))
                    outpostBonus -= 10;
            }
        }
    }

    // Trapped pieces
    for (int i = 0; i < 64; ++i) {
        const Piece& p = board.squares[i];
        if ((p.type() == ROOK || p.type() == BISHOP) && (i % 8 == 0 || i % 8 == 7 || i / 8 == 0 || i / 8 == 7)) {
            if (p.color() == COLOR_WHITE) trappedPiecePenalty -= 5;
            else trappedPiecePenalty += 5;
        }
    }

    // King activity
    int totalMaterial = whiteMaterial + blackMaterial - pieceValues[KING]*2;
    if (totalMaterial < 2000) {
        if (whiteKingFile != -1 && whiteKingRank != -1) kingActivityBonus += (4 - std::abs(whiteKingFile - 3.5) - std::abs(whiteKingRank - 3.5)) * 5;
        if (blackKingFile != -1 && blackKingRank != -1) kingActivityBonus -= (4 - std::abs(blackKingFile - 3.5) - std::abs(blackKingRank - 3.5)) * 5;
    }

    // Game phase scaling
    gamePhaseFactor = std::min(1.0f, totalMaterial / 3200.0f);

    // King piece-square tables, blended between middlegame and endgame by game phase
    if (whiteKingFile != -1 && whiteKingRank != -1) {
        int ks = Board::get1DIndex(whiteKingFile, whiteKingRank);
        pstScore += (int)(PST_KING_MG[ks] * gamePhaseFactor + PST_KING_EG[ks] * (1.0f - gamePhaseFactor));
    }
    if (blackKingFile != -1 && blackKingRank != -1) {
        int ks = Board::get1DIndex(blackKingFile, blackKingRank) ^ 56;
        pstScore -= (int)(PST_KING_MG[ks] * gamePhaseFactor + PST_KING_EG[ks] * (1.0f - gamePhaseFactor));
    }

    // Enhanced Threats and Captures - much more aggressive evaluation
    int captureIncentive = 0;
    int hangingPiecePenalty = 0;

    // Real attack test: same geometry rules as isSquareAttacked, but for a specific
    // from-square attacker, so sliding pieces are blocked by intervening pieces.
    auto attacks = [&board](int from, int to) -> bool {
        const Piece& a = board.squares[from];
        if (a.type() == NONE) return false;
        int fx = from % 8, fy = from / 8;
        int tx = to % 8, ty = to / 8;
        int dx = std::abs(tx - fx), dy = std::abs(ty - fy);
        switch (a.type()) {
            case PAWN: {
                int dir = (a.color() == COLOR_WHITE) ? -1 : 1;
                return (ty == fy + dir) && (std::abs(tx - fx) == 1);
            }
            case KNIGHT:
                return (dx == 2 && dy == 1) || (dx == 1 && dy == 2);
            case KING:
                return dx <= 1 && dy <= 1;
            case ROOK:
                if (dx != 0 && dy != 0) return false;
                break;
            case BISHOP:
                if (dx != dy) return false;
                break;
            case QUEEN:
                if (dx != 0 && dy != 0 && dx != dy) return false;
                break;
            default:
                return false;
        }
        int sx = (tx > fx) ? 1 : (tx < fx) ? -1 : 0;
        int sy = (ty > fy) ? 1 : (ty < fy) ? -1 : 0;
        for (int x = fx + sx, y = fy + sy; x != tx || y != ty; x += sx, y += sy) {
            if (board.squares[y * 8 + x].type() != NONE) return false;
        }
        return true;
    };

    for (int i = 0; i < 64; ++i) {
        const Piece& attacker = board.squares[i];
        if (attacker.type() == NONE) continue;

        // Threat bonus for each enemy piece this piece really attacks
        for (int j = 0; j < 64; ++j) {
            if (i == j) continue;
            const Piece& target = board.squares[j];
            if (target.type() == NONE || attacker.color() == target.color()) continue;
            // The king is handled by the search's mate scores, not the static threats.
            if (target.type() == KING) continue;
            if (!attacks(i, j)) continue;

            int threatValue = ::threatBonus[target.type()];
            if (attacker.color() == COLOR_WHITE) {
                whiteThreats += threatValue;
            } else {
                blackThreats += threatValue;
            }

            // Extra bonus for attacking more valuable pieces with less valuable pieces
            if (pieceValues[target.type()] > pieceValues[attacker.type()]) {
                int valueGap = pieceValues[target.type()] - pieceValues[attacker.type()];
                if (attacker.color() == COLOR_WHITE) {
                    captureIncentive += valueGap / 10; // 10% of value difference
                } else {
                    captureIncentive -= valueGap / 10;
                }
            }
        }

        // Penalize pieces that are really attacked but not really defended.
        // isSquareAttacked is sliding-aware and uses the correct pawn direction.
        // The king is never a hangable piece; checkmate is scored by the search.
        if (attacker.type() == KING) continue;
        PieceColor enemyColor = (attacker.color() == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
        bool isAttacked = board.isSquareAttacked(i, enemyColor);
        bool isDefended = board.isSquareAttacked(i, attacker.color());
        if (isAttacked && !isDefended) {
            int hangingPenalty = pieceValues[attacker.type()];
            if (attacker.color() == COLOR_WHITE) {
                hangingPiecePenalty -= hangingPenalty;
            } else {
                hangingPiecePenalty += hangingPenalty;
            }
        }
    }
    
    threatScore = (whiteThreats - blackThreats) + captureIncentive + hangingPiecePenalty;

    // Undefended pieces
    for (int i = 0; i < 64; ++i) {
        const Piece& p = board.squares[i];
        if (p.type() == NONE) continue;
        bool defended = false;
        for (int j = 0; j < 64; ++j) {
            if (i == j) continue;
            const Piece& q = board.squares[j];
            if (q.type() == NONE) continue;
            if (p.color() == q.color()) {
                if (std::abs((i%8)-(j%8)) <= 1 && std::abs((i/8)-(j/8)) <= 1) defended = true;
            }
        }
        if (!defended) {
            if (p.color() == COLOR_WHITE) whiteUndefended++;
            else blackUndefended++;
        }
    }
    undefendedPenalty = -5 * whiteUndefended + 5 * blackUndefended;

    // Space advantage
    for (int i = 0; i < 64; ++i) {
        int rank = i / 8;
        const Piece& p = board.squares[i];
        if (p.type() == NONE) continue;
        if (p.color() == COLOR_WHITE && rank < 4) whiteSpace++;
        if (p.color() == COLOR_BLACK && rank > 3) blackSpace++;
    }
    spaceScore = whiteSpace - blackSpace;

    // Drawishness
    if (whiteMaterial == 0 && blackMaterial == 0 && whitePawns == 0 && blackPawns == 0) {
        whiteDrawish = 1;
        blackDrawish = 1;
    }

    // NNUE stub
    nnueEvalScore = 0;

    // Repetition/drawish material stub
    repetitionPenalty = 0;
    drawishPenalty = 0;

    // Assign to EvalDetails
    e.total = materialScore + mobilityScore + kingSafetyScore + centerControlScore + bishopPairBonus + doubledPawnPenalty + isolatedPawnPenalty + passedPawnBonus + backwardPawnPenalty + connectedPawnBonus + pawnChainBonus + rooksOpenFileBonus + rooksSemiOpenFileBonus + rooks7thRankBonus + pstScore + outpostBonus + trappedPiecePenalty + coordinationBonus + kingActivityBonus + (int)(gamePhaseFactor * 1.5f) + tempoBonus + threatScore + undefendedPenalty + spaceScore + nnueEvalScore + repetitionPenalty + drawishPenalty;
    e.material = materialScore;
    e.mobility = mobilityScore;
    e.kingSafety = kingSafetyScore;
    e.centerControl = centerControlScore;
    e.bishopPair = bishopPairBonus;
    e.doubledPawn = doubledPawnPenalty;
    e.isolatedPawn = isolatedPawnPenalty;
    e.passedPawn = passedPawnBonus;
    e.backwardPawn = backwardPawnPenalty;
    e.connectedPawn = connectedPawnBonus;
    e.pawnChain = pawnChainBonus;
    e.rooksOpenFile = rooksOpenFileBonus;
    e.rooksSemiOpenFile = rooksSemiOpenFileBonus;
    e.rooks7thRank = rooks7thRankBonus;
    e.pst = pstScore;
    e.outpost = outpostBonus;
    e.trapped = trappedPiecePenalty;
    e.coordination = coordinationBonus;
    e.kingActivity = kingActivityBonus;
    e.threats = threatScore;
    e.undefended = undefendedPenalty;
    e.space = spaceScore;
    e.drawish = drawishPenalty + 30 * (whiteDrawish - blackDrawish) + 30 * (whiteBishopPair - blackBishopPair);
    return e;
}

// Simple evaluate function that returns just the total score
int evaluate(const Board& board) {
    EvalDetails details = evaluate_details(board);
    return details.total;
}
