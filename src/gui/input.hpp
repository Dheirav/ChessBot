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
    void drawDraggedPiece(sf::RenderTarget& window, const std::map<std::string, sf::Texture>& textures) const;

    // Draws the promotion selection dialog
    void drawPromotionDialog(sf::RenderTarget& window, const std::map<std::string, sf::Texture>& textures) const;

    // Returns true if a piece is currently being dragged
    bool isDragging() const;

    // Returns true if a move was just completed
    bool hasCompletedMove() const;

    // Returns the completed move (call only if hasCompletedMove() is true)
    Move getCompletedMove() const;

    // Resets the completed move state (call after processing the move)
    void resetCompletedMove();

    // Returns true if promotion selection is active
    bool isPromotionActive() const;

    int getSelectedSquare() const { return selectedSquare; }

    // The square a drag started from, or -1 when nothing is being dragged.
    // The renderer skips it: the piece is drawn under the cursor instead, and
    // drawing it in both places showed the same pawn twice.
    int getDragSquare() const { return dragging ? dragFromSquare : -1; }

    // Board orientation: when flipped, black is drawn at the bottom.
    // The renderer reads this so both use the same screen<->square mapping.
    void setFlipped(bool value) { flipped = value; }
    bool isFlipped() const { return flipped; }

    std::vector<int> highlightedSquares;
    std::vector<Move> legalMoves;
private:
    bool dragging;
    bool flipped = false;
    bool moveCompleted = false;
    Move completedMove;
    int selectedSquare = -1;
    int dragFromSquare;   // board index the drag started on
    Piece draggedPiece;
    sf::Vector2i mousePos;

    // Promotion selection state
    bool promotionActive = false;
    int promotionHover = -1;          // option under the cursor, or -1
    PieceColor promotionColor = COLOR_WHITE;
    std::vector<Move> promotionMoves; // All promotion moves for this position

    // The four options, in the order they are drawn, hit-tested and keyed.
    static const PieceType PROMOTION_TYPES[4];
    void finishPromotion(int option);

    // Helper to convert a Piece to a string for texture lookup
    std::string pieceToString(const Piece& piece) const;

    // Selection helpers, shared by the click-to-move and drag-and-drop paths.
    void clearSelection();
    void selectSquare(int idx, const Board& board);
    bool tryMove(int from, int to, const Board& board);
};