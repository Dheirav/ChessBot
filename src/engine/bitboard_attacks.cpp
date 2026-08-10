#include "bitboard_attacks.hpp"
#include "magic_bitboards.hpp"

#include <array>

namespace {

std::array<Bitboard, 64> g_knight{};
std::array<Bitboard, 64> g_king{};
std::array<std::array<Bitboard, 64>, 2> g_pawn{};

// [from][to]: squares strictly between, and the whole line through, two
// squares. Built once by walking rays, which is both obviously correct and
// irrelevant to performance since it happens at startup.
std::array<std::array<Bitboard, 64>, 64> g_between{};
std::array<std::array<Bitboard, 64>, 64> g_line{};

bool g_initialized = false;

// Offsets are applied in (file, rank) space with explicit bounds checks rather
// than as shifts with wrap masks. Shift-and-mask is the idiomatic form, but it
// is also where files wrap around the board edge and produce a knight that
// teleports; at init time the clarity is free.
void addLeaper(std::array<Bitboard, 64>& table, const int deltas[][2], int count) {
    for (int sq = 0; sq < 64; ++sq) {
        const int x = sq % 8, y = sq / 8;
        Bitboard bb = 0;
        for (int i = 0; i < count; ++i) {
            const int nx = x + deltas[i][0], ny = y + deltas[i][1];
            if (nx < 0 || nx > 7 || ny < 0 || ny > 7) continue;
            bb |= 1ULL << (ny * 8 + nx);
        }
        table[sq] = bb;
    }
}

}  // namespace

void initBitboardAttacks() {
    if (g_initialized) return;
    g_initialized = true;

    initMagicBitboards();

    static const int KNIGHT_D[8][2] = { {1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,-1},{-2,1},{-1,2} };
    static const int KING_D[8][2]   = { {1,1},{1,0},{1,-1},{0,1},{0,-1},{-1,1},{-1,0},{-1,-1} };
    addLeaper(g_knight, KNIGHT_D, 8);
    addLeaper(g_king, KING_D, 8);

    // White moves toward lower indices (y = 0 is the eighth rank), so a white
    // pawn attacks the two squares one row *up* the board from it.
    for (int sq = 0; sq < 64; ++sq) {
        const int x = sq % 8, y = sq / 8;
        Bitboard w = 0, b = 0;
        if (y > 0) {
            if (x > 0) w |= 1ULL << ((y - 1) * 8 + x - 1);
            if (x < 7) w |= 1ULL << ((y - 1) * 8 + x + 1);
        }
        if (y < 7) {
            if (x > 0) b |= 1ULL << ((y + 1) * 8 + x - 1);
            if (x < 7) b |= 1ULL << ((y + 1) * 8 + x + 1);
        }
        g_pawn[BB_WHITE][sq] = w;
        g_pawn[BB_BLACK][sq] = b;
    }

    // Between and line tables, by walking each of the eight directions.
    static const int DIRS[8][2] = { {0,1},{0,-1},{1,0},{-1,0},{1,1},{1,-1},{-1,1},{-1,-1} };
    for (int from = 0; from < 64; ++from) {
        for (const auto& d : DIRS) {
            Bitboard path = 0;
            int x = from % 8, y = from / 8;
            while (true) {
                x += d[0];
                y += d[1];
                if (x < 0 || x > 7 || y < 0 || y > 7) break;
                const int to = y * 8 + x;
                g_between[from][to] = path;   // squares passed so far, excluding both ends
                path |= 1ULL << to;
            }
            // Walk the same direction again to build the full line: every
            // square reachable from `from` this way shares a line with it.
            Bitboard whole = 1ULL << from;
            x = from % 8; y = from / 8;
            while (true) {
                x += d[0]; y += d[1];
                if (x < 0 || x > 7 || y < 0 || y > 7) break;
                whole |= 1ULL << (y * 8 + x);
            }
            // ...and so does everything in the opposite direction.
            x = from % 8; y = from / 8;
            while (true) {
                x -= d[0]; y -= d[1];
                if (x < 0 || x > 7 || y < 0 || y > 7) break;
                whole |= 1ULL << (y * 8 + x);
            }
            x = from % 8; y = from / 8;
            while (true) {
                x += d[0]; y += d[1];
                if (x < 0 || x > 7 || y < 0 || y > 7) break;
                g_line[from][y * 8 + x] = whole;
            }
        }
    }
}

Bitboard knightAttacks(int sq) { return g_knight[sq]; }
Bitboard kingAttacks(int sq)   { return g_king[sq]; }
Bitboard pawnAttacks(BitboardColor side, int sq) { return g_pawn[side][sq]; }

Bitboard rookAttacks(int sq, Bitboard occupancy)   { return getRookAttacks(sq, occupancy); }
Bitboard bishopAttacks(int sq, Bitboard occupancy) { return getBishopAttacks(sq, occupancy); }
Bitboard queenAttacks(int sq, Bitboard occupancy) {
    return getRookAttacks(sq, occupancy) | getBishopAttacks(sq, occupancy);
}

Bitboard betweenSquares(int a, int b) { return g_between[a][b]; }
Bitboard lineThrough(int a, int b)    { return g_line[a][b]; }

Bitboard attackersTo(const BitboardState& state, int sq, Bitboard occupancy) {
    // Pawn attacks are asymmetric, so the test is inverted: a white pawn
    // attacks sq exactly when sq's *black* pawn-attack set contains that pawn.
    const Bitboard rookLike   = state.white[BB_ROOK]   | state.black[BB_ROOK]
                              | state.white[BB_QUEEN]  | state.black[BB_QUEEN];
    const Bitboard bishopLike = state.white[BB_BISHOP] | state.black[BB_BISHOP]
                              | state.white[BB_QUEEN]  | state.black[BB_QUEEN];

    return (pawnAttacks(BB_BLACK, sq) & state.white[BB_PAWN])
         | (pawnAttacks(BB_WHITE, sq) & state.black[BB_PAWN])
         | (knightAttacks(sq) & (state.white[BB_KNIGHT] | state.black[BB_KNIGHT]))
         | (kingAttacks(sq)   & (state.white[BB_KING]   | state.black[BB_KING]))
         | (rookAttacks(sq, occupancy)   & rookLike)
         | (bishopAttacks(sq, occupancy) & bishopLike);
}

Bitboard attackersTo(const BitboardState& state, int sq, Bitboard occupancy,
                     BitboardColor bySide) {
    const Bitboard side = (bySide == BB_WHITE) ? state.occupancyWhite : state.occupancyBlack;
    return attackersTo(state, sq, occupancy) & side;
}

bool isSquareAttackedBB(const BitboardState& state, int sq, BitboardColor bySide) {
    return attackersTo(state, sq, state.occupancyAll, bySide) != 0;
}

int kingSquare(const BitboardState& state, BitboardColor side) {
    const Bitboard k = (side == BB_WHITE) ? state.white[BB_KING] : state.black[BB_KING];
    return k ? lsb(k) : -1;
}

Bitboard checkers(const BitboardState& state, BitboardColor side) {
    const int ksq = kingSquare(state, side);
    if (ksq < 0) return 0;
    const BitboardColor them = (side == BB_WHITE) ? BB_BLACK : BB_WHITE;
    return attackersTo(state, ksq, state.occupancyAll, them);
}

Bitboard blockersForKing(const BitboardState& state, BitboardColor side,
                         Bitboard* pinners) {
    Bitboard result = 0;
    if (pinners) *pinners = 0;

    const int ksq = kingSquare(state, side);
    if (ksq < 0) return 0;

    const BitboardColor them = (side == BB_WHITE) ? BB_BLACK : BB_WHITE;
    const Bitboard ourPieces = (side == BB_WHITE) ? state.occupancyWhite : state.occupancyBlack;
    const Bitboard theirRookLike   = (them == BB_WHITE)
        ? (state.white[BB_ROOK] | state.white[BB_QUEEN])
        : (state.black[BB_ROOK] | state.black[BB_QUEEN]);
    const Bitboard theirBishopLike = (them == BB_WHITE)
        ? (state.white[BB_BISHOP] | state.white[BB_QUEEN])
        : (state.black[BB_BISHOP] | state.black[BB_QUEEN]);

    // Sliders that would attack the king if the board were empty: only these
    // can possibly be pinning something. Testing with an empty occupancy is
    // what makes the pieces in the way irrelevant to the first step.
    Bitboard candidates = (rookAttacks(ksq, 0) & theirRookLike)
                        | (bishopAttacks(ksq, 0) & theirBishopLike);

    while (candidates) {
        const int sniperSq = lsb(candidates);
        candidates &= candidates - 1;   // clear the lowest set bit

        const Bitboard between = betweenSquares(ksq, sniperSq) & state.occupancyAll;
        // Exactly one piece in the way means that piece is pinned — zero means
        // the slider is giving check, two or more means nothing is pinned.
        if (between == 0 || (between & (between - 1)) != 0) continue;
        if (between & ourPieces) {
            result |= between;
            if (pinners) *pinners |= 1ULL << sniperSq;
        }
    }
    return result;
}

BitboardState toBitboardState(const Board& board) {
    BitboardState state;
    state.clear();

    for (int sq = 0; sq < 64; ++sq) {
        const Piece p = board.squares[sq];
        if (p.type() == NONE) continue;

        BitboardPieceType bt;
        switch (p.type()) {
            case PAWN:   bt = BB_PAWN;   break;
            case KNIGHT: bt = BB_KNIGHT; break;
            case BISHOP: bt = BB_BISHOP; break;
            case ROOK:   bt = BB_ROOK;   break;
            case QUEEN:  bt = BB_QUEEN;  break;
            case KING:   bt = BB_KING;   break;
            default: continue;
        }

        if (p.color() == COLOR_WHITE) {
            state.white[bt] |= 1ULL << sq;
            state.occupancyWhite |= 1ULL << sq;
        } else {
            state.black[bt] |= 1ULL << sq;
            state.occupancyBlack |= 1ULL << sq;
        }
    }
    state.occupancyAll = state.occupancyWhite | state.occupancyBlack;

    // Both representations already agree on these encodings — the castling
    // mask uses the same bit values and the en passant target is the same
    // square index — so this is a copy, not a translation.
    state.castlingRights = board.castlingRights;
    state.enPassantSquare = board.enPassantSquare;
    state.sideToMove = (board.activeColor == COLOR_WHITE) ? BB_WHITE : BB_BLACK;
    return state;
}
