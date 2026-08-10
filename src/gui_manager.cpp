#include "gui_manager.hpp"
#include "gui/constants.hpp"
#include <iostream>

GUIManager::GUIManager() : gameManager(nullptr), isRunning(false) {
}

GUIManager::~GUIManager() {
    shutdown();
}

bool GUIManager::initialize() {
    // Create window
    window.create(sf::VideoMode(TILE_SIZE * BOARD_SIZE, TILE_SIZE * BOARD_SIZE), "ChessBot");
    if (!window.isOpen()) {
        std::cerr << "Failed to create window!" << std::endl;
        return false;
    }
    
    // Load resources
    if (!loadResources()) {
        std::cerr << "Failed to load resources!" << std::endl;
        return false;
    }
    
    isRunning = true;
    return true;
}

void GUIManager::setGameManager(GameManager* gm) {
    gameManager = gm;
}

void GUIManager::run() {
    if (!gameManager) {
        std::cerr << "No game manager set!" << std::endl;
        return;
    }

    // Draw the board from the human player's side
    input.setFlipped(gameManager->getHumanSide() == COLOR_BLACK);


    while (isRunning && window.isOpen()) {
        handleEvents();
        update();
        render();
    }
}

void GUIManager::handleEvents() {
    sf::Event event;
    while (window.pollEvent(event)) {
        if (event.type == sf::Event::Closed) {
            isRunning = false;
            window.close();
            return;
        }
        
        handleMouseInput(event);
        handleKeyboardInput(event);
    }
}

void GUIManager::update() {
    // Apply any engine move that finished computing (main thread only)
    gameManager->processPendingEngineMove();
    
    // Check for game over states first
    GameState state = gameManager->getGameState();
    if (state == GameState::GAME_OVER_CHECKMATE ||
        state == GameState::GAME_OVER_STALEMATE ||
        state == GameState::GAME_OVER_DRAW ||
        state == GameState::GAME_OVER_RESIGNATION) {
        
        // Game is over, but keep window open to show final position
        static bool gameOverMessageShown = false;
        if (!gameOverMessageShown) {
            std::cout << "\n=== GAME OVER ===" << std::endl;
            std::cout << gameManager->getGameResult() << std::endl;
            std::cout << "Press any key or close window to exit..." << std::endl;
            gameOverMessageShown = true;
        }
        return; // Don't process any more game logic
    }
    
    // Check if it's engine's turn and request move
    if (state == GameState::WAITING_FOR_ENGINE && !gameManager->isEngineThinking()) {
        gameManager->requestEngineMove();
    }
    
    // Provide feedback when engine is thinking
    static bool lastThinkingState = false;
    bool currentThinking = gameManager->isEngineThinking();
    if (currentThinking != lastThinkingState) {
        if (currentThinking) {
            std::cout << "Engine is thinking... (Press ESC to interrupt)" << std::endl;
        } else {
            std::cout << "Engine finished thinking." << std::endl;
        }
        lastThinkingState = currentThinking;
    }
    
    // Process any completed human moves
    processCompletedMove();
}

void GUIManager::render() {
    window.clear();
    
    // Render the board
    renderBoard(window, gameManager->getBoard(), textures, input);
    
    // Render dragged piece
    input.drawDraggedPiece(window, textures);
    
    window.display();
}

void GUIManager::shutdown() {
    if (window.isOpen()) {
        window.close();
    }
    isRunning = false;
}

bool GUIManager::loadResources() {
    return loadPieceTextures(textures);
}

void GUIManager::handleMouseInput(const sf::Event& event) {
    // Only handle input during a running game on the human's turn. The
    // game-over check matters on its own: in checkmate the side to move is
    // still the human, so without it the pieces stay selectable and draggable
    // after the game has ended, which reads as "I was mated but could still
    // play" even though no move ever commits.
    if (!gameManager || gameManager->isGameOver() || !gameManager->isHumanTurn()) {
        return;
    }
    
    // Let the input handler process the event
    input.handleEvent(event, const_cast<Board&>(gameManager->getBoard()));
}

void GUIManager::handleKeyboardInput(const sf::Event& event) {
    if (event.type == sf::Event::KeyPressed) {
        // Undo/Redo with Ctrl+Z / Ctrl+Y
        if ((event.key.control || event.key.system) && event.key.code == sf::Keyboard::Z) {
            gameManager->undoLastMove();
        } else if ((event.key.control || event.key.system) && event.key.code == sf::Keyboard::Y) {
            gameManager->redoLastMove();
        }
        // Resign with R key
        else if (event.key.code == sf::Keyboard::R) {
            gameManager->resignGame();
        }
        // Stop engine thinking with Escape key
        else if (event.key.code == sf::Keyboard::Escape) {
            if (gameManager->isEngineThinking()) {
                std::cout << "User interrupted engine thinking..." << std::endl;
                gameManager->stopEngineThinking();
            }
        }
    }
}

void GUIManager::processCompletedMove() {
    if (!gameManager || !gameManager->isHumanTurn()) {
        return;
    }
    
    if (input.hasCompletedMove()) {
        Move move = input.getCompletedMove();
        
        // Validate and make the move through game manager
        if (gameManager->makeHumanMove(move)) {
            // Move was successful
            std::cout << "Move made: " << move.toString() << std::endl;
        } else {
            // Move was invalid
            std::cout << "Invalid move: " << move.toString() << std::endl;
        }
        
        // Reset input state
        input.resetCompletedMove();
    }
}

PieceColor GUIManager::askUserForSide() {
    std::cout << "Choose your side (w/b): ";
    char sideInput;
    std::cin >> sideInput;
    
    if (sideInput == 'b' || sideInput == 'B') {
        return COLOR_BLACK;
    }
    return COLOR_WHITE;
}
