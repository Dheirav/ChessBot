
#include <fstream>
#include <filesystem>
#include <vector>
#include <iostream>

// Helper to save/load a vector<int> array
static void saveTable(const std::vector<int> arr[64], const std::string& filename) {
    std::ofstream out(filename, std::ios::binary);
    for (int i = 0; i < 64; ++i) {
        int sz = arr[i].size();
        out.write(reinterpret_cast<const char*>(&sz), sizeof(int));
        out.write(reinterpret_cast<const char*>(arr[i].data()), sz * sizeof(int));
    }
}

static void loadTable(std::vector<int> arr[64], const std::string& filename) {
    std::ifstream in(filename, std::ios::binary);
    for (int i = 0; i < 64; ++i) {
        int sz = 0;
        in.read(reinterpret_cast<char*>(&sz), sizeof(int));
        arr[i].resize(sz);
        in.read(reinterpret_cast<char*>(arr[i].data()), sz * sizeof(int));
    }
}

static bool tablesExist() {
    using std::filesystem::exists;
    return exists("src/engine/lookup_data/rook.dat") && exists("src/engine/lookup_data/bishop.dat") && exists("src/engine/lookup_data/knight.dat") && exists("src/engine/lookup_data/king.dat") && exists("src/engine/lookup_data/white_pawn.dat") && exists("src/engine/lookup_data/black_pawn.dat");
}
#include "move_lookup.hpp"
#include "gui/constants.hpp"

std::vector<int> rookMovesFrom[64];
std::vector<int> bishopMovesFrom[64];
std::vector<int> knightMovesFrom[64];
std::vector<int> kingMovesFrom[64];
std::vector<int> whitePawnMovesFrom[64];
std::vector<int> blackPawnMovesFrom[64];

static const int ROOK_DIRECTIONS[4][2]   = { {0,1}, {1,0}, {0,-1}, {-1,0} };
static const int BISHOP_DIRECTIONS[4][2] = { {1,1}, {1,-1}, {-1,-1}, {-1,1} };
static const int KNIGHT_DELTAS[8][2]     = { {1,2}, {2,1}, {2,-1}, {1,-2}, {-1,-2}, {-2,-1}, {-2,1}, {-1,2} };
static const int KING_DELTAS[8][2]       = { {1,1}, {1,0}, {1,-1}, {0,-1}, {-1,-1}, {-1,0}, {-1,1}, {0,1} };

void initMoveLookupTables() {
    if (tablesExist()) {
        loadTable(rookMovesFrom, "src/engine/lookup_data/rook.dat");
        loadTable(bishopMovesFrom, "src/engine/lookup_data/bishop.dat");
        loadTable(knightMovesFrom, "src/engine/lookup_data/knight.dat");
        loadTable(kingMovesFrom, "src/engine/lookup_data/king.dat");
        loadTable(whitePawnMovesFrom, "src/engine/lookup_data/white_pawn.dat");
        loadTable(blackPawnMovesFrom, "src/engine/lookup_data/black_pawn.dat");
        std::cout << "Loaded move lookup tables from disk." << std::endl;
    } else {
        for (int sq = 0; sq < 64; ++sq) {
            int x = sq % BOARD_SIZE;
            int y = sq / BOARD_SIZE;
            // Rook moves
            for (int d = 0; d < 4; ++d) {
                int dx = ROOK_DIRECTIONS[d][0], dy = ROOK_DIRECTIONS[d][1];
                int nx = x + dx, ny = y + dy;
                while (nx >= 0 && nx < BOARD_SIZE && ny >= 0 && ny < BOARD_SIZE) {
                    rookMovesFrom[sq].push_back(ny * BOARD_SIZE + nx);
                    nx += dx; ny += dy;
                }
            }
            // Bishop moves
            for (int d = 0; d < 4; ++d) {
                int dx = BISHOP_DIRECTIONS[d][0], dy = BISHOP_DIRECTIONS[d][1];
                int nx = x + dx, ny = y + dy;
                while (nx >= 0 && nx < BOARD_SIZE && ny >= 0 && ny < BOARD_SIZE) {
                    bishopMovesFrom[sq].push_back(ny * BOARD_SIZE + nx);
                    nx += dx; ny += dy;
                }
            }
            // Knight moves
            for (int d = 0; d < 8; ++d) {
                int nx = x + KNIGHT_DELTAS[d][0];
                int ny = y + KNIGHT_DELTAS[d][1];
                if (nx >= 0 && nx < BOARD_SIZE && ny >= 0 && ny < BOARD_SIZE) {
                    knightMovesFrom[sq].push_back(ny * BOARD_SIZE + nx);
                }
            }
            // King moves
            for (int d = 0; d < 8; ++d) {
                int nx = x + KING_DELTAS[d][0];
                int ny = y + KING_DELTAS[d][1];
                if (nx >= 0 && nx < BOARD_SIZE && ny >= 0 && ny < BOARD_SIZE) {
                    kingMovesFrom[sq].push_back(ny * BOARD_SIZE + nx);
                }
            }
            // White pawn moves (forward and captures)
            if (y > 0) {
                // Forward one
                whitePawnMovesFrom[sq].push_back((y - 1) * BOARD_SIZE + x);
                // Forward two from rank 2
                if (y == 6)
                    whitePawnMovesFrom[sq].push_back((y - 2) * BOARD_SIZE + x);
                // Captures
                if (x > 0)
                    whitePawnMovesFrom[sq].push_back((y - 1) * BOARD_SIZE + (x - 1));
                if (x < 7)
                    whitePawnMovesFrom[sq].push_back((y - 1) * BOARD_SIZE + (x + 1));
            }
            // Black pawn moves (forward and captures)
            if (y < 7) {
                // Forward one
                blackPawnMovesFrom[sq].push_back((y + 1) * BOARD_SIZE + x);
                // Forward two from rank 7
                if (y == 1)
                    blackPawnMovesFrom[sq].push_back((y + 2) * BOARD_SIZE + x);
                // Captures
                if (x > 0)
                    blackPawnMovesFrom[sq].push_back((y + 1) * BOARD_SIZE + (x - 1));
                if (x < 7)
                    blackPawnMovesFrom[sq].push_back((y + 1) * BOARD_SIZE + (x + 1));
            }
        }
        saveTable(rookMovesFrom, "src/engine/lookup_data/rook.dat");
        saveTable(bishopMovesFrom, "src/engine/lookup_data/bishop.dat");
        saveTable(knightMovesFrom, "src/engine/lookup_data/knight.dat");
        saveTable(kingMovesFrom, "src/engine/lookup_data/king.dat");
        saveTable(whitePawnMovesFrom, "src/engine/lookup_data/white_pawn.dat");
        saveTable(blackPawnMovesFrom, "src/engine/lookup_data/black_pawn.dat");
        std::cout << "Computed and saved move lookup tables to disk." << std::endl;
    }
}