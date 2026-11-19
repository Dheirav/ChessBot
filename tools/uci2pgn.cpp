// Coordinate moves in, PGN out.
//
//   echo "e2e4 e7e5@598700 g1f3" | ./tools/uci2pgn --white A --black B --result 1-0
//
// tools/chesscom-pgn.py fetches a Chess.com game, whose move list arrives as
// from/to squares and not as SAN. SAN needs the position (does "Nf3" have to be
// "Ngf3"? does the move give check?), and the engine already answers that in
// toSan(). Writing a second SAN generator in Python would be a second place for
// disambiguation to be wrong, which is the reason pgn.hpp gives for reading SAN
// by matching against generated moves in the first place.
//
// Every move is matched against the legal moves of the position it is played
// in, so a decoding mistake upstream (a wrong square, a promotion read as the
// wrong piece) stops here with the ply number instead of producing a game that
// quietly differs from the one that was played.
//
// A token may carry the clock remaining after the move as "e2e4@599500"
// (milliseconds). It is written as {[%clk h:mm:ss.d]}, which is what the review
// reads to chart time use.

#include "engine/board.hpp"
#include "engine/move.hpp"
#include "engine/move_lookup.hpp"
#include "engine/movegen.hpp"
#include "engine/pgn.hpp"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string clockText(long ms) {
    if (ms < 0) ms = 0;
    const long tenths = (ms / 100) % 10;
    const long s = ms / 1000;
    char buf[32];
    std::snprintf(buf, sizeof buf, "[%%clk %ld:%02ld:%02ld.%ld]", s / 3600, (s / 60) % 60, s % 60, tenths);
    return buf;
}

}  // namespace

int main(int argc, char** argv) {
    PgnTags tags;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        if      (a == "--white")  tags.white = next();
        else if (a == "--black")  tags.black = next();
        else if (a == "--result") tags.result = next();
        else if (a == "--date")   tags.date = next();
        else if (a == "--event")  tags.event = next();
        else if (a == "--site")   tags.site = next();
        else if (a == "--tc")     tags.timeControl = next();
        else if (a == "--fen")    tags.startFen = next();
        else {
            std::fprintf(stderr, "usage: uci2pgn [--white N] [--black N] [--result R] [--date D]\n"
                                 "               [--event E] [--site S] [--tc T] [--fen F] < moves\n");
            return 2;
        }
    }

    // Without the tables the generator returns nothing, and every move then
    // reads as illegal, including e2e4 from the opening position.
    initMoveLookupTables();

    Board board;
    if (!board.setFromFEN(tags.startFen.empty() ? Board::INITIAL_FEN : tags.startFen)) {
        std::fprintf(stderr, "unreadable FEN: %s\n", tags.startFen.c_str());
        return 1;
    }

    std::vector<Move> moves;
    std::vector<MoveNote> notes;
    std::string token;
    while (std::cin >> token) {
        long clock = -1;
        const size_t at = token.find('@');
        if (at != std::string::npos) {
            clock = std::atol(token.c_str() + at + 1);
            token.resize(at);
        }

        Move found;
        MoveList legal = generateLegalMoves(board, board.activeColor);
        for (const Move& m : legal) {
            if (toUciMove(m) == token) { found = m; break; }
        }
        if (found.from < 0) {
            std::fprintf(stderr, "ply %zu: \"%s\" is not legal in %s\n",
                         moves.size() + 1, token.c_str(), board.getFEN().c_str());
            return 1;
        }

        MoveNote note;
        if (clock >= 0) note.comment = clockText(clock);
        notes.push_back(note);
        moves.push_back(found);
        board.makeMove(found);
    }

    std::cout << toPgn(moves, tags, notes);
    return 0;
}
