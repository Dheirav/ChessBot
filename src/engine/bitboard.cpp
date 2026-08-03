#include "bitboard.hpp"
#include <iostream>
#include <cassert>

BitboardState::BitboardState() {
    clear();
}

void BitboardState::clear() {
    for (int i = 0; i < 6; ++i) {
        white[i] = 0ULL;
        black[i] = 0ULL;
    }
    occupancyWhite = occupancyBlack = occupancyAll = 0ULL;
}

int popcount(Bitboard b) {
    return __builtin_popcountll(b);
}

int lsb(Bitboard b) {
    assert(b != 0ULL);
    return __builtin_ctzll(b);
}

int msb(Bitboard b) {
    assert(b != 0ULL);
    return 63 - __builtin_clzll(b);
}

void setBit(Bitboard& b, int sq) {
    b |= (1ULL << sq);
}

void clearBit(Bitboard& b, int sq) {
    b &= ~(1ULL << sq);
}

bool testBit(Bitboard b, int sq) {
    return (b >> sq) & 1ULL;
}

void printBitboard(Bitboard b) {
    for (int rank = 7; rank >= 0; --rank) {
        for (int file = 0; file < 8; ++file) {
            int sq = rank * 8 + file;
            std::cout << (testBit(b, sq) ? "1 " : ". ");
        }
        std::cout << std::endl;
    }
}

void printBitboardState(const BitboardState& state) {
    std::cout << "White Pawns:" << std::endl;
    printBitboard(state.white[BB_PAWN]);
    std::cout << "Black Pawns:" << std::endl;
    printBitboard(state.black[BB_PAWN]);
    // ...add more for other piece types as needed
}

// FEN parsing for bitboards
void setBitboardFromFEN(BitboardState& state, const std::string& fen) {
    state.clear();
    int sq = 56; // a8
    std::string::size_type i = 0;
    while (i < fen.size() && fen[i] != ' ') {
        char c = fen[i];
        if (c == '/') {
            sq -= 16; // next rank
        } else if (c >= '1' && c <= '8') {
            sq += (c - '0');
        } else {
            BitboardColor color = (isupper(c) ? BB_WHITE : BB_BLACK);
            BitboardPieceType type = BB_NONE;
            switch (tolower(c)) {
                case 'p': type = BB_PAWN; break;
                case 'n': type = BB_KNIGHT; break;
                case 'b': type = BB_BISHOP; break;
                case 'r': type = BB_ROOK; break;
                case 'q': type = BB_QUEEN; break;
                case 'k': type = BB_KING; break;
                default: type = BB_NONE; break;
            }
            if (type != BB_NONE) {
                if (color == BB_WHITE) setBit(state.white[type], sq);
                else setBit(state.black[type], sq);
            }
            ++sq;
        }
        ++i;
    }
    // Update occupancy
    for (int t = 0; t < 6; ++t) {
        state.occupancyWhite |= state.white[t];
        state.occupancyBlack |= state.black[t];
    }
    state.occupancyAll = state.occupancyWhite | state.occupancyBlack;
}
