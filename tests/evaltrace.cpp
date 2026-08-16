// Replay a game and print what the evaluation said at every ply.
//
// Built during the king-safety investigation (ROADMAP.md 6.4) and kept because
// it answered the question the review tool could not. `tools/review` says what
// a *stronger engine* thinks of a move; this says what *this* engine thought,
// term by term, which is the only way to see a term being silent.
//
// That distinction is what the investigation turned on. Reviewing the games the
// engine lost to 2300+ opposition showed the moves; this showed that king
// safety read +4 while the engine was being mated in nine, and -16 while its
// king sat uncastled in the centre for twenty-seven moves. A term that is wrong
// shows up in a review. A term that says nothing at all only shows up here.
//
//   ./tests/evaltrace <game.pgn> [plies-from-end, default 20]
//
// There is no pass/fail line and it is deliberately not in $(TESTS): what a
// term *should* say is what a gate answers, and this cannot. It is for finding
// out what the evaluation currently says, which is a different question and the
// one usually worth asking first.
#include "engine/board.hpp"
#include "engine/evaluation.hpp"
#include "engine/move_lookup.hpp"
#include "engine/movegen.hpp"
#include "engine/pgn.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    initMoveLookupTables();
    if (argc < 2) {
        std::printf("usage: %s <game.pgn> [plies-from-end, default 20]\n", argv[0]);
        return 1;
    }
    const int tail = (argc > 2) ? std::atoi(argv[2]) : 20;

    PgnGame game;
    std::string err;
    if (!readPgn(argv[1], game, &err)) {
        std::printf("could not read %s: %s\n", argv[1], err.c_str());
        return 1;
    }

    std::printf("%s vs %s  (%s)\n", game.tags.white.c_str(), game.tags.black.c_str(),
                game.tags.result.c_str());
    std::printf("%zu plies; showing the last %d. Scores are white's point of view.\n\n",
                game.moves.size(), tail);
    std::printf("%7s %8s %8s %8s %8s %8s %8s %8s\n",
                "ply", "total", "material", "kSafe", "pst", "mobility", "threats", "pawns");

    const size_t start = game.moves.size() > (size_t)tail ? game.moves.size() - tail : 0;

    Board board;   // constructor sets up the initial position
    for (size_t i = 0; i < game.moves.size(); ++i) {
        board.makeMove(game.moves[i]);
        if (i < start) continue;

        // evaluate_details() rather than evaluate(): it bypasses g_evalCache, so
        // a trace never reports a score another configuration left behind.
        EvalDetails e = evaluate_details(board);
        const int pawns = e.passedPawn + e.doubledPawn + e.isolatedPawn +
                          e.connectedPawn;
        std::printf("%5zu.%s %8d %8d %8d %8d %8d %8d %8d\n",
                    i / 2 + 1, (i % 2 == 0) ? " " : "..",
                    e.total, e.material, e.kingSafety, e.pst, e.mobility,
                    e.threats, pawns);
    }
    return 0;
}
