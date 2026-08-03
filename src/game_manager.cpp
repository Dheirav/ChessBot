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
    
    {
        std::lock_guard<std::mutex> lock(moveMutex);
        if (hasPendingEngineMove) {
            // Engine already finished but the move hasn't been applied yet.
            return;
        }
    }
    
    // Request move from engine asynchronously. The callback only stores the
    // result; the actual board mutation happens on the main thread via
    // processPendingEngineMove().
    engine->findBestMoveAsync(board, [this](const Move& move) {
        std::lock_guard<std::mutex> lock(moveMutex);
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
    
    // Save current state to redo stack
    redoStack.push_back(board.getFEN());
    
    // Restore previous state
    std::string previousFEN = undoStack.back();
    undoStack.pop_back();
    
    board.setFromFEN(previousFEN);
    
    // Remove last moves from history
    if (!moveHistory.empty()) {
        moveHistory.pop_back();
    }
    if (!gameHistory.empty()) {
        gameHistory.pop_back();
    }
    
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
    
    // Save current state to undo stack
    saveStateForUndo();
    
    // Restore redo state
    std::string redoFEN = redoStack.back();
    redoStack.pop_back();
    
    board.setFromFEN(redoFEN);
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
    
    // Game continues
    if (board.activeColor == humanSide) {
        currentState = GameState::WAITING_FOR_PLAYER;
    } else {
        currentState = GameState::WAITING_FOR_ENGINE;
    }
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
