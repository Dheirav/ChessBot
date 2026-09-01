#pragma once
#include "piece.hpp"
#include "gui/constants.hpp"
#include "move.hpp"
#include "zobrist_hash.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

// Castling rights as a 4-bit mask. The bit values are chosen to match the
// zobrist table's index directly, so the mask needs no conversion to be hashed.
enum CastlingRight : uint8_t {
    CASTLE_WK = 1,  // white kingside  'K'
    CASTLE_WQ = 2,  // white queenside 'Q'
    CASTLE_BK = 4,  // black kingside  'k'
    CASTLE_BQ = 8,  // black queenside 'q'
};

// FEN spellings, used only at the FEN and PGN boundaries.
inline uint8_t castlingRightsFromString(const std::string& s) {
    uint8_t mask = 0;
    if (s.find('K') != std::string::npos) mask |= CASTLE_WK;
    if (s.find('Q') != std::string::npos) mask |= CASTLE_WQ;
    if (s.find('k') != std::string::npos) mask |= CASTLE_BK;
    if (s.find('q') != std::string::npos) mask |= CASTLE_BQ;
    return mask;
}

inline std::string castlingRightsToString(uint8_t mask) {
    std::string s;
    if (mask & CASTLE_WK) s += 'K';
    if (mask & CASTLE_WQ) s += 'Q';
    if (mask & CASTLE_BK) s += 'k';
    if (mask & CASTLE_BQ) s += 'q';
    return s.empty() ? "-" : s;
}

// En passant target as a board index, or -1 for none.
inline int enPassantSquareFromString(const std::string& s) {
    if (s.size() != 2 || s == "-") return -1;
    int file = s[0] - 'a';
    int rank = s[1] - '1';
    if (file < 0 || file > 7 || rank < 0 || rank > 7) return -1;
    return (7 - rank) * 8 + file;   // FEN rank 1..8 maps to board y 7..0
}

inline std::string enPassantSquareToString(int square) {
    if (square < 0 || square > 63) return "-";
    std::string s;
    s += (char)('a' + (square % 8));
    s += (char)('1' + (7 - (square / 8)));
    return s;
}

// Data required to reverse a move made via makeMove (see unmakeMove)
struct UndoInfo {
    uint64_t hashBefore = 0;
    uint64_t pawnHashBefore = 0;
    int from = -1;
    int to = -1;
    Piece movedPiece;
    Piece capturedPiece;
    int capturedSquare = -1;      // square the captured piece was removed from (for en passant this differs from 'to')
    int castlingRookFrom = -1;
    int castlingRookTo = -1;
    uint8_t castlingBefore = 0;
    int enPassantBefore = -1;
    int halfmoveBefore = 0;
    int fullmoveBefore = 0;
};

// Data required to reverse a null move (see makeNullMove). A null move changes
// only the side to move, the en passant target and the halfmove clock, so it
// needs far less state than UndoInfo.
struct NullUndo {
    uint64_t hashBefore = 0;
    int enPassantBefore = -1;
    int halfmoveBefore = 0;
};

class Board {
public:
    Piece squares[BOARD_SIZE *BOARD_SIZE];

    // FEN state fields. Castling rights and the en passant target were
    // std::string ("KQkq", "e3"); every makeMove copied both into UndoInfo and
    // re-derived a hash index by scanning characters. They are a bitmask and a
    // square index now, which is also what makes UndoInfo a small POD.
    PieceColor activeColor;
    uint8_t castlingRights;
    int enPassantSquare;
    int halfmoveClock;
    int fullmoveNumber;

    // Store the initial FEN as a static constant
    static constexpr const char* INITIAL_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    // static constexpr const char* INITIAL_FEN = " 8/8/8/8/8/8/5r2/4K3 w - - 0 1";

private:
    // Zobrist hash for transposition table
    uint64_t currentHash;

    // Zobrist hash of the pawns alone, maintained incrementally beside
    // currentHash and deliberately not folded into it.
    //
    // It exists for correction history, which needs a key that is *coarser*
    // than the position: a table indexed by full hash would never see the same
    // entry twice and could not learn anything. Pawn structure is the right
    // grain -- it persists across many moves, and it is what a
    // material-dominated evaluation misprices most consistently.
    //
    // Kept out of currentHash because the transposition table must keep keying
    // on the whole position; two positions with identical pawns are not the
    // same node.
    uint64_t pawnHash;

    // Tag for a lightweight constructor that skips FEN parsing (used by copyForSearch)
    struct SearchCopyTag {};
    explicit Board(SearchCopyTag);
    
public:
    Board(); // sets up initial position

    bool setFromFEN(const std::string& fen);
    std::string getFEN() const;

    UndoInfo makeMove(const Move& move);
    void unmakeMove(const UndoInfo& undo);

    // Pass the turn to the opponent without moving a piece. Used by null-move
    // pruning in the search; never a legal chess move. Must not be called while
    // the side to move is in check.
    NullUndo makeNullMove();
    void unmakeNullMove(const NullUndo& undo);

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
    uint64_t computePawnHash() const;
    uint64_t getHash() const { return currentHash; }
    uint64_t getPawnHash() const { return pawnHash; }
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