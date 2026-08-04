#include <SFML/Graphics.hpp>
#include "src/gui/input.hpp"
#include "src/gui/renderer.hpp"
#include "src/engine/board.hpp"
#include "src/engine/movegen.hpp"
#include <iostream>

int main() {
    std::cout << "=== Testing Promotion UI ===" << std::endl;
    
    // Create window
    sf::RenderWindow window(sf::VideoMode(640, 640), "Promotion Test");
    
    // Load textures
    std::map<std::string, sf::Texture> textures;
    if (!loadPieceTextures(textures)) {
        std::cerr << "Failed to load textures!" << std::endl;
        return 1;
    }
    
    // Create board with promotion position
    Board board;
    // Put white pawn on 7th rank (rank 1 in our array) ready to promote
    std::string testFEN = "k7/P7/8/8/8/8/8/K7 w - - 0 1";
    if (!board.setFromFEN(testFEN)) {
        std::cout << "Failed to set FEN!" << std::endl;
        return 1;
    }
    
    std::cout << "Board FEN: " << board.getFEN() << std::endl;
    
    // Generate moves to check if promotion moves exist
    MoveList moves = generateLegalMoves(board, COLOR_WHITE);
    std::cout << "Generated " << moves.size() << " moves:" << std::endl;
    
    bool hasPromotions = false;
    for (const Move& move : moves) {
        std::cout << "Move: from=" << move.from << " to=" << move.to << " flag=" << move.flag;
        if (move.flag == PROMOTION) {
            std::cout << " promotion=" << static_cast<int>(move.promotionPiece.type());
            hasPromotions = true;
        }
        std::cout << std::endl;
    }
    
    if (!hasPromotions) {
        std::cout << "No promotion moves found!" << std::endl;
        return 1;
    }
    
    Input input;
    
    // Game loop
    while (window.isOpen()) {
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed) {
                window.close();
            }
            
            input.handleEvent(event, board);
            
            if (input.hasCompletedMove()) {
                Move move = input.getCompletedMove();
                std::cout << "Completed move: " << move.toString() << std::endl;
                board.makeMove(move);
                input.resetCompletedMove();
            }
        }
        
        window.clear();
        renderBoard(window, board, textures, input);
        
        // Check if promotion dialog should be shown
        if (input.isPromotionActive()) {
            std::cout << "Promotion is active!" << std::endl;
        }
        
        window.display();
    }
    
    return 0;
}
