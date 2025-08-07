#pragma once
#include <vector>

extern std::vector<int> rookMovesFrom[64];
extern std::vector<int> bishopMovesFrom[64];
extern std::vector<int> knightMovesFrom[64];
extern std::vector<int> kingMovesFrom[64];
extern std::vector<int> whitePawnMovesFrom[64];
extern std::vector<int> blackPawnMovesFrom[64];

void initMoveLookupTables();