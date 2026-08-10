#pragma once
#include <SFML/Graphics.hpp>
#include <map>
#include <string>

#include "engine/piece.hpp"

class Board;
class Input;
class GameManager;
struct Move;

bool loadPieceTextures(std::map<std::string, sf::Texture>& textures);
std::string pieceToString(const Piece& piece);

// The font used by both the board labels and the panel, loaded once.
sf::Font& uiFont();

// These take a RenderTarget, not a RenderWindow: they only ever draw, and a
// RenderTexture is a RenderTarget too. That is what lets a frame be rendered
// offscreen and inspected, which is the only way to check GUI work in an
// environment with no window manager.
//
// lastMove may be null (no move played yet); when set, its from and to squares
// are tinted so the opponent's reply is visible.
void renderBoard(sf::RenderTarget& window, const Board& board,
                 const std::map<std::string, sf::Texture>& textures,
                 const Input& input, const Move* lastMove);

// The right-hand panel: status, clocks, live search readout and move list.
void renderSidePanel(sf::RenderTarget& window, const GameManager& game,
                     long whiteClockMs, long blackClockMs);
