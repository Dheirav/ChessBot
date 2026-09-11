// Which move causes the beta cutoff, as a distribution.
//
// Alpha-beta only has to prove a move is good enough, not find the best one, so
// if the first move tried already refutes the position the other thirty are
// never searched. How often that happens decides what any ordering work can be
// worth: everything that reorders moves below the first is competing for
// whatever fraction is left.
//
// Measured here at 92.9 percent on the first move and 97.3 on the first two,
// which is why `conthist`, `histmalus` and `qcapthist` all measured null. They
// refine the ordering of moves that decide under three percent of cutoffs.
//
// Needs the engine built with -DORDERING_STATS; see the Makefile target.
//
//   make ordering-stats
//
#include "engine/board.hpp"
#include "engine/move_lookup.hpp"
#include "engine/search.hpp"
#include "engine/transposition_table.hpp"
#include <atomic>
#include <cstdio>
#include <iostream>
#include <sstream>
extern std::atomic<uint64_t> g_cutoffAt[64];
static const char* FENS[] = {
  "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
  "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
  "2rq1rk1/pb1nbppp/1p2pn2/3p4/3P4/1BN1PN2/PP2QPPP/2RR2K1 w - - 0 1",
  "r1bq1rk1/pp2bppp/2n1pn2/2pp4/3P1B2/2PBPN2/PP1N1PPP/R2Q1RK1 w - - 0 1",
};
int main(int argc, char** argv) {
  initMoveLookupTables();
  g_searchOptions.quiet = true;
  for (int i = 1; i < argc; ++i) { std::string o(argv[i]); setSearchOption(g_searchOptions, o, true); }
  std::ostringstream sw; std::streambuf* sv = std::cout.rdbuf(sw.rdbuf());
  for (const char* f : FENS) {
    Board b; b.setFromFEN(f);
    TranspositionTable tt(64); std::atomic<bool> stop{false};
    findBestMoveIterativeDeepening(b, 9, stop, tt);
  }
  std::cout.rdbuf(sv);
  uint64_t total = 0; for (int i = 0; i < 64; ++i) total += g_cutoffAt[i].load();
  std::printf("  cutoffs: %llu\n", (unsigned long long)total);
  uint64_t cum = 0;
  for (int i = 0; i < 6; ++i) {
    const uint64_t c = g_cutoffAt[i].load(); cum += c;
    std::printf("    move %d %10llu  %5.1f%%   cumulative %5.1f%%\n",
                i + 1, (unsigned long long)c, c * 100.0 / total, cum * 100.0 / total);
  }
  uint64_t rest = total - cum;
  std::printf("    later  %10llu  %5.1f%%\n", (unsigned long long)rest, rest * 100.0 / total);
  return 0;
}
