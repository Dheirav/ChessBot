#include "input.hpp"
#include "engine/movegen.hpp"
#include <cctype>
#include <iostream>

Input::Input()
    : dragging(false), dragStartX(-1), dragStartY(-1), draggedPiece(), mousePos(0, 0) {}

void Input::handleEvent(const sf::Event& event, Board& board) {
    if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left) {
        int x = event.mouseButton.x / TILE_SIZE;
        int y = event.mouseButton.y / TILE_SIZE;
        int idx = board.get1DIndex(x, y);
        const Piece& piece = board.squares[idx];
        if (x >= 0 && x < BOARD_SIZE && y >= 0 && y < BOARD_SIZE && piece.type() != NONE) {
            if (piece.color() == board.activeColor) {
                if (selectedSquare != idx) {
                    selectedSquare = idx;
                    highlightedSquares.clear();
                    legalMoves.clear();
                    MoveList moves = generateMoves(board, board.activeColor);
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
            // Clicked outside the board or on empty square: do nothing, keep highlight
        }
    }
    // Start dragging if a piece is selected and mouse is pressed again on it
    if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left && selectedSquare != -1) {
        int x = event.mouseButton.x / TILE_SIZE;
        int y = event.mouseButton.y / TILE_SIZE;
        int idx = board.get1DIndex(x, y);
        if (idx == selectedSquare) {
            dragging = true;
            dragStartX = x;
            dragStartY = y;
            draggedPiece = board.squares[idx];
        }
    }
    // Handle drop
    if (event.type == sf::Event::MouseButtonReleased && event.mouseButton.button == sf::Mouse::Left && dragging) {
        int x = event.mouseButton.x / TILE_SIZE;
        int y = event.mouseButton.y / TILE_SIZE;
        int from = Board::get1DIndex(dragStartX, dragStartY);
        int to = Board::get1DIndex(x, y);
        if (from != to) {
            // Only allow the move if it is in legalMoves
            bool found = false;
            for (const Move& m : legalMoves) {
                if (m.from == from && m.to == to) {
                    completedMove = m;
                    moveCompleted = true;
                    found = true;
                    break;
                }
            }
            if (!found) {
                // Not a legal move, reset piece
                board.squares[from] = draggedPiece;
            } else {
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
