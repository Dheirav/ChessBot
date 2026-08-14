#pragma once
#include "board.hpp"
#include "move.hpp"
#include "transposition_table.hpp"
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

// Search heuristics. Unlike alpha-beta these are not exact: they trade a small
// risk of missing a line for a much smaller tree, so they are toggleable both
// for A/B match testing and so they can be disabled if they ever misbehave.
struct SearchOptions {
    bool nullMove = true;    // null-move pruning: skip a turn, prune if still failing high
    bool lmr = true;         // late move reductions: search unpromising moves shallower
    bool aspiration = true;  // aspiration windows: narrow root window around the last score
    bool quiet = false;      // suppress the per-depth progress output

    // Static exchange evaluation, as two independently gated uses (PLAN.md 3.2).
    // Each is accepted or rejected by its own A/B match, and the defaults below
    // are what those matches returned on 2026-08-13 over 3 360 games each.
    //
    // Ordering is ON: +25.6 Elo, 95% CI [+16.1, +35.2], at equal nodes.
    //
    // Pruning is OFF, but *not* because it was rejected. It measured +2.2 with
    // the interval spanning zero on a node-budgeted gate — and a node budget
    // pays both sides the same nodes, so it divides out exactly the thing
    // pruning buys. Skipping losing captures in quiescence mostly reaches the
    // same conclusion faster (−41.1% nodes at bench 6); it barely changes which
    // conclusion that is. It stays off until a *timed* gate rules on it, since
    // shipping an unmeasured default is the habit these toggles exist to break.
    bool seeOrdering = true;   // sort captures by the exchange result, not the victim
    bool seePruning = false;   // quiescence skips captures that lose material

    // Quiescence bounds (PLAN.md 3.1, BUGS.md 4).
    //
    // qBound defaults ON because it is a repair, not a feature — the same
    // reasoning that put ttAging on. quiescence() takes a ply and never capped
    // it, and while in check it generates every legal evasion, so a long
    // forcing sequence recursed without limit. The failure mode is a search
    // that overruns its budget, and an overrun on a clock is a forfeit. It is
    // still a toggle so the repair can be measured rather than assumed.
    //
    // deltaPruning defaults OFF because it *is* a feature: it changes which
    // captures get searched, so it ships only if a gate says it earns its
    // place. Shipping an unmeasured default is the habit these toggles exist
    // to break.
    bool qBound = true;        // cap how far past the horizon quiescence may recurse
    bool deltaPruning = false; // skip captures that cannot raise alpha

    // Check extensions (PLAN.md 3.3): search one ply deeper when in check.
    // Off until gated, like every other feature here.
    bool checkExtension = false;

    // Age the transposition table once per search, so entries left by earlier
    // searches lose their depth-preference and can be displaced.
    //
    // Defaults ON, unlike the two above, because it is a repair rather than a
    // feature: without it the table is ageless and a game's dead positions
    // crowd out the live search. It is still a toggle so the repair can be
    // measured — see PLAN.md's gate for it — and so the old behaviour is one
    // flag away if the measurement ever disagrees.
    bool ttAging = true;
};
extern SearchOptions g_searchOptions;

// Set one option by name: "nullmove", "lmr", "aspiration". Returns false if the
// name is unknown, so a caller can report a typo rather than silently ignore it.
//
// Named lookup exists so a single heuristic can be toggled on its own. Every
// Phase 3 search feature is accepted or rejected by an A/B match, and that
// needs the control arm to differ in exactly one thing — which a single "all
// heuristics on/off" switch cannot express. Each new feature adds one line
// here and becomes testable from the match harness and over UCI at once.
bool setSearchOption(SearchOptions& opts, const std::string& name, bool value);

// Every switchable search feature, in one table. setSearchOption() and
// describeSearchOptions() both read it, so a name can never be settable but
// undescribable — which is exactly the drift that invalidated a gate once.
struct SearchOptionEntry {
    const char* name;       // lookup key, lowercase: "seepruning"
    const char* shortName;  // label used in match headers: "seeprune"
    const char* uciName;    // as advertised over UCI: "SeePruning"
    bool SearchOptions::*field;
};
extern const SearchOptionEntry SEARCH_OPTIONS[];
extern const size_t SEARCH_OPTION_COUNT;

// "nullmove+lmr+asp+seeprune", or "plain-alphabeta" when nothing is enabled.
//
// Any logged comparison must say precisely what was compared or it cannot be
// interpreted later — the problem with the −30 Elo figure in BACKLOG.md that
// did not record its depth.
std::string describeSearchOptions(const SearchOptions& opts);

// Nodes visited by the last search: every entry to the main search or to
// quiescence. Reset at the start of findBestMoveIterativeDeepening.
//
// Deliberately not atomic. Only one search runs at a time, and the value is
// read after that search returns, so the increment stays a single add in the
// hottest loop in the engine. If the search is ever made concurrent (Lazy SMP),
// this becomes per-thread state, not an atomic.
//
// It exists for two reasons: UCI reports nodes and nps, and tests/bench.cpp
// uses the total as a signature. Any change that claims to preserve search
// behaviour must reproduce the signature exactly.
extern uint64_t g_searchNodes;

// Checkmate score, from the perspective of the side to move: being mated is
// -(SEARCH_MATE_SCORE - ply). Exposed so callers can recognise a mate score and
// convert it to a distance (UCI reports "mate N" rather than a centipawn value).
constexpr int SEARCH_MATE_SCORE = 30000;

// Called once per completed iteration, if set. `score` is centipawns from the
// side to move's point of view, or a mate score as above. Used by the UCI layer
// to emit "info depth ... score ... nodes ... pv ...". Null by default, so the
// search prints nothing extra unless someone asks.
using SearchInfoFn = void (*)(int depth, int score, uint64_t nodes,
                              long elapsedMs, const Move& best);
extern SearchInfoFn g_searchInfo;

// What the search is allowed to spend.
//
// A depth limit alone cannot play a real game: the same depth costs
// milliseconds in an endgame and seconds in a dense middlegame. A time limit
// alone cannot be benchmarked: node counts stop being reproducible. Both exist,
// and whichever binds first ends the search.
struct SearchLimits {
    int maxDepth = 64;       // hard ceiling on iterations
    long moveTimeMs = 0;     // wall-clock budget for this move; 0 = no budget
    uint64_t maxNodes = 0;   // node budget for this move; 0 = no budget

    SearchLimits() = default;
    explicit SearchLimits(int depth) : maxDepth(depth) {}
    SearchLimits(int depth, long ms) : maxDepth(depth), moveTimeMs(ms) {}
};

// A node budget is a time control that does not depend on the machine.
//
// Two engine configurations given the same milliseconds are only comparable if
// they get the same share of the CPU for the whole match — which is false the
// moment anything else runs, and false in a different way on every machine. The
// SEE gate that prompted this spent its first hours against a job pinning
// fifteen cores and its last hours on an idle box: the same command, two
// different time controls, one pooled result.
//
// Nodes remove that. The budget is spent identically wherever it runs, the same
// seed replays the same games move for move, and matches can be sharded across
// cores without each shard changing the others' effective time control. It
// costs one thing worth stating: a feature whose value is speed per node rather
// than quality per node — a faster evaluation, say — is invisible to a
// node-limited match, and must be gated on the clock instead.

// Positions the game already visited, for repetition detection.
//
// A search that is handed only a Board cannot see a repetition, because a
// board is a position and a repetition is a property of a *game*. Without this
// the engine detects only the repetitions it invents inside its own tree and
// is blind to the ones it is actually walking into: it once drew a rook-up
// position by playing the move that made the threefold, its own evaluation
// reading +5.16 the whole way (BUGS.md 1).
//
// Callers replaying a game call this after each makeMove. `hashBefore` is the
// hash of the position the move was made from, and `after` is the board once
// the move has been played. Everything before an irreversible move is dropped,
// because no position across a capture or a pawn move can ever recur — which
// also keeps the list short enough for the linear scan the search does on it.
void recordGamePosition(std::vector<uint64_t>& history, uint64_t hashBefore,
                        const Board& after);

// The engine's only search entry point. Iterative deepening over a
// transposition table, with the heuristics in SearchOptions above.
//
// There is deliberately no non-TT variant: a second copy of the search logic
// drifts from this one, and benchmarking it produces numbers that describe a
// search the application never runs.
//
// With a time budget the search returns the best move from the last *completed*
// iteration. A partial iteration is never used: its move list is only partly
// searched, so its "best" move is just the best of an arbitrary prefix.
//
// `gameHistory` defaults to empty, which means "this position has no past" —
// correct for a bench or a test position, and wrong for anything playing a
// game. Every caller that plays games passes it.
Move findBestMoveIterativeDeepening(Board& board, const SearchLimits& limits,
                                   const std::atomic<bool>& shouldStop,
                                   TranspositionTable& tt,
                                   const std::vector<uint64_t>& gameHistory = {});

// Depth-only convenience overload, for tests and for callers with no clock.
Move findBestMoveIterativeDeepening(Board& board, int maxDepth,
                                   const std::atomic<bool>& shouldStop,
                                   TranspositionTable& tt,
                                   const std::vector<uint64_t>& gameHistory = {});

