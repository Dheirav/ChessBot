#pragma once
#include "engine/board.hpp"
#include "engine/move.hpp"
#include "engine/chess_engine_interface.hpp"
#include "engine/evaluation.hpp"
#include <string>
#include <vector>
#include <fstream>
#include <memory>
#include <mutex>

// Forward declaration
class ChessBotEngine;

enum class GameState {
    WAITING_FOR_PLAYER,
    WAITING_FOR_ENGINE,
    GAME_OVER_CHECKMATE,
    GAME_OVER_STALEMATE,
    GAME_OVER_DRAW,
    GAME_OVER_RESIGNATION
};

enum class PlayerType {
    HUMAN,
    ENGINE
};

/**
 * Manages the chess game state and coordinates between GUI and engine
 * Handles game flow, move validation, and game state transitions
 */
class GameManager {
private:
    Board board;
    std::unique_ptr<IChessEngine> engine;
    
    PieceColor humanSide;
    PieceColor engineSide;

    // Play-along: the engine advises instead of playing (README, "Play-along
    // mode"). Both sides are entered by hand, and every search produces a
    // suggestion to draw rather than a move to make.
    bool coachMode = false;
    Move suggestedMove;
    bool hasSuggestion = false;
    
    GameState currentState;
    
    // Game history and logging
    std::vector<std::string> gameHistory;  // FEN strings
    std::vector<Move> moveHistory;
    std::ofstream evaluationLog;
    bool evaluationLogging = false;
    
    // Undo/Redo management. Redo entries carry the move that produced the
    // position so redoing can restore moveHistory/gameHistory correctly.
    struct RedoEntry {
        std::string fen;
        Move move;
    };
    std::vector<std::string> undoStack;
    std::vector<RedoEntry> redoStack;
    
    // Game result
    std::string gameResult;
    
    // Pending engine move hand-off (engine thread stores, main thread applies)
    mutable std::mutex moveMutex;
    Move pendingEngineMove;
    bool hasPendingEngineMove = false;
    // Bumped (under moveMutex) whenever the position is reset out from under
    // a running search (undo/redo/new game/resign). The engine callback
    // captures the generation at request time and its result is dropped on
    // mismatch, so a search racing against the discard can't sneak a stale
    // move back in.
    uint64_t searchGeneration = 0;
    
public:
    GameManager(std::unique_ptr<IChessEngine> chessEngine);
    ~GameManager();
    
    // Game setup
    void initializeGame();
    // Per-move evaluation dump, off unless chessbot.conf turns it on. Call
    // before initializeGame(), which is where the file is opened.
    void setEvaluationLogging(bool enabled) { evaluationLogging = enabled; }
    void setHumanSide(PieceColor side);
    void startNewGame();
    void loadGameFromFEN(const std::string& fen);
    
    // Game state queries
    const Board& getBoard() const { return board; }
    GameState getGameState() const { return currentState; }
    PieceColor getHumanSide() const { return humanSide; }
    PieceColor getEngineSide() const { return engineSide; }
    PieceColor getCurrentPlayer() const { return board.activeColor; }
    // True once the game has reached a terminal state. Callers that accept
    // input must consult this: several terminal states (the draws, and
    // resignation) still have legal moves available, so "is there a legal
    // move?" is not a substitute for "is the game still running?".
    bool isGameOver() const;
    // In play-along mode there is no engine side: the person enters both
    // colours, so every turn is theirs. Input is gated on this, and the state
    // machine still moves to WAITING_FOR_ENGINE to trigger the search that
    // produces the suggestion -- which is why this must not consult the state.
    bool isHumanTurn() const {
        return !isGameOver() && (coachMode || board.activeColor == humanSide);
    }

    void setCoachMode(bool on) { coachMode = on; }
    bool isCoachMode() const { return coachMode; }
    // The move the engine would play here, or from == -1 if it has not
    // finished thinking about this position yet.
    Move getSuggestion() const { return hasSuggestion ? suggestedMove : Move(); }
    bool isEngineThinking() const { return engine->isThinking(); }
    
    // Move handling
    bool makeHumanMove(const Move& move);
    void requestEngineMove();
    bool isValidMove(const Move& move) const;
    std::vector<Move> getLegalMoves() const;
    std::vector<Move> getLegalMovesFromSquare(int fromSquare) const;
    
    // Applies an engine move that finished computing on the engine thread.
    // Must be called on the main thread (e.g. every GUI update tick).
    void processPendingEngineMove();
    void stopEngineThinking();
    
    // Game control
    void undoLastMove();
    void redoLastMove();
    void resignGame();
    
    // Game information
    std::string getGameResult() const { return gameResult; }

    // The game as PGN, written under `directory` (created if missing). Returns
    // the path written, or "" on failure. The engine could play a whole game
    // and then had no way to hand it to anything that reads chess.
    std::string savePgn(const std::string& directory = "games") const;
    int getCurrentEvaluation() const;
    const std::vector<Move>& getMoveHistory() const { return moveHistory; }

    // Zobrist keys of the positions before the current one, back to the last
    // irreversible move — what a search needs in order to see a repetition
    // rather than only the ones it invents inside its own tree (BUGS.md 1).
    //
    // Derived from gameHistory rather than tracked beside it. gameHistory is
    // already kept correct across undo and redo; a second list would be correct
    // only until the first time the two drifted, and this is rebuilt once per
    // engine move against a search that then thinks for seconds.
    std::vector<uint64_t> repetitionHistory() const;
    std::string getCurrentFEN() const { return board.getFEN(); }
    
    // Engine control
    void setEngineDepth(int depth) { engine->setSearchDepth(depth); }
    int getEngineDepth() const { return engine->getSearchDepth(); }
    // The engine's per-move time budget, for the GUI's progress display.
    // 0 when the engine is searching by depth alone.
    long getEngineMoveTimeMs() const;
    std::string getEngineName() const { return engine->getEngineName(); }
    
    // Transposition table control (if engine supports it)
    void clearTranspositionTable();
    void printTranspositionTableStats() const;
    
private:
    void updateGameState();
    bool isThreefoldRepetition() const;
    bool isInsufficientMaterial() const;
    void logEvaluation();
    void onEngineMove(const Move& move);
    void saveStateForUndo();
    void discardPendingEngineMove();
    void stopEngineAndDiscardPending();
};
