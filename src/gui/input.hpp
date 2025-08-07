#pragma once
#include <SFML/Graphics.hpp>
#include <map>
#include <string>
#include "engine/board.hpp"

class Input {
public:
    Input();

    // Handles mouse events and updates board state for drag-and-drop
    void handleEvent(const sf::Event& event, Board& board);

    // Draws the dragged piece following the mouse (call after renderBoard)
    void drawDraggedPiece(sf::RenderWindow& window, const std::map<std::string, sf::Texture>& textures) const;

    // Returns true if a piece is currently being dragged
    bool isDragging() const;

    // Returns true if a move was just completed
    bool hasCompletedMove() const;

    // Returns the completed move (call only if hasCompletedMove() is true)
    Move getCompletedMove() const;

    // Resets the completed move state (call after processing the move)
    void resetCompletedMove();

    int getSelectedSquare() const { return selectedSquare; }

    std::vector<int> highlightedSquares;
    std::vector<Move> legalMoves;
private:
    bool dragging;
    bool moveCompleted = false;
    Move completedMove;
    int selectedSquare = -1;
    int dragStartX, dragStartY;
    Piece draggedPiece;
    sf::Vector2i mousePos;

    // Helper to convert a Piece to a string for texture lookup
    std::string pieceToString(const Piece& piece) const;
};