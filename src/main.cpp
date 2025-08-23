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
#include <fstream>


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

    // Open evaluation log file
    std::string logFileName = "evaluation_log_";
    logFileName += std::to_string(time(nullptr));
    logFileName += ".txt";
    std::ofstream evalLog(logFileName);
    evalLog << "FEN,Eval,Material,Mobility,KingSafety,CenterControl,BishopPair,DoubledPawn,IsolatedPawn,PassedPawn,BackwardPawn,ConnectedPawn,PawnChain,RooksOpenFile,RooksSemiOpenFile,Rooks7thRank,PST,Outpost,Trapped,Coordination,KingActivity,Threats,Undefended,Space,Drawish\n";

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
                    // Get detailed evaluation
                    auto evalDetails = evaluate_details(board);
                    evalLog << board.getFEN() << "," << evalDetails.total << "," << evalDetails.material << "," << evalDetails.mobility << "," << evalDetails.kingSafety << "," << evalDetails.centerControl << "," << evalDetails.bishopPair << "," << evalDetails.doubledPawn << "," << evalDetails.isolatedPawn << "," << evalDetails.passedPawn << "," << evalDetails.backwardPawn << "," << evalDetails.connectedPawn << "," << evalDetails.pawnChain << "," << evalDetails.rooksOpenFile << "," << evalDetails.rooksSemiOpenFile << "," << evalDetails.rooks7thRank << "," << evalDetails.pst << "," << evalDetails.outpost << "," << evalDetails.trapped << "," << evalDetails.coordination << "," << evalDetails.kingActivity << "," << evalDetails.threats << "," << evalDetails.undefended << "," << evalDetails.space << "," << evalDetails.drawish << "\n";
                    std::cout << "Current evaluation: " << evalDetails.total << std::endl;
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
                // Get detailed evaluation
                auto evalDetails = evaluate_details(board);
                evalLog << board.getFEN() << "," << evalDetails.total << "," << evalDetails.material << "," << evalDetails.mobility << "," << evalDetails.kingSafety << "," << evalDetails.centerControl << "," << evalDetails.bishopPair << "," << evalDetails.doubledPawn << "," << evalDetails.isolatedPawn << "," << evalDetails.passedPawn << "," << evalDetails.backwardPawn << "," << evalDetails.connectedPawn << "," << evalDetails.pawnChain << "," << evalDetails.rooksOpenFile << "," << evalDetails.rooksSemiOpenFile << "," << evalDetails.rooks7thRank << "," << evalDetails.pst << "," << evalDetails.outpost << "," << evalDetails.trapped << "," << evalDetails.coordination << "," << evalDetails.kingActivity << "," << evalDetails.threats << "," << evalDetails.undefended << "," << evalDetails.space << "," << evalDetails.drawish << "\n";
                std::cout << "Current evaluation: " << evalDetails.total << std::endl;
            } else {
                // Check for checkmate or stalemate
                int kingSq = -1;
                PieceColor engineColor = (userSide == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
                for (int i = 0; i < 64; ++i) {
                    if (board.squares[i].type() == KING && board.squares[i].color() == engineColor) {
                        kingSq = i;
                        break;
                    }
                }
                bool isCheckmate = false;
                if (kingSq != -1 && board.isSquareAttacked(kingSq, userSide)) {
                    isCheckmate = true;
                }
                if (isCheckmate) {
                    std::cout << "Checkmate! You win.\n";
                } else {
                    std::cout << "Stalemate! It's a draw.\n";
                }
                evalLog.close();
                return 0;
            }
        }

        window.clear();
        renderBoard(window, board, textures, input);
        input.drawDraggedPiece(window, textures);
        window.display();
    }
    evalLog.close();
    return 0;
}