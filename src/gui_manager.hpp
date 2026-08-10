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
    // Cumulative thinking time per side, in milliseconds.
    long whiteClockMs = 0;
    long blackClockMs = 0;
    std::chrono::steady_clock::time_point lastTick = std::chrono::steady_clock::now();
    bool wasThinking = false;

    sf::RenderWindow window;
    std::map<std::string, sf::Texture> textures;
    Input input;
    GameManager* gameManager;
    
    // GUI state
    bool isRunning;
    
public:
    GUIManager();
    ~GUIManager();
    
    // Initialization
    bool initialize();
    void setGameManager(GameManager* gm);
    
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
    void handleMouseInput(const sf::Event& event);
    void tickClocks();
    void handleKeyboardInput(const sf::Event& event);
    void processCompletedMove();
};
