#pragma once
#include "piece.hpp"
#include "gui/constants.hpp"
#include "move.hpp"
#include "zobrist_hash.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

// Data required to reverse a move made via makeMove (see unmakeMove)
struct UndoInfo {
    uint64_t hashBefore = 0;
    int from = -1;
    int to = -1;
    Piece movedPiece;
    Piece capturedPiece;
    int capturedSquare = -1;      // square the captured piece was removed from (for en passant this differs from 'to')
    int castlingRookFrom = -1;
    int castlingRookTo = -1;
    std::string castlingBefore;
    std::string enPassantBefore;
    int halfmoveBefore = 0;
    int fullmoveBefore = 0;
};

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

private:
    // Zobrist hash for transposition table
    uint64_t currentHash;

    // Tag for a lightweight constructor that skips FEN parsing (used by copyForSearch)
    struct SearchCopyTag {};
    explicit Board(SearchCopyTag);
    
public:
    Board(); // sets up initial position

    bool setFromFEN(const std::string& fen);
    std::string getFEN() const;

    UndoInfo makeMove(const Move& move);
    void unmakeMove(const UndoInfo& undo);

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

    // Hash functions for transposition table
    uint64_t computeHash() const;
    uint64_t getHash() const { return currentHash; }
    void updateHash();

    // Lightweight copy for search: copies the position state but skips the
    // GUI undo/redo stacks (FEN strings), which the search never uses.
    Board copyForSearch() const;

    // Helper functions to convert between 1D and 2D indices
    static int get1DIndex(int file, int rank);
    static int get2DIndex(int index);
    static int getRank(int index);
    static int getFile(int index);

    // Declaration restored for implementation in board.cpp
    bool isSquareAttacked(int square, int byColor) const;
};