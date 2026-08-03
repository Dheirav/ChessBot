#include "magic_bitboards.hpp"
#include <cstring>
#include <vector>

// Stockfish magic numbers for rooks
const uint64_t rookMagicNumbers[NUM_SQUARES] = {
    0x8a80104000800020ULL, 0x140002000100040ULL, 0x2801880a0017001ULL, 0x100081001000420ULL,
    0x200020010080420ULL, 0x3001c0002010008ULL, 0x8480008002000100ULL, 0x2080088004402900ULL,
    0x800098204000ULL, 0x2024401000200040ULL, 0x100802000801000ULL, 0x120800800801000ULL,
    0x208808088000400ULL, 0x2802200800400ULL, 0x2200800100020080ULL, 0x801000060821100ULL,
    0x80044006422000ULL, 0x100808020004000ULL, 0x12108a0010204200ULL, 0x140848010000802ULL,
    0x481828014002800ULL, 0x8094004002004100ULL, 0x4010040010010802ULL, 0x20008806104ULL,
    0x100400080208000ULL, 0x2040002120081000ULL, 0x21200680100081ULL, 0x20100080080080ULL,
    0x2000a00200410ULL, 0x20080800400ULL, 0x80088400100102ULL, 0x80004600042881ULL,
    0x4040008040800020ULL, 0x440003000200801ULL, 0x4200011004500ULL, 0x188020010100100ULL,
    0x14800401802800ULL, 0x2080040080800200ULL, 0x124080204001001ULL, 0x200046502000484ULL,
    0x480400080088020ULL, 0x1000422010034000ULL, 0x30200100110040ULL, 0x100021010009ULL,
    0x2002080100110004ULL, 0x202008004008002ULL, 0x20020004010100ULL, 0x2048440040820001ULL,
    0x101002200408200ULL, 0x40802000401080ULL, 0x4008142004410100ULL, 0x2060820c0120200ULL,
    0x1001004080100ULL, 0x20c020080040080ULL, 0x2935610830022400ULL, 0x44440041009200ULL,
    0x280001040802101ULL, 0x2100190040002085ULL, 0x80c0084100102001ULL, 0x4024081001000421ULL,
    0x20030a0244872ULL, 0x12001008414402ULL, 0x2006104900a0804ULL, 0x1004081002402ULL
};

// Stockfish magic numbers for bishops
const uint64_t bishopMagicNumbers[NUM_SQUARES] = {
    0x40040844404084ULL, 0x2004208a004208ULL, 0x10190041080202ULL, 0x108060845042010ULL,
    0x581104180800210ULL, 0x2112080446200010ULL, 0x1080820820060210ULL, 0x3c0808410220200ULL,
    0x4050404440404ULL, 0x21001420088ULL, 0x24d0080801082102ULL, 0x1020a0a020400ULL,
    0x40308200402ULL, 0x4011002100800ULL, 0x401484104104005ULL, 0x801010402020200ULL,
    0x400210c3880100ULL, 0x404022024108200ULL, 0x810018200204102ULL, 0x4002801a02003ULL,
    0x85040820080400ULL, 0x810102c808880400ULL, 0xe900410884800ULL, 0x8002020480840102ULL,
    0x220200865090201ULL, 0x2010100a02021202ULL, 0x152048408022401ULL, 0x20080002081110ULL,
    0x4001001021004000ULL, 0x800040400a011002ULL, 0xe4004081011002ULL, 0x1c004001012080ULL,
    0x8004200962a00220ULL, 0x8422100208500202ULL, 0x2000402200300c08ULL, 0x8646020080080080ULL,
    0x80020a0200100808ULL, 0x2010004880111000ULL, 0x623000a080011400ULL, 0x42008c0340209202ULL,
    0x209188240001000ULL, 0x400408a884001800ULL, 0x110400a6080400ULL, 0x1840060a44020800ULL,
    0x90080104000041ULL, 0x201011000808101ULL, 0x1a2208080504f080ULL, 0x801202060021121ULL,
    0x500861011240000ULL, 0x180806108200800ULL, 0x4000020e01040044ULL, 0x300000261044000aULL,
    0x802241102020002ULL, 0x20906061210001ULL, 0x5a84841004010310ULL, 0x4010801011c04ULL,
    0xa010109502200ULL, 0x4a02012000ULL, 0x500201010098b028ULL, 0x8040002811040900ULL,
    0x28000010020204ULL, 0x6000020202d0240ULL, 0x8918844842082200ULL, 0x4010011029020020ULL
};

uint64_t rookMasks[NUM_SQUARES];
uint64_t bishopMasks[NUM_SQUARES];
std::array<std::array<uint64_t, 4096>, NUM_SQUARES> rookAttackTable;
std::array<std::array<uint64_t, 512>, NUM_SQUARES> bishopAttackTable;

// Helper: generate mask for rook attacks from a square
uint64_t maskRookAttacks(int sq) {
    uint64_t mask = 0ULL;
    int rank = sq / 8, file = sq % 8;
    for (int r = rank + 1; r < 7; ++r) mask |= (1ULL << (r * 8 + file));
    for (int r = rank - 1; r > 0; --r) mask |= (1ULL << (r * 8 + file));
    for (int f = file + 1; f < 7; ++f) mask |= (1ULL << (rank * 8 + f));
    for (int f = file - 1; f > 0; --f) mask |= (1ULL << (rank * 8 + f));
    return mask;
}

// Helper: generate mask for bishop attacks from a square
uint64_t maskBishopAttacks(int sq) {
    uint64_t mask = 0ULL;
    int rank = sq / 8, file = sq % 8;
    for (int r = rank + 1, f = file + 1; r < 7 && f < 7; ++r, ++f) mask |= (1ULL << (r * 8 + f));
    for (int r = rank + 1, f = file - 1; r < 7 && f > 0; ++r, --f) mask |= (1ULL << (r * 8 + f));
    for (int r = rank - 1, f = file + 1; r > 0 && f < 7; --r, ++f) mask |= (1ULL << (r * 8 + f));
    for (int r = rank - 1, f = file - 1; r > 0 && f > 0; --r, --f) mask |= (1ULL << (r * 8 + f));
    return mask;
}

// Helper: enumerate all blocker subsets for a mask
std::vector<uint64_t> generateBlockerSubsets(uint64_t mask) {
    std::vector<uint64_t> subsets;
    int bits = __builtin_popcountll(mask);
    int num = 1 << bits;
    for (int i = 0; i < num; ++i) {
        uint64_t subset = 0ULL;
        int idx = 0;
        for (int sq = 0; sq < 64; ++sq) {
            if (mask & (1ULL << sq)) {
                if (i & (1 << idx)) subset |= (1ULL << sq);
                ++idx;
            }
        }
        subsets.push_back(subset);
    }
    return subsets;
}

// Helper: generate rook attacks for a square given blockers
uint64_t computeRookAttacks(int sq, uint64_t blockers) {
    uint64_t attacks = 0ULL;
    int rank = sq / 8, file = sq % 8;
    // North
    for (int r = rank + 1; r < 8; ++r) {
        int s = r * 8 + file;
        attacks |= (1ULL << s);
        if (blockers & (1ULL << s)) break;
    }
    // South
    for (int r = rank - 1; r >= 0; --r) {
        int s = r * 8 + file;
        attacks |= (1ULL << s);
        if (blockers & (1ULL << s)) break;
    }
    // East
    for (int f = file + 1; f < 8; ++f) {
        int s = rank * 8 + f;
        attacks |= (1ULL << s);
        if (blockers & (1ULL << s)) break;
    }
    // West
    for (int f = file - 1; f >= 0; --f) {
        int s = rank * 8 + f;
        attacks |= (1ULL << s);
        if (blockers & (1ULL << s)) break;
    }
    return attacks;
}

// Helper: generate bishop attacks for a square given blockers
uint64_t computeBishopAttacks(int sq, uint64_t blockers) {
    uint64_t attacks = 0ULL;
    int rank = sq / 8, file = sq % 8;
    // NE
    for (int r = rank + 1, f = file + 1; r < 8 && f < 8; ++r, ++f) {
        int s = r * 8 + f;
        attacks |= (1ULL << s);
        if (blockers & (1ULL << s)) break;
    }
    // NW
    for (int r = rank + 1, f = file - 1; r < 8 && f >= 0; ++r, --f) {
        int s = r * 8 + f;
        attacks |= (1ULL << s);
        if (blockers & (1ULL << s)) break;
    }
    // SE
    for (int r = rank - 1, f = file + 1; r >= 0 && f < 8; --r, ++f) {
        int s = r * 8 + f;
        attacks |= (1ULL << s);
        if (blockers & (1ULL << s)) break;
    }
    // SW
    for (int r = rank - 1, f = file - 1; r >= 0 && f >= 0; --r, --f) {
        int s = r * 8 + f;
        attacks |= (1ULL << s);
        if (blockers & (1ULL << s)) break;
    }
    return attacks;
}

void initMagicBitboards() {
    for (int sq = 0; sq < NUM_SQUARES; ++sq) {
        rookMasks[sq] = maskRookAttacks(sq);
        bishopMasks[sq] = maskBishopAttacks(sq);

        // Rook attack table
        auto rookBlockers = generateBlockerSubsets(rookMasks[sq]);
        for (size_t i = 0; i < rookBlockers.size(); ++i) {
            uint64_t blockers = rookBlockers[i];
            uint64_t magicIndex = ((blockers * rookMagicNumbers[sq]) >> (64 - ROOK_MAGIC_BITS));
            rookAttackTable[sq][magicIndex] = computeRookAttacks(sq, blockers);
        }

        // Bishop attack table
        auto bishopBlockers = generateBlockerSubsets(bishopMasks[sq]);
        for (size_t i = 0; i < bishopBlockers.size(); ++i) {
            uint64_t blockers = bishopBlockers[i];
            uint64_t magicIndex = ((blockers * bishopMagicNumbers[sq]) >> (64 - BISHOP_MAGIC_BITS));
            bishopAttackTable[sq][magicIndex] = computeBishopAttacks(sq, blockers);
        }
    }
}

uint64_t getRookAttacks(int sq, uint64_t occupancy) {
    uint64_t blockers = occupancy & rookMasks[sq];
    uint64_t magicIndex = ((blockers * rookMagicNumbers[sq]) >> (64 - ROOK_MAGIC_BITS));
    return rookAttackTable[sq][magicIndex];
}

uint64_t getBishopAttacks(int sq, uint64_t occupancy) {
    uint64_t blockers = occupancy & bishopMasks[sq];
    uint64_t magicIndex = ((blockers * bishopMagicNumbers[sq]) >> (64 - BISHOP_MAGIC_BITS));
    return bishopAttackTable[sq][magicIndex];
}
