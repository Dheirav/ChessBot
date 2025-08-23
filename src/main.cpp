#include <iostream>
#include <memory>

#include "game_manager.hpp"
#include "gui_manager.hpp"
#include "engine/chessbot_engine.hpp"

int main() {
    std::cout << "=== ChessBot ===" << std::endl;
    
    // Create and initialize GUI manager
    GUIManager guiManager;
    if (!guiManager.initialize()) {
        std::cerr << "Failed to initialize GUI!" << std::endl;
        return 1;
    }
    
    // Create chess engine
    auto engine = std::make_unique<ChessBotEngine>();
    
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
    
    // Run the main game loop
    guiManager.run();
    
    std::cout << "Game ended. Final result: " << gameManager.getGameResult() << std::endl;
    
    return 0;
}