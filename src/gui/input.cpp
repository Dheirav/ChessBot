#include "input.hpp"
#include "engine/movegen.hpp"
#include "constants.hpp"
#include "renderer.hpp"

Input::Input()
    : dragging(false), dragFromSquare(-1), draggedPiece(), mousePos(0, 0),
      promotionActive(false) {}

// Clears everything tied to the currently selected piece. The board is never
// modified while a piece is selected or dragged, so dropping the UI state is
// all that "cancel" means here.
void Input::clearSelection() {
    selectedSquare = -1;
    highlightedSquares.clear();
    legalMoves.clear();
    dragging = false;
}

// Selects a square and caches the legal moves leaving it, which both the
// highlights and the move-completion path read.
void Input::selectSquare(int idx, const Board& board) {
    selectedSquare = idx;
    highlightedSquares.clear();
    legalMoves.clear();
    MoveList moves = generateLegalMoves(board, board.activeColor);
    for (const Move& m : moves) {
        if (m.from == idx) {
            highlightedSquares.push_back(m.to);
            legalMoves.push_back(m);
        }
    }
}

// Commits selectedSquare -> to if that is legal, or opens the promotion dialog
// when the destination needs a piece chosen. Returns false when the pair is not
// a legal move, so the caller can treat the click as something else.
bool Input::tryMove(int from, int to, const Board& board) {
    std::vector<Move> promotions;
    for (const Move& m : legalMoves) {
        if (m.from != from || m.to != to) continue;
        if (m.flag == PROMOTION) {
            promotions.push_back(m);
            continue;
        }
        completedMove = m;
        moveCompleted = true;
        clearSelection();
        return true;
    }

    if (!promotions.empty()) {
        // Hold the move open until a piece is picked. Selection state stays as
        // it is; the dialog handler clears it once the choice is made.
        promotionActive = true;
        promotionColor = board.squares[from].color();
        promotionMoves = promotions;
        dragging = false;
        return true;
    }
    return false;
}

// The promotion options, in the order they are drawn and hit-tested.
const PieceType Input::PROMOTION_TYPES[4] = {QUEEN, ROOK, BISHOP, KNIGHT};

// Closes the dialog, playing option i (-1 cancels). The board was never
// modified — the pawn is still on its original square — so cancelling only has
// to drop the UI state.
void Input::finishPromotion(int option) {
    if (option >= 0) {
        for (const Move& move : promotionMoves) {
            if (move.promotionPiece.type() == PROMOTION_TYPES[option]) {
                completedMove = move;
                moveCompleted = true;
                break;
            }
        }
    }
    promotionActive = false;
    promotionMoves.clear();
    promotionHover = -1;
    clearSelection();
}

void Input::handleEvent(const sf::Event& event, Board& board) {
    if (promotionActive) {
        // A click picks the option under the cursor, or cancels if it lands
        // outside the dialog. Hitboxes come from the same helper the dialog is
        // drawn with, so the two cannot drift apart.
        if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left) {
            finishPromotion(promotionOptionAt(event.mouseButton.x, event.mouseButton.y));
            return;
        }
        // Keys, for anyone who would rather not aim: the piece letters choose,
        // Escape cancels.
        if (event.type == sf::Event::KeyPressed) {
            switch (event.key.code) {
                case sf::Keyboard::Q: finishPromotion(0); break;
                case sf::Keyboard::R: finishPromotion(1); break;
                case sf::Keyboard::B: finishPromotion(2); break;
                case sf::Keyboard::N: finishPromotion(3); break;
                case sf::Keyboard::Escape: finishPromotion(-1); break;
                default: break;
            }
            return;
        }
        // Track the cursor so the dialog can show which option it is over.
        if (event.type == sf::Event::MouseMoved) {
            mousePos = {event.mouseMove.x, event.mouseMove.y};
            promotionHover = promotionOptionAt(mousePos.x, mousePos.y);
        }
        // Everything else — board clicks especially — is ignored while the
        // dialog owns the input.
        return;
    }

    if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left) {
        // Bounds-check pixel coordinates before touching board.squares: the
        // click may land on the side panel, and negative coords truncate
        // toward zero.
        int px = event.mouseButton.x;
        int py = event.mouseButton.y;
        int x = px / TILE_SIZE;
        int y = py / TILE_SIZE;
        if (px < 0 || py < 0 || x >= BOARD_SIZE || y >= BOARD_SIZE) {
            // Clicked off the board (panel or outside): keep the selection.
            return;
        }
        int idx = screenToSquare(x, y, flipped);

        // Click-to-move: with a piece already selected, a click on one of its
        // destinations plays the move. This runs before the selection logic so
        // that captures work — the destination holds an enemy piece, which the
        // selection branch below would otherwise just ignore.
        if (selectedSquare != -1 && idx != selectedSquare && tryMove(selectedSquare, idx, board)) {
            return;
        }

        const Piece& piece = board.squares[idx];
        if (piece.type() != NONE && piece.color() == board.activeColor) {
            // Own piece: select it (or keep it selected) and arm a drag, so
            // click-click and drag-and-drop both work from the same press.
            if (selectedSquare != idx) {
                selectSquare(idx, board);
            }
            dragging = true;
            dragFromSquare = idx;
            draggedPiece = piece;
        } else {
            // Empty square, enemy piece, or an illegal destination: deselect.
            clearSelection();
        }
        return;
    }

    // Handle drop
    if (event.type == sf::Event::MouseButtonReleased && event.mouseButton.button == sf::Mouse::Left && dragging) {
        dragging = false;
        int px = event.mouseButton.x;
        int py = event.mouseButton.y;
        int x = px / TILE_SIZE;
        int y = py / TILE_SIZE;
        // Releases outside the window can arrive during a mouse grab; a drop
        // off the board just cancels the drag and keeps the piece selected.
        if (px < 0 || py < 0 || x >= BOARD_SIZE || y >= BOARD_SIZE) {
            return;
        }
        int from = dragFromSquare;
        int to = screenToSquare(x, y, flipped);
        // A release on the square the drag started from is a plain click: the
        // piece stays selected so the next click can name its destination.
        // Anything else is a drop; an illegal one leaves the selection alone.
        if (from != to) {
            tryMove(from, to, board);
        }
        return;
    }

    if (event.type == sf::Event::MouseMoved) {
        mousePos = {event.mouseMove.x, event.mouseMove.y};
    }
}

void Input::drawDraggedPiece(sf::RenderTarget& window, const std::map<std::string, sf::Texture>& textures) const {
    if (dragging && draggedPiece.type() != NONE) {
        std::string name = pieceToString(draggedPiece);
        if (!name.empty() && textures.count(name)) {
            sf::Sprite sprite;
            sprite.setTexture(textures.at(name));
            sprite.setPosition(mousePos.x - TILE_SIZE / 2, mousePos.y - TILE_SIZE / 2);
            sprite.setScale(
                TILE_SIZE / (float)textures.at(name).getSize().x,
                TILE_SIZE / (float)textures.at(name).getSize().y
            );
            window.draw(sprite);
        }
    }
}

bool Input::isDragging() const {
    return dragging;
}

std::string Input::pieceToString(const Piece& piece) const {
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

bool Input::hasCompletedMove() const {
    return moveCompleted;
}

Move Input::getCompletedMove() const {
    return completedMove;
}

void Input::resetCompletedMove() {
    moveCompleted = false;
}

bool Input::isPromotionActive() const {
    return promotionActive;
}

void Input::drawPromotionDialog(sf::RenderTarget& window, const std::map<std::string, sf::Texture>& textures) const {
    if (!promotionActive) return;
    
    // Dim the board only. The dialog is anchored to the board, not the window,
    // because that is the coordinate space the clicks are tested in.
    sf::RectangleShape overlay(sf::Vector2f(BOARD_PIXELS, BOARD_PIXELS));
    overlay.setFillColor(sf::Color(0, 0, 0, 128));
    window.draw(overlay);

    const int dialogX = PROMO_DIALOG_X;
    const int dialogY = PROMO_DIALOG_Y;
    const int dialogWidth = PROMO_DIALOG_W;

    sf::RectangleShape dialogBg(sf::Vector2f(PROMO_DIALOG_W, PROMO_DIALOG_H));
    dialogBg.setPosition(dialogX, dialogY);
    dialogBg.setFillColor(sf::Color(240, 217, 181));
    dialogBg.setOutlineColor(sf::Color(139, 69, 19));
    dialogBg.setOutlineThickness(3);
    window.draw(dialogBg);
    
    // Draw title text, in the font the rest of the UI already loaded once
    {
        sf::Text titleText("Choose promotion piece  (Q R B N, Esc to cancel)", uiFont(), 14);
        titleText.setFillColor(sf::Color::Black);
        // Center the title text
        sf::FloatRect textBounds = titleText.getLocalBounds();
        titleText.setPosition(dialogX + (dialogWidth - textBounds.width) / 2, dialogY + 5);
        window.draw(titleText);
    }

    // Draw promotion piece options
    for (int i = 0; i < 4; i++) {
        int pieceX, pieceY;
        promotionOptionPos(i, pieceX, pieceY);
        const int pieceSize = PROMO_PIECE_SIZE;
        const bool hovered = (i == promotionHover);

        // Draw background for piece. The hovered option lights up, so it is
        // clear which one a click will take before the click is spent.
        sf::RectangleShape pieceBg(sf::Vector2f(pieceSize, pieceSize));
        pieceBg.setPosition(pieceX, pieceY);
        pieceBg.setFillColor(hovered ? sf::Color(255, 246, 200, 255)
                                     : sf::Color(255, 255, 255, 200));
        pieceBg.setOutlineColor(hovered ? sf::Color(0, 160, 210) : sf::Color::Black);
        pieceBg.setOutlineThickness(hovered ? 3.f : 2.f);
        window.draw(pieceBg);

        // Draw piece
        Piece promotionPiece(promotionColor, PROMOTION_TYPES[i]);
        std::string textureName = pieceToString(promotionPiece);
        
        if (!textureName.empty() && textures.count(textureName)) {
            sf::Sprite sprite;
            sprite.setTexture(textures.at(textureName));
            sprite.setPosition(pieceX, pieceY);
            sprite.setScale(
                pieceSize / (float)textures.at(textureName).getSize().x,
                pieceSize / (float)textures.at(textureName).getSize().y
            );
            window.draw(sprite);
        }
    }
}
