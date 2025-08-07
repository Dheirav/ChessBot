#include <SFML/Graphics.hpp>
#include <map>
#include <string>
#include <iostream>
#include "engine/board.hpp"
#include "engine/move.hpp"
#include "engine/movegen.hpp"
#include "engine/move_lookup.hpp"
#include "gui/input.hpp"
#include "gui/renderer.hpp"
#include "gui/constants.hpp"

#include "engine/evaluation.hpp"
#include "engine/search.hpp"


int main() {
    initMoveLookupTables(); // Initialize move lookup tables for move generation
    sf::RenderWindow window(sf::VideoMode(TILE_SIZE * BOARD_SIZE, TILE_SIZE * BOARD_SIZE), "ChessBot");

    std::map<std::string, sf::Texture> textures;
    if (!loadPieceTextures(textures)) return 1;

    Board board;
    Input input;

    // Ask user for side
    PieceColor userSide = COLOR_WHITE;
    std::cout << "Choose your side (w/b): ";
    char sideInput;
    std::cin >> sideInput;
    if (sideInput == 'b' || sideInput == 'B') userSide = COLOR_BLACK;
    board.activeColor = COLOR_WHITE; // Always start with white to move

    while (window.isOpen()) {
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed)
                window.close();

            // Only allow user to move if it's their side to move
            if (board.activeColor == userSide) {
                input.handleEvent(event, board);
                if (input.hasCompletedMove()) {
                    board.saveStateForUndo();
                    Move move = input.getCompletedMove();
                    std::cout << "Board rendered with " << board.getFEN() << std::endl;
                    board.makeMove(move);
                    int eval = evaluate(board);
                    std::cout << "Current evaluation: " << eval << std::endl;
                    input.resetCompletedMove();
                }
            }

            // Undo/redo with keyboard (Ctrl+Z / Ctrl+Y)
            if (event.type == sf::Event::KeyPressed) {
                if ((event.key.control || event.key.system) && event.key.code == sf::Keyboard::Z) {
                    board.undoMove();
                } else if ((event.key.control || event.key.system) && event.key.code == sf::Keyboard::Y) {
                    board.redoMove();
                }
            }
        }

        // Engine plays as opponent
        if (window.isOpen() && board.activeColor != userSide) {
            std::cout << "Engine thinking...\n";
            Move engineMove = findBestMove(board, 3); // Depth 3 for reasonable speed
            if (engineMove.from != -1 && engineMove.to != -1) {
                board.saveStateForUndo();
                board.makeMove(engineMove);
                std::cout << "Engine played: from " << engineMove.from << " to " << engineMove.to << std::endl;
                int eval = evaluate(board);
                std::cout << "Current evaluation: " << eval << std::endl;
            } else {
                std::cout << "No legal moves for engine. Game over?\n";
            }
        }

        window.clear();
        renderBoard(window, board, textures, input);
        input.drawDraggedPiece(window, textures);
        window.display();
    }
    return 0;
}