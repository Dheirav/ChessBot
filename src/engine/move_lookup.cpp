
#include "move_lookup.hpp"
#include "gui/constants.hpp"
#include <fstream>
#include <filesystem>
#include <vector>
#include <iostream>

// Resolve the move-table directory relative to the executable location so the
static std::string lookupDataDir() {
    std::error_code ec;
    std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        std::filesystem::path dir = exe.parent_path() / "src" / "engine" / "lookup_data";
        return dir.string() + "/";
    }
    return "src/engine/lookup_data/";
}

// Helper to save/load a vector<int> array
static void saveTable(const std::vector<int> arr[64], const std::string& filename) {
    std::ofstream out(filename, std::ios::binary);
    if (!out) return;
    for (int i = 0; i < 64; ++i) {
        int sz = arr[i].size();
        out.write(reinterpret_cast<const char*>(&sz), sizeof(int));
        out.write(reinterpret_cast<const char*>(arr[i].data()), sz * sizeof(int));
    }
}

// Loads a table; returns false if the file is missing, truncated or otherwise
// corrupt so the caller can recompute instead of trusting bad data.
static bool loadTable(std::vector<int> arr[64], const std::string& filename) {
    std::ifstream in(filename, std::ios::binary);
    if (!in) return false;
    for (int i = 0; i < 64; ++i) {
        int sz = 0;
        in.read(reinterpret_cast<char*>(&sz), sizeof(int));
        if (!in || sz < 0 || sz > 28) return false;
        arr[i].resize(sz);
        if (sz > 0) {
            in.read(reinterpret_cast<char*>(arr[i].data()), sz * sizeof(int));
            if (!in) return false;
        }
    }
    return true;
}

static void clearTables() {
    for (int i = 0; i < 64; ++i) {
        rookMovesFrom[i].clear();
        bishopMovesFrom[i].clear();
        knightMovesFrom[i].clear();
        kingMovesFrom[i].clear();
        whitePawnMovesFrom[i].clear();
        blackPawnMovesFrom[i].clear();
    }
}

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
    clearTables();
    std::string dir = lookupDataDir();

    if (loadTable(rookMovesFrom, dir + "rook.dat") &&
        loadTable(bishopMovesFrom, dir + "bishop.dat") &&
        loadTable(knightMovesFrom, dir + "knight.dat") &&
        loadTable(kingMovesFrom, dir + "king.dat") &&
        loadTable(whitePawnMovesFrom, dir + "white_pawn.dat") &&
        loadTable(blackPawnMovesFrom, dir + "black_pawn.dat")) {
        std::cerr << "Loaded move lookup tables from disk." << std::endl;
    } else {
        clearTables();
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
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        saveTable(rookMovesFrom, dir + "rook.dat");
        saveTable(bishopMovesFrom, dir + "bishop.dat");
        saveTable(knightMovesFrom, dir + "knight.dat");
        saveTable(kingMovesFrom, dir + "king.dat");
        saveTable(whitePawnMovesFrom, dir + "white_pawn.dat");
        saveTable(blackPawnMovesFrom, dir + "black_pawn.dat");
        std::cerr << "Computed and saved move lookup tables to disk." << std::endl;
    }
}