// Engine-vs-engine match harness.
//
// The search heuristics (null-move pruning, late move reductions, aspiration
// windows) deliberately return different results from a plain alpha-beta
// search, so unlike a movegen or evaluation change they cannot be validated by
// comparing output against a reference. The only way to know whether they help
// is to play games and measure the score.
//
// Two configurations play a match under controlled conditions: the same
// openings, every opening played twice with colours swapped so neither side
// benefits from a favourable start, and either the same fixed depth or the same
// per-move time budget.
//
// Time control matters more than it looks. A fixed-depth match answers "do
// these cost accuracy at equal depth?" - it cannot answer "are they worth it?",
// which is a question about equal *time*. The heuristics here are 1.31x faster
// at depth 4 and 31x faster at depth 9 (BACKLOG.md section 7), so a fixed-depth
// match at a shallow depth charges them their full accuracy cost while giving
// them almost none of their benefit.
//
// Equal time is best spent as equal *nodes* (-N). A millisecond is worth
// whatever the machine happens to have spare, so a match on the clock is not
// reproducible, and its result depends on what else was running; a node budget
// is the same everywhere, replays exactly, and lets shards run in parallel
// without changing what is being measured. Use -t only when the change under
// test is meant to make the engine faster per node rather than better per node,
// since a node budget is blind to speed by construction.
//
// Build and run:  make test-match
//   ./tests/match [gamePairs] [depth] [seed]        (positional, back-compatible)
//   ./tests/match -n 100 -t 1000 --sprt             (time-equalized, sequential)
//
// Options:
//   -n <pairs>      game pairs to play; each pair is two games (default 25)
//   -s <seed>       opening-line seed (default 20260810)
//   -d <depth>      fixed depth for both sides (default 4)
//   -t <ms>         per-move time budget for both sides; 0 = depth only
//   -N <nodes>      per-move node budget for both sides; 0 = no node budget
//   --tc <b>[+<i>]  a real game clock in seconds, e.g. --tc 60+1. Each side
//                     gets a clock that runs down and is handed it over UCI, so
//                     the *engine* decides what to spend; overstepping loses on
//                     time. Needs --engineA/--engineB, refuses to combine with
//                     -t/-N, and cannot be sharded. This is the only way to
//                     measure time management, which -t makes invisible by
//                     answering the question the time manager exists to answer
//                     (BUGS.md 11).
//   --da/--db <d>   per-side depth, for comparing two depth settings
//   --ta/--tb <ms>  per-side time budget
//   --na/--nb <n>   per-side node budget
//   --ha/--hb on|off  all three search heuristics at once. Both default to ON:
//                     each side starts from the shipped configuration and a
//                     gate states its one difference. Asking for nothing is an
//                     error, not a match against itself.
//   --engineA/--engineB <path>   drive an engine *binary* over UCI instead of
//                     searching in this process. The only way to A/B anything
//                     that is not a SearchOption — an evaluation change, most
//                     of all, since evaluate_details() reads the board and no
//                     options at all. Two processes also share no eval cache
//                     and no transposition table, which one process cannot
//                     honestly claim (BUGS.md 8). Build the two commits you
//                     want to compare and pass both paths.
//   --optA/--optB     individual options, e.g. --optA nullmove=on,lmr=off
//                     Applied after --ha/--hb, so they refine that baseline.
//                     This is how a single feature gets its own A/B: the two
//                     sides must differ in exactly one thing for the result to
//                     mean anything about that thing.
//   --sprt          stop as soon as the result is decided (see below)
//   --elo0/--elo1   SPRT hypotheses in Elo (default 0 and 10)
//
// Interpreting the result. A fixed-size match reports a 95% confidence
// interval; if it spans zero, the match has not demonstrated a difference in
// either direction, and at the observed draw rates that needs many hundreds of
// games. --sprt instead stops the moment the evidence is conclusive either way,
// which usually costs a fraction of the games - and fails a bad change fast.
#include "engine/board.hpp"
#include "engine/search.hpp"
#include "engine/movegen.hpp"
#include "engine/move_lookup.hpp"
#include "engine/transposition_table.hpp"
#include "engine/piece.hpp"
#include "engine/uci_engine.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

struct EngineConfig {
    const char* name;
    SearchOptions opts;
    SearchLimits limits;
    // Empty means "search in this process with `opts`". Otherwise the path to
    // an engine binary driven over UCI, which is the only way to A/B anything
    // that is not a SearchOption — evaluation, most of all (BUGS.md 8).
    std::string binary;
};


enum Result { A_WINS, DRAW, B_WINS };

struct GameOutcome {
    Result result;
    int plies;
    const char* how;   // "mate", "stalemate", "50-move", ... "adjudicated"
};

// A real game clock, as opposed to a budget per move.
//
// `-t` states how long a move may take, which is the decision an engine's time
// manager exists to make -- so a match run that way cannot see time management
// at all, and ChessBot's allocation (uci.cpp parseGo) had never been executed
// by any test in this repository (BUGS.md 11). This gives each side a clock
// that runs down, hands it the position and the clock, and lets it choose.
//
// It also makes a *forfeit* observable. That is this project's most expensive
// failure mode (BUGS.md 7, -120 rating) and until now it could only be detected
// by losing a rated game on Lichess.
//
// Deliberately not shardable and not reproducible from a seed: wall-clock time
// depends on machine load, so parallel shards would each play a weaker engine
// than they would alone. shard-gate.sh already refuses anything that is not
// node-limited, which covers this without a new rule.
struct TimeControl {
    long baseMs = 0;    // 0 = no clock; use depth/time/node budgets instead
    long incMs  = 0;
    bool active() const { return baseMs > 0; }
};

// --- Adjudication ---
//
// Without it every won game is played out to mate and every dead ending grinds
// on to the fifty-move rule, at full search cost per move, to confirm a result
// both engines already agree on. These are cutechess-cli's defaults, which is
// deliberate: they are the numbers the rest of the field's results were
// produced under, and a threshold tuned here would make this harness's Elo
// incomparable with everyone else's.
static const int RESIGN_SCORE  = 800;  // centipawns
static const int RESIGN_PLIES  = 8;    // consecutive plies, i.e. 4 moves each
static const int DRAW_SCORE    = 10;
static const int DRAW_PLIES    = 8;
static const int DRAW_MIN_PLY  = 80;   // no draw adjudication before move 40,
                                       // counted from the end of the opening

// The search reports each completed iteration through this callback; the last
// one is the score the engine actually moved on. Capturing it here avoids a
// second search or a new engine API, at the cost of one global — which is fine
// in a harness that already runs one game at a time.
static int g_lastScore = 0;
static bool g_haveScore = false;
static void captureScore(int, int score, uint64_t, long, const Move&) {
    g_lastScore = score;
    g_haveScore = true;
}

// Draws that no amount of play can escape: bare kings, or a lone minor piece.
static bool insufficientMaterial(const Board& b) {
    int minors = 0;
    for (int i = 0; i < 64; ++i) {
        PieceType t = b.squares[i].type();
        if (t == NONE || t == KING) continue;
        if (t == PAWN || t == ROOK || t == QUEEN) return false;
        if (++minors > 1) return false;
    }
    return true;
}

// Plays one game. Returns the result from configuration A's point of view.
static GameOutcome playGame(const std::vector<Move>& opening, bool aPlaysWhite,
                            const EngineConfig& A, const EngineConfig& B,
                            TranspositionTable& ttA, TranspositionTable& ttB,
                            int maxPlies,
                            UciEngine* extA, UciEngine* extB,
                            const TimeControl& tc = TimeControl()) {
    Board board;
    board.setFromFEN(Board::INITIAL_FEN);
    // Positions the game has visited, handed to each search so the engines can
    // see repetitions rather than only the ones they invent inside their own
    // trees. Without this a gate on repetition handling would be two blind
    // engines playing each other, which measures nothing.
    std::vector<uint64_t> history;
    // The same game in UCI notation, for whichever side is an external binary.
    // A "position" command states the whole game, so this is the full move list
    // from the initial position, not a delta.
    std::vector<std::string> uciMoves;
    for (const Move& m : opening) {
        const uint64_t before = board.getHash();
        uciMoves.push_back(toUciMove(m));
        board.makeMove(m);
        recordGamePosition(history, before, board);
    }
    if (extA) extA->newGame();
    if (extB) extB->newGame();

    ttA.clear();
    ttB.clear();
    std::atomic<bool> stop{false};
    std::unordered_map<uint64_t, int> seen;
    seen[board.getHash()] = 1;

    // Consecutive plies for which every score seen has favoured one side by the
    // resign margin, or has been level within the draw margin. Counted in
    // plies, so reaching RESIGN_PLIES means both engines agreed for four moves
    // each — cutechess's "twosided" rule, which is the one worth having: a
    // single engine's opinion that it is winning is exactly the opinion an
    // evaluation bug produces.
    int whiteWinning = 0, blackWinning = 0, level = 0;

    // Clocks are per colour, not per engine, because that is what the protocol
    // states and what the engine has to reason about.
    long clockMs[2] = { tc.baseMs, tc.baseMs };   // [0] = white, [1] = black

    for (int ply = 0; ply < maxPlies; ++ply) {
        MoveList legal = generateLegalMoves(board, board.activeColor);
        if (legal.empty()) {
            // No legal move: checkmate if in check, otherwise stalemate.
            bool whiteToMove = (board.activeColor == COLOR_WHITE);
            int kingSq = -1;
            for (int i = 0; i < 64; ++i)
                if (board.squares[i].type() == KING && board.squares[i].color() == board.activeColor) { kingSq = i; break; }
            PieceColor opp = whiteToMove ? COLOR_BLACK : COLOR_WHITE;
            bool inCheck = (kingSq >= 0) && board.isSquareAttacked(kingSq, (int)opp);
            if (!inCheck) return {DRAW, ply, "stalemate"}; // stalemate
            // The side to move is mated, so the other side won.
            bool whiteWon = !whiteToMove;
            return {(whiteWon == aPlaysWhite) ? A_WINS : B_WINS, ply, "mate"};
        }
        if (board.halfmoveClock >= 100) return {DRAW, ply, "50-move"};
        if (insufficientMaterial(board)) return {DRAW, ply, "material"};

        bool whiteToMove = (board.activeColor == COLOR_WHITE);
        bool aToMove = (whiteToMove == aPlaysWhite);
        const EngineConfig& cfg = aToMove ? A : B;
        TranspositionTable& tt = aToMove ? ttA : ttB;

        UciEngine* ext = aToMove ? extA : extB;
        Move best;
        if (ext) {
            // External binary: it keeps its own board, transposition table and
            // eval cache, so nothing crosses between the two sides.
            const auto t0 = std::chrono::steady_clock::now();
            std::string mv = ext->bestMove(uciMoves, cfg.limits.moveTimeMs,
                                           cfg.limits.maxNodes, cfg.limits.maxDepth,
                                           tc.active() ? clockMs[0] : 0,
                                           tc.active() ? clockMs[1] : 0,
                                           tc.incMs, tc.incMs);
            if (tc.active()) {
                const long spent = (long)std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now() - t0).count();
                const int side = whiteToMove ? 0 : 1;
                clockMs[side] -= spent;
                if (clockMs[side] < 0) {
                    // Overstepping is a loss, exactly as it is on Lichess. The
                    // engine that ran out is the one to move.
                    const bool whiteLost = whiteToMove;
                    return {(whiteLost == aPlaysWhite) ? B_WINS : A_WINS, ply, "time forfeit"};
                }
                clockMs[side] += tc.incMs;
            }
            g_haveScore = ext->haveScore();
            g_lastScore = ext->lastScore();
            bool matched = false;
            for (const Move& m : legal)
                if (toUciMove(m) == mv) { best = m; matched = true; break; }
            if (!matched) {
                // An engine that answers with an illegal move is broken, and
                // quietly substituting a legal one would turn that into a
                // mysteriously weak score instead of an error.
                std::printf("engine %s returned unusable move \"%s\"\n",
                            cfg.name, mv.c_str());
                std::exit(1);
            }
        } else {
            g_searchOptions = cfg.opts;
            g_searchOptions.quiet = true;
            g_haveScore = false;
            best = findBestMoveIterativeDeepening(board, cfg.limits, stop, tt, history);
        }

        // Defensive: a search that returns nothing usable would otherwise
        // corrupt the game; fall back to the first legal move.
        bool ok = false;
        for (const Move& m : legal) if (m == best) { ok = true; break; }
        if (!ok) best = legal[0];

        // Adjudication counters, from white's point of view. A move whose
        // search reported nothing (no iteration completed) breaks every run
        // rather than being read as agreement.
        if (g_haveScore) {
            int whiteScore = whiteToMove ? g_lastScore : -g_lastScore;
            whiteWinning = (whiteScore >=  RESIGN_SCORE) ? whiteWinning + 1 : 0;
            blackWinning = (whiteScore <= -RESIGN_SCORE) ? blackWinning + 1 : 0;
            level        = (std::abs(whiteScore) <= DRAW_SCORE) ? level + 1 : 0;
        } else {
            whiteWinning = blackWinning = level = 0;
        }

        const uint64_t before = board.getHash();
        uciMoves.push_back(toUciMove(best));
        board.makeMove(best);
        recordGamePosition(history, before, board);
        if (++seen[board.getHash()] >= 3) return {DRAW, ply, "repetition"};

        if (whiteWinning >= RESIGN_PLIES || blackWinning >= RESIGN_PLIES) {
            bool whiteWon = (whiteWinning >= RESIGN_PLIES);
            return {(whiteWon == aPlaysWhite) ? A_WINS : B_WINS, ply, "resign"};
        }
        if (level >= DRAW_PLIES && ply >= DRAW_MIN_PLY) {
            return {DRAW, ply, "agreed"};
        }
    }
    return {DRAW, maxPlies, "ply limit"};
}

// Builds a random but legal opening line so the games are not all identical.
static std::vector<Move> makeOpening(std::mt19937& rng, int plies) {
    Board b;
    b.setFromFEN(Board::INITIAL_FEN);
    std::vector<Move> line;
    for (int i = 0; i < plies; ++i) {
        MoveList legal = generateLegalMoves(b, b.activeColor);
        if (legal.empty()) break;
        std::uniform_int_distribution<size_t> pick(0, legal.size() - 1);
        Move m = legal[pick(rng)];
        line.push_back(m);
        b.makeMove(m);
    }
    return line;
}

// --- SPRT ---
//
// A fixed-size match commits to N games before seeing any of them. A sequential
// test stops as soon as the evidence is conclusive, which for a clearly good or
// clearly bad change is a small fraction of N.
//
// H0: the change is worth elo0 (default 0, i.e. no gain).
// H1: the change is worth elo1 (default 10).
// The log-likelihood ratio walks between two bounds set by the error rates;
// crossing the upper bound accepts H1, the lower accepts H0.
static double eloToScore(double elo) {
    return 1.0 / (1.0 + std::pow(10.0, -elo / 400.0));
}

// Generalized SPRT under a normal approximation, over game *pairs*.
//
// The unit of observation is the pair, not the game. Both games of a pair are
// played from the same opening with the colours swapped, so the two results are
// strongly correlated: an opening that is simply good for white produces a win
// and a loss whichever engine is stronger, and scoring the games separately
// counts that shared noise twice. Scoring the pair once — its five possible
// outcomes 0, 0.5, 1, 1.5, 2 give this its usual name of pentanomial — measures
// the variance that is actually there. It is worth roughly 15-20% of the games
// to a decision, for no extra play.
//
// Counts are indexed by twice the pair score: 0 = lost both, 4 = won both.
//
// The only degenerate case is zero variance — every pair so far having had the
// identical result — and the variance test below catches it directly. An
// earlier version also bailed whenever any one of wins/draws/losses was still
// zero, which was wrong in exactly the case this test is most useful for: an
// engine that never loses keeps losses at 0 indefinitely, so the LLR would sit
// at 0.00 forever and a decisive match would never stop early.
static double computeLLR(const int pentanomial[5], double elo0, double elo1) {
    double n = 0.0;
    for (int i = 0; i < 5; ++i) n += pentanomial[i];
    if (n <= 0.0) return 0.0;

    // Per-pair score normalized to a per-game score in [0,1], so that it is
    // directly comparable with the Elo hypotheses.
    double mean = 0.0, second = 0.0;
    for (int i = 0; i < 5; ++i) {
        double x = i / 4.0;
        double p = pentanomial[i] / n;
        mean   += p * x;
        second += p * x * x;
    }
    double variance = second - mean * mean;
    if (variance <= 0.0) return 0.0;

    double s0 = eloToScore(elo0), s1 = eloToScore(elo1);
    return n * (s1 - s0) * (2.0 * mean - s0 - s1) / (2.0 * variance);
}

int main(int argc, char** argv) {
    initMoveLookupTables();

    int pairs = 25, depth = 4;
    long timeMs = 0;
    TimeControl tc;
    uint64_t nodes = 0, nodesA = 0, nodesB = 0;
    int depthA = -1, depthB = -1;
    long timeA = -1, timeB = -1;
    // Both sides start from the shipped configuration, and a gate changes one
    // thing with --optA/--optB.
    //
    // B used to default to heuristics-off, which was right for the single
    // comparison this harness was written for (gate 1.5, heuristics on vs off)
    // and a trap for every gate after it: `--optA seepruning=on` then silently
    // compared four differences instead of one. The old comparison is still
    // available, it just has to be asked for: `--hb off`.
    bool heurA = true, heurB = true;
    // Paths to engine binaries, for comparing two *builds* instead of two
    // option sets. Empty means "search in this process" (BUGS.md 8).
    std::string binA, binB;
    unsigned seed = 20260810u;
    bool depthGiven = false;
    bool useSprt = false;
    double elo0 = 0.0, elo1 = 10.0;
    const double ALPHA = 0.05, BETA = 0.05;
    const int MAX_PLIES = 300;
    const int OPENING_PLIES = 6;

    auto onOff = [](const char* v) { return std::string(v) == "on"; };

    // "nullmove=on,lmr=off" -> individual option settings on one side.
    std::vector<std::string> optsA, optsB;
    auto splitOpts = [](const char* text) {
        std::vector<std::string> out;
        std::string cur;
        for (const char* c = text; ; ++c) {
            if (*c == ',' || *c == '\0') {
                if (!cur.empty()) out.push_back(cur);
                cur.clear();
                if (*c == '\0') break;
            } else {
                cur += *c;
            }
        }
        return out;
    };

    // Positional form kept working: ./tests/match [pairs] [depth] [seed]
    if (argc > 1 && argv[1][0] != '-') {
        pairs = std::atoi(argv[1]);
        if (argc > 2) depth = std::atoi(argv[2]);
        if (argc > 3) seed = (unsigned)std::atoi(argv[3]);
        // It used to read three arguments and ignore the rest in silence, so
        // `./tests/match 2 4 <seed> --hb off` dropped the --hb and
        // `./tests/match 100 6 <seed> -N 100000` quietly ran a depth match.
        // A misconfigured measurement that runs is worse than one that refuses.
        if (argc > 4) {
            std::string rest;
            for (int i = 4; i < argc; ++i) rest += std::string(i > 4 ? " " : "") + argv[i];
            std::printf("the positional form takes at most [pairs] [depth] [seed], "
                        "so \"%s\" would be ignored.\n"
                        "Use the flag form to combine them:\n"
                        "  ./tests/match -n %d -d %d -s %u %s\n",
                        rest.c_str(), pairs, depth, seed, rest.c_str());
            return 1;
        }
    } else {
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            auto next = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : "0"; };
            if      (a == "-n")      pairs = std::atoi(next());
            else if (a == "-s")      seed = (unsigned)std::atoi(next());
            else if (a == "-d")    { depth = std::atoi(next()); depthGiven = true; }
            else if (a == "-t")      timeMs = std::atol(next());
            else if (a == "--tc") {
                // "<base>+<inc>" in seconds, the notation every chess tool uses
                // ("60+1"). Fractional base allowed so short controls are
                // expressible; "60" alone means no increment.
                const std::string v = next();
                const size_t plus = v.find('+');
                tc.baseMs = (long)(std::atof(v.substr(0, plus).c_str()) * 1000.0);
                tc.incMs  = (plus == std::string::npos)
                          ? 0 : (long)(std::atof(v.substr(plus + 1).c_str()) * 1000.0);
                if (tc.baseMs <= 0) {
                    std::printf("--tc wants <base>[+<inc>] in seconds, e.g. --tc 60+1\n");
                    return 1;
                }
            }
            else if (a == "-N")      nodes = std::strtoull(next(), nullptr, 10);
            else if (a == "--na")    nodesA = std::strtoull(next(), nullptr, 10);
            else if (a == "--nb")    nodesB = std::strtoull(next(), nullptr, 10);
            else if (a == "--da")    depthA = std::atoi(next());
            else if (a == "--db")    depthB = std::atoi(next());
            else if (a == "--ta")    timeA = std::atol(next());
            else if (a == "--tb")    timeB = std::atol(next());
            else if (a == "--ha")    heurA = onOff(next());
            else if (a == "--hb")    heurB = onOff(next());
            else if (a == "--optA")  optsA = splitOpts(next());
            else if (a == "--optB")  optsB = splitOpts(next());
            else if (a == "--engineA") binA = next();
            else if (a == "--engineB") binB = next();
            else if (a == "--sprt")  useSprt = true;
            else if (a == "--elo0")  elo0 = std::atof(next());
            else if (a == "--elo1")  elo1 = std::atof(next());
            else {
                std::printf("unknown option: %s\n", a.c_str());
                return 1;
            }
        }
    }

    // A clock and a per-move budget are two different experiments, and running
    // both makes the answer meaningless: whichever binds first decides, and the
    // one that binds is the one the time manager was supposed to choose.
    if (tc.active() && (timeMs > 0 || timeA >= 0 || timeB >= 0 || nodes > 0 || nodesA || nodesB)) {
        std::printf("refusing: --tc is a game clock, -t/-N are budgets per move.\n"
                    "Pass one or the other; together, whichever binds first "
                    "makes the time manager's decision for it.\n");
        return 1;
    }

    // A depth ceiling is always set. Under a time budget it is a safety limit
    // rather than the stopping condition, so it sits high enough not to bind.
    long budgetA = (timeA >= 0) ? timeA : timeMs;
    long budgetB = (timeB >= 0) ? timeB : timeMs;
    uint64_t nodeA = nodesA ? nodesA : nodes;
    uint64_t nodeB = nodesB ? nodesB : nodes;
    // Under a time budget, an unspecified depth means "as deep as the clock
    // allows" rather than the fixed-depth default, which would silently cap the
    // faster engine and defeat the point of equalizing on time.
    auto ceilingFor = [&](int sideDepth, long budget, uint64_t nodeBudget) {
        if (sideDepth > 0) return sideDepth;
        if ((budget > 0 || nodeBudget > 0) && !depthGiven) return 64;
        return depth;
    };
    int ceilA = ceilingFor(depthA, budgetA, nodeA);
    int ceilB = ceilingFor(depthB, budgetB, nodeB);

    EngineConfig A{"A", SearchOptions{}, SearchLimits(ceilA, budgetA)};
    A.limits.maxNodes = nodeA;
    A.opts.nullMove = heurA; A.opts.lmr = heurA; A.opts.aspiration = heurA;

    EngineConfig B{"B", SearchOptions{}, SearchLimits(ceilB, budgetB)};
    B.limits.maxNodes = nodeB;
    B.opts.nullMove = heurB; B.opts.lmr = heurB; B.opts.aspiration = heurB;

    // Individual options refine the on/off baseline set above.
    auto applyOpts = [](SearchOptions& opts, const std::vector<std::string>& list,
                        const char* which) {
        for (const std::string& item : list) {
            size_t eq = item.find('=');
            std::string key = (eq == std::string::npos) ? item : item.substr(0, eq);
            bool value = (eq == std::string::npos) ? true
                                                  : (item.substr(eq + 1) == "on");
            if (!setSearchOption(opts, key, value)) {
                std::printf("unknown option for %s: %s\n", which, key.c_str());
                std::exit(1);
            }
        }
    };
    applyOpts(A.opts, optsA, "--optA");
    applyOpts(B.opts, optsB, "--optB");

    // Name each side by the options it actually ends up with, not by the
    // --ha/--hb baseline. This lives in search.cpp beside the option table, so
    // that a feature added there cannot go missing from the header here.
    std::string nameA = describeSearchOptions(A.opts);
    std::string nameB = describeSearchOptions(B.opts);
    // Under --tc neither side has a budget of its own: the clock is the whole
    // condition, and it is the same for both, so it is named once.
    // %g so a sub-second increment does not print as "+0" -- the label is how
    // the result gets quoted later, and "60+0" for a 60+0.5 match is a wrong
    // record of what was run.
    char tcBuf[64] = {0};
    if (tc.active())
        std::snprintf(tcBuf, sizeof tcBuf, " @%g+%g", tc.baseMs / 1000.0, tc.incMs / 1000.0);
    const std::string tcLabel = tcBuf;
    if      (tc.active())  { nameA += tcLabel; nameB += tcLabel; }
    else {
    if      (nodeA > 0)   nameA += " @" + std::to_string(nodeA) + "n";
    else if (budgetA > 0) nameA += " @" + std::to_string(budgetA) + "ms";
    else                  nameA += " @d" + std::to_string(ceilA);
    if      (nodeB > 0)   nameB += " @" + std::to_string(nodeB) + "n";
    else if (budgetB > 0) nameB += " @" + std::to_string(budgetB) + "ms";
    else                  nameB += " @d" + std::to_string(ceilB);
    }
    A.binary = binA;
    B.binary = binB;
    if (!binA.empty()) nameA = binA + " " + nameA;
    if (!binB.empty()) nameB = binB + " " + nameB;
    A.name = nameA.c_str();
    B.name = nameB.c_str();

    std::printf("%s  vs  %s\n", A.name, B.name);

    // Started once and reused across games via "ucinewgame", rather than
    // spawned per game: a process launch per game would be most of the wall
    // clock at short time controls.
    // Same size as the in-process tables below, so switching a gate between
    // in-process and two-binary mode does not silently change the hash each
    // side gets — and so a sharded run's memory stays bounded.
    const int EXT_HASH_MB = 32;
    UciEngine extEngineA, extEngineB;
    // The time manager lives behind the UCI clock tokens (uci.cpp parseGo), so
    // only an engine driven as a binary can be measured on a clock at all. The
    // in-process path is handed a SearchLimits and never makes the decision.
    if (tc.active() && (A.binary.empty() || B.binary.empty())) {
        std::printf("refusing: --tc needs --engineA and --engineB.\n"
                    "The in-process search takes a budget it is given; only a "
                    "UCI binary chooses its own time (BUGS.md 11).\n");
        return 1;
    }

    // Hand an external engine the side's whole configuration, not just the
    // options that differ from its build defaults.
    //
    // Stating all of them is the point. A gate is only interpretable if exactly
    // one thing differs, and "the rest were left at whatever this binary
    // defaults to" is not a statement about what was compared — it is a promise
    // about a build, and builds are what change between the two sides.
    //
    // Only ChessBot understands these names, so a foreign engine (Stockfish, in
    // a sanity check) is left alone: it answers `info string unknown option`,
    // which is not a failure worth aborting on but is not something to send
    // fourteen of either.
    auto configure = [&](UciEngine& eng, const EngineConfig& side, const char* which) {
        for (size_t i = 0; i < SEARCH_OPTION_COUNT; ++i) {
            const bool on = side.opts.*(SEARCH_OPTIONS[i].field);
            if (!eng.setOption(SEARCH_OPTIONS[i].uciName, on ? "true" : "false")) {
                std::printf("engine %s stopped responding while being configured "
                            "(option %s)\n", which, SEARCH_OPTIONS[i].uciName);
                return false;
            }
        }
        return true;
    };

    UciEngine* extA = nullptr;
    UciEngine* extB = nullptr;
    if (!A.binary.empty()) {
        if (!extEngineA.start(A.binary, EXT_HASH_MB)) {
            std::printf("could not start engine A: %s\n", A.binary.c_str());
            return 1;
        }
        if (!configure(extEngineA, A, "A")) return 1;
        extA = &extEngineA;
    }
    if (!B.binary.empty()) {
        if (!extEngineB.start(B.binary, EXT_HASH_MB)) {
            std::printf("could not start engine B: %s\n", B.binary.c_str());
            return 1;
        }
        if (!configure(extEngineB, B, "B")) return 1;
        extB = &extEngineB;
    }

    // Say out loud what actually differs. A gate is only interpretable if
    // exactly one thing changed, and the cheapest moment to notice otherwise is
    // now rather than a day of wall clock later.
    {
        std::string diff;
        for (size_t i = 0; i < SEARCH_OPTION_COUNT; ++i) {
            bool a = A.opts.*(SEARCH_OPTIONS[i].field);
            bool b = B.opts.*(SEARCH_OPTIONS[i].field);
            if (a == b) continue;
            if (!diff.empty()) diff += ", ";
            diff += std::string(SEARCH_OPTIONS[i].shortName) +
                    (a ? " (A on, B off)" : " (A off, B on)");
        }
        if (budgetA != budgetB) {
            if (!diff.empty()) diff += ", ";
            diff += "time budget";
        }
        if (A.binary != B.binary) {
            if (!diff.empty()) diff += ", ";
            diff += "engine binary";
        }
        if (nodeA != nodeB) {
            if (!diff.empty()) diff += ", ";
            diff += "node budget";
        }
        if (ceilA != ceilB) {
            if (!diff.empty()) diff += ", ";
            diff += "depth ceiling";
        }

        if (diff.empty()) {
            std::printf("\nREFUSING TO RUN: A and B are configured identically, "
                        "so this match cannot measure anything.\n"
                        "Set the feature under test with --optA <name>=on.\n");
            return 1;
        }
        std::printf("difference: %s\n", diff.c_str());
    }

    std::printf("%d game pairs (up to %d games) | seed %u\n", pairs, pairs * 2, seed);
    if (useSprt) {
        std::printf("SPRT: H0 = %+.0f Elo, H1 = %+.0f Elo, alpha = beta = %.2f\n",
                    elo0, elo1, ALPHA);
    }
    std::printf("Each opening is played twice with colours swapped.\n\n");

    TranspositionTable ttA(32), ttB(32);
    std::mt19937 rng(seed);
    g_searchInfo = captureScore;

    int wins = 0, draws = 0, losses = 0;
    int pentanomial[5] = {0, 0, 0, 0, 0};
    long totalPlies = 0;
    int adjudicated = 0;
    const double lowerBound = std::log(BETA / (1.0 - ALPHA));
    const double upperBound = std::log((1.0 - BETA) / ALPHA);
    const char* sprtVerdict = nullptr;

    auto t0 = std::chrono::steady_clock::now();
    for (int p = 0; p < pairs && !sprtVerdict; ++p) {
        std::vector<Move> opening = makeOpening(rng, OPENING_PLIES);

        // Both games of the pair are always played. Stopping between them would
        // leave the sample with one more white game than black for A, and white
        // scores better — a bias introduced by the stopping rule itself, in the
        // one place the harness works hardest to avoid one.
        int pairScore = 0;  // twice A's score over the pair: 0..4
        for (int side = 0; side < 2; ++side) {
            bool aWhite = (side == 0);
            GameOutcome g = playGame(opening, aWhite, A, B, ttA, ttB, MAX_PLIES,
                                     extA, extB, tc);

            if (g.result == A_WINS)      { ++wins;   pairScore += 2; }
            else if (g.result == B_WINS) { ++losses; }
            else                         { ++draws;  pairScore += 1; }
            totalPlies += g.plies;
            if (std::string(g.how) == "resign" || std::string(g.how) == "agreed") ++adjudicated;

            std::printf("  pair %3d %s: %-7s %-10s (W-D-L %d-%d-%d)\n",
                        p + 1, aWhite ? "A=white" : "A=black",
                        g.result == A_WINS ? "A wins" : (g.result == B_WINS ? "B wins" : "draw"),
                        g.how, wins, draws, losses);
            std::fflush(stdout);
        }
        ++pentanomial[pairScore];

        if (useSprt) {
            double llr = computeLLR(pentanomial, elo0, elo1);
            std::printf("  pair %3d done: %.1f/2 | LLR %+.2f [%.2f,%.2f]\n",
                        p + 1, pairScore / 2.0, llr, lowerBound, upperBound);
            std::fflush(stdout);
            if (llr >= upperBound)      sprtVerdict = "H1 accepted: the change is an improvement";
            else if (llr <= lowerBound) sprtVerdict = "H0 accepted: the change is not an improvement";
        }
    }
    auto t1 = std::chrono::steady_clock::now();

    int games = wins + draws + losses;
    if (games == 0) { std::printf("no games played\n"); return 1; }
    double score = (wins + 0.5 * draws) / games;

    // Standard error over per-*pair* results, for the same reason the LLR is
    // computed over pairs: the two games of a pair share an opening, so the
    // per-game standard error counts that shared noise twice and reports a
    // confidence interval wider than the evidence actually is.
    int completedPairs = 0;
    for (int i = 0; i < 5; ++i) completedPairs += pentanomial[i];
    double pairMean = 0.0, pairSecond = 0.0;
    for (int i = 0; i < 5; ++i) {
        double x = i / 4.0;
        double p = completedPairs ? pentanomial[i] / (double)completedPairs : 0.0;
        pairMean   += p * x;
        pairSecond += p * x * x;
    }
    double var = pairSecond - pairMean * pairMean;
    double se = completedPairs ? std::sqrt(var / completedPairs) : 0.0;
    auto toElo = [](double s) {
        if (s <= 0.0) return -9999.0;
        if (s >= 1.0) return 9999.0;
        return -400.0 * std::log10(1.0 / s - 1.0);
    };
    double lo = toElo(std::max(0.0, score - 1.96 * se));
    double hi = toElo(std::min(1.0, score + 1.96 * se));

    std::printf("\n=== result (%s relative to %s) ===\n", A.name, B.name);
    std::printf("games   : %d  (W %d / D %d / L %d)\n", games, wins, draws, losses);
    std::printf("pairs   : %d  (0-0.5-1-1.5-2: %d-%d-%d-%d-%d)\n", completedPairs,
                pentanomial[0], pentanomial[1], pentanomial[2],
                pentanomial[3], pentanomial[4]);
    std::printf("score   : %.1f%%\n", 100.0 * score);
    // Every pair having scored identically leaves no variance to estimate from,
    // and the interval collapses to a point — which would read as certainty
    // when it is the opposite. Say so instead.
    if (var <= 0.0) {
        std::printf("Elo     : %+.0f   95%% CI unavailable: every pair scored the same,\n"
                    "          so this sample carries no variance to estimate from\n",
                    toElo(score));
    } else {
        std::printf("Elo     : %+.0f   95%% CI [%+.0f, %+.0f]  (pentanomial)\n",
                    toElo(score), lo, hi);
    }
    std::printf("length  : %.0f plies average, %d of %d games adjudicated\n",
                games ? (double)totalPlies / games : 0.0, adjudicated, games);
    std::printf("wall    : %.1f s\n", std::chrono::duration<double>(t1 - t0).count());

    if (useSprt) {
        double llr = computeLLR(pentanomial, elo0, elo1);
        std::printf("LLR     : %+.2f  (bounds %.2f / %.2f)\n", llr, lowerBound, upperBound);
        if (sprtVerdict) {
            std::printf("SPRT    : %s\n", sprtVerdict);
        } else {
            std::printf("SPRT    : inconclusive within %d pairs - the test ran out of\n"
                        "          games before the evidence settled. Raise -n.\n", pairs);
        }
    } else if (lo < 0.0 && hi > 0.0) {
        std::printf("\nThe confidence interval spans zero: this match size does NOT\n"
                    "demonstrate a difference. Increase the number of game pairs,\n"
                    "or use --sprt to stop as soon as the result is decided.\n");
    }
    return 0;
}
