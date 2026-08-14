// Game review: read a PGN, analyse every position, report what each move cost.
//
//   ./tools/review game.pgn [--engine <path>] [--depth N] [--hash MB]
//
// The analysis engine is a parameter, not this engine (docs/REVIEW.md). A
// review is only worth reading if the thing doing the reviewing is stronger
// than the players being reviewed, and at Lichess rapid ~2065 ChessBot is a
// peer of its own games rather than an oracle over them. Point this at
// Stockfish for output to trust; point it at ./chessbot to see what ChessBot
// thinks, which is a different and much narrower question.
//
// **One search per position, not two.** The plan called for searching each
// position and then the position after the move played — but the position after
// move i *is* position i+1, which the loop already visits. So n moves need n+1
// searches rather than 2n. Everything below depends on that identity holding,
// which is why the engine is fed the game as a move list from the start rather
// than being handed positions independently.

#include "engine/move_lookup.hpp"
#include "engine/board.hpp"
#include "engine/movegen.hpp"
#include "engine/pgn.hpp"
#include "engine/uci_engine.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

// Mate scores are not centipawns and must not be subtracted as if they were:
// "mate in 3" minus "mate in 5" is not 200 of anything. Clamping puts them on
// a scale where the arithmetic is meaningless but bounded, so one missed mate
// cannot swamp a whole game's average.
constexpr int MATE_CLAMP = 1000;

int clampScore(int s) {
    if (s >  MATE_CLAMP) return  MATE_CLAMP;
    if (s < -MATE_CLAMP) return -MATE_CLAMP;
    return s;
}

const char* classify(int loss) {
    if (loss >= 300) return "Blunder";
    if (loss >= 150) return "Mistake";
    if (loss >=  50) return "Inaccuracy";
    if (loss >=  20) return "Good";
    if (loss >=   5) return "Excellent";
    return "Best";
}

}  // namespace

int main(int argc, char** argv) {
    initMoveLookupTables();

    std::string pgnPath, enginePath = "/usr/games/stockfish";
    int depth = 16, hashMb = 256;
    // Standard UCI engines take no arguments. ChessBot is the exception — its
    // default mode opens a window — so reviewing with it needs
    //   --engine ./chessbot --engine-arg --uci
    std::vector<std::string> engineArgs;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : ""; };
        if      (a == "--engine") enginePath = next();
        else if (a == "--depth")  depth = std::atoi(next());
        else if (a == "--hash")   hashMb = std::atoi(next());
        else if (a == "--engine-arg") engineArgs.push_back(next());
        else if (a[0] != '-')     pgnPath = a;
        else { std::printf("unknown option: %s\n", a.c_str()); return 1; }
    }
    if (pgnPath.empty()) {
        std::printf("usage: review <game.pgn> [--engine <path>] [--engine-arg <a>]\n"
                    "                 [--depth N] [--hash MB]\n"
                    "  default engine is /usr/games/stockfish; ChessBot needs --engine-arg --uci\n");
        return 1;
    }

    PgnGame game;
    std::string err;
    if (!readPgn(pgnPath, game, &err)) {
        std::printf("cannot read %s: %s\n", pgnPath.c_str(), err.c_str());
        return 1;
    }

    UciEngine engine;
    if (!engine.start(enginePath, hashMb, engineArgs)) {
        std::printf("cannot start engine: %s\n", enginePath.c_str());
        return 1;
    }
    engine.newGame();

    std::printf("%s vs %s  (%s)\n", game.tags.white.c_str(), game.tags.black.c_str(),
                game.tags.result.c_str());
    std::printf("%zu moves | %s at depth %d\n\n", game.moves.size(),
                enginePath.c_str(), depth);

    // Score of every position, from the point of view of the side to move in it.
    // SAN is collected alongside, because "Qa8+" is what a player recognises and
    // "c6a8" is what the wire format calls it.
    std::vector<std::string> uci;
    std::vector<std::string> san(game.moves.size());
    std::vector<std::string> bestSan(game.moves.size());
    {
        Board b;
        if (!game.tags.startFen.empty()) b.setFromFEN(game.tags.startFen);
        for (size_t i = 0; i < game.moves.size(); ++i) {
            san[i] = toSan(b, game.moves[i]);
            b.makeMove(game.moves[i]);
        }
    }
    std::vector<int> score(game.moves.size() + 1, 0);
    std::vector<std::string> best(game.moves.size() + 1);

    for (size_t i = 0; i <= game.moves.size(); ++i) {
        const std::string mv = engine.bestMove(uci, /*ms=*/0, /*nodes=*/0, depth);
        if (!engine.haveScore()) break;          // terminal position: nothing to search
        score[i] = clampScore(engine.lastScore());
        best[i] = mv;
        if (i < game.moves.size()) {
            // The engine answers in UCI; name it the way the game does.
            Board at;
            if (!game.tags.startFen.empty()) at.setFromFEN(game.tags.startFen);
            for (size_t k = 0; k < i; ++k) at.makeMove(game.moves[k]);
            for (const Move& cand : generateLegalMoves(at, at.activeColor)) {
                if (toUciMove(cand) == mv) { bestSan[i] = toSan(at, cand); break; }
            }
            uci.push_back(toUciMove(game.moves[i]));
        }
        std::fprintf(stderr, "\ranalysing %zu/%zu", i + 1, game.moves.size() + 1);
    }
    std::fprintf(stderr, "\r                              \r");

    // Loss for move i is the value the mover gave up. score[i+1] is from the
    // *opponent's* point of view, so it is negated to bring both onto the
    // mover's scale.
    long lossSum[2] = {0, 0};
    int lossCount[2] = {0, 0};
    int blunders[2] = {0, 0};

    for (size_t i = 0; i < game.moves.size(); ++i) {
        const int played = -score[i + 1];
        int loss = score[i] - played;
        if (loss < 0) loss = 0;                  // nothing beats the best move

        const bool white = (i % 2 == 0);
        lossSum[white ? 0 : 1] += loss;
        ++lossCount[white ? 0 : 1];
        if (loss >= 300) ++blunders[white ? 0 : 1];

        if (loss >= 50) {
            const std::string b = bestSan[i].empty() ? best[i] : bestSan[i];
            std::printf("%3zu.%s%-8s %-11s -%4d cp   (best %s, eval %+d)\n",
                        i / 2 + 1, white ? " " : "..", san[i].c_str(),
                        classify(loss), loss, b.c_str(), score[i]);
        }
    }

    std::printf("\n%-8s %-14s %-10s %s\n", "", "avg loss", "blunders", "moves");
    for (int s = 0; s < 2; ++s) {
        if (!lossCount[s]) continue;
        std::printf("%-8s %-14.1f %-10d %d\n", s == 0 ? "White" : "Black",
                    (double)lossSum[s] / lossCount[s], blunders[s], lossCount[s]);
    }
    return 0;
}
