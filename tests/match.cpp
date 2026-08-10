// Engine-vs-engine match harness.
//
// The search heuristics (null-move pruning, late move reductions, aspiration
// windows) deliberately return different results from a plain alpha-beta
// search, so unlike a movegen or evaluation change they cannot be validated by
// comparing output against a reference. The only way to know whether they help
// is to play games and measure the score.
//
// Two SearchOptions configurations play a match under identical conditions:
// same fixed depth, same openings, and every opening played twice with colours
// swapped so neither side benefits from a favourable start.
//
// Build and run:  make test-match
//   ./tests/match [gamePairs] [depth] [seed]
//
// Note on interpreting the result: a short match measures very little. The
// reported 95% confidence interval is the honest read - if it spans zero, the
// match has not demonstrated a difference in either direction.
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
static Result playGame(const std::vector<Move>& opening, bool aPlaysWhite, int depth,
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
        Move best = findBestMoveIterativeDeepening(board, depth, stop, tt);

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

int main(int argc, char** argv) {
    initMoveLookupTables();

    int pairs = (argc > 1) ? std::atoi(argv[1]) : 25;
    int depth = (argc > 2) ? std::atoi(argv[2]) : 4;
    unsigned seed = (argc > 3) ? (unsigned)std::atoi(argv[3]) : 20260810u;
    const int MAX_PLIES = 300;
    const int OPENING_PLIES = 6;

    EngineConfig A{"heuristics-on", SearchOptions{}};
    A.opts.nullMove = true;  A.opts.lmr = true;  A.opts.aspiration = true;

    EngineConfig B{"baseline-off", SearchOptions{}};
    B.opts.nullMove = false; B.opts.lmr = false; B.opts.aspiration = false;

    std::printf("%s vs %s | %d game pairs (%d games) | depth %d | seed %u\n",
                A.name, B.name, pairs, pairs * 2, depth, seed);
    std::printf("Each opening is played twice with colours swapped.\n\n");

    TranspositionTable ttA(32), ttB(32);
    std::mt19937 rng(seed);

    int wins = 0, draws = 0, losses = 0;
    double aTimeMs = 0, bTimeMs = 0;

    auto t0 = std::chrono::steady_clock::now();
    for (int p = 0; p < pairs; ++p) {
        std::vector<Move> opening = makeOpening(rng, OPENING_PLIES);
        for (int side = 0; side < 2; ++side) {
            bool aWhite = (side == 0);
            auto g0 = std::chrono::steady_clock::now();
            Result r = playGame(opening, aWhite, depth, A, B, ttA, ttB, MAX_PLIES);
            auto g1 = std::chrono::steady_clock::now();
            double ms = std::chrono::duration<double, std::milli>(g1 - g0).count();
            (aWhite ? aTimeMs : bTimeMs) += ms;

            if (r == A_WINS) ++wins; else if (r == B_WINS) ++losses; else ++draws;
            std::printf("  pair %2d %s: %s   (running W-D-L %d-%d-%d)\n",
                        p + 1, aWhite ? "A=white" : "A=black",
                        r == A_WINS ? "A wins" : (r == B_WINS ? "B wins" : "draw"),
                        wins, draws, losses);
            std::fflush(stdout);
        }
    }
    auto t1 = std::chrono::steady_clock::now();

    int games = wins + draws + losses;
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
    if (lo < 0.0 && hi > 0.0) {
        std::printf("\nThe confidence interval spans zero: this match size does NOT\n"
                    "demonstrate a difference. Increase the number of game pairs.\n");
    }
    return 0;
}
