#pragma once
#include <cstdint>
#include <array>

constexpr int NUM_SQUARES = 64;
constexpr int ROOK_MAGIC_BITS = 12;
constexpr int BISHOP_MAGIC_BITS = 9;

// Magic bitboard tables for rook and bishop
extern const uint64_t rookMagicNumbers[NUM_SQUARES];
extern const uint64_t bishopMagicNumbers[NUM_SQUARES];

extern uint64_t rookMasks[NUM_SQUARES];
extern uint64_t bishopMasks[NUM_SQUARES];

extern std::array<std::array<uint64_t, 4096>, NUM_SQUARES> rookAttackTable;
extern std::array<std::array<uint64_t, 512>, NUM_SQUARES> bishopAttackTable;

// Initialization
void initMagicBitboards();

// Attack lookup
uint64_t getRookAttacks(int sq, uint64_t occupancy);
uint64_t getBishopAttacks(int sq, uint64_t occupancy);
