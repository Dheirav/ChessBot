#pragma once
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
    void handleKeyboardInput(const sf::Event& event);
    void processCompletedMove();
};
