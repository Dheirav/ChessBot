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
    //
    // ON since 2026-08-14: **+23.0 Elo, 95% CI [+13.3, +32.7]** over 3 360
    // games at equal nodes, 11 of 12 shards positive. The largest accepted gain
    // since `seeordering`, and the right instrument saw it — this is
    // quality-per-node, unlike `seepruning` and `deltaPruning`, which buy speed
    // and which a node budget therefore hides.
    //
    // It costs +9.2% nodes at bench 6 and is worth it, which is the cleanest
    // demonstration in this file that node count is not the thing being
    // optimised.
    bool checkExtension = true;

    // Age the transposition table once per search, so entries left by earlier
    // searches lose their depth-preference and can be displaced.
    //
    // Defaults ON, unlike the two above, because it is a repair rather than a
    // feature: without it the table is ageless and a game's dead positions
    // crowd out the live search. It is still a toggle so the repair can be
    // measured — see PLAN.md's gate for it — and so the old behaviour is one
    // flag away if the measurement ever disagrees.
    bool ttAging = true;

    // Time management: begin an iteration whenever the per-move target has not
    // passed and let it run to a separate hard cap, instead of refusing to
    // begin one unless the whole predicted iteration fits inside the target
    // (BUGS.md 11).
    //
    // **ON since 2026-08-20: +42 Elo, 95% CI [+6, +79]** over 200 games at
    // `--tc 120+1.33`, **zero time forfeits**. The engine was spending only 75%
    // of the time it allocated itself; this recovers it.
    //
    // Read the interval against the first attempt's, because the difference is
    // the whole story. Gated at `--tc 30+0.33` this measured **+78 [+40,
    // +117]**, shipped on 2026-08-16, and forfeited a rated game the next day.
    // Its hard bound was a *ratio* -- `budget * 3` -- which permits a
    // 2-second overshoot at a 30-second control and a **seventy-second** one at
    // 900+10. The engine took them: 73 s on move one, then 1.3x to 3.8x the
    // target every move until the clock ran out.
    //
    // The bound is now absolute as well as proportional:
    // `min(budget + increment, budget * 3, cap)`. One increment is the bound
    // that travels between time controls, because overshooting by it is
    // self-financing -- the increment arrives on the next move. The multiple
    // survives only for the zero-increment case, where `budget + 0` would
    // collapse the split to nothing.
    //
    // **That bound costs about half the gain, +78 down to +42, and it is worth
    // paying.** The engine now uses less of the extra time than it did when it
    // was free to overshoot by seventy seconds. A forfeit is a whole game.
    //
    // Two things were done differently this time and both were the point.
    // The bound was verified at 900+10 clocks *before* any gate ran -- 44.8 s
    // spent against a 44.9 s bound where the old code took 73 s. And the gate
    // ran at a control with the same 90:1 shape as the one the bot plays,
    // rather than a thirtieth of it, so a failure that only appears at long
    // time controls had somewhere to appear.
    //
    // It changes nothing except when a caller states a clock: the split is
    // armed in parseGo's clock branch alone, so `-t`, `-N` and every depth
    // search are untouched by construction -- which is why `bench` and
    // `evalref` are unmoved by a change worth 42 Elo.
    bool softTime = true;

    // Internal iterative deepening (PLAN.md 3.5): with no transposition-table
    // move to order on, search the position shallowly first and order on
    // whatever that finds.
    //
    // **Gated 2026-08-18 and stays off: -0.1 Elo, 95% CI [-4.9, +4.7]** over
    // 3 360 games at `-N 100000`. 1 183 wins against 1 184 losses.
    //
    // This is the tightest null this project has produced, and it is a real
    // answer rather than an inconclusive one: `seepruning` measured [-7.2,
    // +11.6] and `timealloc` [-22, +50], intervals wide enough to hide
    // something useful. +/-4.9 is not. There is nothing here worth having.
    //
    // The reason is structural and the bench numbers said so before the gate
    // did. IID exists to supply an ordering move when the transposition table
    // has none, and iterative deepening means every node at remaining depth
    // >= 5 was visited by the previous iteration and is already in the table.
    // It never fires at bench 6 at all, and moves the tree 0.19% at depth 7
    // and 0.30% at depth 8. A technique that barely changes the tree cannot
    // change the result.
    //
    // Kept, off, with its number recorded -- the same treatment `seepruning`
    // and `deltapruning` get -- so nobody builds it again expecting free Elo.
    // If the transposition table ever shrinks enough for misses to be common
    // at depth, this becomes worth re-asking; not before.
    bool iid = false;

    // The other half of BUGS.md 11: how large the per-move target is, and how
    // it changes as the game goes on.
    //
    // Off computes `remaining / 30 + increment / 2` — a constant divisor, so
    // the allocation decays geometrically as the clock shrinks, and half the
    // increment banked for nothing. On counts the moves down (`remaining /
    // max(80 - moveNumber, 30) + increment`), which flattens the curve and
    // moves time out of the opening and into the middlegame.
    //
    // **Gated 2026-08-17 and stays off: +14 Elo, 95% CI [-22, +50]** over 200
    // games at `--tc 30+0.33`, zero forfeits. The interval spans zero, so this
    // demonstrates nothing in either direction — it is not a rejection.
    //
    // The point estimate is positive and the argument is sound, which is
    // exactly the position `seepruning` and `deltapruning` are in, and it gets
    // the same treatment: kept, off, with its number recorded so nobody re-runs
    // the experiment by accident. Resolving a +14 effect needs roughly four
    // times the games — about twenty hours of unshardable wall clock, because
    // `--tc` cannot be sharded — which is why this is where it stops.
    //
    // Worth knowing before reopening it: after softTime shipped, *every*
    // allocation formula converges to the same total, about 97% of the clock.
    // The `remaining/4` cap and the increment dominate. So there is no more
    // time to find, and this only redistributes what is already being spent —
    // which bounds how much it could ever have been worth.
    bool timeAlloc = false;

    // --- PLAN 3.4: futility at shallow depths, both halves separately ---
    //
    // Both are the same bet delta pruning makes: trust the static evaluation to
    // say a node cannot matter, and skip work on the strength of it. 3.1 is the
    // record of what that bet costs when the margin is wrong — delta pruning at
    // a 200cp margin measured **−50.0 Elo**, and the same rule at 900cp
    // measured **+7.1**. Fifty-seven Elo of swing from one constant.
    //
    // What is different this time is that the evaluation's error is *measured*
    // rather than assumed. Over 688 ordinary positions (`tests/evalerror`, the
    // `ctl` set) the static evaluation differs from Stockfish at depth 16 by:
    //
    //     median 125cp | 75th 256cp | 90th 407cp | mean 182cp
    //
    // Textbook futility margins are 100–150cp per ply. **This evaluation is
    // wrong by more than that in half of all ordinary positions**, so a
    // textbook margin here prunes on the evaluation's own noise — which is
    // exactly the mistake 3.1 made and paid 50 Elo for. The margins in
    // search.cpp are sized off that distribution instead, and are roughly
    // 2–3× textbook.
    //
    // `razoring` is **ON since 2026-08-22: +39.1 Elo, 95% CI [+28.4, +49.9]**
    // over 2 400 games at `-N 100000`, `shard-20260822-033651/`. The largest
    // accepted gain since the `threats` deletion, and positive on both axes —
    // better per node *and* 21% fewer nodes at bench 6, which a node-budgeted
    // gate cannot credit it for. No best move changed on any of the nine bench
    // positions.
    //
    // The margin is why it worked. 3.1 lost 50 Elo making this bet at a
    // textbook-ish 200cp; 500cp here sits outside the evaluation's own measured
    // error, and that is the whole difference.
    //
    // `revFutility` is **ON since 2026-08-22: +18.4 Elo, 95% CI [+7.8, +29.1]**
    // over 2 400 games, `shard-20260822-113235/` — measured *on top of*
    // razoring, not against the bare baseline, because both prune the same
    // shallow nodes and "worth +12 alone" is not "worth +12 on top". It was
    // +12.3 [+1.5, +23.1] alone (`shard-20260822-025838/`), so the two are not
    // redundant; if anything each is worth slightly more with the other in.
    bool revFutility = true;   // node already far above beta: cut without searching
    bool razoring = true;      // node far below alpha: drop straight to quiescence

    // Late move pruning (PLAN.md 3.4's remaining item). LMR already searches
    // late quiet moves *shallower*; this skips them entirely once the move
    // count is high enough and the depth is low enough that a full search
    // cannot pay for itself.
    //
    // **ON since 2026-08-26: +13.1 Elo, 95% CI [+3.5, +22.8]** over 3 360 games
    // at equal nodes, `shard-20260826-181028/`. Cuts 45.5% of nodes at bench
    // depth 6 and 41.4% at depth 8.
    //
    // It is the most dangerous kind of pruning in this engine because it never
    // looks at the move: a reduction that guesses wrong costs a re-search,
    // while a prune that guesses wrong loses the move. The guards are what make
    // it safe -- PV nodes, checks, captures, promotions and the first move at
    // any node are all exempt.
    //
    // The endgame worry was checked rather than assumed, because the `zugzwang`
    // bench position changes its depth-6 answer with this on. Over twelve
    // endgame positions at depth 12: **no move changed anywhere**, pure pawn
    // endings came out bit-identical in node count (the hasNonPawnMaterial
    // guard doing its job), and the zugzwang study resolves to the same move
    // as the shipped build once the search is deep enough -- e1f1 from both.
    // The depth-6 disagreement was LMP arriving at the deeper answer sooner,
    // not disagreeing with it.
    bool lateMovePruning = true;

    // Fires LMP only to remaining depth 2 instead of 3.
    //
    // **ON since 2026-08-27: +15.0 Elo, 95% CI [+5.6, +24.4]** over 3 360 games
    // at equal nodes, `shard-20260827-150404/`. It costs 2.1% more nodes at
    // bench 6 and buys back judgement worth ten times that.
    //
    // A toggle rather than an int option because the harness takes booleans,
    // and because the question was binary: the shipped 3 against the one value
    // worth trying.
    //
    // The reason it is worth trying is measured. Over 120 positions sampled
    // from the game archive, LMP changes the chosen move in 49 of them, and a
    // depth-11 search adjudicating those disagreements endorses LMP's move 13
    // times against the shipped move 20. So LMP gives up judgement on roughly
    // one position in six to buy its 45% of nodes. Pruning one ply shallower
    // should hand some of that back while keeping most of the saving --
    // which was a hypothesis, and the gate settled it. The diagnostic is the
    // reason this exists at all: LMP shipped at +13.1 and looked finished, and
    // the 13-20 adjudication is what turned it into a testable question worth
    // another +15.
    bool lmpShallow = true;

    // One rung shallower again: LMP fires only at remaining depth 1. Off until
    // gated, and stacked on `lmpShallow` rather than replacing it, so an A/B
    // differs in exactly one thing with the shipped setting on both sides.
    //
    // Deliberately proposed with **no theory attached**. Depth 3 -> 2 was worth
    // +15.0 and the explanation offered for it — that pruning less hands back
    // judgement — did not survive measurement: over the same 120 positions the
    // referee still sided against LMP 18 times against 20 before, so the
    // disagreement *rate* barely moved while the Elo did. The honest position
    // is that the mechanism is not understood, only the trend, and this asks
    // whether the trend continues rather than predicting that it will.
    bool lmpDepth1 = false;

    // Singular extensions. If the transposition table's move looks much better
    // than every alternative, search it one ply deeper.
    //
    // The test is a search with that move *excluded*, at a window just below
    // the score the table already claims for it. If everything else fails to
    // reach that window, the move is "singular" -- the position hangs on it --
    // and the extra ply is spent where it decides the game.
    //
    // Off until gated. It is the most intricate thing in this file: it runs a
    // nested search inside move ordering, and that search must not read or
    // write the transposition table for this node, because it is answering a
    // different question about the same position.
    bool singularExt = false;

    // There is no king-safety toggle here, and on 2026-08-16 there briefly were
    // two. Both were gated and neither earned its place; the numbers and the
    // reasoning are in evaluation.cpp beside the term they describe, and in
    // ROADMAP.md 6.4.
    //
    // Worth carrying forward: unlike every toggle above, an *evaluation* toggle
    // cannot be gated by --optA/--optB, because tests/match runs both sides in
    // one process and g_evalCache is keyed on position alone (BUGS.md 8). It
    // needs two processes -- which now means tests/engine and the per-side
    // setoption forwarding added the same day, not two hand-maintained builds.
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

    // The point past which a running iteration is abandoned, as opposed to
    // moveTimeMs, which is the point past which a *new* one is not begun
    // (BUGS.md 11). 0 means the two coincide, which is how every caller that
    // states a plain budget behaves and what tests/bench and tests/timecontrol
    // rely on.
    //
    // Only a caller playing to a real clock has any use for the distinction: it
    // is the difference between "spend about this much" and "never exceed
    // this", and collapsing them costs a quarter of the budget in unstarted
    // iterations. A per-move budget like `-t` has no such distinction to make,
    // which is why it is not the default.
    long hardTimeMs = 0;

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

