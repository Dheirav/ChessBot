#include "gui/hud.hpp"
#include "engine/search.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace hud {
namespace {

std::atomic<bool> g_active{false};
std::atomic<bool> g_hasInfo{false};
std::atomic<int> g_depth{0};
std::atomic<int> g_score{0};
std::atomic<unsigned long long> g_nodes{0};
std::atomic<long> g_elapsed{0};
std::atomic<long> g_budget{0};

// Written on the main thread when a search begins, read on the main thread
// when rendering; the search thread never touches it.
std::chrono::steady_clock::time_point g_start;

void onInfo(int depth, int score, uint64_t nodes, long elapsedMs, const Move&) {
    g_depth.store(depth, std::memory_order_relaxed);
    g_score.store(score, std::memory_order_relaxed);
    g_nodes.store(nodes, std::memory_order_relaxed);
    g_elapsed.store(elapsedMs, std::memory_order_relaxed);
    g_hasInfo.store(true, std::memory_order_relaxed);
}

}  // namespace

void installSearchCallback() {
    g_searchInfo = onInfo;
    // The search used to narrate every iteration to stdout. That was the only
    // progress report there was; now that the panel shows it, the console
    // spam is just noise.
    g_searchOptions.quiet = true;
}

void beginSearch(long budgetMs) {
    g_active.store(true, std::memory_order_relaxed);
    g_hasInfo.store(false, std::memory_order_relaxed);
    g_depth.store(0, std::memory_order_relaxed);
    g_nodes.store(0, std::memory_order_relaxed);
    g_elapsed.store(0, std::memory_order_relaxed);
    g_budget.store(budgetMs, std::memory_order_relaxed);
    g_start = std::chrono::steady_clock::now();
}

void endSearch() {
    g_active.store(false, std::memory_order_relaxed);
}

Snapshot read() {
    Snapshot s;
    s.active = g_active.load(std::memory_order_relaxed);
    s.hasInfo = g_hasInfo.load(std::memory_order_relaxed);
    s.depth = g_depth.load(std::memory_order_relaxed);
    s.scoreCp = g_score.load(std::memory_order_relaxed);
    s.nodes = g_nodes.load(std::memory_order_relaxed);
    s.budgetMs = g_budget.load(std::memory_order_relaxed);
    s.infoElapsedMs = g_elapsed.load(std::memory_order_relaxed);
    // While a search runs, the wall clock is more truthful than the last
    // iteration's stamp: an iteration that is still running has not reported
    // yet, and a frozen number reads as a hung engine.
    if (s.active) {
        s.elapsedMs = (long)std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - g_start).count();
    } else {
        s.elapsedMs = g_elapsed.load(std::memory_order_relaxed);
    }
    return s;
}

std::string formatScore(int scoreCp) {
    char buf[32];
    if (std::abs(scoreCp) > SEARCH_MATE_SCORE - 1000) {
        const int plies = SEARCH_MATE_SCORE - std::abs(scoreCp);
        const int moves = (plies + 1) / 2;
        std::snprintf(buf, sizeof(buf), "%sM%d", scoreCp > 0 ? "" : "-", moves);
    } else {
        std::snprintf(buf, sizeof(buf), "%+.2f", scoreCp / 100.0);
    }
    return buf;
}

std::string formatClock(long ms) {
    if (ms < 0) ms = 0;
    const long tenths = (ms / 100) % 10;
    const long totalSeconds = ms / 1000;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%ld:%02ld.%ld",
                  totalSeconds / 60, totalSeconds % 60, tenths);
    return buf;
}

}  // namespace hud
