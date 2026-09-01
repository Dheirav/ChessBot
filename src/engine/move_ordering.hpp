#pragma once
#include "move.hpp"
#include "board.hpp"
#include <vector>
#include <array>

/**
 * Move ordering utilities for improving search efficiency
 * Implements killer moves, history heuristic, and MVV-LVA
 */
class MoveOrderer {
public:
    static constexpr int MAX_DEPTH = 64;
    static constexpr int MAX_KILLER_MOVES = 2;
    static constexpr int HISTORY_MAX = 16384; // 2^14 for scaling

    // Twelve piece codes: colour (2) x type (6). Piece::type() is 1..6 and
    // Piece::color() is 1..2, so both are shifted down to zero-based here.
    static constexpr int PIECE_CODES = 12;

    // Continuation history is a *refutation* table: what answered well after
    // the opponent played a particular move. Plain history averages a move's
    // worth over the whole search and so forgets the one thing that decides a
    // reply -- what was just played. 12*64*12*64 ints is 2.4 MB, indexed
    // rather than searched, so it costs one array read at ordering time.
    static constexpr size_t CONT_HIST_SIZE =
        (size_t)PIECE_CODES * 64 * PIECE_CODES * 64;

    // Capture history keys on [piece][destination][victim type]. Victim type
    // is 0..6 with 0 unused, which wastes one slot per entry and keeps the
    // index free of arithmetic.
    static constexpr size_t CAPT_HIST_SIZE = (size_t)PIECE_CODES * 64 * 7;

    // Kept small enough that no history bonus can lift a move across the band
    // boundaries in getMoveScore(): the bands are 300 000 apart and these cap
    // at 16 384 and 4 096.
    static constexpr int CAPT_HIST_SHIFT = 2;

    // Upper bound on moves ordered in one call. No legal chess position has
    // more than 218 legal moves; the buffer is stack-allocated, so this caps
    // it rather than allocating per node.
    static constexpr size_t MAX_ORDERED_MOVES = 256;
    
    MoveOrderer();
    
    // Reset all move ordering data
    void clear();
    
    // Order moves for a given position and depth
    // `prevMove` is the move that led to this node, or nullptr at the root and
    // after a null move. Continuation history is the only consumer.
    void orderMoves(MoveList& moves, const Board& board, int depth,
                    Move ttMove = Move(), const Move* prevMove = nullptr) const;
    
    // Update killer moves when a move causes a cutoff
    void updateKillerMove(const Move& move, int depth);
    
    // Update history heuristic when a move causes a cutoff
    void updateHistory(const Move& move, int depth, const Move* prevMove = nullptr);

    // Update capture history when a capture causes a cutoff. Separate from
    // updateHistory because that one deliberately ignores captures.
    void updateCaptureHistory(const Move& move, int depth);
    
    // Reduce history scores periodically to prevent overflow
    void ageHistory();
    
private:
    // Killer moves: moves that caused beta cutoffs at each depth
    std::array<std::array<Move, MAX_KILLER_MOVES>, MAX_DEPTH> killerMoves;
    
    // History heuristic: [from][to] -> score
    std::array<std::array<int, 64>, 64> historyTable;

    // See the size constants above for what each of these is for.
    std::array<int, CONT_HIST_SIZE> contHistory;
    std::array<int, CAPT_HIST_SIZE> captureHistory;
    
    // MVV-LVA (Most Valuable Victim - Least Valuable Attacker) scoring
    int getMVVLVAScore(const Move& move, const Board& board) const;
    
    // Get move priority for ordering
    int getMoveScore(const Move& move, const Board& board, int depth,
                     Move ttMove, const Move* prevMove) const;
    
    // Check if move is a killer move at given depth
    bool isKillerMove(const Move& move, int depth) const;
    
    // Get history score for a move
    int getHistoryScore(const Move& move) const;

    // Zero-based piece code, or -1 for an empty square.
    static int pieceCode(const Piece& p);

    // Flat indices into the two tables above. Both return a negative value when
    // any component is out of range, which the callers treat as "no entry".
    static long contIndex(const Move& prevMove, const Move& move);
    static long captIndex(const Move& move);

    int getContHistScore(const Move& move, const Move* prevMove) const;
    int getCaptHistScore(const Move& move) const;
};

// Global move orderer instance
extern MoveOrderer g_moveOrderer;
