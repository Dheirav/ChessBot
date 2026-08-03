#include "zobrist_hash.hpp"
#include "piece.hpp"
#include <random>
#include <algorithm>

// Static member definitions
uint64_t ZobristHash::pieceSquareHashes[64][12];
uint64_t ZobristHash::sideToMoveHash;
uint64_t ZobristHash::castlingRightsHashes[16];
uint64_t ZobristHash::enPassantFileHashes[8];
bool ZobristHash::initialized = false;

void ZobristHash::initialize() {
    if (initialized) return;
    
    // Use a fixed seed for reproducible hashes
    std::mt19937_64 rng(0x1234567890ABCDEFULL);
    
    // Initialize piece-square hashes
    for (int square = 0; square < 64; ++square) {
        for (int piece = 0; piece < 12; ++piece) {
            pieceSquareHashes[square][piece] = rng();
        }
    }
    
    // Initialize side to move hash
    sideToMoveHash = rng();
    
    // Initialize castling rights hashes
    for (int i = 0; i < 16; ++i) {
        castlingRightsHashes[i] = rng();
    }
    
    // Initialize en passant file hashes
    for (int file = 0; file < 8; ++file) {
        enPassantFileHashes[file] = rng();
    }
    
    initialized = true;
}

uint64_t ZobristHash::getPieceSquareHash(int square, int pieceType, int color) {
    if (!initialized) initialize();
    
    // Convert piece type and color to index (0-11).
    // PieceType is 1..6 and PieceColor is 1 (white) or 2 (black).
    if (pieceType < KING || pieceType > QUEEN || color < COLOR_WHITE || color > COLOR_BLACK) {
        return 0;
    }
    int index = (pieceType - 1) * 2 + (color - 1);
    return pieceSquareHashes[square][index];
}

uint64_t ZobristHash::getSideToMoveHash() {
    if (!initialized) initialize();
    return sideToMoveHash;
}

uint64_t ZobristHash::getCastlingHash(const std::string& castlingRights) {
    if (!initialized) initialize();
    return castlingRightsHashes[castlingRightsToIndex(castlingRights)];
}

uint64_t ZobristHash::getEnPassantHash(const std::string& enPassantTarget) {
    if (!initialized) initialize();
    
    if (enPassantTarget == "-" || enPassantTarget.empty()) {
        return 0;
    }
    
    // Extract file from en passant target (e.g., "e3" -> file 4)
    int file = enPassantTarget[0] - 'a';
    if (file >= 0 && file < 8) {
        return enPassantFileHashes[file];
    }
    
    return 0;
}

int ZobristHash::castlingRightsToIndex(const std::string& rights) {
    int index = 0;
    
    if (rights.find('K') != std::string::npos) index |= 1;  // White kingside
    if (rights.find('Q') != std::string::npos) index |= 2;  // White queenside
    if (rights.find('k') != std::string::npos) index |= 4;  // Black kingside
    if (rights.find('q') != std::string::npos) index |= 8;  // Black queenside
    
    return index;
}
