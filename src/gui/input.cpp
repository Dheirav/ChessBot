#include "input.hpp"
#include "engine/movegen.hpp"
#include "constants.hpp"
#include <cctype>
#include <iostream>

Input::Input()
    : dragging(false), dragStartX(-1), dragStartY(-1), draggedPiece(), mousePos(0, 0),
      promotionActive(false), promotionFromSquare(-1), promotionToSquare(-1) {}

void Input::handleEvent(const sf::Event& event, Board& board) {
    // Handle promotion selection clicks first
    if (promotionActive && event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left) {
        int x = event.mouseButton.x;
        int y = event.mouseButton.y;
        
        // Check if click is within promotion dialog area - use same centering as drawing
        int pieceSize = 80;
        int spacing = 10;
        int dialogWidth = 4 * pieceSize + 3 * spacing + 20;
        int dialogHeight = pieceSize + 40;
        
        // Use actual window size
        int windowWidth = TILE_SIZE * BOARD_SIZE;
        int windowHeight = TILE_SIZE * BOARD_SIZE;
        
        int dialogX = (windowWidth - dialogWidth) / 2;
        int dialogY = (windowHeight - dialogHeight) / 2;
        
        for (int i = 0; i < 4; i++) {
            int pieceX = dialogX + 10 + (pieceSize + spacing) * i;
            int pieceY = dialogY + 30;
            
            if (x >= pieceX && x <= pieceX + pieceSize && y >= pieceY && y <= pieceY + pieceSize) {
                // Clicked on promotion piece
                PieceType selectedType;
                switch (i) {
                    case 0: selectedType = QUEEN; break;
                    case 1: selectedType = ROOK; break;
                    case 2: selectedType = BISHOP; break;
                    case 3: selectedType = KNIGHT; break;
                    default: selectedType = QUEEN; break;
                }
                
                // Find the matching promotion move
                for (const Move& move : promotionMoves) {
                    if (move.promotionPiece.type() == selectedType) {
                        completedMove = move;
                        moveCompleted = true;
                        break;
                    }
                }
                
                // Clear promotion state
                promotionActive = false;
                promotionMoves.clear();
                selectedSquare = -1;
                highlightedSquares.clear();
                return;
            }
        }
        
        // Clicked outside promotion dialog - cancel promotion.
        // The board was never modified during the drag (the pawn is still on
        // its original square), so only the UI state needs to be cleared.
        promotionActive = false;
        promotionMoves.clear();
        selectedSquare = -1;
        highlightedSquares.clear();
        legalMoves.clear();
        return;
    }
    
    // Don't handle other events while promotion is active
    if (promotionActive) {
        return;
    }

    if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left) {
        // Bounds-check pixel coordinates before touching board.squares: the
        // window is resizable, and negative coords truncate toward zero.
        int px = event.mouseButton.x;
        int py = event.mouseButton.y;
        int x = px / TILE_SIZE;
        int y = py / TILE_SIZE;
        if (px >= 0 && py >= 0 && x < BOARD_SIZE && y < BOARD_SIZE) {
        int idx = board.get1DIndex(x, y);
        const Piece& piece = board.squares[idx];
        if (piece.type() != NONE) {
            if (piece.color() == board.activeColor) {
                if (selectedSquare != idx) {
                    selectedSquare = idx;
                    highlightedSquares.clear();
                    legalMoves.clear();
                    MoveList moves = generateLegalMoves(board, board.activeColor);
                    std::cout << "All generated moves for side " << board.activeColor << ": ";
                    for (const Move& m : moves) {
                        std::cout << "(" << m.from << "," << m.to << ") ";
                    }
                    std::cout << std::endl;
                    for (const Move& m : moves) {
                        if (m.from == idx && idx >= 0 && idx < 64) {
                            highlightedSquares.push_back(m.to);
                            legalMoves.push_back(m);
                        }
                    }
                    std::cout << "Selected square: " << idx << ", possible moves: ";
                    for (int to : highlightedSquares) std::cout << to << " ";
                    std::cout << std::endl;
                }
                dragging = false;
            } else {
                // Clicked on opponent's piece or empty square: do nothing, keep highlight
            }
        } else {
            // Clicked on empty square: do nothing, keep highlight
        }
        } else {
            // Clicked outside the board: do nothing, keep highlight
        }
    }
    // Start dragging if a piece is selected and mouse is pressed again on it
    if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left && selectedSquare != -1) {
        int px = event.mouseButton.x;
        int py = event.mouseButton.y;
        int x = px / TILE_SIZE;
        int y = py / TILE_SIZE;
        if (px >= 0 && py >= 0 && x < BOARD_SIZE && y < BOARD_SIZE) {
            int idx = board.get1DIndex(x, y);
            if (idx == selectedSquare) {
                dragging = true;
                dragStartX = x;
                dragStartY = y;
                draggedPiece = board.squares[idx];
            }
        }
    }
    // Handle drop
    if (event.type == sf::Event::MouseButtonReleased && event.mouseButton.button == sf::Mouse::Left && dragging) {
        int px = event.mouseButton.x;
        int py = event.mouseButton.y;
        int x = px / TILE_SIZE;
        int y = py / TILE_SIZE;
        // Releases outside the window can arrive during a mouse grab; a drop
        // off the board just cancels the drag (the board was never modified).
        if (px < 0 || py < 0 || x >= BOARD_SIZE || y >= BOARD_SIZE) {
            dragging = false;
            return;
        }
        int from = Board::get1DIndex(dragStartX, dragStartY);
        int to = Board::get1DIndex(x, y);
        if (from != to) {
            // Check if this is a promotion move
            bool found = false;
            bool isPromotionMove = false;
            std::vector<Move> possiblePromotions;
            
            for (const Move& m : legalMoves) {
                if (m.from == from && m.to == to) {
                    if (m.flag == PROMOTION) {
                        isPromotionMove = true;
                        possiblePromotions.push_back(m);
                    } else {
                        // Non-promotion move
                        completedMove = m;
                        moveCompleted = true;
                        found = true;
                        break;
                    }
                }
            }
            
            if (isPromotionMove && !possiblePromotions.empty()) {
                // Start promotion selection
                promotionActive = true;
                promotionFromSquare = from;
                promotionToSquare = to;
                promotionColor = board.squares[from].color();
                promotionMoves = possiblePromotions;
                found = true; // Don't reset the piece position
            }
            
            if (!found) {
                // Not a legal move, reset piece
                board.squares[from] = draggedPiece;
            } else if (!isPromotionMove) {
                // After a move, clear selection and highlights
                selectedSquare = -1;
                highlightedSquares.clear();
            }
        } else {
            board.squares[from] = draggedPiece;
        }
        dragging = false;
    }
    if (event.type == sf::Event::MouseMoved) {
        mousePos = {event.mouseMove.x, event.mouseMove.y};
    }
}

void Input::drawDraggedPiece(sf::RenderWindow& window, const std::map<std::string, sf::Texture>& textures) const {
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

void Input::drawPromotionDialog(sf::RenderWindow& window, const std::map<std::string, sf::Texture>& textures) const {
    if (!promotionActive) return;
    
    // Draw semi-transparent overlay
    sf::RectangleShape overlay(sf::Vector2f(window.getSize().x, window.getSize().y));
    overlay.setFillColor(sf::Color(0, 0, 0, 128));
    window.draw(overlay);
    
    // Draw dialog background - center it on the screen
    int pieceSize = 80;
    int spacing = 10;
    int dialogWidth = 4 * pieceSize + 3 * spacing + 20; // 4 pieces + 3 gaps + padding
    int dialogHeight = pieceSize + 40; // piece height + title space
    
    int dialogX = (window.getSize().x - dialogWidth) / 2;
    int dialogY = (window.getSize().y - dialogHeight) / 2;
    
    sf::RectangleShape dialogBg(sf::Vector2f(dialogWidth, dialogHeight));
    dialogBg.setPosition(dialogX, dialogY);
    dialogBg.setFillColor(sf::Color(240, 217, 181));
    dialogBg.setOutlineColor(sf::Color(139, 69, 19));
    dialogBg.setOutlineThickness(3);
    window.draw(dialogBg);
    
    // Draw title text
    sf::Font font;
    if (font.loadFromFile("src/gui/assets/fonts/arial.ttf")) {
        sf::Text titleText("Choose promotion piece:", font, 16);
        titleText.setFillColor(sf::Color::Black);
        // Center the title text
        sf::FloatRect textBounds = titleText.getLocalBounds();
        titleText.setPosition(dialogX + (dialogWidth - textBounds.width) / 2, dialogY + 5);
        window.draw(titleText);
    }
    
    // Draw promotion piece options
    PieceType promotionTypes[] = {QUEEN, ROOK, BISHOP, KNIGHT};
    
    for (int i = 0; i < 4; i++) {
        int pieceX = dialogX + 10 + (pieceSize + spacing) * i; // 10px padding from dialog edge
        int pieceY = dialogY + 30; // Below the title
        
        // Draw background for piece
        sf::RectangleShape pieceBg(sf::Vector2f(pieceSize, pieceSize));
        pieceBg.setPosition(pieceX, pieceY);
        pieceBg.setFillColor(sf::Color(255, 255, 255, 200));
        pieceBg.setOutlineColor(sf::Color::Black);
        pieceBg.setOutlineThickness(2);
        window.draw(pieceBg);
        
        // Draw piece
        Piece promotionPiece(promotionColor, promotionTypes[i]);
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
