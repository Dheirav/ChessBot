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
    // The mask from board.hpp is already the table index; see CastlingRight.
    static uint64_t getCastlingHash(uint8_t castlingRights);
    // Square index, or -1 for none. Only the file affects the hash.
    static uint64_t getEnPassantHash(int enPassantSquare);
    
private:
    static uint64_t randomUInt64();
};
