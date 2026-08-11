#pragma once

#include <string>
#include <vector>

#include "board.hpp"
#include "move.hpp"

// PGN export.
//
// The engine could play a game and then had no way to hand it to anything else.
// Move::toString() writes "e2e4", which is the internal spelling: it is not SAN,
// it carries no result, no tags and no move numbers, so a finished game could
// not be pasted into Lichess analysis, replayed in a viewer, or kept as a
// record. This turns a move list into the format every one of those tools reads.

struct PgnTags {
    std::string event  = "Casual game";
    std::string site   = "ChessBot";
    std::string date;                    // "YYYY.MM.DD"; empty prints "????.??.??"
    std::string round  = "-";
    std::string white  = "?";
    std::string black  = "?";
    std::string result = "*";            // "1-0", "0-1", "1/2-1/2" or "*"
    std::string startFen;                // empty = the standard opening position
};

// Today's date as PGN spells it.
std::string pgnToday();

// Standard Algebraic Notation for one move, which must be legal in `board`.
//
// SAN is not a property of the move alone: "Nf3" needs to know whether the
// other knight could also go there, and the "+" needs to know what the position
// looks like afterwards. Both come from `board`, which is left unmodified.
std::string toSan(const Board& board, const Move& move);

// A complete PGN document: the seven-tag roster, then the movetext wrapped at
// 80 columns and terminated by the result.
std::string toPgn(const std::vector<Move>& moves, const PgnTags& tags);

// toPgn() written to `path`. False if the file cannot be opened.
bool writePgn(const std::string& path, const std::vector<Move>& moves,
              const PgnTags& tags);
