// Paired node-count comparison of one search toggle over many positions.
//
// tests/bench totals twelve positions, and that instrument has now been wrong
// twice in the same way: `improving` read +1.7% at depth 11 while eight of the
// twelve positions shrank, and the ordering sort measured -2.9% at depth 7 and
// +12.1% at depth 9. A dozen positions cannot resolve a five percent effect,
// because one position swinging by 700k nodes swamps the aggregate.
//
// This fixes both halves of that. It runs many more positions, and it compares
// them PAIRWISE: each position is searched both ways, and the statistic is the
// distribution of per-position ratios rather than the ratio of two totals. A
// total is dominated by whichever position happens to be largest; a median
// ratio is not, and the quartiles say whether the effect is consistent or just
// one position shouting.
//
//   ./tools/treecost <depth> <positions> <option> [held-options]
//   ./tools/treecost 8 200 detsort
//   ./tools/treecost 9 200 nulldepthr nullverify,lmpdeep
//
#include "engine/board.hpp"
#include "engine/move_lookup.hpp"
#include "engine/movegen.hpp"
#include "engine/search.hpp"
#include "engine/transposition_table.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

// Positions from random legal games: reachable, varied, and not chosen by
// whoever wants a particular answer.
std::vector<std::string> corpus(int want) {
    std::vector<std::string> out;
    uint64_t r = 0x5CBE7A1F2026ULL;
    auto next = [&r] { r ^= r << 13; r ^= r >> 7; r ^= r << 17; return r; };
    for (int g = 0; (int)out.size() < want; ++g) {
        Board b;
        for (int ply = 0; ply < 100 && (int)out.size() < want; ++ply) {
            MoveList m = generateLegalMoves(b, b.activeColor);
            if (m.empty() || b.halfmoveClock >= 100) break;
            // Skip the first few plies of each game: they are all the same
            // opening tree and would over-weight one shape of position.
            if (ply >= 4) out.push_back(b.getFEN());
            b.makeMove(m[next() % m.size()]);
        }
    }
    return out;
}

uint64_t searchNodes(const std::string& fen, int depth) {
    Board b;
    if (!b.setFromFEN(fen)) return 0;
    TranspositionTable tt(64);
    std::atomic<bool> stop{false};
    findBestMoveIterativeDeepening(b, depth, stop, tt);
    return g_searchNodes;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::printf("usage: %s <depth> <positions> <option-name>\n", argv[0]);
        return 1;
    }
    const int depth = std::atoi(argv[1]);
    const int want = std::atoi(argv[2]);
    const char* opt = argv[3];
    // Options held on for BOTH sides, so a toggle can be measured in the
    // configuration it actually ships in rather than against bare defaults.
    const char* held = (argc > 4) ? argv[4] : "";

    initMoveLookupTables();
    g_searchOptions.quiet = true;
    {
        std::string h(held), one;
        std::stringstream ss(h);
        while (std::getline(ss, one, ','))
            if (!one.empty() && !setSearchOption(g_searchOptions, one, true)) {
                std::printf("unknown held option '%s'\n", one.c_str());
                return 1;
            }
    }
    if (!setSearchOption(g_searchOptions, opt, false)) {
        std::printf("unknown search option '%s'\n", opt);
        return 1;
    }

    const std::vector<std::string> fens = corpus(want);
    std::printf("%s at depth %d over %zu positions, paired\n\n",
                opt, depth, fens.size());
    std::fflush(stdout);

    std::streambuf* saved = std::cout.rdbuf();
    std::ostringstream swallow;

    std::vector<double> ratios;
    ratios.reserve(fens.size());
    uint64_t totalOff = 0, totalOn = 0;
    int worse = 0, better = 0;
    const auto start = std::chrono::steady_clock::now();

    for (size_t i = 0; i < fens.size(); ++i) {
        std::cout.rdbuf(swallow.rdbuf());
        setSearchOption(g_searchOptions, opt, false);
        const uint64_t off = searchNodes(fens[i], depth);
        setSearchOption(g_searchOptions, opt, true);
        const uint64_t on = searchNodes(fens[i], depth);
        std::cout.rdbuf(saved);
        if (!off || !on) continue;

        totalOff += off;
        totalOn += on;
        ratios.push_back((double)on / (double)off);
        if (on > off) ++worse; else if (on < off) ++better;

        if ((i + 1) % 10 == 0 || i + 1 == fens.size()) {
            const double elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - start).count();
            const double eta = elapsed / (i + 1) * (fens.size() - i - 1);
            std::printf("\rPROGRESS %zu/%zu  elapsed %.0fs  eta %.0fs",
                        i + 1, fens.size(), elapsed, eta);
            std::fflush(stdout);
        }
    }
    std::printf("\n\n");

    if (ratios.empty()) { std::printf("no usable positions\n"); return 1; }
    std::sort(ratios.begin(), ratios.end());
    auto pct = [&](double p) { return ratios[(size_t)(p * (ratios.size() - 1))]; };

    std::printf("  positions            %zu\n", ratios.size());
    std::printf("  total nodes off      %llu\n", (unsigned long long)totalOff);
    std::printf("  total nodes on       %llu\n", (unsigned long long)totalOn);
    std::printf("  ratio of totals      %+.2f%%   (the tests/bench statistic)\n",
                (double)((long long)totalOn - (long long)totalOff) * 100.0 / (double)totalOff);
    std::printf("\n  median ratio         %+.2f%%   (the one to read)\n", (pct(0.50) - 1) * 100);
    std::printf("  quartiles            %+.2f%% .. %+.2f%%\n",
                (pct(0.25) - 1) * 100, (pct(0.75) - 1) * 100);
    std::printf("  10th..90th           %+.2f%% .. %+.2f%%\n",
                (pct(0.10) - 1) * 100, (pct(0.90) - 1) * 100);
    std::printf("\n  bigger tree on       %d positions\n", worse);
    std::printf("  smaller tree on      %d positions\n", better);
    std::printf("  unchanged            %zu positions\n", ratios.size() - worse - better);

    // A sign test: if the toggle did nothing, bigger and smaller should split
    // evenly. This asks how far from even the split is, in standard deviations,
    // which is the question "is this a real effect" rather than "how big".
    const int n = worse + better;
    if (n > 0) {
        const double z = (worse - n / 2.0) / (0.5 * std::sqrt((double)n));
        std::printf("\n  sign test            %+.1f sd from even\n", z);
        std::printf("  %s\n", std::fabs(z) < 2.0
            ? "  -> consistent with no systematic effect"
            : "  -> a systematic effect, in the direction of the sign");
    }
    return 0;
}
