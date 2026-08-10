// Bitboard module tests.
//
// The magic bitboard technique fails silently. A bad magic number does not
// crash or return an obviously wrong value — it returns the attack set of some
// *other* blocker configuration, so a rook occasionally misses a move in one
// position out of thousands. initMagicBitboards() fills its tables by
// overwriting, with no collision check, so nothing else in the codebase would
// notice.
//
// So the tables are verified exhaustively here: for every square, for every
// blocker subset of that square's mask, the magic lookup must equal the
// directly computed attack set. That is 64 x 2^bits comparisons and it is the
// only way to be sure the tables are usable at all.
//
// Run:  make test-bitboard

#include "engine/bitboard_attacks.hpp"
#include "engine/board.hpp"
#include "engine/magic_bitboards.hpp"
#include "engine/move_lookup.hpp"
#include "engine/movegen.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

static int failures = 0;

// Rebuilt here rather than reused from magic_bitboards.cpp so the test does not
// depend on the code it is checking for its idea of what a blocker subset is.
static std::vector<uint64_t> blockerSubsets(uint64_t mask) {
    std::vector<uint64_t> subsets;
    std::vector<int> bits;
    for (int sq = 0; sq < 64; ++sq)
        if (mask & (1ULL << sq)) bits.push_back(sq);

    const int n = (int)bits.size();
    subsets.reserve((size_t)1 << n);
    for (uint32_t i = 0; i < (1u << n); ++i) {
        uint64_t subset = 0;
        for (int b = 0; b < n; ++b)
            if (i & (1u << b)) subset |= (1ULL << bits[b]);
        subsets.push_back(subset);
    }
    return subsets;
}

int main() {
    initMagicBitboards();

    std::printf("Magic bitboard validation\n");

    // --- Index width must cover the widest mask ---
    // The shift is a fixed width for every square rather than per-square, which
    // is safe only while that width is at least the largest relevant-bit count:
    // a coarser index is a right shift of a finer one, so it can only merge
    // entries that a valid magic already proved identical. If a mask ever needs
    // more bits than the table has, the lookup runs off the end of the row.
    int maxRookBits = 0, maxBishopBits = 0;
    for (int sq = 0; sq < 64; ++sq) {
        int r = __builtin_popcountll(rookMasks[sq]);
        int b = __builtin_popcountll(bishopMasks[sq]);
        if (r > maxRookBits) maxRookBits = r;
        if (b > maxBishopBits) maxBishopBits = b;
    }
    bool widthOk = (maxRookBits <= ROOK_MAGIC_BITS) && (maxBishopBits <= BISHOP_MAGIC_BITS);
    if (!widthOk) ++failures;
    std::printf("  index width      rook %d bits needed / %d available, "
                "bishop %d / %d   %s\n",
                maxRookBits, ROOK_MAGIC_BITS, maxBishopBits, BISHOP_MAGIC_BITS,
                widthOk ? "ok" : "TOO NARROW");

    // --- Exhaustive: every square, every blocker subset ---
    long rookChecks = 0, bishopChecks = 0;
    int badRookSquares = 0, badBishopSquares = 0;

    for (int sq = 0; sq < 64; ++sq) {
        bool squareOk = true;
        for (uint64_t blockers : blockerSubsets(rookMasks[sq])) {
            ++rookChecks;
            if (getRookAttacks(sq, blockers) != computeRookAttacks(sq, blockers)) {
                if (squareOk && badRookSquares < 5) {
                    std::printf("    rook   sq %2d: magic lookup disagrees "
                                "(blockers 0x%016llx)\n",
                                sq, (unsigned long long)blockers);
                }
                squareOk = false;
            }
        }
        if (!squareOk) ++badRookSquares;
    }

    for (int sq = 0; sq < 64; ++sq) {
        bool squareOk = true;
        for (uint64_t blockers : blockerSubsets(bishopMasks[sq])) {
            ++bishopChecks;
            if (getBishopAttacks(sq, blockers) != computeBishopAttacks(sq, blockers)) {
                if (squareOk && badBishopSquares < 5) {
                    std::printf("    bishop sq %2d: magic lookup disagrees "
                                "(blockers 0x%016llx)\n",
                                sq, (unsigned long long)blockers);
                }
                squareOk = false;
            }
        }
        if (!squareOk) ++badBishopSquares;
    }

    if (badRookSquares) ++failures;
    if (badBishopSquares) ++failures;
    std::printf("  rook magics      %ld lookups over 64 squares   %s\n",
                rookChecks,
                badRookSquares ? "FAILED" : "ok");
    if (badRookSquares)
        std::printf("                   %d square(s) have unusable magics\n", badRookSquares);
    std::printf("  bishop magics    %ld lookups over 64 squares   %s\n",
                bishopChecks,
                badBishopSquares ? "FAILED" : "ok");
    if (badBishopSquares)
        std::printf("                   %d square(s) have unusable magics\n", badBishopSquares);

    // --- Occupancy outside the mask must not change the answer ---
    // The mask deliberately excludes the board edge, because a piece on the far
    // edge cannot block anything beyond itself. Squares outside the mask must
    // therefore be irrelevant to the lookup; if they are not, the mask is wrong.
    bool maskOk = true;
    for (int sq = 0; sq < 64; ++sq) {
        uint64_t inMask = rookMasks[sq];
        uint64_t noise = ~inMask & ~(1ULL << sq);
        if (getRookAttacks(sq, 0) != getRookAttacks(sq, noise)) maskOk = false;
        uint64_t bNoise = ~bishopMasks[sq] & ~(1ULL << sq);
        if (getBishopAttacks(sq, 0) != getBishopAttacks(sq, bNoise)) maskOk = false;
    }
    if (!maskOk) ++failures;
    std::printf("  mask irrelevance occupancy outside the mask ignored   %s\n",
                maskOk ? "ok" : "FAILED");

    // ------------------------------------------------------------------
    // Cross-validation against the mailbox engine.
    //
    // The magic tables being self-consistent says nothing about whether this
    // module agrees with the move generator the engine actually uses. A second
    // implementation is only worth having if it is checked against the first,
    // so every claim below is tested against movegen.cpp / Board over positions
    // drawn from real games rather than hand-picked ones.
    // ------------------------------------------------------------------
    initMoveLookupTables();
    initBitboardAttacks();

    uint64_t rng = 0x5CBE7A1F2026ULL;
    auto nextRand = [&rng]() {
        rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng;
    };

    long positions = 0, attackChecks = 0;
    int attackMismatch = 0, pinMismatch = 0, checkerMismatch = 0;

    for (int game = 0; game < 60 && attackMismatch + pinMismatch + checkerMismatch < 5; ++game) {
        Board board;
        for (int ply = 0; ply < 100; ++ply) {
            MoveList moves = generateLegalMoves(board, board.activeColor);
            if (moves.empty() || board.halfmoveClock >= 100) break;
            ++positions;

            BitboardState bb = toBitboardState(board);

            // 1. isSquareAttacked must agree on every square, for both colours.
            for (int sq = 0; sq < 64; ++sq) {
                for (int c = 0; c < 2; ++c) {
                    PieceColor mc = (c == 0) ? COLOR_WHITE : COLOR_BLACK;
                    BitboardColor bc = (c == 0) ? BB_WHITE : BB_BLACK;
                    bool mailbox = board.isSquareAttacked(sq, (int)mc);
                    bool bitboard = isSquareAttackedBB(bb, sq, bc);
                    ++attackChecks;
                    if (mailbox != bitboard && attackMismatch < 5) {
                        std::printf("    attack disagreement sq %d by %s: "
                                    "mailbox %d bitboard %d\n    %s\n",
                                    sq, c == 0 ? "white" : "black",
                                    (int)mailbox, (int)bitboard,
                                    board.getFEN().c_str());
                        ++attackMismatch;
                    }
                }
            }

            // 2. checkers() must be non-empty exactly when the side to move is
            //    in check, which the mailbox decides by testing the king square.
            BitboardColor stm = (board.activeColor == COLOR_WHITE) ? BB_WHITE : BB_BLACK;
            PieceColor opp = (board.activeColor == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
            int ksq = kingSquare(bb, stm);
            bool mailboxCheck = (ksq >= 0) && board.isSquareAttacked(ksq, (int)opp);
            bool bbCheck = checkers(bb, stm) != 0;
            if (mailboxCheck != bbCheck && checkerMismatch < 5) {
                std::printf("    checkers disagreement: mailbox %d bitboard %d\n    %s\n",
                            (int)mailboxCheck, (int)bbCheck, board.getFEN().c_str());
                ++checkerMismatch;
            }

            // 3. The real prize: pin detection, which is what pin-aware legal
            //    move generation (PLAN.md 5.5) is built on.
            //
            //    The oracle is the definition itself rather than anything
            //    derived from the move list: a piece is a blocker for its king
            //    exactly when removing it leaves the king attacked. An earlier
            //    version of this test inferred pins from the legal move list
            //    and was simply wrong — a fully pinned piece has no legal moves
            //    at all, so there was nothing to infer from, and every genuine
            //    pin was reported as a disagreement.
            //    The oracle only holds while the king is not already in check.
            //    If some enemy piece already attacks the king, removing any
            //    friendly piece still leaves it attacked, and "exposes the
            //    king" stops distinguishing a blocker from a bystander. The
            //    code path is the same either way — blockersForKing() skips a
            //    slider with nothing between it and the king, which is exactly
            //    the checking case — so restricting the oracle costs coverage
            //    of the logic, not of the code.
            Bitboard pinned = blockersForKing(bb, stm);
            if (ksq >= 0 && !bbCheck) {
                Board scratch = board.copyForSearch();
                for (int sq = 0; sq < 64; ++sq) {
                    if (board.squares[sq].type() == NONE) continue;
                    if (board.squares[sq].color() != board.activeColor) continue;
                    if (sq == ksq) continue;

                    const Piece saved = scratch.squares[sq];
                    scratch.squares[sq] = Piece();
                    const bool exposesKing = scratch.isSquareAttacked(ksq, (int)opp);
                    scratch.squares[sq] = saved;

                    const bool claimsPinned = ((pinned >> sq) & 1ULL) != 0;
                    if (claimsPinned != exposesKing && pinMismatch < 5) {
                        std::printf("    pin disagreement: sq %d bitboard says %d, "
                                    "removing it %s the king\n    %s\n",
                                    sq, (int)claimsPinned,
                                    exposesKing ? "exposes" : "does not expose",
                                    board.getFEN().c_str());
                        ++pinMismatch;
                    }
                }
            }

            board.makeMove(moves[nextRand() % moves.size()]);
        }
    }

    if (attackMismatch) ++failures;
    if (checkerMismatch) ++failures;
    if (pinMismatch) ++failures;

    std::printf("\nCross-validation against the mailbox engine (%ld positions)\n", positions);
    std::printf("  isSquareAttacked %ld comparisons   %s\n", attackChecks,
                attackMismatch ? "FAILED" : "ok");
    std::printf("  checkers()       agrees with the mailbox check test   %s\n",
                checkerMismatch ? "FAILED" : "ok");
    std::printf("  blockersForKing() matches remove-and-test-the-king   %s\n",
                pinMismatch ? "FAILED" : "ok");

    if (failures) {
        std::printf("\nFAILED: %d check group(s).\n", failures);
        return 1;
    }
    std::printf("\nPASSED: bitboard attacks agree with the mailbox engine\n");
    return 0;
}
