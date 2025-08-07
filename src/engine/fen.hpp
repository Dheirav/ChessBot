#pragma once
#include <string>
#include "board.hpp"

struct FENInfo {
    std::string piecePlacement;
    char activeColor;
    std::string castlingRights;
    std::string enPassant;
    int halfmoveClock;
    int fullmoveNumber;
};

bool parseFEN(const std::string& fen, Board& board, FENInfo& info);