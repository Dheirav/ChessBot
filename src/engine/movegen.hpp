#pragma once
#include "board.hpp"
#include "move.hpp"
#include "piece.hpp"

MoveList generateMoves(const Board& board, PieceColor sideToMove, bool includeCastling);
MoveList generateMoves(const Board& board, PieceColor sideToMove);