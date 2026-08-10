#include "game_manager.hpp"
#include "engine/movegen.hpp"
#include "engine/chessbot_engine.hpp"
#include <iostream>
#include <algorithm>
#include <ctime>

GameManager::GameManager(std::unique_ptr<IChessEngine> chessEngine) 
    : engine(std::move(chessEngine)), humanSide(COLOR_WHITE), engineSide(COLOR_BLACK), 
      currentState(GameState::WAITING_FOR_PLAYER) {
}

GameManager::~GameManager() {
    if (evaluationLog.is_open()) {
        evaluationLog.close();
    }
    if (engine) {
        engine->shutdown();
    }
}

void GameManager::initializeGame() {
    if (engine) {
        engine->initialize();
    }
    
    // Open evaluation log file
    std::string logFileName = "evaluation_log_";
    logFileName += std::to_string(time(nullptr));
    logFileName += ".txt";
    evaluationLog.open(logFileName);
    
    if (evaluationLog.is_open()) {
        evaluationLog << "FEN,Eval,Material,Mobility,KingSafety,CenterControl,BishopPair,DoubledPawn,IsolatedPawn,PassedPawn,BackwardPawn,ConnectedPawn,PawnChain,RooksOpenFile,RooksSemiOpenFile,Rooks7thRank,PST,Outpost,Trapped,Coordination,KingActivity,Threats,Undefended,Space,Drawish\n";
    }
}

void GameManager::setHumanSide(PieceColor side) {
    humanSide = side;
    engineSide = (side == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
}

void GameManager::startNewGame() {
    // Stop any in-progress search so it can't mutate state mid-reset
    stopEngineAndDiscardPending();
    
    // Reset board to initial position
    board = Board();
    
    // Clear history
    gameHistory.clear();
    moveHistory.clear();
    undoStack.clear();
    redoStack.clear();
    gameResult.clear();
    
    // Save initial state
    gameHistory.push_back(board.getFEN());
    
    // Set initial game state
    updateGameState();
    
    std::cout << "New game started. Human plays as " 
              << (humanSide == COLOR_WHITE ? "White" : "Black") << std::endl;
    std::cout << "Engine: " << engine->getEngineName() << std::endl;
}

void GameManager::loadGameFromFEN(const std::string& fen) {
    stopEngineAndDiscardPending();
    
    if (board.setFromFEN(fen)) {
        gameHistory.clear();
        moveHistory.clear();
        undoStack.clear();
        redoStack.clear();
        gameResult.clear();
        
        gameHistory.push_back(board.getFEN());
        updateGameState();
        
        std::cout << "Game loaded from FEN: " << fen << std::endl;
    } else {
        std::cerr << "Failed to load FEN: " << fen << std::endl;
    }
}

bool GameManager::makeHumanMove(const Move& move) {
    if (!isHumanTurn()) {
        std::cout << "Not human's turn!" << std::endl;
        return false;
    }
    
    if (!isValidMove(move)) {
        std::cout << "Invalid move attempted!" << std::endl;
        return false;
    }
    
    // Save state for undo
    saveStateForUndo();
    
    // Make the move
    board.makeMove(move);
    moveHistory.push_back(move);
    gameHistory.push_back(board.getFEN());
    
    // Clear redo stack since we made a new move
    redoStack.clear();
    
    // Log evaluation
    logEvaluation();
    
    // Update game state
    updateGameState();
    
    std::cout << "Human move: " << move.toString() << std::endl;
    std::cout << "Current evaluation: " << getCurrentEvaluation() << std::endl;
    
    return true;
}

void GameManager::requestEngineMove() {
    if (currentState != GameState::WAITING_FOR_ENGINE) {
        return;
    }
    
    if (engine->isThinking()) {
        // Engine is already thinking, don't start another search
        return;
    }
    
    std::cout << "Requesting engine move..." << std::endl;

    uint64_t generation;
    {
        std::lock_guard<std::mutex> lock(moveMutex);
        if (hasPendingEngineMove) {
            // Engine already finished but the move hasn't been applied yet.
            return;
        }
        generation = searchGeneration;
    }

    // Request move from engine asynchronously. The callback only stores the
    // result; the actual board mutation happens on the main thread via
    // processPendingEngineMove(). Results from a search started before the
    // last position reset are dropped by the generation check.
    engine->findBestMoveAsync(board, [this, generation](const Move& move) {
        std::lock_guard<std::mutex> lock(moveMutex);
        if (generation != searchGeneration) {
            return; // Position changed while searching - stale result
        }
        pendingEngineMove = move;
        hasPendingEngineMove = true;
    });
}

void GameManager::processPendingEngineMove() {
    Move move;
    {
        std::lock_guard<std::mutex> lock(moveMutex);
        if (!hasPendingEngineMove) {
            return;
        }
        move = pendingEngineMove;
        hasPendingEngineMove = false;
    }
    
    // Defensive: if the board changed while the engine was thinking (e.g. an
    // undo), discard the stale move instead of applying it to a new position.
    if (move.from != -1 && move.to != -1 && !isValidMove(move)) {
        std::cout << "Discarding stale engine move: " << move.toString() << std::endl;
        return;
    }
    
    onEngineMove(move);
}

void GameManager::stopEngineThinking() {
    if (engine) {
        engine->stopThinking();
    }
}

void GameManager::discardPendingEngineMove() {
    std::lock_guard<std::mutex> lock(moveMutex);
    hasPendingEngineMove = false;
    pendingEngineMove = Move();
    // Invalidate any in-flight search result that hasn't landed yet.
    searchGeneration++;
}

void GameManager::stopEngineAndDiscardPending() {
    stopEngineThinking();
    discardPendingEngineMove();
}

void GameManager::onEngineMove(const Move& move) {
    if (move.from == -1 || move.to == -1) {
        // Engine couldn't find a move - game over
        updateGameState();
        return;
    }
    
    // Save state for undo
    saveStateForUndo();
    
    // Make the move
    board.makeMove(move);
    moveHistory.push_back(move);
    gameHistory.push_back(board.getFEN());
    
    // Clear redo stack
    redoStack.clear();
    
    // Log evaluation
    logEvaluation();
    
    // Update game state
    updateGameState();
    
    std::cout << "Engine move: " << move.toString() << std::endl;
    std::cout << "Current evaluation: " << getCurrentEvaluation() << std::endl;
}

bool GameManager::isValidMove(const Move& move) const {
    std::vector<Move> legalMoves = getLegalMoves();
    return std::find(legalMoves.begin(), legalMoves.end(), move) != legalMoves.end();
}

std::vector<Move> GameManager::getLegalMoves() const {
    return generateLegalMoves(board, board.activeColor);
}

std::vector<Move> GameManager::getLegalMovesFromSquare(int fromSquare) const {
    std::vector<Move> allMoves = getLegalMoves();
    std::vector<Move> movesFromSquare;
    
    for (const Move& move : allMoves) {
        if (move.from == fromSquare) {
            movesFromSquare.push_back(move);
        }
    }
    
    return movesFromSquare;
}

void GameManager::undoLastMove() {
    if (undoStack.empty()) {
        std::cout << "Nothing to undo!" << std::endl;
        return;
    }

    // Stop the engine so a stale move isn't applied to the undone position
    stopEngineAndDiscardPending();

    // Undo plies until it is the human's turn again. A single-ply undo would
    // leave the engine on move, and it would immediately replay - making
    // undo useless for taking back the human's own move.
    do {
        Move lastMove = moveHistory.empty() ? Move() : moveHistory.back();
        redoStack.push_back({board.getFEN(), lastMove});

        board.setFromFEN(undoStack.back());
        undoStack.pop_back();

        if (!moveHistory.empty()) {
            moveHistory.pop_back();
        }
        if (!gameHistory.empty()) {
            gameHistory.pop_back();
        }
    } while (!undoStack.empty() && board.activeColor != humanSide);

    // Leave any terminal state (undo can revive a finished game) and let
    // updateGameState recompute from the restored position.
    currentState = GameState::WAITING_FOR_PLAYER;
    gameResult.clear();
    updateGameState();
    std::cout << "Move undone" << std::endl;
}

void GameManager::redoLastMove() {
    if (redoStack.empty()) {
        std::cout << "Nothing to redo!" << std::endl;
        return;
    }

    // Stop the engine so a stale move isn't applied to the redone position
    stopEngineAndDiscardPending();

    // Redo plies until it is the human's turn again (mirror of undo),
    // restoring the move/FEN histories that undo removed.
    do {
        saveStateForUndo();

        RedoEntry entry = redoStack.back();
        redoStack.pop_back();

        board.setFromFEN(entry.fen);
        if (entry.move.from != -1) {
            moveHistory.push_back(entry.move);
        }
        gameHistory.push_back(entry.fen);
    } while (!redoStack.empty() && board.activeColor != humanSide);

    currentState = GameState::WAITING_FOR_PLAYER;
    gameResult.clear();
    updateGameState();

    std::cout << "Move redone" << std::endl;
}

void GameManager::resignGame() {
    stopEngineAndDiscardPending();
    currentState = GameState::GAME_OVER_RESIGNATION;
    gameResult = (board.activeColor == humanSide) ? "Human resigned" : "Engine resigned";
    std::cout << gameResult << std::endl;
}

int GameManager::getCurrentEvaluation() const {
    return engine->evaluatePosition(board);
}

void GameManager::updateGameState() {
    if (isGameOver()) {
        return; // Already in a terminal state
    }
    
    std::vector<Move> legalMoves = getLegalMoves();
    
    if (legalMoves.empty()) {
        // No legal moves - checkmate or stalemate
        int kingSq = -1;
        PieceColor currentSide = board.activeColor;
        
        // Find king
        for (int i = 0; i < 64; ++i) {
            if (board.squares[i].type() == KING && board.squares[i].color() == currentSide) {
                kingSq = i;
                break;
            }
        }
        
        std::cout << "DEBUG: No legal moves found for " << (currentSide == COLOR_WHITE ? "White" : "Black") << std::endl;
        std::cout << "DEBUG: King square: " << kingSq << std::endl;
        
        bool kingInCheck = false;
        if (kingSq != -1) {
            PieceColor opponent = (currentSide == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
            kingInCheck = board.isSquareAttacked(kingSq, opponent);
            std::cout << "DEBUG: King in check: " << (kingInCheck ? "YES" : "NO") << std::endl;
        }
        
        if (kingInCheck) {
            currentState = GameState::GAME_OVER_CHECKMATE;
            gameResult = (currentSide == humanSide) ? "Engine wins by checkmate" : "Human wins by checkmate";
            std::cout << "CHECKMATE DETECTED: " << gameResult << std::endl;
        } else {
            currentState = GameState::GAME_OVER_STALEMATE;
            gameResult = "Draw by stalemate";
            std::cout << "STALEMATE DETECTED: " << gameResult << std::endl;
        }
        
        return;
    }

    // Draw detection: fifty-move rule, threefold repetition, insufficient material
    if (board.halfmoveClock >= 100) {
        currentState = GameState::GAME_OVER_DRAW;
        gameResult = "Draw by fifty-move rule";
        std::cout << gameResult << std::endl;
        return;
    }

    if (isThreefoldRepetition()) {
        currentState = GameState::GAME_OVER_DRAW;
        gameResult = "Draw by threefold repetition";
        std::cout << gameResult << std::endl;
        return;
    }

    if (isInsufficientMaterial()) {
        currentState = GameState::GAME_OVER_DRAW;
        gameResult = "Draw by insufficient material";
        std::cout << gameResult << std::endl;
        return;
    }

    // Game continues
    if (board.activeColor == humanSide) {
        currentState = GameState::WAITING_FOR_PLAYER;
    } else {
        currentState = GameState::WAITING_FOR_ENGINE;
    }
}

// Position key for repetition detection: the first four FEN fields (piece
// placement, side to move, castling rights, en passant target). Move
// counters must be ignored - they differ on every repetition.
static std::string repetitionKey(const std::string& fen) {
    size_t pos = 0;
    for (int field = 0; field < 4 && pos != std::string::npos; ++field) {
        pos = fen.find(' ', pos + 1);
    }
    return (pos == std::string::npos) ? fen : fen.substr(0, pos);
}

bool GameManager::isThreefoldRepetition() const {
    std::string currentKey = repetitionKey(board.getFEN());
    int count = 0;
    for (const std::string& fen : gameHistory) {
        if (repetitionKey(fen) == currentKey) {
            count++;
        }
    }
    return count >= 3;
}

bool GameManager::isInsufficientMaterial() const {
    int minorCount = 0;
    for (int i = 0; i < 64; ++i) {
        switch (board.squares[i].type()) {
            case PAWN:
            case ROOK:
            case QUEEN:
                return false; // Mating material exists
            case KNIGHT:
            case BISHOP:
                minorCount++;
                break;
            default:
                break;
        }
    }
    // King vs king, or king + single minor vs king: no forced mate possible
    return minorCount <= 1;
}

void GameManager::logEvaluation() {
    if (!evaluationLog.is_open()) return;
    
    auto evalDetails = evaluate_details(board);
    evaluationLog << board.getFEN() << "," << evalDetails.total << "," 
                  << evalDetails.material << "," << evalDetails.mobility << "," 
                  << evalDetails.kingSafety << "," << evalDetails.centerControl << "," 
                  << evalDetails.bishopPair << "," << evalDetails.doubledPawn << "," 
                  << evalDetails.isolatedPawn << "," << evalDetails.passedPawn << "," 
                  << evalDetails.backwardPawn << "," << evalDetails.connectedPawn << "," 
                  << evalDetails.pawnChain << "," << evalDetails.rooksOpenFile << "," 
                  << evalDetails.rooksSemiOpenFile << "," << evalDetails.rooks7thRank << "," 
                  << evalDetails.pst << "," << evalDetails.outpost << "," 
                  << evalDetails.trapped << "," << evalDetails.coordination << "," 
                  << evalDetails.kingActivity << "," << evalDetails.threats << "," 
                  << evalDetails.undefended << "," << evalDetails.space << "," 
                  << evalDetails.drawish << "\n";
}

bool GameManager::isGameOver() const {
    return currentState == GameState::GAME_OVER_CHECKMATE ||
           currentState == GameState::GAME_OVER_STALEMATE ||
           currentState == GameState::GAME_OVER_DRAW ||
           currentState == GameState::GAME_OVER_RESIGNATION;
}

void GameManager::saveStateForUndo() {
    undoStack.push_back(board.getFEN());
    
    // Limit undo stack size to prevent memory issues
    const size_t MAX_UNDO_STATES = 100;
    if (undoStack.size() > MAX_UNDO_STATES) {
        undoStack.erase(undoStack.begin());
    }
}

void GameManager::clearTranspositionTable() {
    // Try to cast to ChessBotEngine to access TT methods
    if (auto* chessBotEngine = dynamic_cast<ChessBotEngine*>(engine.get())) {
        chessBotEngine->clearTranspositionTable();
    }
}

void GameManager::printTranspositionTableStats() const {
    // Try to cast to ChessBotEngine to access TT methods
    if (auto* chessBotEngine = dynamic_cast<ChessBotEngine*>(engine.get())) {
        chessBotEngine->printTranspositionTableStats();
    }
}
