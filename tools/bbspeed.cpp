// Speed comparison between the mailbox engine and the bitboard replacement.
//
// docs/BITBOARD-REPLACEMENT.md gates the switch-over on two numbers: the
// bitboard search must visit exactly the same nodes as the mailbox search, and
// it must actually be faster by enough to be worth the risk. tests/bbequiv
// answers the first. This answers the second.
//
// Both halves compare like with like: the same positions, the same work, in one
// process so the machine state is shared. The generator comparison is perft,
// which is generation plus make and unmake and nothing else; the evaluation
// comparison calls the detail form so neither side's cache can answer for it.
//
#include "engine/bb_evaluation.hpp"
#include "engine/bb_search.hpp"
#include "engine/transposition_table.hpp"
#include "engine/bb_movegen.hpp"
#include "engine/bitboard_attacks.hpp"
#include "engine/evaluation.hpp"
#include "engine/movegen.hpp"
#include "engine/move_lookup.hpp"
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>
#include <atomic>
#include <iostream>
#include <sstream>

using Clock = std::chrono::steady_clock;

namespace {

// Kiwipete: dense, tactical, and the standard position for this measurement.
const int SEARCH_DEPTH = 8;

// tests/bench's positions, so the search comparison is over the same ground the
// signature is measured on.
const char* BENCH[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 0 1",
    "rnbqkbnr/pp1ppppp/8/2p5/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 2",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "r1bq1rk1/pp2bppp/2n1pn2/2pp4/3P1B2/2PBPN2/PP1N1PPP/R2Q1RK1 w - - 0 1",
    "2rq1rk1/pb1nbppp/1p2pn2/3p4/3P4/1BN1PN2/PP2QPPP/2RR2K1 w - - 0 1",
    "r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "8/8/8/4k3/8/8/4K3/R7 w - - 0 1",
    "8/1p3pp1/7p/5P1P/2k3P1/8/2K2P2/8 w - - 0 1",
    "6k1/5ppp/8/8/8/8/5PPP/3Q2K1 w - - 0 1",
    "8/8/p1p5/1p5p/1P5p/8/PPP2K1p/4R1rk w - - 0 1",
};

const char* PERFT_FEN =
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";

uint64_t perftNew(Position& p, int d) {
    BBMoveList m;
    bbGenerate(p, m);
    if (d <= 1) return (uint64_t)m.size();
    uint64_t n = 0;
    for (const BitboardMove& mv : m) {
        const PositionUndo u = p.makeMove(mv);
        n += perftNew(p, d - 1);
        p.unmakeMove(u);
    }
    return n;
}

uint64_t perftOld(BitboardState& s, int d) {
    BitboardMoveList m;
    generateBitboardLegal(s, s.sideToMove, m);
    if (d <= 1) return (uint64_t)m.size();
    uint64_t n = 0;
    for (const BitboardMove& mv : m) {
        BitboardUndo u = makeBitboardMove(s, mv);
        n += perftOld(s, d - 1);
        unmakeBitboardMove(s, u);
    }
    return n;
}

uint64_t perftMailbox(Board& b, int d) {
    MoveList m = generateLegalMoves(b, b.activeColor);
    if (d <= 1) return (uint64_t)m.size();
    uint64_t n = 0;
    for (const Move& mv : m) {
        auto u = b.makeMove(mv);
        n += perftMailbox(b, d - 1);
        b.unmakeMove(u);
    }
    return n;
}

// The same corpus tests/bbequiv uses: positions from random legal games, so
// they are reachable and varied rather than chosen.
std::vector<std::string> buildCorpus() {
    std::vector<std::string> out;
    uint64_t r = 0x5CBE7A1F2026ULL;
    auto next = [&r] { r ^= r << 13; r ^= r >> 7; r ^= r << 17; return r; };
    for (int g = 0; g < 60; ++g) {
        Board b;
        for (int ply = 0; ply < 100; ++ply) {
            MoveList m = generateLegalMoves(b, b.activeColor);
            if (m.empty() || b.halfmoveClock >= 100) break;
            out.push_back(b.getFEN());
            b.makeMove(m[next() % m.size()]);
        }
    }
    return out;
}

double msSince(Clock::time_point t) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}

} // namespace

int main(int argc, char** argv) {
    const int depth = (argc > 1) ? std::atoi(argv[1]) : 5;
    initMoveLookupTables();
    initBitboardAttacks();

    std::printf("Move generation, perft depth %d on kiwipete\n", depth);
    double mailboxGen = 0, newGen = 0;
    {
        Board b;
        b.setFromFEN(PERFT_FEN);
        auto t = Clock::now();
        const uint64_t n = perftMailbox(b, depth);
        mailboxGen = msSince(t);
        std::printf("  %-22s %11llu nodes  %8.0f ms  %7.2f Mnps\n",
                    "mailbox movegen.cpp", (unsigned long long)n, mailboxGen,
                    n / mailboxGen / 1000.0);

        BitboardState s = toBitboardState(b);
        t = Clock::now();
        const uint64_t n2 = perftOld(s, depth);
        const double ms = msSince(t);
        std::printf("  %-22s %11llu nodes  %8.0f ms  %7.2f Mnps\n",
                    "old bitboard legal", (unsigned long long)n2, ms, n2 / ms / 1000.0);

        Position p;
        p.setFromFEN(PERFT_FEN);
        t = Clock::now();
        const uint64_t n3 = perftNew(p, depth);
        newGen = msSince(t);
        std::printf("  %-22s %11llu nodes  %8.0f ms  %7.2f Mnps   %.2fx\n",
                    "new bb_movegen", (unsigned long long)n3, newGen,
                    n3 / newGen / 1000.0, mailboxGen / newGen);
        if (n != n2 || n != n3) {
            std::printf("  NODE COUNTS DISAGREE -- the comparison is meaningless\n");
            return 1;
        }
    }

    std::printf("\nEvaluation\n");
    const std::vector<std::string> corpus = buildCorpus();
    std::vector<Board> boards;
    std::vector<Position> positions;
    for (const std::string& f : corpus) {
        Board b;
        b.setFromFEN(f);
        boards.push_back(b);
        Position p;
        p.setFromFEN(f);
        positions.push_back(p);
    }
    const int reps = 20;
    const long n = (long)boards.size() * reps;
    long long sink = 0;

    auto t = Clock::now();
    for (int r = 0; r < reps; ++r)
        for (Board& b : boards) sink += evaluate_details(b).total;
    const double mailboxEval = msSince(t);

    t = Clock::now();
    for (int r = 0; r < reps; ++r)
        for (Position& p : positions) sink += bbEvaluateDetails(p).total;
    const double bbEval = msSince(t);

    std::printf("  %ld evaluations each\n", n);
    std::printf("  %-22s %8.0f ms  %7.2f M/s\n", "mailbox evaluation",
                mailboxEval, n / mailboxEval / 1000.0);
    std::printf("  %-22s %8.0f ms  %7.2f M/s   %.2fx\n", "bitboard evaluation",
                bbEval, n / bbEval / 1000.0, mailboxEval / bbEval);
    std::printf("  (checksum %lld, equal by construction -- tests/bbequiv proves it)\n", sink);

    // The whole search, on tests/bench's own positions.
    //
    // tests/bbequiv proves the two visit exactly the same nodes with
    // orderTieBreak on, so this is a pure time comparison: same tree, same
    // work, and the ratio is what connecting the bitboard core would buy.
    std::printf("\nSearch to depth %d over the bench positions\n", SEARCH_DEPTH);
    g_searchOptions.quiet = true;
    g_searchOptions.orderTieBreak = true;
    {
        std::streambuf* savedBuf = std::cout.rdbuf();
        std::ostringstream swallowed;
        std::cout.rdbuf(swallowed.rdbuf());
        std::atomic<bool> stop{false};
        uint64_t mailboxNodes = 0, bbNodes = 0;

        auto t0 = Clock::now();
        for (const auto& bp : BENCH) {
            Board b;
            b.setFromFEN(bp);
            TranspositionTable tt(64);
            findBestMoveIterativeDeepening(b, SEARCH_DEPTH, stop, tt);
            mailboxNodes += g_searchNodes;
        }
        const double mailboxMs = msSince(t0);

        t0 = Clock::now();
        for (const auto& bp : BENCH) {
            Position pp;
            pp.setFromFEN(bp);
            TranspositionTable tt(64);
            std::vector<uint64_t> path;
            bbNodes += bbSearchRoot(pp, SEARCH_DEPTH, tt, stop, path).nodes;
        }
        const double bbMs = msSince(t0);
        std::cout.rdbuf(savedBuf);

        std::printf("  %-22s %11llu nodes  %8.0f ms  %7.0f knps\n", "mailbox search",
                    (unsigned long long)mailboxNodes, mailboxMs, mailboxNodes / mailboxMs);
        std::printf("  %-22s %11llu nodes  %8.0f ms  %7.0f knps   %.2fx\n", "bitboard search",
                    (unsigned long long)bbNodes, bbMs, bbNodes / bbMs, mailboxMs / bbMs);
        if (mailboxNodes != bbNodes)
            std::printf("  NODE COUNTS DISAGREE -- the ratio is meaningless\n");
    }
    return 0;
}
