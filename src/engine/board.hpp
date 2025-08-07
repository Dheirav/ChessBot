#pragma once
#include "piece.hpp"
#include "gui/constants.hpp"
#include "move.hpp"
#include <string>
#include <unordered_map>
#include <vector>

struct AttackMemoKey {
    int square;
    int byColor;
    bool operator==(const AttackMemoKey& other) const {
        return square == other.square && byColor == other.byColor;
    }
};
namespace std {
    template<>
    struct hash<AttackMemoKey> {
        std::size_t operator()(const AttackMemoKey& k) const {
            return std::hash<int>()(k.square) ^ (std::hash<int>()(k.byColor) << 1);
        }
    };
}

class Board {
public:
    Piece squares[BOARD_SIZE *BOARD_SIZE];

    // FEN state fields
    PieceColor activeColor;
    std::string castlingRights;
    std::string enPassantTarget;
    int halfmoveClock;
    int fullmoveNumber;

    // Store the initial FEN as a static constant
    static constexpr const char* INITIAL_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    // static constexpr const char* INITIAL_FEN = " 8/8/8/8/8/8/5r2/4K3 w - - 0 1";
    Board(); // sets up initial position

    bool setFromFEN(const std::string& fen);
    std::string getFEN() const;

    void makeMove(const Move& move);

    // Undo/redo stacks
    std::vector<std::string> undoStack;
    std::vector<std::string> redoStack;

    // Debugging helpers
    void printBoardState(const std::string& context = "") const;
    void printCastlingRights() const;
    void printKingPositions() const;
    void printActiveColor() const;
    void printEnPassantTarget() const;
    void printHalfmoveClock() const;
    void printFullmoveNumber() const;
    char pieceToChar(Piece piece) const;

    // Call this before making a move
    void saveStateForUndo();
    void undoMove();
    void redoMove();

    // Helper functions to convert between 1D and 2D indices
    static int get1DIndex(int file, int rank);
    static int get2DIndex(int index);
    static int getRank(int index);
    static int getFile(int index);

    // Declaration restored for implementation in board.cpp
    bool isSquareAttacked(int square, int byColor) const;
};