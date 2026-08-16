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

// The move the engine would play, drawn on the board as an arrow.
//
// Play-along mode has to say "play this" about a square rather than in words,
// and a coordinate pair in the panel makes the reader do the lookup the picture
// is there to save. The head stops short of the destination so it does not
// cover the piece standing on it -- the same choice tools/review made for the
// same reason. Does nothing when the move is null.
void renderSuggestionArrow(sf::RenderTarget& window, const Move& move, bool flipped);

// A banner across the board once the game is finished. The result was reported
// to stdout only, which is not where someone playing in the window is looking.
void renderGameOverBanner(sf::RenderTarget& window, const GameManager& game);

// A one-line prompt across the foot of the board, for a question the GUI needs
// answered before it acts (currently only the resign confirmation).
void renderPrompt(sf::RenderTarget& window, const std::string& text);

// The pre-game "which side do you want?" screen. mouse is in layout
// coordinates, or negative when the cursor has not been over the window yet.
void renderSideChooser(sf::RenderTarget& window,
                       const std::map<std::string, sf::Texture>& textures,
                       const sf::Vector2f& buttonSize,
                       const sf::Vector2f& whitePos,
                       const sf::Vector2f& blackPos,
                       const sf::Vector2f& mouse);
