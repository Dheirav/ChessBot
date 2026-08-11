#include "gui/renderer.hpp"
#include <SFML/Graphics.hpp>
#include <map>
#include <string>
#include <vector>
#include <iostream>
#include "gui/constants.hpp"
#include "engine/board.hpp"
#include "engine/piece.hpp"
#include "gui/input.hpp"
#include "gui/hud.hpp"
#include "game_manager.hpp"
#include <algorithm>
#include <cstdio>

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

// Shared with the panel, so both use one font and load it once.
sf::Font& uiFont() {
    static sf::Font font;
    static bool loaded = false;
    if (!loaded) {
        if (!font.loadFromFile("src/gui/assets/fonts/arial.ttf")) {
            std::cerr << "Failed to load src/gui/assets/fonts/arial.ttf" << std::endl;
        }
        loaded = true;
    }
    return font;
}

void renderBoard(sf::RenderTarget& window, const Board& board, const std::map<std::string, sf::Texture>& textures, const Input& input, const Move* lastMove){
    sf::Font& indexFont = uiFont();
    // Draw board
    for (int y = 0; y < BOARD_SIZE; ++y) {
        for (int x = 0; x < BOARD_SIZE; ++x) {
            sf::RectangleShape tile(sf::Vector2f(TILE_SIZE, TILE_SIZE));
            tile.setPosition(x * TILE_SIZE, y * TILE_SIZE);
            bool isLight = (x + y) % 2 == 0;
            tile.setFillColor(isLight ? sf::Color(239, 208, 157) /*rgb(239, 208, 157) */ : sf::Color(167, 106, 59) /*rgb(167, 108, 59) */);
            window.draw(tile);

            int squareIdx = screenToSquare(x, y, input.isFlipped());

            // Highlight the move the opponent just played. Without this the
            // engine's reply is easy to miss entirely — the board simply looks
            // different from how you left it.
            if (lastMove && (squareIdx == lastMove->from || squareIdx == lastMove->to)) {
                sf::RectangleShape trail(sf::Vector2f(TILE_SIZE, TILE_SIZE));
                trail.setPosition(x * TILE_SIZE, y * TILE_SIZE);
                trail.setFillColor(sf::Color(246, 246, 105, 110));  // soft yellow
                window.draw(trail);
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

            // Draw piece if present. The square a drag started from is left
            // empty: that piece is drawn under the cursor by drawDraggedPiece,
            // and drawing it here too showed the same piece in two places at
            // once for the whole drag.
            const Piece& piece = board.squares[squareIdx];
            std::string name = squareIdx == input.getDragSquare() ? "" : pieceToString(piece);
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
    
    // Coordinates go on last, over the pieces. Drawn inside the loop they were
    // painted first and then hidden by whatever stood on a8 or a1 — which is
    // every game, since those are rook squares.
    for (int y = 0; y < BOARD_SIZE; ++y) {
        for (int x = 0; x < BOARD_SIZE; ++x) {
            const int squareIdx = screenToSquare(x, y, input.isFlipped());
            const bool bottomRow = (y == BOARD_SIZE - 1);
            const bool leftCol   = (x == 0);
            if (!bottomRow && !leftCol) continue;

            // Coordinates sit on the same corners pieces occupy, so contrast
            // against the *square* is not enough — a dark glyph vanishes into
            // the black rook on a8. A thin outline in the opposing tone keeps
            // them readable over anything drawn beneath.
            const bool isLightSq = (x + y) % 2 == 0;
            const sf::Color inkColor  = isLightSq ? sf::Color(90, 62, 34)
                                                  : sf::Color(240, 226, 202);
            const sf::Color halo = isLightSq ? sf::Color(245, 232, 210, 220)
                                             : sf::Color(60, 40, 22, 220);
            // Labels follow the board, not the screen, so they stay correct
            // when the view is flipped.
            const int file = squareIdx % 8;
            const int rank = 8 - (squareIdx / 8);
            if (bottomRow) {
                sf::Text t(std::string(1, char('a' + file)), indexFont, 12);
                t.setFillColor(inkColor);
                t.setOutlineColor(halo);
                t.setOutlineThickness(1.2f);
                t.setStyle(sf::Text::Bold);
                t.setPosition(x * TILE_SIZE + TILE_SIZE - 12.0f,
                              y * TILE_SIZE + TILE_SIZE - 18.0f);
                window.draw(t);
            }
            if (leftCol) {
                sf::Text t(std::to_string(rank), indexFont, 12);
                t.setFillColor(inkColor);
                t.setOutlineColor(halo);
                t.setOutlineThickness(1.2f);
                t.setStyle(sf::Text::Bold);
                t.setPosition(x * TILE_SIZE + 3.0f, y * TILE_SIZE + 2.0f);
                window.draw(t);
            }
        }
    }

    // Draw promotion dialog if active
    input.drawPromotionDialog(window, textures);
}
// ---------------------------------------------------------------------------
// Side panel
//
// Everything here was already known to the program and had nowhere to go: the
// engine's depth and score, how long it had been thinking against its budget,
// the moves played, and whether the game had ended at all.
// ---------------------------------------------------------------------------

namespace {

constexpr float PANEL_X = (float)BOARD_PIXELS;
const sf::Color PANEL_BG   = sf::Color(38, 36, 33);
const sf::Color TEXT_MAIN  = sf::Color(232, 228, 220);
const sf::Color TEXT_DIM   = sf::Color(150, 145, 138);
const sf::Color ACCENT     = sf::Color(120, 190, 120);

void drawText(sf::RenderTarget& w, const std::string& s, float x, float y,
              unsigned size, sf::Color color, bool bold = false) {
    sf::Text t(s, uiFont(), size);
    t.setFillColor(color);
    if (bold) t.setStyle(sf::Text::Bold);
    t.setPosition(x, y);
    w.draw(t);
}

// Same, positioned by its own width so it sits centred on centreX.
void drawCentered(sf::RenderTarget& w, const std::string& s, float centreX, float y,
                  unsigned size, sf::Color color, bool bold = false) {
    sf::Text t(s, uiFont(), size);
    t.setCharacterSize(size);
    if (bold) t.setStyle(sf::Text::Bold);
    const sf::FloatRect b = t.getLocalBounds();
    drawText(w, s, centreX - (b.left + b.width) / 2.f, y, size, color, bold);
}

std::string stateLabel(const GameManager& game) {
    switch (game.getGameState()) {
        case GameState::GAME_OVER_CHECKMATE:   return "Checkmate";
        case GameState::GAME_OVER_STALEMATE:   return "Stalemate";
        case GameState::GAME_OVER_DRAW:        return "Draw";
        case GameState::GAME_OVER_RESIGNATION: return "Resignation";
        default: break;
    }
    if (game.isEngineThinking()) return "Engine thinking";
    return game.isHumanTurn() ? "Your move" : "Engine to move";
}

}  // namespace

void renderSidePanel(sf::RenderTarget& window, const GameManager& game,
                     long whiteClockMs, long blackClockMs) {
    sf::RectangleShape bg(sf::Vector2f((float)PANEL_WIDTH, (float)WINDOW_HEIGHT));
    bg.setPosition(PANEL_X, 0.f);
    bg.setFillColor(PANEL_BG);
    window.draw(bg);

    const float left = PANEL_X + 16.f;
    float y = 14.f;

    // --- Status -----------------------------------------------------------
    const bool over = game.isGameOver();
    drawText(window, stateLabel(game), left, y, 19, over ? sf::Color(235, 170, 90) : TEXT_MAIN, true);
    y += 26.f;
    if (over && !game.getGameResult().empty()) {
        drawText(window, game.getGameResult(), left, y, 13, TEXT_DIM);
        y += 20.f;
    }
    y += 6.f;

    // --- Clocks -----------------------------------------------------------
    // Cumulative time each side has spent. The engine now plays to a real
    // budget, so how that time is being spent is worth seeing.
    const bool whiteToMove = (game.getCurrentPlayer() == COLOR_WHITE);
    for (int i = 0; i < 2; ++i) {
        const bool white = (i == 0);
        const bool onMove = (white == whiteToMove) && !over;
        sf::RectangleShape row(sf::Vector2f((float)PANEL_WIDTH - 32.f, 30.f));
        row.setPosition(left - 6.f, y - 4.f);
        row.setFillColor(onMove ? sf::Color(58, 56, 52) : sf::Color(46, 44, 41));
        window.draw(row);

        drawText(window, white ? "White" : "Black", left, y, 15,
                 onMove ? ACCENT : TEXT_DIM, onMove);
        drawText(window, hud::formatClock(white ? whiteClockMs : blackClockMs),
                 left + 150.f, y, 15, onMove ? TEXT_MAIN : TEXT_DIM, onMove);
        y += 34.f;
    }
    y += 10.f;

    // --- Engine ------------------------------------------------------------
    drawText(window, "ENGINE", left, y, 11, TEXT_DIM, true);
    y += 18.f;

    const hud::Snapshot info = hud::read();
    if (info.hasInfo) {
        drawText(window, "depth " + std::to_string(info.depth), left, y, 15, TEXT_MAIN);
        // The search reports from the side to move's point of view; a human
        // reads an evaluation as white-positive, so flip it for black.
        const int shown = whiteToMove ? info.scoreCp : -info.scoreCp;
        drawText(window, hud::formatScore(shown), left + 150.f, y, 15,
                 shown >= 0 ? ACCENT : sf::Color(220, 120, 110), true);
        y += 20.f;

        char line[96];
        std::snprintf(line, sizeof(line), "%.1fM nodes", info.nodes / 1e6);
        drawText(window, line, left, y, 12, TEXT_DIM);
        // Pair the node count with the elapsed time it was actually measured
        // against. Dividing it by the live wall clock mixes two time bases and
        // reports a rate the engine never achieved.
        if (info.infoElapsedMs > 0) {
            std::snprintf(line, sizeof(line), "%.0fk nps",
                          info.nodes / (double)info.infoElapsedMs);
            drawText(window, line, left + 150.f, y, 12, TEXT_DIM);
        }
        y += 20.f;
    } else {
        drawText(window, info.active ? "searching..." : "idle", left, y, 13, TEXT_DIM);
        y += 20.f;
    }

    // Time against budget, as a bar. This is the one piece of feedback that
    // makes a three-second think feel like progress rather than a freeze.
    if (info.active && info.budgetMs > 0) {
        const float frac = std::min(1.0f, (float)info.elapsedMs / (float)info.budgetMs);
        const float barW = (float)PANEL_WIDTH - 32.f;
        sf::RectangleShape track(sf::Vector2f(barW, 5.f));
        track.setPosition(left, y);
        track.setFillColor(sf::Color(60, 58, 54));
        window.draw(track);
        sf::RectangleShape fill(sf::Vector2f(barW * frac, 5.f));
        fill.setPosition(left, y);
        fill.setFillColor(ACCENT);
        window.draw(fill);
        y += 12.f;
        drawText(window, hud::formatClock(info.elapsedMs) + " / " +
                 hud::formatClock(info.budgetMs), left, y, 11, TEXT_DIM);
        y += 18.f;
    }
    y += 8.f;

    // --- Move list ---------------------------------------------------------
    drawText(window, "MOVES", left, y, 11, TEXT_DIM, true);
    y += 18.f;

    const std::vector<Move>& history = game.getMoveHistory();
    const float lineHeight = 17.f;
    const int visibleRows = (int)((WINDOW_HEIGHT - y - 10.f) / lineHeight);
    const int totalRows = ((int)history.size() + 1) / 2;
    // Show the tail: the recent moves are the ones being looked at.
    const int firstRow = totalRows > visibleRows ? totalRows - visibleRows : 0;

    for (int row = firstRow; row < totalRows; ++row) {
        const size_t whiteIdx = (size_t)row * 2;
        const size_t blackIdx = whiteIdx + 1;
        char num[8];
        std::snprintf(num, sizeof(num), "%d.", row + 1);
        drawText(window, num, left, y, 13, TEXT_DIM);
        drawText(window, history[whiteIdx].toString(), left + 34.f, y, 13, TEXT_MAIN);
        if (blackIdx < history.size())
            drawText(window, history[blackIdx].toString(), left + 130.f, y, 13, TEXT_MAIN);
        y += lineHeight;
    }
}

// ---------------------------------------------------------------------------
// Overlays
// ---------------------------------------------------------------------------

void renderGameOverBanner(sf::RenderTarget& window, const GameManager& game) {
    if (!game.isGameOver()) return;

    // Dim the board, not the panel: the panel is still live information (the
    // move list, the final clocks) and dimming it makes it harder to read.
    sf::RectangleShape dim(sf::Vector2f((float)BOARD_PIXELS, (float)BOARD_PIXELS));
    dim.setFillColor(sf::Color(0, 0, 0, 120));
    window.draw(dim);

    const float boxH = 104.f;
    const float boxY = ((float)BOARD_PIXELS - boxH) / 2.f;
    sf::RectangleShape box(sf::Vector2f((float)BOARD_PIXELS - 56.f, boxH));
    box.setPosition(28.f, boxY);
    box.setFillColor(sf::Color(32, 30, 27, 244));
    box.setOutlineColor(sf::Color(235, 170, 90));
    box.setOutlineThickness(2.f);
    window.draw(box);

    const float centre = (float)BOARD_PIXELS / 2.f;
    drawCentered(window, stateLabel(game), centre, boxY + 14.f, 26, sf::Color(235, 170, 90), true);
    if (!game.getGameResult().empty()) {
        drawCentered(window, game.getGameResult(), centre, boxY + 50.f, 16, TEXT_MAIN);
    }
    // Undo revives a finished game (see GameManager::undoLastMove), so this is
    // a real offer rather than a description of the keymap.
    drawCentered(window, "Ctrl+Z takes the move back", centre, boxY + 76.f, 12, TEXT_DIM);
}

void renderPrompt(sf::RenderTarget& window, const std::string& text) {
    const float h = 34.f;
    sf::RectangleShape strip(sf::Vector2f((float)BOARD_PIXELS, h));
    strip.setPosition(0.f, (float)BOARD_PIXELS - h);
    strip.setFillColor(sf::Color(18, 16, 14, 230));
    window.draw(strip);
    drawCentered(window, text, (float)BOARD_PIXELS / 2.f, (float)BOARD_PIXELS - h + 8.f,
                 15, sf::Color(245, 220, 150), true);
}

void renderSideChooser(sf::RenderTarget& window,
                       const std::map<std::string, sf::Texture>& textures,
                       const sf::Vector2f& buttonSize,
                       const sf::Vector2f& whitePos,
                       const sf::Vector2f& blackPos,
                       const sf::Vector2f& mouse) {
    const float centre = (float)WINDOW_WIDTH / 2.f;
    drawCentered(window, "ChessBot", centre, 96.f, 40, TEXT_MAIN, true);
    drawCentered(window, "Which side would you like to play?", centre, 146.f, 16, TEXT_DIM);

    struct Option { const sf::Vector2f& pos; const char* label; const char* key; const char* texture; };
    const Option options[2] = {
        {whitePos, "Play as White", "W", "wK"},
        {blackPos, "Play as Black", "B", "bK"},
    };

    for (const Option& o : options) {
        const bool hovered = mouse.x >= o.pos.x && mouse.x <= o.pos.x + buttonSize.x &&
                             mouse.y >= o.pos.y && mouse.y <= o.pos.y + buttonSize.y;

        sf::RectangleShape box(buttonSize);
        box.setPosition(o.pos);
        box.setFillColor(hovered ? sf::Color(58, 56, 52) : sf::Color(46, 44, 41));
        box.setOutlineColor(hovered ? ACCENT : sf::Color(70, 68, 63));
        box.setOutlineThickness(2.f);
        window.draw(box);

        // The king of that colour, so the choice reads without the label. It
        // stands on a board-coloured tile: the black king is nearly invisible
        // against the panel's own dark grey.
        if (textures.count(o.texture)) {
            const sf::Texture& tex = textures.at(o.texture);
            const float pieceSize = 64.f;
            const float px = o.pos.x + (buttonSize.x - pieceSize) / 2.f;
            const float py = o.pos.y + 14.f;

            sf::RectangleShape tile(sf::Vector2f(pieceSize, pieceSize));
            tile.setPosition(px, py);
            tile.setFillColor(sf::Color(239, 208, 157));
            window.draw(tile);

            sf::Sprite sprite;
            sprite.setTexture(tex);
            sprite.setScale(pieceSize / tex.getSize().x, pieceSize / tex.getSize().y);
            sprite.setPosition(px, py);
            window.draw(sprite);
        }

        const float cx = o.pos.x + buttonSize.x / 2.f;
        drawCentered(window, o.label, cx, o.pos.y + 84.f, 16, hovered ? TEXT_MAIN : TEXT_DIM, hovered);
        drawCentered(window, std::string("press ") + o.key, cx, o.pos.y + 106.f, 11, TEXT_DIM);
    }
}
