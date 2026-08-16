#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "coach.hpp"
#include "config.hpp"
#include "game_manager.hpp"
#include "gui_manager.hpp"
#include "engine/chessbot_engine.hpp"
#include "engine/uci.hpp"
#include "gui/hud.hpp"

int main(int argc, char** argv) {
    // UCI mode is a pure stdin/stdout protocol with no window, so it is handled
    // before anything touches SFML. This is what lets cutechess-cli, Arena and
    // the rest of the standard tooling drive the engine.
    //
    // Play-along mode is handled here for the same reason: it is a terminal
    // conversation about a game happening somewhere else, and opening a window
    // for it would be beside the point.
    bool coach = false, coachBlack = false;
    long coachMs = 5000;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--uci") return uciLoop();
        else if (a == "--coach" || a == "--play-along") coach = true;
        else if (a == "--black") coachBlack = true;
        else if (a == "--white") coachBlack = false;
        else if (a == "--time" && i + 1 < argc) coachMs = std::atol(argv[++i]) * 1000;
    }
    if (coach) return coachLoop(!coachBlack, coachMs);

    std::cout << "=== ChessBot ===" << std::endl;

    // Load optional settings from chessbot.conf (defaults if absent)
    Settings settings = loadSettings();

    // Create and initialize GUI manager
    GUIManager guiManager;
    if (!guiManager.initialize()) {
        std::cerr << "Failed to initialize GUI!" << std::endl;
        return 1;
    }

    // Route the search's per-iteration output to the side panel instead of
    // stdout, which is where it used to go with nobody watching.
    hud::installSearchCallback();

    // Create chess engine
    auto engine = std::make_unique<ChessBotEngine>();
    engine->setSearchDepth(settings.searchDepth);
    engine->setMoveTimeMs(settings.moveTimeMs);
    engine->resizeTranspositionTable(settings.transpositionTableSizeMB);

    // Create game manager with the engine
    GameManager gameManager(std::move(engine));

    // Initialize the game
    gameManager.setEvaluationLogging(settings.logEvaluations);
    gameManager.initializeGame();
    
    // Connect GUI and game manager
    guiManager.setGameManager(&gameManager);
    
    // Ask user for side preference
    PieceColor userSide = guiManager.askUserForSide();
    gameManager.setHumanSide(userSide);
    
    // Start a new game
    gameManager.startNewGame();
    
    std::cout << "Game started! Use mouse to make moves." << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  - Click a piece, then click a highlighted square (or drag it there)" << std::endl;
    std::cout << "  - Q/R/B/N: choose the piece when promoting (Esc cancels)" << std::endl;
    std::cout << "  - Ctrl+Z: Undo move" << std::endl;
    std::cout << "  - Ctrl+Y: Redo move" << std::endl;
    std::cout << "  - Ctrl+S: Save the game as PGN (games/)" << std::endl;
    std::cout << "  - R, twice: Resign game" << std::endl;
    std::cout << "  - ESC: Interrupt engine thinking" << std::endl;
    std::cout << "Engine: " << gameManager.getEngineName() << " with Transposition Table" << std::endl;
    
    // Run the main game loop
    guiManager.run();
    
    std::cout << "Game ended. Final result: " << gameManager.getGameResult() << std::endl;
    
    // Make sure any in-progress search is stopped before inspecting the TT
    gameManager.stopEngineThinking();
    
    // Show transposition table statistics
    std::cout << "\n=== Final Engine Statistics ===" << std::endl;
    gameManager.printTranspositionTableStats();
    
    return 0;
}