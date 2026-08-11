#include "gui_manager.hpp"
#include "gui/hud.hpp"
#include "gui/constants.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

GUIManager::GUIManager() : gameManager(nullptr), isRunning(false) {
}

GUIManager::~GUIManager() {
    shutdown();
}

bool GUIManager::initialize() {
    // Create window
    window.create(sf::VideoMode(WINDOW_WIDTH, WINDOW_HEIGHT), "ChessBot");
    if (!window.isOpen()) {
        std::cerr << "Failed to create window!" << std::endl;
        return false;
    }
    applyLetterboxView(WINDOW_WIDTH, WINDOW_HEIGHT);

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

// The window is resizable, but the GUI is laid out in fixed pixels: 64-pixel
// tiles and a 280-pixel panel. Rather than reflow, the whole layout is scaled
// to fit and centred, with bars on whichever axis has room to spare. Without
// this the content stretched while input still divided raw pixels by
// TILE_SIZE, so after any resize the clicks landed on the wrong squares.
void GUIManager::applyLetterboxView(unsigned width, unsigned height) {
    if (width == 0 || height == 0) return;
    sf::View view(sf::FloatRect(0.f, 0.f, (float)WINDOW_WIDTH, (float)WINDOW_HEIGHT));
    const float scale = std::min((float)width / WINDOW_WIDTH, (float)height / WINDOW_HEIGHT);
    const float w = WINDOW_WIDTH * scale / (float)width;
    const float h = WINDOW_HEIGHT * scale / (float)height;
    view.setViewport(sf::FloatRect((1.f - w) / 2.f, (1.f - h) / 2.f, w, h));
    window.setView(view);
}

sf::Event GUIManager::toLayoutCoords(const sf::Event& event) const {
    sf::Event out = event;
    if (event.type == sf::Event::MouseButtonPressed ||
        event.type == sf::Event::MouseButtonReleased) {
        const sf::Vector2f p = window.mapPixelToCoords(
            {event.mouseButton.x, event.mouseButton.y});
        out.mouseButton.x = (int)std::floor(p.x);
        out.mouseButton.y = (int)std::floor(p.y);
    } else if (event.type == sf::Event::MouseMoved) {
        const sf::Vector2f p = window.mapPixelToCoords(
            {event.mouseMove.x, event.mouseMove.y});
        out.mouseMove.x = (int)std::floor(p.x);
        out.mouseMove.y = (int)std::floor(p.y);
    }
    return out;
}

void GUIManager::handleEvents() {
    sf::Event event;
    while (window.pollEvent(event)) {
        if (event.type == sf::Event::Closed) {
            isRunning = false;
            window.close();
            return;
        }
        if (event.type == sf::Event::Resized) {
            applyLetterboxView(event.size.width, event.size.height);
            continue;
        }

        handleMouseInput(event);
        handleKeyboardInput(event);
    }
}

// Accumulate time against whichever side is to move, and keep the search
// status in step with the engine. Driven from the frame loop rather than from
// move events, because a clock that only updates when a move is made is not a
// clock.
void GUIManager::tickClocks() {
    const auto now = std::chrono::steady_clock::now();
    const long dt = (long)std::chrono::duration_cast<std::chrono::milliseconds>(
        now - lastTick).count();
    lastTick = now;

    if (!gameManager || gameManager->isGameOver()) return;
    if (gameManager->getCurrentPlayer() == COLOR_WHITE) whiteClockMs += dt;
    else                                               blackClockMs += dt;

    const bool thinking = gameManager->isEngineThinking();
    if (thinking && !wasThinking) hud::beginSearch(gameManager->getEngineMoveTimeMs());
    if (!thinking && wasThinking) hud::endSearch();
    wasThinking = thinking;
}

void GUIManager::update() {
    tickClocks();
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

    // The last move played, so the board can show what just changed. Null
    // before either side has moved.
    const std::vector<Move>& history = gameManager->getMoveHistory();
    const Move* lastMove = history.empty() ? nullptr : &history.back();

    renderBoard(window, gameManager->getBoard(), textures, input, lastMove);
    input.drawDraggedPiece(window, textures);
    renderSidePanel(window, *gameManager, whiteClockMs, blackClockMs);
    renderGameOverBanner(window, *gameManager);
    if (resignArmed && !gameManager->isGameOver()) {
        renderPrompt(window, "Resign?  R again to confirm, any other key to cancel");
    }

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

    // Clicking somewhere is not an answer to "resign?", so it withdraws the
    // question rather than leaving it armed behind the next keypress.
    if (event.type == sf::Event::MouseButtonPressed) {
        resignArmed = false;
    }

    // Let the input handler process the event, in layout coordinates
    input.handleEvent(toLayoutCoords(event), const_cast<Board&>(gameManager->getBoard()));
}

void GUIManager::handleKeyboardInput(const sf::Event& event) {
    if (event.type != sf::Event::KeyPressed) {
        return;
    }

    // The promotion dialog owns the keyboard while it is open: its letters (and
    // Escape) mean "promote to this" and "cancel", not "resign" and "interrupt
    // the engine".
    if (input.isPromotionActive()) {
        input.handleEvent(event, const_cast<Board&>(gameManager->getBoard()));
        return;
    }

    const bool ctrl = event.key.control || event.key.system;

    // Resigning is irreversible and R is one key away from everything else, so
    // it takes two presses. Anything else answers "no" — including Ctrl+R,
    // which is a reload reflex rather than an intent to lose.
    if (resignArmed) {
        resignArmed = false;
        if (event.key.code == sf::Keyboard::R && !ctrl) {
            gameManager->resignGame();
            return;
        }
        // fall through: the key still does whatever it normally does
    }

    // Undo/Redo with Ctrl+Z / Ctrl+Y
    if (ctrl && event.key.code == sf::Keyboard::Z) {
        gameManager->undoLastMove();
    } else if (ctrl && event.key.code == sf::Keyboard::Y) {
        gameManager->redoLastMove();
    }
    // Arm resignation with R
    else if (event.key.code == sf::Keyboard::R && !ctrl) {
        if (!gameManager->isGameOver()) {
            resignArmed = true;
        }
    }
    // Stop engine thinking with Escape key
    else if (event.key.code == sf::Keyboard::Escape) {
        if (gameManager->isEngineThinking()) {
            std::cout << "User interrupted engine thinking..." << std::endl;
            gameManager->stopEngineThinking();
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

// Asked in the window, which is already open by this point. It used to block on
// std::cin behind the window: the app looked hung, and the answer had to be
// typed into a terminal the player may not even have been looking at.
PieceColor GUIManager::askUserForSide() {
    // Two buttons, side by side, in the fixed layout's coordinates.
    const sf::Vector2f size(230.f, 130.f);
    const float y = (WINDOW_HEIGHT - size.y) / 2.f;
    const sf::Vector2f whitePos(WINDOW_WIDTH / 2.f - size.x - 20.f, y);
    const sf::Vector2f blackPos(WINDOW_WIDTH / 2.f + 20.f, y);

    auto hits = [&](const sf::Vector2f& pos, float px, float py) {
        return px >= pos.x && px <= pos.x + size.x && py >= pos.y && py <= pos.y + size.y;
    };

    sf::Vector2f mouse(-1.f, -1.f);
    while (window.isOpen()) {
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed) {
                window.close();
                isRunning = false;
                return COLOR_WHITE;   // the caller exits immediately anyway
            }
            if (event.type == sf::Event::Resized) {
                applyLetterboxView(event.size.width, event.size.height);
            } else if (event.type == sf::Event::MouseMoved) {
                const sf::Event m = toLayoutCoords(event);
                mouse = {(float)m.mouseMove.x, (float)m.mouseMove.y};
            } else if (event.type == sf::Event::MouseButtonPressed &&
                       event.mouseButton.button == sf::Mouse::Left) {
                const sf::Event m = toLayoutCoords(event);
                if (hits(whitePos, (float)m.mouseButton.x, (float)m.mouseButton.y)) return COLOR_WHITE;
                if (hits(blackPos, (float)m.mouseButton.x, (float)m.mouseButton.y)) return COLOR_BLACK;
            } else if (event.type == sf::Event::KeyPressed) {
                if (event.key.code == sf::Keyboard::W) return COLOR_WHITE;
                if (event.key.code == sf::Keyboard::B) return COLOR_BLACK;
            }
        }

        window.clear(sf::Color(32, 30, 27));
        renderSideChooser(window, textures, size, whitePos, blackPos, mouse);
        window.display();
    }
    return COLOR_WHITE;
}
