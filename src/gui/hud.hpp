#pragma once
#include <cstdint>
#include <string>

// Live search status, for display.
//
// The engine already produces depth, score, node count and elapsed time — the
// UCI layer reports all of it — but the GUI had no way to show any of it, so
// the only feedback during a search was the window sitting still. This is the
// bridge: the search thread publishes here, the render thread reads.
//
// Values are atomics rather than a mutex-guarded struct because the reader
// only needs a recent value, never a consistent set. A frame that pairs a new
// depth with an old node count is invisible; a frame that blocks on a lock
// held by the search is not.
namespace hud {

// Installs the callback on the engine's per-iteration hook. Call once at
// startup. Also silences the search's own stdout logging, which this replaces.
void installSearchCallback();

// Cleared when a new search starts, so a finished search's numbers do not sit
// on screen looking live.
void beginSearch(long budgetMs);
void endSearch();

struct Snapshot {
    bool active = false;      // a search is running
    bool hasInfo = false;     // at least one iteration has completed
    int depth = 0;
    int scoreCp = 0;          // centipawns, from the searching side's view
    uint64_t nodes = 0;
    long elapsedMs = 0;       // wall clock since the search began
    long infoElapsedMs = 0;   // elapsed as of the last completed iteration;
                              // the node count belongs to this, not to the
                              // wall clock, so nps must be computed from it
    long budgetMs = 0;        // 0 when the search is depth-limited
};
Snapshot read();

// Formats a centipawn score the way a human reads it: "+1.25", "-0.40",
// "M3" for a mate the side to move gives, "-M2" for one it receives.
std::string formatScore(int scoreCp);

// Milliseconds as m:ss.t, which stays readable as a game gets long.
std::string formatClock(long ms);

}  // namespace hud
