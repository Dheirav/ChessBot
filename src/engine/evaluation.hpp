// Forward declare Board
class Board;

// Structure for detailed evaluation breakdown
struct EvalDetails {
	int total;
	int material;
	int mobility;
	int kingSafety;
	int centerControl;
	int bishopPair;
	int doubledPawn;
	int isolatedPawn;
	int passedPawn;
	int backwardPawn;
	int connectedPawn;
	int pawnChain;
	int rooksOpenFile;
	int rooksSemiOpenFile;
	int rooks7thRank;
	int pst;
	int outpost;
	int trapped;
	int coordination;
	int kingActivity;
	int threats;
	int undefended;
	int space;
	int drawish;
};

// Returns a detailed breakdown of evaluation for logging
EvalDetails evaluate_details(const Board& board);
#pragma once
#include "board.hpp"

// Piece values for MVV-LVA and evaluation
extern const int pieceValues[7];

// Returns a score from the perspective of the side to move (positive = good for white, negative = good for black)
int evaluate(const Board& board);
