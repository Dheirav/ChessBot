// Game-over regression test.
//
// Guards two things that are easy to get wrong and were both broken:
//
//  1. Terminal detection. Checkmate, stalemate and the draws must be detected
//     for the side to move.
//
//  2. That a finished game actually stops. Checkmate and stalemate happen to
//     have no legal moves, so a missing game-over check is invisible there --
//     isValidMove() rejects everything anyway. The draws and resignation are
//     terminal *with* legal moves still on the board, and that is where a
//     missing check lets play continue after the game has ended.
//
// Also covers terminal state being sticky: updateGameState() returns early
// when the game is already over, so anything that starts a new position has to
// clear the old state first or every later game begins already finished.
//
// Build and run:  make test-gamestate
#include "game_manager.hpp"
#include "engine/board.hpp"
#include "engine/movegen.hpp"
#include "engine/move_lookup.hpp"
#include "engine/legal_move_validator.hpp"
#include "engine/chessbot_engine.hpp"
#include <cstdio>
#include <memory>

struct Case {
    const char* name;
    const char* fen;
    GameState   expectedState;
    bool        expectMoveAccepted; // may a human move still be played?
};

static const Case CASES[] = {
    {"fools mate (w mated)",  "rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3",
     GameState::GAME_OVER_CHECKMATE, false},
    {"back rank (b mated)",   "R5k1/5ppp/8/8/8/8/8/6K1 b - - 0 1",
     GameState::GAME_OVER_CHECKMATE, false},
    {"scholars (b mated)",    "r1bqkbnr/pppp1Qpp/2n5/4p3/2B1P3/8/PPPP1PPP/RNB1K1NR b KQkq - 0 4",
     GameState::GAME_OVER_CHECKMATE, false},
    {"smothered (b mated)",   "6rk/5Npp/8/8/8/8/8/6K1 b - - 0 1",
     GameState::GAME_OVER_CHECKMATE, false},
    {"stalemate (b)",         "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1",
     GameState::GAME_OVER_STALEMATE, false},
    // Terminal but with legal moves available - the case that exposed the bug.
    {"fifty-move draw",       "8/8/4k3/8/8/4K3/8/6R1 w - - 100 80",
     GameState::GAME_OVER_DRAW, false},
    {"insufficient material", "8/8/4k3/8/8/4K3/8/6B1 w - - 0 40",
     GameState::GAME_OVER_DRAW, false},
    {"normal position",       "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
     GameState::WAITING_FOR_PLAYER, true},
};

static const char* stateName(GameState st) {
    switch (st) {
        case GameState::GAME_OVER_CHECKMATE:   return "GAME_OVER_CHECKMATE";
        case GameState::GAME_OVER_STALEMATE:   return "GAME_OVER_STALEMATE";
        case GameState::GAME_OVER_DRAW:        return "GAME_OVER_DRAW";
        case GameState::GAME_OVER_RESIGNATION: return "GAME_OVER_RESIGNATION";
        case GameState::WAITING_FOR_PLAYER:    return "WAITING_FOR_PLAYER";
        default:                               return "WAITING_FOR_ENGINE";
    }
}

static std::unique_ptr<GameManager> makeManager() {
    auto engine = std::make_unique<ChessBotEngine>();
    engine->setSearchDepth(1);
    auto gm = std::make_unique<GameManager>(std::move(engine));
    gm->initializeGame();
    return gm;
}

int main() {
    initMoveLookupTables();
    int failures = 0;

    std::printf("--- terminal detection and move rejection ---\n");
    for (const Case& c : CASES) {
        Board probe;
        if (!probe.setFromFEN(c.fen)) {
            std::printf("%-24s FEN PARSE FAILED\n", c.name);
            ++failures;
            continue;
        }

        auto gm = makeManager();
        gm->setHumanSide(probe.activeColor);
        gm->loadGameFromFEN(c.fen);

        GameState st = gm->getGameState();
        bool stateOk = (st == c.expectedState);

        MoveList legal = generateLegalMoves(probe, probe.activeColor);
        bool accepted = legal.empty() ? false : gm->makeHumanMove(legal[0]);
        bool moveOk = (accepted == c.expectMoveAccepted);

        bool overOk = (gm->isGameOver() == (c.expectedState != GameState::WAITING_FOR_PLAYER &&
                                            c.expectedState != GameState::WAITING_FOR_ENGINE));

        if (!stateOk || !moveOk || !overOk) ++failures;
        std::printf("%-24s legal=%-3zu state=%-21s moveAccepted=%-3s  %s\n",
                    c.name, legal.size(), stateName(st), accepted ? "yes" : "no",
                    (stateOk && moveOk && overOk) ? "ok" : "*** WRONG ***");
    }

    std::printf("\n--- terminal state must not be sticky ---\n");
    {
        auto gm = makeManager();
        gm->setHumanSide(COLOR_WHITE);

        gm->loadGameFromFEN("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");
        bool a = (gm->getGameState() == GameState::GAME_OVER_CHECKMATE);
        std::printf("%-40s %s\n", "checkmate FEN -> GAME_OVER_CHECKMATE", a ? "ok" : "*** WRONG ***");

        gm->loadGameFromFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        bool b = (gm->getGameState() == GameState::WAITING_FOR_PLAYER);
        std::printf("%-40s %s\n", "then startpos -> WAITING_FOR_PLAYER", b ? "ok" : "*** WRONG ***");

        gm->resignGame();
        bool c = (gm->getGameState() == GameState::GAME_OVER_RESIGNATION);
        std::printf("%-40s %s\n", "resign -> GAME_OVER_RESIGNATION", c ? "ok" : "*** WRONG ***");

        gm->startNewGame();
        bool d = (gm->getGameState() == GameState::WAITING_FOR_PLAYER);
        std::printf("%-40s %s\n", "then startNewGame -> WAITING_FOR_PLAYER", d ? "ok" : "*** WRONG ***");

        if (!a || !b || !c || !d) ++failures;
    }

    std::printf("\n%s\n", failures ? "FAILED" : "PASSED: all game-state checks");
    return failures ? 1 : 0;
}
