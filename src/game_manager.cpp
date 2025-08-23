#include "game_manager.hpp"
#include "engine/movegen.hpp"
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
    
    // Request move from engine asynchronously
    engine->findBestMoveAsync(board, [this](const Move& move) {
        this->onEngineMove(move);
    });
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
    return generateMoves(board, board.activeColor);
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
        
        if (kingSq != -1 && board.isSquareAttacked(kingSq, (currentSide == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE)) {
            currentState = GameState::GAME_OVER_CHECKMATE;
            gameResult = (currentSide == humanSide) ? "Engine wins by checkmate" : "Human wins by checkmate";
        } else {
            currentState = GameState::GAME_OVER_STALEMATE;
            gameResult = "Draw by stalemate";
        }
        
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
