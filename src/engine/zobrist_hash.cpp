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

uint64_t ZobristHash::getCastlingHash(uint8_t castlingRights) {
    if (!initialized) initialize();
    // CastlingRight's bit values are the table index, so no conversion is
    // needed — this used to scan a string on every makeMove.
    return castlingRightsHashes[castlingRights & 0x0F];
}

uint64_t ZobristHash::getEnPassantHash(int enPassantSquare) {
    if (!initialized) initialize();
    if (enPassantSquare < 0 || enPassantSquare > 63) return 0;
    return enPassantFileHashes[enPassantSquare % 8];
}


