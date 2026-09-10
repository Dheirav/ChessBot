#pragma once
//
// Phase B0 of the bitboard replacement (docs/BITBOARD-REPLACEMENT.md).
//
// BitboardState is a board. Position is what a search can actually run on: it
// adds the Zobrist key, the pawn key, the halfmove clock, the move number and
// the repetition history, none of which the generator needed but all of which
// the search does.
//
// It derives from BitboardState rather than wrapping it so that everything
// already written against BitboardState — attackersTo, checkers,
// blockersForKing, the generator — keeps working on a Position unchanged.
//
// The keys are deliberately the *same* keys the mailbox Board produces, built
// from the same ZobristHash tables with the same composition. That is not a
// nicety: the acceptance test for the whole replacement is that the bitboard
// search visits the same nodes as the mailbox search, and it shares a
// transposition table with it during verification. Different keys would mean
// different TT hits and the trees could never be compared.
//
#include "bitboard.hpp"
#include "bitboard_move_gen.hpp"
#include "board.hpp"
#include "move.hpp"
#include <cstdint>
#include <string>
#include <vector>

// BB_PAWN..BB_KING to the mailbox PieceType the zobrist tables are indexed by.
// The two enums disagree on both order and base, and getting this wrong
// produces keys that are self-consistent and therefore pass every test that
// does not compare against the mailbox engine.
inline PieceType toMailboxType(BitboardPieceType t) {
    static constexpr PieceType MAP[6] = { PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING };
    return (t >= BB_PAWN && t <= BB_KING) ? MAP[t] : NONE;
}
inline PieceColor toMailboxColor(BitboardColor c) {
    return (c == BB_WHITE) ? COLOR_WHITE : COLOR_BLACK;
}

struct PositionUndo {
    BitboardUndo bb;
    uint64_t hashBefore = 0;
    uint64_t pawnHashBefore = 0;
    int halfmoveBefore = 0;
    int fullmoveBefore = 1;
};

struct Position : BitboardState {
    // The piece on each square, in the mailbox engine's own encoding.
    //
    // Keeping a square array beside the bitboards is not a hedge or a leftover:
    // it is what every serious bitboard engine does, because the two answer
    // different questions. "Which squares hold a white rook" is a bitboard and
    // is miserable as a loop; "what is on e4" is an array read and is miserable
    // as six bitboard tests, which is exactly what the move generator was doing
    // to find a capture's victim.
    //
    // It is deliberately the same Piece type the mailbox engine uses, so the
    // evaluation can be ported by substituting the accessor rather than by
    // rewriting every square lookup, and so an exact-equality test has a chance
    // of passing.
    Piece squares[64] = {};

    uint64_t hash = 0;
    uint64_t pawnHash = 0;
    int halfmoveClock = 0;
    int fullmoveNumber = 1;

    // Keys of the positions that led here, for threefold detection. The search
    // pushes on make and pops on unmake, so this is the game line plus the
    // current variation, which is what repetition detection needs.
    std::vector<uint64_t> history;

    void setFromFEN(const std::string& fen);
    std::string toFEN() const;

    // Recomputed from scratch. Used to seed a position and to assert in tests
    // that the incremental updates have not drifted.
    uint64_t computeHash() const;
    uint64_t computePawnHash() const;

    BitboardPieceType pieceAt(int sq, BitboardColor& colorOut) const;

    // Rebuild the square array from the bitboards. Called after a bulk load;
    // makeMove keeps it in step from there.
    void syncSquares();

    // Every invariant the incremental updates are supposed to hold: the square
    // array agrees with the bitboards, the occupancies agree with the piece
    // boards, and the keys agree with a fresh recomputation.
    bool consistent() const;

    PositionUndo makeMove(const BitboardMove& move);
    void unmakeMove(const PositionUndo& undo);

    // Passing the turn. Null-move pruning's whole premise is that doing nothing
    // is worse than any real move, so the position has to be reachable without
    // one: the pieces do not move, the side does, and the en passant right is
    // lost exactly as it would be after any move that is not a double push.
    struct NullUndo {
        uint64_t hashBefore = 0;
        int enPassantBefore = -1;
        int halfmoveBefore = 0;
    };
    NullUndo makeNullMove();
    void unmakeNullMove(const NullUndo& undo);

    bool isRepetition(int count = 2) const;
};

// A BitboardMove as the mailbox Move that means the same thing.
//
// The boundary of the bitboard core: everything outside it -- the UCI layer,
// the GUI, the PGN writer, the game manager -- speaks Move, and converting once
// per search result is free. `us` comes from the position the move is played
// from, because BitboardMove carries no colour of its own.
Move toMailboxMove(const Position& pos, const BitboardMove& m);

// Build a Position from a mailbox Board, for cross-checking during
// verification. Everything including the keys and clocks comes across, so the
// two representations can be compared field by field.
Position toPosition(const Board& board);
