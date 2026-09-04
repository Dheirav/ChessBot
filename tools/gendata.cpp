// Self-play corpus generation for NNUE training.
//
//   tools/gendata --out data/shard0.txt --games 2000 --nodes 5000 --seed 1
//
// Writes one line per retained position:  <fen>;<score_cp_white>;<result_white>
// score is the search's own verdict in centipawns from White's point of view;
// result is 1.0 / 0.5 / 0.0, the outcome of the game the position came from.
//
// Why the label is the *search* score and not evaluate()
// ------------------------------------------------------
// This is the whole reason a 2150 engine can generate data that beats it. The
// static evaluation is what we are trying to replace, so training a net to
// reproduce it would at best reproduce its blindness. A few thousand nodes of
// alpha-beta is strictly better informed than the evaluation it calls, and the
// gap between them is exactly the knowledge `MEASUREMENTS.md` says is missing:
// `material` leads half of every criticised move at a p90 of 535cp, and
// `ROADMAP.md` puts ~290cp of static error as addressable.
//
// Both labels are written because they are different signals and the trainer
// should get to weigh them. The score is dense and slightly wrong everywhere;
// the result is sparse -- one bit spread over a whole game -- and right. Texel
// tuning here failed partly for using results alone: 267 games carried 267
// independent labels and the fit memorised them (`tools/sf-label.py`).
//
// The filters, each of which throws away real data on purpose
// -----------------------------------------------------------
// Not in check, and the move actually chosen was not a capture or a promotion.
// A static evaluation cannot be asked to score a position whose value is
// decided by a tactic; scoring it against one measures search depth wearing an
// evaluation's name. Same reasoning as `tools/texel-corpus.py`, which says it
// at more length. Opening plies are skipped because they are shared across
// games and carry almost nothing, and mate scores are skipped because a
// centipawn label for "mate in 4" is a fiction.
//
// Determinism and resumption
// --------------------------
// A node budget is spent identically whatever else is running, so a shard is
// reproducible from its seed and this can be niced beside a live bot. Progress
// goes to stderr, data to stdout or --out, so a shard can be inspected while it
// runs -- 33 hours of generation will be interrupted, and `--append` continues
// a shard rather than restarting it.
#include "engine/board.hpp"
#include "engine/movegen.hpp"
#include "engine/move_lookup.hpp"
#include "engine/search.hpp"
#include "engine/transposition_table.hpp"
#include "engine/legal_move_validator.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

constexpr int OPENING_PLIES   = 8;
constexpr int MAX_PLIES       = 400;
constexpr int MATE_LABEL_CUT  = SEARCH_MATE_SCORE - 1000;  // above this, not a centipawn value
// Adjudication threshold, and it is a *data quality* setting rather than a
// speed one. The first smoke test produced a game the engine scored at +1355
// for forty plies and then drew by shuffling: at a few thousand nodes it knows
// it is a queen up and cannot find the mate. Playing those out does not just
// waste nodes, it writes a 0.5 result label onto a hundred won positions, which
// is worse than not generating them. A queen is ~900, so a sustained 1000
// across eight plies is the engine being certain rather than optimistic.
constexpr int ADJUDICATE_CP   = 1000;
constexpr int ADJUDICATE_PLIES = 8;

int  g_lastScore = 0;
bool g_haveScore = false;
void captureScore(int, int score, uint64_t, long, const Move&) {
    g_lastScore = score;
    g_haveScore = true;
}

struct Sample { std::string fen; int scoreWhite; };

bool insufficientMaterial(const Board& b) {
    int minors = 0;
    for (int i = 0; i < 64; ++i) {
        PieceType t = b.squares[i].type();
        if (t == NONE || t == KING) continue;
        if (t == PAWN || t == ROOK || t == QUEEN) return false;
        if (++minors > 1) return false;
    }
    return true;
}

const char* argOf(int argc, char** argv, const char* flag, const char* dflt) {
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], flag) == 0) return argv[i + 1];
    return dflt;
}
bool hasFlag(int argc, char** argv, const char* flag) {
    for (int i = 1; i < argc; ++i) if (std::strcmp(argv[i], flag) == 0) return true;
    return false;
}

} // namespace

int main(int argc, char** argv) {
    const std::string out = argOf(argc, argv, "--out", "");
    const long  games     = std::atol(argOf(argc, argv, "--games", "1000"));
    const uint64_t nodes  = std::strtoull(argOf(argc, argv, "--nodes", "5000"), nullptr, 10);
    const unsigned seed   = (unsigned)std::atol(argOf(argc, argv, "--seed", "1"));
    const bool append     = hasFlag(argc, argv, "--append");

    if (games <= 0 || nodes == 0) {
        std::fprintf(stderr, "usage: gendata --out <file> [--games N] [--nodes N] "
                             "[--seed N] [--append]\n");
        return 1;
    }

    FILE* fh = stdout;
    if (!out.empty()) {
        fh = std::fopen(out.c_str(), append ? "a" : "w");
        if (!fh) { std::fprintf(stderr, "cannot open %s\n", out.c_str()); return 1; }
    }

    initMoveLookupTables();
    g_searchOptions.quiet = true;
    g_searchInfo = captureScore;

    std::mt19937 rng(seed);
    TranspositionTable tt(64);
    std::atomic<bool> stop{false};

    SearchLimits limits;
    limits.maxNodes = nodes;
    limits.maxDepth = 64;

    long   written = 0, played = 0;
    const auto t0 = std::chrono::steady_clock::now();

    for (long g = 0; g < games; ++g) {
        Board board;
        board.setFromFEN(Board::INITIAL_FEN);   // never parseFEN: it leaves the hash stale
        std::vector<uint64_t> history;

        // Random opening, for variety and to decorrelate the shards.
        bool aborted = false;
        for (int i = 0; i < OPENING_PLIES; ++i) {
            MoveList legal = generateLegalMoves(board, board.activeColor);
            if (legal.empty()) { aborted = true; break; }
            std::uniform_int_distribution<size_t> pick(0, legal.size() - 1);
            const Move m = legal[pick(rng)];
            const uint64_t before = board.getHash();
            board.makeMove(m);
            recordGamePosition(history, before, board);
        }
        if (aborted) continue;

        tt.clear();
        clearCorrectionHistory();

        std::vector<Sample> pending;
        std::unordered_map<uint64_t, int> seen;
        // Positions already written *from this game*. A won endgame the engine
        // cannot finish shuffles through the same handful of positions dozens
        // of times, and writing each one repeatedly would weight it by how long
        // the engine dithered rather than by anything about chess. Per game
        // rather than global: a set over 100M positions does not fit, and the
        // repetition this is for is within a game.
        std::unordered_map<uint64_t, bool> emitted;
        double result = 0.5;
        int decisivePlies = 0;

        for (int ply = 0; ply < MAX_PLIES; ++ply) {
            const PieceColor side = board.activeColor;
            MoveList legal = generateLegalMoves(board, side);
            const bool inCheck = LegalMoveValidator::isInCheck(board, side);

            if (legal.empty()) {                       // mate or stalemate
                if (inCheck) result = (side == COLOR_WHITE) ? 0.0 : 1.0;
                break;
            }
            if (board.halfmoveClock >= 100 || insufficientMaterial(board)) break;
            if (++seen[board.getHash()] >= 3) break;   // threefold

            g_haveScore = false;
            const Move best = findBestMoveIterativeDeepening(board, limits, stop, tt, history);
            if (best.from < 0) break;

            // Keep the position only if it is quiet and its score is a real
            // centipawn value. The move about to be played is the evidence for
            // "quiet": a position whose best move is a capture is one whose
            // value a static evaluation cannot be asked for.
            if (g_haveScore && !inCheck &&
                best.capturedPiece.type() == NONE && best.flag != PROMOTION &&
                std::abs(g_lastScore) < MATE_LABEL_CUT) {
                const int white = (side == COLOR_WHITE) ? g_lastScore : -g_lastScore;
                if (!emitted[board.getHash()]) {
                    emitted[board.getHash()] = true;
                    pending.push_back({board.getFEN(), white});
                }
            }

            // Adjudicate a settled game rather than playing it out: those plies
            // cost the same nodes and the positions are ones no net needs.
            if (g_haveScore && std::abs(g_lastScore) >= ADJUDICATE_CP) {
                if (++decisivePlies >= ADJUDICATE_PLIES) {
                    const bool whiteWinning = (side == COLOR_WHITE) == (g_lastScore > 0);
                    result = whiteWinning ? 1.0 : 0.0;
                    break;
                }
            } else {
                decisivePlies = 0;
            }

            const uint64_t before = board.getHash();
            board.makeMove(best);
            recordGamePosition(history, before, board);
        }

        for (const Sample& s : pending) {
            std::fprintf(fh, "%s;%d;%.1f\n", s.fen.c_str(), s.scoreWhite, result);
            ++written;
        }
        ++played;

        if (played % 25 == 0) {
            std::fflush(fh);
            const double el = std::chrono::duration<double>(
                                  std::chrono::steady_clock::now() - t0).count();
            const double rate = written / (el > 0 ? el : 1);
            const double eta  = (games - played) * (el / played);
            std::fprintf(stderr,
                "\r  games %ld/%ld  positions %ld  %.0f pos/s  elapsed %.0fm  ETA %.0fm   ",
                played, games, written, rate, el / 60, eta / 60);
            std::fflush(stderr);
        }
    }

    std::fflush(fh);
    if (fh != stdout) std::fclose(fh);
    const double el = std::chrono::duration<double>(
                          std::chrono::steady_clock::now() - t0).count();
    std::fprintf(stderr, "\n%ld games -> %ld positions in %.1f min (%.0f pos/s)\n",
                 played, written, el / 60, written / (el > 0 ? el : 1));
    return 0;
}
