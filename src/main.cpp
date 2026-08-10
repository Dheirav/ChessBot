#include <iostream>
#include <memory>
#include <string>

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
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--uci") return uciLoop();
    }

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
    std::cout << "  - Drag and drop pieces to move" << std::endl;
    std::cout << "  - Ctrl+Z: Undo move" << std::endl;
    std::cout << "  - Ctrl+Y: Redo move" << std::endl;
    std::cout << "  - R: Resign game" << std::endl;
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