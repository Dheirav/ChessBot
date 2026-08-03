#include <SFML/Graphics.hpp>
#include <map>
#include <string>
#include <vector>
#include <iostream>
#include "gui/constants.hpp"
#include "engine/board.hpp"
#include "engine/piece.hpp"
#include "gui/input.hpp"

bool loadPieceTextures(std::map<std::string, sf::Texture>& textures) {
    std::vector<std::string> pieces = {
        "wP", "wR", "wN", "wB", "wQ", "wK",
        "bP", "bR", "bN", "bB", "bQ", "bK"
    };
    for (const auto& name : pieces) {
        sf::Texture tex;
        if (!tex.loadFromFile("src/gui/assets/piece_images/" + name + ".png")) {
            std::cerr << "Failed to load: piece_images/" << name << ".png\n";
            return false;
        }
        textures[name] = tex;
    }
    return true;
}

// Helper to get piece string for texture lookup
std::string pieceToString(const Piece& piece) {
    if (piece.type() == NONE) return "";
    char colorChar = (piece.color() == COLOR_WHITE) ? 'w' : 'b';
    char typeChar = 'P';
    switch (piece.type()) {
        case PAWN:   typeChar = 'P'; break;
        case KNIGHT: typeChar = 'N'; break;
        case BISHOP: typeChar = 'B'; break;
        case ROOK:   typeChar = 'R'; break;
        case QUEEN:  typeChar = 'Q'; break;
        case KING:   typeChar = 'K'; break;
        default:     typeChar = '?'; break;
    }
    return std::string() + colorChar + typeChar;
}

void renderBoard(sf::RenderWindow& window, const Board& board, const std::map<std::string, sf::Texture>& textures, const Input& input){
    // Load font for square indices (static so only loads once)
    static sf::Font indexFont;
    static bool fontLoaded = false;
    if (!fontLoaded) {
        if (!indexFont.loadFromFile("src/gui/assets/fonts/arial.ttf")) {
            std::cerr << "Failed to load font src/gui/assets/fonts/arial.ttf for square indices!" << std::endl;
        } else {
            fontLoaded = true;
        }
    }
    // Draw board
    for (int y = 0; y < BOARD_SIZE; ++y) {
        for (int x = 0; x < BOARD_SIZE; ++x) {
            sf::RectangleShape tile(sf::Vector2f(TILE_SIZE, TILE_SIZE));
            tile.setPosition(x * TILE_SIZE, y * TILE_SIZE);
            bool isLight = (x + y) % 2 == 0;
            tile.setFillColor(isLight ? sf::Color(239, 208, 157) /*rgb(239, 208, 157) */ : sf::Color(167, 106, 59) /*rgb(167, 108, 59) */);
            window.draw(tile);

            // Draw square index overlay
            int squareIdx = Board::get1DIndex(x, y);
            if (fontLoaded) {
                sf::Text idxText;
                idxText.setFont(indexFont);
                idxText.setString(std::to_string(squareIdx));
                idxText.setCharacterSize(16);
                idxText.setFillColor(sf::Color(50, 50, 50, 180));
                idxText.setStyle(sf::Text::Bold);
                // Center the text in the square
                sf::FloatRect textRect = idxText.getLocalBounds();
                idxText.setOrigin(textRect.left + textRect.width / 2.0f, textRect.top + textRect.height / 2.0f);
                idxText.setPosition(x * TILE_SIZE + TILE_SIZE / 2.0f, y * TILE_SIZE + TILE_SIZE / 2.0f);
                window.draw(idxText);
            }
            if (input.getSelectedSquare() != -1) {
                for (const Move& m : input.legalMoves) {
                    if (m.to == squareIdx) {
                        sf::RectangleShape highlight(sf::Vector2f(TILE_SIZE, TILE_SIZE));
                        highlight.setPosition(x * TILE_SIZE, y * TILE_SIZE);
                        if (m.flag == CAPTURE || m.flag == EN_PASSANT) {
                            highlight.setFillColor(sf::Color(255, 0, 0, 120)); // semi-transparent red for capture
                        } else {
                            highlight.setFillColor(sf::Color(0, 255, 0, 100)); // semi-transparent green
                        }
                        window.draw(highlight);
                        break;
                    }
                }
            }

            // Draw piece if present
            const Piece& piece = board.squares[squareIdx];
            std::string name = pieceToString(piece);
            if (!name.empty() && textures.count(name)) {
                sf::Sprite sprite;
                sprite.setTexture(textures.at(name));
                sprite.setPosition(x * TILE_SIZE, y * TILE_SIZE);
                sprite.setScale(
                    TILE_SIZE / (float)textures.at(name).getSize().x,
                    TILE_SIZE / (float)textures.at(name).getSize().y
                );
                window.draw(sprite);
            }

            // Draw selected square outline if this is the selected square
            if (input.getSelectedSquare() == squareIdx) {
                float margin = 4.0f;
                float cornerLen = 16.0f;
                sf::Color outlineColor = sf::Color(0, 200, 255, 255); // Cyan
                float thickness = 3.0f;
                // Draw 4 corner lines
                // Top-left
                sf::RectangleShape h1(sf::Vector2f(cornerLen, thickness));
                h1.setPosition(x * TILE_SIZE + margin, y * TILE_SIZE + margin);
                h1.setFillColor(outlineColor);
                window.draw(h1);
                sf::RectangleShape v1(sf::Vector2f(thickness, cornerLen));
                v1.setPosition(x * TILE_SIZE + margin, y * TILE_SIZE + margin);
                v1.setFillColor(outlineColor);
                window.draw(v1);
                // Top-right
                sf::RectangleShape h2(sf::Vector2f(cornerLen, thickness));
                h2.setPosition((x + 1) * TILE_SIZE - margin - cornerLen, y * TILE_SIZE + margin);
                h2.setFillColor(outlineColor);
                window.draw(h2);
                sf::RectangleShape v2(sf::Vector2f(thickness, cornerLen));
                v2.setPosition((x + 1) * TILE_SIZE - margin - thickness, y * TILE_SIZE + margin);
                v2.setFillColor(outlineColor);
                window.draw(v2);
                // Bottom-left
                sf::RectangleShape h3(sf::Vector2f(cornerLen, thickness));
                h3.setPosition(x * TILE_SIZE + margin, (y + 1) * TILE_SIZE - margin - thickness);
                h3.setFillColor(outlineColor);
                window.draw(h3);
                sf::RectangleShape v3(sf::Vector2f(thickness, cornerLen));
                v3.setPosition(x * TILE_SIZE + margin, (y + 1) * TILE_SIZE - margin - cornerLen);
                v3.setFillColor(outlineColor);
                window.draw(v3);
                // Bottom-right
                sf::RectangleShape h4(sf::Vector2f(cornerLen, thickness));
                h4.setPosition((x + 1) * TILE_SIZE - margin - cornerLen, (y + 1) * TILE_SIZE - margin - thickness);
                h4.setFillColor(outlineColor);
                window.draw(h4);
                sf::RectangleShape v4(sf::Vector2f(thickness, cornerLen));
                v4.setPosition((x + 1) * TILE_SIZE - margin - thickness, (y + 1) * TILE_SIZE - margin - cornerLen);
                v4.setFillColor(outlineColor);
                window.draw(v4);
            }
        }
    }
    
    // Draw promotion dialog if active
    input.drawPromotionDialog(window, textures);
}