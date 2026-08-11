// Search bench: a reproducible signature for the search tree.
//
// Searches a fixed set of positions to a fixed depth with a fixed transposition
// table and prints the total node count and the best move per position. That
// total is a signature. Any change that is supposed to leave search behaviour
// alone — a refactor, a data structure swap, a rename — must reproduce it
// exactly. Any change that is supposed to alter behaviour must change it in a
// way you can explain.
//
// This is the search-side counterpart to tests/evalref.cpp, and it is what
// makes an otherwise unverifiable refactor (the negamax conversion, PLAN.md
// 0.9) checkable by identity instead of by playing games.
//
//   make bench              print the signature at the default depth
//   make test-bench         compare against tests/data/bench.txt, fail on drift
//   make bench-regen        rewrite the stored signature
//   ./tests/bench <depth>   print at any other depth
//
// A search change that is meant to alter behaviour will fail test-bench. That
// is the intent: the failure is the prompt to look at the new numbers, decide
// they are what you meant, and regenerate — the same contract as evalref.
//
// Determinism: one thread, no clock-dependent decisions in the search, a fresh
// table per position and move-ordering state cleared by the search itself. If
// this ever stops being reproducible run to run, that is itself a bug — most
// likely a time-based cutoff that has crept into the search path.

#include "engine/board.hpp"
#include "engine/move_lookup.hpp"
#include "engine/search.hpp"
#include "engine/transposition_table.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

// Twelve positions spanning the phases the search behaves differently in:
// quiet openings, tactical middlegames, and endgames where null-move pruning
// and LMR are least safe.
static const struct { const char* name; const char* fen; } POSITIONS[] = {
    {"startpos",   "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"},
    {"open-ital",  "r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 0 1"},
    {"open-sicil", "rnbqkbnr/pp1ppppp/8/2p5/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 2"},
    {"kiwipete",   "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"},
    {"midgame-1",  "r1bq1rk1/pp2bppp/2n1pn2/2pp4/3P1B2/2PBPN2/PP1N1PPP/R2Q1RK1 w - - 0 1"},
    {"midgame-2",  "2rq1rk1/pb1nbppp/1p2pn2/3p4/3P4/1BN1PN2/PP2QPPP/2RR2K1 w - - 0 1"},
    {"tactical",   "r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1"},
    {"promo-race", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"},
    {"rook-endg",  "8/8/8/4k3/8/8/4K3/R7 w - - 0 1"},
    {"pawn-endg",  "8/1p3pp1/7p/5P1P/2k3P1/8/2K2P2/8 w - - 0 1"},
    {"queen-endg", "6k1/5ppp/8/8/8/8/5PPP/3Q2K1 w - - 0 1"},
    {"zugzwang",   "8/8/p1p5/1p5p/1P5p/8/PPP2K1p/4R1rk w - - 0 1"},
};
static const int NUM_POSITIONS = (int)(sizeof(POSITIONS) / sizeof(POSITIONS[0]));

static const int DEFAULT_DEPTH = 6;
static const size_t TT_SIZE_MB = 64;
static const char* SIG_PATH = "tests/data/bench.txt";

// The signature: everything that must not drift. Deliberately excludes elapsed
// time and nps, which vary by machine and by load.
static std::string signature(int depth, long long* elapsedOut, int* failuresOut) {
    std::ostringstream sig;
    std::atomic<bool> stop{false};
    uint64_t totalNodes = 0;
    int failures = 0;

    sig << "bench depth " << depth << ", " << TT_SIZE_MB << " MB table\n\n";
    sig << "position           nodes  best\n";

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < NUM_POSITIONS; ++i) {
        Board board;
        if (!board.setFromFEN(POSITIONS[i].fen)) {
            sig << POSITIONS[i].name << " FEN PARSE FAILED\n";
            ++failures;
            continue;
        }

        // A fresh table per position: carrying one over would make each
        // position's node count depend on the ones before it, so inserting or
        // reordering a position would change every later number.
        TranspositionTable tt(TT_SIZE_MB);

        Move best = findBestMoveIterativeDeepening(board, depth, stop, tt);
        totalNodes += g_searchNodes;

        char line[128];
        std::snprintf(line, sizeof(line), "%-11s %12llu  %s\n", POSITIONS[i].name,
                      (unsigned long long)g_searchNodes, best.toString().c_str());
        sig << line;
    }

    char total[128];
    std::snprintf(total, sizeof(total), "\n%-11s %12llu\n", "total",
                  (unsigned long long)totalNodes);
    sig << total;

    *elapsedOut = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    *failuresOut = failures;
    return sig.str();
}

int main(int argc, char** argv) {
    initMoveLookupTables();

    bool check = (argc > 1 && std::strcmp(argv[1], "--check") == 0);
    bool regen = (argc > 1 && std::strcmp(argv[1], "--regen") == 0);
    int depth = DEFAULT_DEPTH;
    if (!check && !regen && argc > 1) depth = std::atoi(argv[1]);

    // --opt <name>=<on|off> toggles one search option for this run.
    //
    // A match says whether a feature wins games; it cannot say why, and it
    // costs hours. This says how the feature changes the tree, in seconds — the
    // first question to ask of anything in Phase 3, and the one that catches a
    // feature wired in backwards before a match is spent on it.
    //
    // Deliberately not usable with --check: the stored signature describes the
    // defaults, and comparing a modified search against it would be meaningless.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--opt") != 0 || i + 1 >= argc) continue;
        std::string spec = argv[++i];
        size_t eq = spec.find('=');
        if (eq == std::string::npos) {
            std::printf("--opt wants <name>=<on|off>, got '%s'\n", spec.c_str());
            return 1;
        }
        std::string name = spec.substr(0, eq), value = spec.substr(eq + 1);
        if (!setSearchOption(g_searchOptions, name, value == "on" || value == "true" || value == "1")) {
            std::printf("unknown search option '%s'\n", name.c_str());
            return 1;
        }
        if (check) {
            std::printf("--opt cannot be combined with --check: the stored "
                        "signature describes the default options\n");
            return 1;
        }
    }

    if (depth < 1) {
        std::printf("usage: %s [depth | --check | --regen] [--opt <name>=<on|off>]...\n", argv[0]);
        return 1;
    }

    // The search prints per-depth progress by default; that is noise here.
    // The transposition table announces every resize on stdout, which would
    // interleave with the signature, so it is built while stdout is quiet.
    g_searchOptions.quiet = true;
    std::streambuf* saved = std::cout.rdbuf();
    std::ostringstream swallowed;
    std::cout.rdbuf(swallowed.rdbuf());

    long long elapsed = 0;
    int failures = 0;
    std::string sig = signature(depth, &elapsed, &failures);

    std::cout.rdbuf(saved);

    if (failures) {
        std::fputs(sig.c_str(), stdout);
        std::printf("\nFAILED: %d position(s) did not parse\n", failures);
        return 1;
    }

    if (regen) {
        std::ofstream out(SIG_PATH, std::ios::binary);
        if (!out) {
            std::printf("cannot write %s (run from the repository root)\n", SIG_PATH);
            return 1;
        }
        out << sig;
        std::fputs(sig.c_str(), stdout);
        std::printf("\nwrote %s\n", SIG_PATH);
        return 0;
    }

    if (check) {
        std::ifstream in(SIG_PATH, std::ios::binary);
        if (!in) {
            std::printf("FAILED: no signature at %s\n"
                        "        Run 'make bench-regen' on a build you trust.\n",
                        SIG_PATH);
            return 1;
        }
        std::stringstream buf;
        buf << in.rdbuf();
        if (buf.str() != sig) {
            std::printf("FAILED: the search tree changed.\n\n"
                        "--- expected (%s)\n%s\n"
                        "--- got\n%s\n"
                        "If this change was intended, check the new numbers are what you\n"
                        "meant and run 'make bench-regen'.\n",
                        SIG_PATH, buf.str().c_str(), sig.c_str());
            return 1;
        }
        std::printf("PASSED: search unchanged (%lld ms)\n", elapsed);
        return 0;
    }

    std::fputs(sig.c_str(), stdout);
    std::printf("%-11s %12lld ms\n", "time", elapsed);
    return 0;
}
