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
// Build and run:  make test-match
//   ./tests/match [gamePairs] [depth] [seed]        (positional, back-compatible)
//   ./tests/match -n 100 -t 1000 --sprt             (time-equalized, sequential)
//
// Options:
//   -n <pairs>      game pairs to play; each pair is two games (default 25)
//   -s <seed>       opening-line seed (default 20260810)
//   -d <depth>      fixed depth for both sides (default 4)
//   -t <ms>         per-move time budget for both sides; 0 = depth only
//   --da/--db <d>   per-side depth, for comparing two depth settings
//   --ta/--tb <ms>  per-side time budget
//   --ha/--hb on|off  all three search heuristics at once (default A on, B off)
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
};

enum Result { A_WINS, DRAW, B_WINS };

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
static Result playGame(const std::vector<Move>& opening, bool aPlaysWhite,
                       const EngineConfig& A, const EngineConfig& B,
                       TranspositionTable& ttA, TranspositionTable& ttB,
                       int maxPlies) {
    Board board;
    board.setFromFEN(Board::INITIAL_FEN);
    for (const Move& m : opening) board.makeMove(m);

    ttA.clear();
    ttB.clear();
    std::atomic<bool> stop{false};
    std::unordered_map<uint64_t, int> seen;
    seen[board.getHash()] = 1;

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
            if (!inCheck) return DRAW; // stalemate
            // The side to move is mated, so the other side won.
            bool whiteWon = !whiteToMove;
            return (whiteWon == aPlaysWhite) ? A_WINS : B_WINS;
        }
        if (board.halfmoveClock >= 100) return DRAW;
        if (insufficientMaterial(board)) return DRAW;

        bool whiteToMove = (board.activeColor == COLOR_WHITE);
        bool aToMove = (whiteToMove == aPlaysWhite);
        const EngineConfig& cfg = aToMove ? A : B;
        TranspositionTable& tt = aToMove ? ttA : ttB;

        g_searchOptions = cfg.opts;
        g_searchOptions.quiet = true;
        Move best = findBestMoveIterativeDeepening(board, cfg.limits, stop, tt);

        // Defensive: a search that returns nothing usable would otherwise
        // corrupt the game; fall back to the first legal move.
        bool ok = false;
        for (const Move& m : legal) if (m == best) { ok = true; break; }
        if (!ok) best = legal[0];

        board.makeMove(best);
        if (++seen[board.getHash()] >= 3) return DRAW; // threefold repetition
    }
    return DRAW; // adjudicated as a draw at the ply limit
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

// Generalized SPRT under a normal approximation to the per-game score.
//
// The only degenerate case is zero variance — every game so far having had the
// identical result — and the variance test below catches it directly. An
// earlier version also bailed whenever any one of wins/draws/losses was still
// zero, which was wrong in exactly the case this test is most useful for: an
// engine that never loses keeps losses at 0 indefinitely, so the LLR would sit
// at 0.00 forever and a decisive match would never stop early. A sample like
// 3 wins / 1 draw / 0 losses has perfectly good variance and should be scored.
static double computeLLR(int wins, int draws, int losses, double elo0, double elo1) {
    double n = wins + draws + losses;
    if (n <= 0) return 0.0;
    double w = wins / n, d = draws / n;
    double score = w + d / 2.0;
    // Second moment of the per-game score: wins contribute 1, draws 0.25.
    double variance = (w + d / 4.0) - score * score;
    if (variance <= 0.0) return 0.0;

    double s0 = eloToScore(elo0), s1 = eloToScore(elo1);
    return n * (s1 - s0) * (2.0 * score - s0 - s1) / (2.0 * variance);
}

int main(int argc, char** argv) {
    initMoveLookupTables();

    int pairs = 25, depth = 4;
    long timeMs = 0;
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
    } else {
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            auto next = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : "0"; };
            if      (a == "-n")      pairs = std::atoi(next());
            else if (a == "-s")      seed = (unsigned)std::atoi(next());
            else if (a == "-d")    { depth = std::atoi(next()); depthGiven = true; }
            else if (a == "-t")      timeMs = std::atol(next());
            else if (a == "--da")    depthA = std::atoi(next());
            else if (a == "--db")    depthB = std::atoi(next());
            else if (a == "--ta")    timeA = std::atol(next());
            else if (a == "--tb")    timeB = std::atol(next());
            else if (a == "--ha")    heurA = onOff(next());
            else if (a == "--hb")    heurB = onOff(next());
            else if (a == "--optA")  optsA = splitOpts(next());
            else if (a == "--optB")  optsB = splitOpts(next());
            else if (a == "--sprt")  useSprt = true;
            else if (a == "--elo0")  elo0 = std::atof(next());
            else if (a == "--elo1")  elo1 = std::atof(next());
            else {
                std::printf("unknown option: %s\n", a.c_str());
                return 1;
            }
        }
    }

    // A depth ceiling is always set. Under a time budget it is a safety limit
    // rather than the stopping condition, so it sits high enough not to bind.
    long budgetA = (timeA >= 0) ? timeA : timeMs;
    long budgetB = (timeB >= 0) ? timeB : timeMs;
    // Under a time budget, an unspecified depth means "as deep as the clock
    // allows" rather than the fixed-depth default, which would silently cap the
    // faster engine and defeat the point of equalizing on time.
    auto ceilingFor = [&](int sideDepth, long budget) {
        if (sideDepth > 0) return sideDepth;
        if (budget > 0 && !depthGiven) return 64;
        return depth;
    };
    int ceilA = ceilingFor(depthA, budgetA);
    int ceilB = ceilingFor(depthB, budgetB);

    EngineConfig A{"A", SearchOptions{}, SearchLimits(ceilA, budgetA)};
    A.opts.nullMove = heurA; A.opts.lmr = heurA; A.opts.aspiration = heurA;

    EngineConfig B{"B", SearchOptions{}, SearchLimits(ceilB, budgetB)};
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
    if (budgetA > 0) nameA += " @" + std::to_string(budgetA) + "ms";
    else             nameA += " @d" + std::to_string(ceilA);
    if (budgetB > 0) nameB += " @" + std::to_string(budgetB) + "ms";
    else             nameB += " @d" + std::to_string(ceilB);
    A.name = nameA.c_str();
    B.name = nameB.c_str();

    std::printf("%s  vs  %s\n", A.name, B.name);

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

    int wins = 0, draws = 0, losses = 0;
    const double lowerBound = std::log(BETA / (1.0 - ALPHA));
    const double upperBound = std::log((1.0 - BETA) / ALPHA);
    const char* sprtVerdict = nullptr;

    auto t0 = std::chrono::steady_clock::now();
    for (int p = 0; p < pairs && !sprtVerdict; ++p) {
        std::vector<Move> opening = makeOpening(rng, OPENING_PLIES);
        for (int side = 0; side < 2; ++side) {
            bool aWhite = (side == 0);
            Result r = playGame(opening, aWhite, A, B, ttA, ttB, MAX_PLIES);

            if (r == A_WINS) ++wins; else if (r == B_WINS) ++losses; else ++draws;
            std::printf("  pair %3d %s: %-7s  (W-D-L %d-%d-%d",
                        p + 1, aWhite ? "A=white" : "A=black",
                        r == A_WINS ? "A wins" : (r == B_WINS ? "B wins" : "draw"),
                        wins, draws, losses);
            if (useSprt) {
                double llr = computeLLR(wins, draws, losses, elo0, elo1);
                std::printf(" | LLR %+.2f [%.2f,%.2f]", llr, lowerBound, upperBound);
                if (llr >= upperBound)      sprtVerdict = "H1 accepted: the change is an improvement";
                else if (llr <= lowerBound) sprtVerdict = "H0 accepted: the change is not an improvement";
            }
            std::printf(")\n");
            std::fflush(stdout);
            if (sprtVerdict) break;
        }
    }
    auto t1 = std::chrono::steady_clock::now();

    int games = wins + draws + losses;
    if (games == 0) { std::printf("no games played\n"); return 1; }
    double score = (wins + 0.5 * draws) / games;

    // Standard error over per-game results (win=1, draw=0.5, loss=0).
    double var = (wins * std::pow(1.0 - score, 2)
                + draws * std::pow(0.5 - score, 2)
                + losses * std::pow(0.0 - score, 2)) / games;
    double se = std::sqrt(var / games);
    auto toElo = [](double s) {
        if (s <= 0.0) return -9999.0;
        if (s >= 1.0) return 9999.0;
        return -400.0 * std::log10(1.0 / s - 1.0);
    };
    double lo = toElo(std::max(0.0, score - 1.96 * se));
    double hi = toElo(std::min(1.0, score + 1.96 * se));

    std::printf("\n=== result (%s relative to %s) ===\n", A.name, B.name);
    std::printf("games   : %d  (W %d / D %d / L %d)\n", games, wins, draws, losses);
    std::printf("score   : %.1f%%\n", 100.0 * score);
    std::printf("Elo     : %+.0f   95%% CI [%+.0f, %+.0f]\n", toElo(score), lo, hi);
    std::printf("wall    : %.1f s\n", std::chrono::duration<double>(t1 - t0).count());

    if (useSprt) {
        double llr = computeLLR(wins, draws, losses, elo0, elo1);
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
