#pragma once
#include <SFML/Graphics.hpp>
#include <map>
#include <string>
#include "engine/board.hpp"
#include "input.hpp"

// Function declarations
bool loadPieceTextures(std::map<std::string, sf::Texture>& textures);
void renderBoard(sf::RenderWindow& window, const Board& board, const std::map<std::string, sf::Texture>& textures, const Input& input);