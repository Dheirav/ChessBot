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

// --- Reading ---
//
// The engine could write a game and not read one back, which makes every game
// ever played — its own 48 on Lichess, or anyone's downloaded PGN — unusable as
// input. Analysis, review and opening statistics all start here.

struct PgnGame {
    PgnTags tags;
    std::vector<Move> moves;
};

// One SAN token against a position. `board` is left unmodified.
//
// Deliberately implemented by generating the legal moves and asking toSan()
// which one spells this way, rather than by parsing the notation. SAN's hard
// part is disambiguation — whether "Nf3" needs to be "Nbd2" depends on what
// else could reach the square — and toSan() already solves it. A second,
// independent implementation would be a second place for that logic to be
// wrong, and the two would drift.
//
// Returns a move with from == -1 if the token matches no legal move, which is
// how an illegal or malformed move reports itself.
Move fromSan(Board& board, const std::string& san);

// A whole PGN document: tag pairs, then movetext. Comments, variations, NAGs
// and move numbers are skipped; the result token ends the game.
//
// Returns false with `error` set on the first token that matches no legal move.
// That is a real failure and not something to skip past: a PGN whose moves do
// not fit the position it claims is either corrupt or was produced against
// different rules, and silently dropping the move would replay a different game
// than the one recorded — which is the same failure mode SAN disambiguation
// exists to prevent.
bool parsePgn(const std::string& text, PgnGame& out, std::string* error = nullptr);

// parsePgn() over a file's contents. False if the file cannot be opened.
bool readPgn(const std::string& path, PgnGame& out, std::string* error = nullptr);
