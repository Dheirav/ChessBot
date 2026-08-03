#pragma once
#include <cstdint>
#include <string>

/**
 * Zobrist hashing for chess positions
 * Provides fast, incremental hash updates for transposition table
 */
class ZobristHash {
private:
    // Hash values for each piece on each square
    static uint64_t pieceSquareHashes[64][12]; // [square][piece_type * 2 + color]
    
    // Hash values for game state
    static uint64_t sideToMoveHash;
    static uint64_t castlingRightsHashes[16]; // All combinations of KQkq
    static uint64_t enPassantFileHashes[8];   // Files a-h
    
    static bool initialized;
    
public:
    static void initialize();
    
    // Get hash values
    static uint64_t getPieceSquareHash(int square, int pieceType, int color);
    static uint64_t getSideToMoveHash();
    static uint64_t getCastlingHash(const std::string& castlingRights);
    static uint64_t getEnPassantHash(const std::string& enPassantTarget);
    
private:
    static uint64_t randomUInt64();
    static int castlingRightsToIndex(const std::string& rights);
};
