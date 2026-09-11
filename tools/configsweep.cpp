// Depth reached in a fixed time, and whether the move survived, per feature set.
//
// Depth on its own is a bad way to choose pruning settings: a rule that throws
// away more moves buys depth with the moves it threw away, and the tree looks
// better right up until the one it discarded was the answer. So this measures
// both halves.
//
// Speed  : mean depth reached in the time budget, over many positions.
// Accuracy: whether the move chosen matches a long reference search.
//
// The reference is the *shipped* configuration given several times the budget.
// That makes the question "does this setting still find what the engine itself
// finds when it has time", which is exactly what a forward-pruning rule risks.
// It is not an oracle: where the reference is itself wrong, agreement with it
// is not virtue. It is the right comparison anyway, because a pruning rule
// should not change what the engine concludes, only how fast it concludes it.
//
//   ./tools/configsweep <seconds> <positions> [reference-multiplier]
//
#include "engine/board.hpp"
#include "engine/move_lookup.hpp"
#include "engine/movegen.hpp"
#include "engine/search.hpp"
#include "engine/transposition_table.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Config { const char* label; const char* opts; };

// Each row is one feature set. "" is the configuration that ships today.
const Config CONFIGS[] = {
    {"shipped",            ""},
    {"null+verify+lmp",    "nulldepthr,nullverify,lmpdeep"},
    {"q2 bare",            "bbcore,qmovecount,qnounderpromo"},
    {"q2+check",           "bbcore,qmovecount,qnounderpromo,qcheckexempt"},
    {"q2+check+capthist",  "bbcore,qmovecount,qnounderpromo,qcheckexempt,capthist,qcapthist"},
    {"everything",         "bbcore,nulldepthr,nullverify,lmpdeep,interiorpvs,qmovecount,qnounderpromo,qcheckexempt,capthist,qcapthist"},
};
const int NUM_CONFIGS = (int)(sizeof(CONFIGS) / sizeof(CONFIGS[0]));

void applyOpts(SearchOptions& o, const std::string& spec) {
    std::stringstream ss(spec);
    std::string one;
    while (std::getline(ss, one, ',')) {
        if (one.empty()) continue;
        if (!setSearchOption(o, one, true)) {
            std::printf("unknown option '%s'\n", one.c_str());
            std::exit(1);
        }
    }
}

// Positions from random legal games, past the opening so they are real
// middlegames rather than twelve variations on the same tree.
std::vector<std::string> corpus(int want) {
    std::vector<std::string> out;
    uint64_t r = 0x5CBE7A1F2026ULL;
    auto next = [&r] { r ^= r << 13; r ^= r >> 7; r ^= r << 17; return r; };
    for (int g = 0; (int)out.size() < want; ++g) {
        Board b;
        for (int ply = 0; ply < 60 && (int)out.size() < want; ++ply) {
            MoveList m = generateLegalMoves(b, b.activeColor);
            if (m.empty() || b.halfmoveClock >= 100) break;
            if (ply >= 12 && ply % 7 == 0) out.push_back(b.getFEN());
            b.makeMove(m[next() % m.size()]);
        }
    }
    return out;
}

struct Result { int depth; std::string move; };

Result search(const std::string& fen, long ms, const SearchOptions& opts) {
    const SearchOptions saved = g_searchOptions;
    g_searchOptions = opts;
    g_searchOptions.quiet = true;

    Board b;
    b.setFromFEN(fen);
    TranspositionTable tt(64);
    std::atomic<bool> stop{false};
    SearchLimits limits;
    limits.maxDepth = 64;
    limits.moveTimeMs = ms;

    int reachedDepth = 0;
    SearchInfoFn savedInfo = g_searchInfo;
    static int* depthSink = nullptr;
    depthSink = &reachedDepth;
    g_searchInfo = [](int d, int, uint64_t, long, const Move&) { if (depthSink) *depthSink = d; };

    const Move best = findBestMoveIterativeDeepening(b, limits, stop, tt);
    g_searchInfo = savedInfo;
    g_searchOptions = saved;
    return Result{reachedDepth, best.toString()};
}

} // namespace

int main(int argc, char** argv) {
    const long seconds = (argc > 1) ? std::atol(argv[1]) : 5;
    const int want = (argc > 2) ? std::atoi(argv[2]) : 24;
    const int refMult = (argc > 3) ? std::atoi(argv[3]) : 4;

    initMoveLookupTables();
    std::streambuf* saved = std::cout.rdbuf();
    std::ostringstream swallow;

    const std::vector<std::string> fens = corpus(want);
    std::printf("%zu positions, %lds per search, reference at %lds (shipped settings)\n\n",
                fens.size(), seconds, seconds * refMult);
    std::fflush(stdout);

    // Reference first: the shipped configuration, given several times the time.
    SearchOptions shipped;
    shipped.quiet = true;
    std::vector<std::string> reference;
    for (size_t i = 0; i < fens.size(); ++i) {
        std::cout.rdbuf(swallow.rdbuf());
        reference.push_back(search(fens[i], seconds * refMult * 1000, shipped).move);
        std::cout.rdbuf(saved);
        std::printf("\rreference %zu/%zu", i + 1, fens.size());
        std::fflush(stdout);
    }
    std::printf("\r                              \r");

    std::vector<std::string> disagreements;
    std::printf("  %-20s %7s %9s %s\n", "configuration", "depth", "agree", "differs on");
    for (int c = 0; c < NUM_CONFIGS; ++c) {
        SearchOptions o;
        applyOpts(o, CONFIGS[c].opts);
        long totalDepth = 0;
        int agree = 0;
        std::string differs;
        for (size_t i = 0; i < fens.size(); ++i) {
            std::cout.rdbuf(swallow.rdbuf());
            const Result r = search(fens[i], seconds * 1000, o);
            std::cout.rdbuf(saved);
            totalDepth += r.depth;
            if (r.move == reference[i]) ++agree;
            else {
                if (differs.size() < 30) differs += std::to_string(i) + " ";
                // Recorded so an outside engine can say which of the two is
                // right. The reference is only this engine's own deeper
                // opinion, so a disagreement is not automatically a mistake.
                disagreements.push_back(std::string(CONFIGS[c].label) + "\t" + fens[i]
                                        + "\t" + reference[i] + "\t" + r.move);
            }
        }
        std::printf("  %-20s %7.2f %6d/%zu  %s\n", CONFIGS[c].label,
                    (double)totalDepth / (double)fens.size(), agree, fens.size(),
                    differs.c_str());
        std::fflush(stdout);
    }

    // Written for tools/adjudicate.py, which asks Stockfish which move was
    // actually better rather than which matched.
    if (FILE* f = std::fopen("/tmp/disagreements.tsv", "w")) {
        for (const std::string& d : disagreements) std::fprintf(f, "%s\n", d.c_str());
        std::fclose(f);
        std::printf("\n  %zu disagreements written to /tmp/disagreements.tsv\n",
                    disagreements.size());
    }
    return 0;
}
