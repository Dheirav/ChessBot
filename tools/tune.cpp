// Texel tuning: fit the evaluation's scalar weights to real game results.
//
//   make tune && ./tools/tune [corpus.epd]
//
// The objective is the standard one. Map each static evaluation to a win
// probability with a logistic curve, and pick the weights that minimise the
// mean squared error against what actually happened in the game:
//
//     E = (1/N) * sum( result - sigma(eval) )^2,  sigma(s) = 1/(1 + 10^(-K*s/400))
//
// K is fitted first and then held fixed. It is not a weight -- it is the scale
// that converts this evaluation's centipawns into the probabilities the corpus
// is labelled with, and letting it move during the weight search would let the
// tuner reduce E by rescaling the curve instead of improving the evaluation.
//
// Three things this file has to get right, each of which would silently
// produce a wrong answer rather than an error:
//
// 1. **It calls evaluate_details(), never evaluate().** evaluate() is a cache
//    keyed on the Zobrist hash (CLAUDE.md, and evaluation.cpp's cache block).
//    Weights change between passes while hashes do not, so a cached score is
//    an answer to the previous question. The symptom would be a tune that
//    reports improvement and changes nothing.
//
// 2. **It is built with -DEVAL_TUNING**, which turns the EvalWeights constants
//    into mutable globals. Without it this file would not link, which is the
//    intended failure.
//
// 3. **Positions are loaded with setFromFEN**, not parseFEN, so the Zobrist
//    hash is correct. parseFEN leaves it stale and every position collides on
//    the default hash -- the classic symptom is an evaluation of exactly 0
//    everywhere, which reads as a broken evaluation rather than a broken
//    loader.
//
// The search is coordinate descent with a shrinking step: try each weight up
// and down, keep any change that lowers E, and halve the step when a whole
// pass finds nothing. It is slow and dumb and cannot get stuck oscillating,
// which is what is wanted for eighteen parameters over fifteen thousand
// positions -- a full pass costs milliseconds.

#include "engine/board.hpp"
#include "engine/evaluation.hpp"
#include "engine/move_lookup.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

// Provided by evaluation.cpp when compiled with -DEVAL_TUNING.
namespace EvalWeights {
extern int DOUBLED_PAWN, ISOLATED_PAWN, BACKWARD_PAWN, CONNECTED_PAWN;
extern int PASSED_PAWN, PAWN_CHAIN;
extern int BISHOP_PAIR, MOBILITY, ROOK_OPEN_FILE, ROOK_SEMI_OPEN, ROOK_ON_7TH;
extern int OUTPOST, TRAPPED_PIECE, UNDEFENDED;
extern int CENTRE_CONTROL, KING_CENTRE_DIST, KING_PAWN_SHIELD, KING_ACTIVITY;
}

struct Knob {
    const char* name;
    int* value;
    int original;
};

static std::vector<Knob> knobs() {
    using namespace EvalWeights;
    std::vector<Knob> k = {
        {"DOUBLED_PAWN", &DOUBLED_PAWN, 0},
        {"ISOLATED_PAWN", &ISOLATED_PAWN, 0},
        {"BACKWARD_PAWN", &BACKWARD_PAWN, 0},
        {"CONNECTED_PAWN", &CONNECTED_PAWN, 0},
        {"PASSED_PAWN", &PASSED_PAWN, 0},
        {"PAWN_CHAIN", &PAWN_CHAIN, 0},
        {"BISHOP_PAIR", &BISHOP_PAIR, 0},
        {"MOBILITY", &MOBILITY, 0},
        {"ROOK_OPEN_FILE", &ROOK_OPEN_FILE, 0},
        {"ROOK_SEMI_OPEN", &ROOK_SEMI_OPEN, 0},
        {"ROOK_ON_7TH", &ROOK_ON_7TH, 0},
        {"OUTPOST", &OUTPOST, 0},
        {"TRAPPED_PIECE", &TRAPPED_PIECE, 0},
        {"UNDEFENDED", &UNDEFENDED, 0},
        {"CENTRE_CONTROL", &CENTRE_CONTROL, 0},
        {"KING_CENTRE_DIST", &KING_CENTRE_DIST, 0},
        {"KING_PAWN_SHIELD", &KING_PAWN_SHIELD, 0},
        {"KING_ACTIVITY", &KING_ACTIVITY, 0},
    };
    for (Knob& x : k) x.original = *x.value;
    return k;
}

struct Sample {
    Board board;
    double result;   // 1.0, 0.5, 0.0 -- White's point of view
};

static bool loadCorpus(const char* path, std::vector<Sample>& out) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        const size_t tag = line.find(" c9 \"");
        if (tag == std::string::npos) continue;
        const std::string fen = line.substr(0, tag);
        const double result = std::atof(line.c_str() + tag + 5);
        Sample s;
        // setFromFEN, not parseFEN -- see the header comment.
        if (!s.board.setFromFEN(fen)) continue;
        s.result = result;
        out.push_back(std::move(s));
    }
    return true;
}

static double meanSquaredError(const std::vector<Sample>& data, double k) {
    double total = 0.0;
    for (const Sample& s : data) {
        // evaluate_details, never evaluate -- see the header comment.
        const double score = (double)evaluate_details(s.board).total;
        const double sigma = 1.0 / (1.0 + std::pow(10.0, -k * score / 400.0));
        const double diff = s.result - sigma;
        total += diff * diff;
    }
    return total / (double)data.size();
}

// Scan for the K that best fits the *current* weights, then hold it.
static double fitK(const std::vector<Sample>& data) {
    double best = 1.0, bestE = 1e9;
    for (double k = 0.20; k <= 3.01; k += 0.05) {
        const double e = meanSquaredError(data, k);
        if (e < bestE) { bestE = e; best = k; }
    }
    // Refine around the winner.
    for (double k = best - 0.05; k <= best + 0.05; k += 0.005) {
        if (k <= 0) continue;
        const double e = meanSquaredError(data, k);
        if (e < bestE) { bestE = e; best = k; }
    }
    return best;
}

int main(int argc, char** argv) {
    const char* path = (argc > 1) ? argv[1] : "tests/data/texel.epd";
    // A held-out set, built by tools/texel-corpus.py --split, divided by game.
    //
    // This is the only thing that distinguishes a tune from a memorisation.
    // Training error always falls -- coordinate descent guarantees it, since a
    // change is kept only when it falls. The question is whether the fit
    // transfers to games the tuner never saw, and with 267 games carrying one
    // label each it is entirely possible that it does not.
    const char* testPath = (argc > 2) ? argv[2] : nullptr;
    initMoveLookupTables();

    std::vector<Sample> data;
    if (!loadCorpus(path, data)) { std::fprintf(stderr, "cannot read %s\n", path); return 1; }
    if (data.empty()) { std::fprintf(stderr, "%s held no usable positions\n", path); return 1; }
    std::printf("corpus: %zu positions from %s\n", data.size(), path);

    std::vector<Sample> held;
    if (testPath) {
        if (!loadCorpus(testPath, held)) { std::fprintf(stderr, "cannot read %s\n", testPath); return 1; }
        std::printf("held out: %zu positions from %s\n", held.size(), testPath);
    }

    std::vector<Knob> k = knobs();

    const double K = fitK(data);
    const double startE = meanSquaredError(data, K);
    // K is fitted on the training set and reused on the held-out set. Refitting
    // it there would be scoring against a curve tuned to the answers.
    const double startHeld = held.empty() ? 0.0 : meanSquaredError(held, K);
    std::printf("K = %.3f   starting E = %.6f", K, startE);
    if (!held.empty()) std::printf("   held-out %.6f", startHeld);
    std::printf("\n\n");

    double bestE = startE;
    int step = 8;
    long evals = 0;
    while (step >= 1) {
        bool improvedAnything = false;
        for (Knob& x : k) {
            for (int dir : {+1, -1}) {
                const int saved = *x.value;
                *x.value = saved + dir * step;
                const double e = meanSquaredError(data, K);
                ++evals;
                if (e < bestE - 1e-12) {
                    bestE = e;
                    improvedAnything = true;
                    std::printf("  %-18s %4d -> %4d   E = %.6f\n", x.name, saved, *x.value, e);
                } else {
                    *x.value = saved;   // reject
                }
            }
        }
        if (!improvedAnything) { step /= 2; if (step) std::printf("-- step %d\n", step); }
    }

    std::printf("\nE: %.6f -> %.6f  (%.3f%% lower, %ld corpus passes)\n",
                startE, bestE, 100.0 * (startE - bestE) / startE, evals);
    if (!held.empty()) {
        const double endHeld = meanSquaredError(held, K);
        const double trainGain = 100.0 * (startE - bestE) / startE;
        const double heldGain = 100.0 * (startHeld - endHeld) / startHeld;
        std::printf("held-out: %.6f -> %.6f  (%+.3f%%)\n", startHeld, endHeld, heldGain);
        std::printf("\n  training improved %.2f%%, unseen games %+.2f%%.\n", trainGain, heldGain);
        if (heldGain <= 0.0)
            std::printf("  ** The fit does not transfer. This is memorisation, not tuning. **\n");
        else if (heldGain < trainGain * 0.5)
            std::printf("  ** Less than half the gain transfers -- treat every weight below\n"
                        "     as suspect, especially any that changed sign. **\n");
    }
    std::printf("\n%-18s %8s %8s %8s\n", "weight", "before", "after", "delta");
    bool any = false;
    for (const Knob& x : k) {
        if (*x.value == x.original) continue;
        any = true;
        std::printf("%-18s %8d %8d %+8d\n", x.name, x.original, *x.value, *x.value - x.original);
    }
    if (!any) std::printf("(nothing moved)\n");
    std::printf("\nWrite the winners into EvalWeights by hand, rebuild, then check\n"
                "tests/evalerror (which is scored against Stockfish and is not part of\n"
                "this objective) before gating. A tune that improves E and worsens\n"
                "evalerror has fitted this archive, not chess.\n");
    return 0;
}
