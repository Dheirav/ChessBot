

#include "evaluation.hpp"
#include "board.hpp"
#include "movegen.hpp" // For generateMoves
#include <algorithm>
#include <cmath>
#include <vector>

// Extern/static declarations for feature extraction
extern const int pieceValues[];
static bool isCenter(int idx);

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
        pstScore += pst[p.type()][color][i];
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

    // Mobility
    whiteMobility = (int)generateMoves(board, COLOR_WHITE).size();
    blackMobility = (int)generateMoves(board, COLOR_BLACK).size();
    mobilityScore = 2 * (whiteMobility - blackMobility);

    // King safety
    if (whiteKingFile != -1 && whiteKingRank != -1) {
        int distFromCenter = std::abs(whiteKingFile - 3) + std::abs(whiteKingRank - 3);
        whiteKingSafety = -distFromCenter * 5;
        if (whiteKingRank == 7) {
            for (int df = -1; df <= 1; ++df) {
                int f = whiteKingFile + df;
                if (f >= 0 && f < 8) {
                    int idx = 6 * 8 + f;
                    if (board.squares[idx].type() == PAWN && board.squares[idx].color() == COLOR_WHITE) whiteKingSafety += 10;
                }
            }
        }
    }
    if (blackKingFile != -1 && blackKingRank != -1) {
        int distFromCenter = std::abs(blackKingFile - 3) + std::abs(blackKingRank - 3);
        blackKingSafety = -distFromCenter * 5;
        if (blackKingRank == 0) {
            for (int df = -1; df <= 1; ++df) {
                int f = blackKingFile + df;
                if (f >= 0 && f < 8) {
                    int idx = 1 * 8 + f;
                    if (board.squares[idx].type() == PAWN && board.squares[idx].color() == COLOR_BLACK) blackKingSafety += 10;
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
                if ((file > 0 && board.squares[i-9].type() == PAWN && board.squares[i-9].color() == COLOR_WHITE) ||
                    (file < 7 && board.squares[i-7].type() == PAWN && board.squares[i-7].color() == COLOR_WHITE))
                    outpostBonus += 10;
            }
            if (p.color() == COLOR_BLACK && rank >= 4) {
                if ((file > 0 && board.squares[i+7].type() == PAWN && board.squares[i+7].color() == COLOR_BLACK) ||
                    (file < 7 && board.squares[i+9].type() == PAWN && board.squares[i+9].color() == COLOR_BLACK))
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

    // Threats
    for (int i = 0; i < 64; ++i) {
        const Piece& p = board.squares[i];
        if (p.type() == NONE) continue;
        for (int j = 0; j < 64; ++j) {
            if (i == j) continue;
            const Piece& q = board.squares[j];
            if (q.type() == NONE) continue;
            if (p.color() != q.color() && pieceValues[q.type()] < pieceValues[p.type()]) {
                if (std::abs((i%8)-(j%8)) <= 1 && std::abs((i/8)-(j/8)) <= 1) {
                    if (p.color() == COLOR_WHITE) whiteThreats++;
                    else blackThreats++;
                }
            }
        }
    }
    threatScore = 5 * (whiteThreats - blackThreats);

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
#include "evaluation.hpp"
#include <algorithm>
#include "movegen.hpp"
#include <cmath>

// Piece values
const int pieceValues[] = { 01, 100, 320, 330, 500, 900, 20000 };
// Example piece-square tables (PST) for each piece type and color (simplified)
const int pst[7][2][64] = { { {0}, {0} } }; // Fill with real PSTs for best results

// Helper: is square in center
static bool isCenter(int idx) {
    return idx == 27 || idx == 28 || idx == 35 || idx == 36;
}

int evaluate(const Board& board) {
    int score = 0;
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

    // Material, king position, pawn structure, PST, coordination, outposts, trapped, king activity, threats, undefended, space, drawish
    // Center control, rook file/7th, bishop pair logic
    int whiteBishopCount = 0, blackBishopCount = 0;
    for (int i = 0; i < 64; ++i) {
        const Piece& p = board.squares[i];
        if (p.type() == NONE) continue;
        int file = i % 8, rank = i / 8;
        int color = p.color();
        materialScore += (color == COLOR_WHITE ? 1 : -1) * pieceValues[p.type()];
        pstScore += pst[p.type()][color][i];
        // Center control using isCenter helper
        if (isCenter(i)) {
            if (color == COLOR_WHITE) whiteCenterControl++;
            else blackCenterControl++;
        }
        if (color == COLOR_WHITE) {
            whiteMaterial += pieceValues[p.type()];
            if (p.type() == PAWN) {
                whitePawns++;
                pawnFiles[file]++;
                // Connected pawn
                if (file > 0 && board.squares[i-1].type() == PAWN && board.squares[i-1].color() == COLOR_WHITE) whiteConnectedPawns++;
                if (file < 7 && board.squares[i+1].type() == PAWN && board.squares[i+1].color() == COLOR_WHITE) whiteConnectedPawns++;
                // Passed pawn (no enemy pawns ahead on same file or adjacent files)
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
                // Doubled pawn
                for (int r = rank + 1; r < 8; ++r) {
                    int idx = r * 8 + file;
                    if (board.squares[idx].type() == PAWN && board.squares[idx].color() == COLOR_WHITE) whiteDoubledPawns++;
                }
                // Isolated pawn
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
                // Backward pawn (simple: no friendly pawn behind on adjacent files)
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
                // Pawn chain (simple: pawn diagonally behind)
                if ((file > 0 && rank < 7 && board.squares[(rank+1)*8+file-1].type() == PAWN && board.squares[(rank+1)*8+file-1].color() == COLOR_WHITE) ||
                    (file < 7 && rank < 7 && board.squares[(rank+1)*8+file+1].type() == PAWN && board.squares[(rank+1)*8+file+1].color() == COLOR_WHITE))
                    whitePawnChains++;
            }
            if (p.type() == KING) {
                whiteKingFile = file;
                whiteKingRank = rank;
            }
            // Outpost (knight/bishop on protected square in enemy territory)
            if ((p.type() == KNIGHT || p.type() == BISHOP) && rank <= 3) {
                bool protectedByPawn = false;
                if ((file > 0 && rank < 7 && board.squares[(rank+1)*8+file-1].type() == PAWN && board.squares[(rank+1)*8+file-1].color() == COLOR_WHITE) ||
                    (file < 7 && rank < 7 && board.squares[(rank+1)*8+file+1].type() == PAWN && board.squares[(rank+1)*8+file+1].color() == COLOR_WHITE))
                    protectedByPawn = true;
                if (protectedByPawn) whiteOutposts++;
            }
            // Trapped piece (rook/bishop on edge)
            if ((p.type() == ROOK || p.type() == BISHOP) && (file == 0 || file == 7 || rank == 0 || rank == 7)) whiteTrapped++;
            // Rooks on open/semi-open files and 7th rank
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
            // Bishop count for bishop pair
            if (p.type() == BISHOP) whiteBishopCount++;
            // Coordination: count friendly pieces protecting each other
            for (int j = 0; j < 64; ++j) {
                if (i == j) continue;
                if (board.squares[j].type() != NONE && board.squares[j].color() == COLOR_WHITE) {
                    if (std::abs((i%8)-(j%8)) <= 1 && std::abs((i/8)-(j/8)) <= 1) whiteCoord++;
                }
            }
            // King activity (distance from center, endgame)
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
                // Passed pawn
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
                // Doubled pawn
                for (int r = rank - 1; r >= 0; --r) {
                    int idx = r * 8 + file;
                    if (board.squares[idx].type() == PAWN && board.squares[idx].color() == COLOR_BLACK) blackDoubledPawns++;
                }
                // Isolated pawn
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
                // Backward pawn
                bool backward = true;
                for (int df = -1; df <= 1; ++df) {
                    int f2 = file + df;
                    if (f2 < 0 || f2 > 7) continue;
                    for (int r = rank - 1; r >= 0; --r) {
                        int idx = r * 8 + f2;
                        if (board.squares[idx].type() == PAWN && board.squares[idx].color() == COLOR_BLACK) backward = false;
                    }
                }
                if (backward) blackBackwardPawns++;
                // Pawn chain
                if ((file > 0 && rank > 0 && board.squares[(rank-1)*8+file-1].type() == PAWN && board.squares[(rank-1)*8+file-1].color() == COLOR_BLACK) ||
                    (file < 7 && rank > 0 && board.squares[(rank-1)*8+file+1].type() == PAWN && board.squares[(rank-1)*8+file+1].color() == COLOR_BLACK))
                    blackPawnChains++;
            }
            if (p.type() == KING) {
                blackKingFile = file;
                blackKingRank = rank;
            }
            // Outpost
            if ((p.type() == KNIGHT || p.type() == BISHOP) && rank >= 4) {
                bool protectedByPawn = false;
                if ((file > 0 && rank > 0 && board.squares[(rank-1)*8+file-1].type() == PAWN && board.squares[(rank-1)*8+file-1].color() == COLOR_BLACK) ||
                    (file < 7 && rank > 0 && board.squares[(rank-1)*8+file+1].type() == PAWN && board.squares[(rank-1)*8+file+1].color() == COLOR_BLACK))
                    protectedByPawn = true;
                if (protectedByPawn) blackOutposts++;
            }
            // Trapped piece
            if ((p.type() == ROOK || p.type() == BISHOP) && (file == 0 || file == 7 || rank == 0 || rank == 7)) blackTrapped++;
            // Rooks on open/semi-open files and 7th rank
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
            // Bishop count for bishop pair
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

        // --- Piece Safety: Penalize hanging pieces (attacked but not defended) ---
    int hangingPenalty = 0;
    for (int i = 0; i < 64; ++i) {
        const Piece& p = board.squares[i];
        if (p.type() == NONE) continue;
        bool attacked = false, defended = false;
        for (int j = 0; j < 64; ++j) {
            if (i == j) continue;
            const Piece& q = board.squares[j];
            if (q.type() == NONE) continue;
            // Attacked by opponent (simple: adjacent squares)
            if (p.color() != q.color() && std::abs((i%8)-(j%8)) <= 1 && std::abs((i/8)-(j/8)) <= 1)
                attacked = true;
            // Defended by friendly (simple: adjacent squares)
            if (p.color() == q.color() && std::abs((i%8)-(j%8)) <= 1 && std::abs((i/8)-(j/8)) <= 1)
                defended = true;
        }
        if (attacked && !defended) {
            int penalty = pieceValues[p.type()] * 2 / 3; // Penalize 2/3 the piece value
            if (p.color() == COLOR_WHITE) hangingPenalty -= penalty;
            else hangingPenalty += penalty;
        }
    }

    // --- Development: Reward minor pieces off the back rank in the opening ---
    int developmentBonus = 0;
    for (int i = 0; i < 64; ++i) {
        const Piece& p = board.squares[i];
        if (p.type() == KNIGHT || p.type() == BISHOP) {
            int rank = i / 8;
            if (p.color() == COLOR_WHITE && rank < 7) developmentBonus += 20;
            if (p.color() == COLOR_BLACK && rank > 0) developmentBonus -= 20;
        }
    }


    // Pawn structure: doubled/isolated/backward/connected/pawn chain/passed pawns
    for (int f = 0; f < 8; ++f) {
        if (pawnFiles[f] > 1) doubledPawnPenalty += (pawnFiles[f] - 1) * 10;
        if (pawnFiles[f] < -1) doubledPawnPenalty -= (-pawnFiles[f] - 1) * 10;
        // Isolated pawns
        if (pawnFiles[f] > 0) {
            if ((f == 0 || pawnFiles[f-1] == 0) && (f == 7 || pawnFiles[f+1] == 0)) isolatedPawnPenalty += 15;
        }
        if (pawnFiles[f] < 0) {
            if ((f == 0 || pawnFiles[f-1] == 0) && (f == 7 || pawnFiles[f+1] == 0)) isolatedPawnPenalty -= 15;
        }
        // Pawn chains (simple: 2+ connected pawns) -- reduce bonus
        if (pawnFiles[f] > 1) pawnChainBonus += 2 * (pawnFiles[f] - 1);
        if (pawnFiles[f] < -1) pawnChainBonus -= 2 * ((-pawnFiles[f]) - 1);
    }
    // Bishop pair (set variable, also keep bonus for compatibility)
    if (whiteBishopCount >= 2) {
        whiteBishopPair = 1;
        bishopPairBonus += 50;
    }
    if (blackBishopCount >= 2) {
        blackBishopPair = 1;
        bishopPairBonus -= 50;
    }

    // Mobility (number of legal moves)
    whiteMobility = (int)generateMoves(board, COLOR_WHITE).size();
    blackMobility = (int)generateMoves(board, COLOR_BLACK).size();
    mobilityScore = 2 * (whiteMobility - blackMobility);

    // King safety (distance from center, pawn shield)
    if (whiteKingFile != -1 && whiteKingRank != -1) {
        int distFromCenter = std::abs(whiteKingFile - 3) + std::abs(whiteKingRank - 3);
        whiteKingSafety = -distFromCenter * 5;
        // Pawn shield
        if (whiteKingRank == 7) {
            for (int df = -1; df <= 1; ++df) {
                int f = whiteKingFile + df;
                if (f >= 0 && f < 8) {
                    int idx = 6 * 8 + f;
                    if (board.squares[idx].type() == PAWN && board.squares[idx].color() == COLOR_WHITE) whiteKingSafety += 10;
                }
            }
        }
    }
    if (blackKingFile != -1 && blackKingRank != -1) {
        int distFromCenter = std::abs(blackKingFile - 3) + std::abs(blackKingRank - 3);
        blackKingSafety = -distFromCenter * 5;
        if (blackKingRank == 0) {
            for (int df = -1; df <= 1; ++df) {
                int f = blackKingFile + df;
                if (f >= 0 && f < 8) {
                    int idx = 1 * 8 + f;
                    if (board.squares[idx].type() == PAWN && board.squares[idx].color() == COLOR_BLACK) blackKingSafety += 10;
                }
            }
        }
    }
    kingSafetyScore = whiteKingSafety - blackKingSafety;

    // Center control (pieces on e4, d4, e5, d5)
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

    // Outposts (knights/bishops on protected squares in enemy territory)
    for (int i = 0; i < 64; ++i) {
        const Piece& p = board.squares[i];
        if (p.type() == KNIGHT || p.type() == BISHOP) {
            int rank = i / 8, file = i % 8;
            if (p.color() == COLOR_WHITE && rank <= 3) {
                // Supported by pawn
                if ((file > 0 && board.squares[i-9].type() == PAWN && board.squares[i-9].color() == COLOR_WHITE) ||
                    (file < 7 && board.squares[i-7].type() == PAWN && board.squares[i-7].color() == COLOR_WHITE))
                    outpostBonus += 10;
            }
            if (p.color() == COLOR_BLACK && rank >= 4) {
                if ((file > 0 && board.squares[i+7].type() == PAWN && board.squares[i+7].color() == COLOR_BLACK) ||
                    (file < 7 && board.squares[i+9].type() == PAWN && board.squares[i+9].color() == COLOR_BLACK))
                    outpostBonus -= 10;
            }
        }
    }

    // Trapped pieces (rooks/bishops with no safe moves)
    // (Simple: count pieces on edge squares)
    for (int i = 0; i < 64; ++i) {
        const Piece& p = board.squares[i];
        if ((p.type() == ROOK || p.type() == BISHOP) && (i % 8 == 0 || i % 8 == 7 || i / 8 == 0 || i / 8 == 7)) {
            if (p.color() == COLOR_WHITE) trappedPiecePenalty -= 5;
            else trappedPiecePenalty += 5;
        }
    }

    // King activity (endgame): encourage central king if little material
    int totalMaterial = whiteMaterial + blackMaterial - pieceValues[KING]*2;
    if (totalMaterial < 2000) {
        if (whiteKingFile != -1 && whiteKingRank != -1) kingActivityBonus += (4 - std::abs(whiteKingFile - 3.5) - std::abs(whiteKingRank - 3.5)) * 5;
        if (blackKingFile != -1 && blackKingRank != -1) kingActivityBonus -= (4 - std::abs(blackKingFile - 3.5) - std::abs(blackKingRank - 3.5)) * 5;
    }

    // Game phase scaling (simple: based on total material)
    gamePhaseFactor = std::min(1.0f, totalMaterial / 3200.0f);

    // Tempo bonus
    // (Assume white to move for now; adjust if you track side to move)
    score += tempoBonus;

    // Threats: count attacks on high-value pieces
    // (Simple: count if a piece is attacked by a lower-value piece)
    for (int i = 0; i < 64; ++i) {
        const Piece& p = board.squares[i];
        if (p.type() == NONE) continue;
        for (int j = 0; j < 64; ++j) {
            if (i == j) continue;
            const Piece& q = board.squares[j];
            if (q.type() == NONE) continue;
            if (p.color() != q.color() && pieceValues[q.type()] < pieceValues[p.type()]) {
                // If q attacks i (simple: adjacent)
                if (std::abs((i%8)-(j%8)) <= 1 && std::abs((i/8)-(j/8)) <= 1) {
                    if (p.color() == COLOR_WHITE) whiteThreats++;
                    else blackThreats++;
                }
            }
        }
    }
    threatScore = 5 * (whiteThreats - blackThreats);

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

    // Space advantage: count squares controlled in enemy half
    for (int i = 0; i < 64; ++i) {
        int rank = i / 8;
        const Piece& p = board.squares[i];
        if (p.type() == NONE) continue;
        if (p.color() == COLOR_WHITE && rank < 4) whiteSpace++;
        if (p.color() == COLOR_BLACK && rank > 3) blackSpace++;
    }
    spaceScore = whiteSpace - blackSpace;

    // Drawishness (simple: only kings left)
    if (whiteMaterial == 0 && blackMaterial == 0 && whitePawns == 0 && blackPawns == 0) {
        whiteDrawish = 1;
        blackDrawish = 1;
    }

    // NNUE stub (not implemented)
    nnueEvalScore = 0;

    // Repetition/drawish material stub (not implemented)
    repetitionPenalty = 0;
    drawishPenalty = 0;

    // Combine all features, including per-feature variables
    score += materialScore;
    score += mobilityScore;
    score += kingSafetyScore;
    score += centerControlScore;
    score += bishopPairBonus;
    score += doubledPawnPenalty;
    score += isolatedPawnPenalty;
    score += passedPawnBonus;
    score += backwardPawnPenalty;
    score += connectedPawnBonus;
    score += pawnChainBonus;
    score += rooksOpenFileBonus;
    score += rooksSemiOpenFileBonus;
    score += rooks7thRankBonus;
    score += pstScore;
    score += outpostBonus;
    score += trappedPiecePenalty;
    score += coordinationBonus;
    score += kingActivityBonus;
    score += (int)(gamePhaseFactor * 1.5f);
    score += tempoBonus;
    score += threatScore;
    score += undefendedPenalty;
    score += spaceScore;
    score += nnueEvalScore;
    score += repetitionPenalty;
    score += drawishPenalty;

    // Add per-feature variables with reasonable weights
    score += 10 * (whiteCenterControl - blackCenterControl);
    score += 10 * (whiteRooksOpenFile - blackRooksOpenFile);
    score += 5 * (whiteRooksSemiOpenFile - blackRooksSemiOpenFile);
    score += 10 * (whiteRooks7th - blackRooks7th);
    score += 30 * (whiteBishopPair - blackBishopPair);
    score += 20 * (whiteOutposts - blackOutposts);
    score += -10 * (whiteTrapped - blackTrapped);
    score += 2 * (whiteCoord - blackCoord);
    score += 10 * (whiteKingActivity - blackKingActivity);
    score += -30 * (whiteDrawish - blackDrawish);

    return score;
}
