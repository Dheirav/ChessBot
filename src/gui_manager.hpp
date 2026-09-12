#pragma once
#include <chrono>
#include <SFML/Graphics.hpp>
#include <map>
#include <string>
#include "game_manager.hpp"
#include "gui/input.hpp"
#include "gui/renderer.hpp"

/**
 * Manages the GUI components and coordinates with GameManager
 * Handles user input, rendering, and GUI events
 */
class GUIManager {
private:
    // Per-side clocks in milliseconds. Untimed, they count up from zero as
    // thinking time; under a time control they count down from the base, gain
    // the increment after each move, and a clock reaching zero is a loss.
    long whiteClockMs = 0;
    long blackClockMs = 0;
    long tcBaseMs = 0;   // 0 = untimed
    long tcIncMs = 0;
    // Whose turn it was on the previous tick, so the increment lands exactly
    // once, on the side that just moved.
    PieceColor lastSideToMove = COLOR_WHITE;
    std::chrono::steady_clock::time_point lastTick = std::chrono::steady_clock::now();
    bool wasThinking = false;

    sf::RenderWindow window;
    std::map<std::string, sf::Texture> textures;
    Input input;
    GameManager* gameManager;
    
    // GUI state
    bool isRunning;
    // Resigning takes two presses of R; this is whether the first has landed.
    bool resignArmed = false;
    // A short-lived line across the foot of the board — "saved to ..." and the
    // like. Feedback that only reaches stdout is feedback the player never sees.
    std::string statusMessage;
    std::chrono::steady_clock::time_point statusUntil;

public:
    GUIManager();
    ~GUIManager();
    
    // Initialization
    bool initialize();
    void setGameManager(GameManager* gm);

    // A time control for every game from now on. 0 base means untimed.
    void setTimeControl(long baseMs, long incMs);
    bool isTimed() const { return tcBaseMs > 0; }

    // Reset the position and the clocks. Bound to N.
    void newGame();
    
    // Main loop
    void run();
    void handleEvents();
    void update();
    void render();
    
    // Cleanup
    void shutdown();
    
    // User interaction
    PieceColor askUserForSide();
    
private:
    bool loadResources();
    // Keeps the fixed WINDOW_WIDTH x WINDOW_HEIGHT layout centred and
    // aspect-correct inside a window the user may have resized.
    void applyLetterboxView(unsigned width, unsigned height);
    // A copy of the event with mouse coordinates translated from window pixels
    // into that layout, so nothing downstream has to know about the view.
    sf::Event toLayoutCoords(const sf::Event& event) const;
    void handleMouseInput(const sf::Event& event);
    void tickClocks();
    void handleKeyboardInput(const sf::Event& event);
    void processCompletedMove();
    void setStatus(const std::string& text, int seconds = 4);
};
