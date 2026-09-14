// Fit the king-danger parameters against the evaluation-error corpus.
//
//   make kstune && ./tools/kstune [tests/data/evalerr.epd]
//
// King safety has been built four times here and rejected four times, and the
// last write-up (docs/KING-SAFETY.md) ended on the real blocker: seven
// interacting weights, all guessed. Every guess that helped `comp`, the
// positions where material is wrong, also wrecked `ctl`, the ordinary ones.
// That is not a reason the term cannot work. It is a reason it cannot be
// tuned by hand.
//
// So this tunes it by the two-tag objective directly. It searches the
// parameters by coordinate descent, and a step is accepted only if it lowers
// the mean error on `comp` while keeping `ctl` within a small tolerance of
// where it started. The constraint is the whole point: a term that moves the
// error rather than removing it fails the step.
//
// Built with -DEVAL_TUNING, which makes the parameters mutable globals.
// Calls evaluate_details(), never evaluate(), because the latter is cached on
// the position's hash and would answer the previous parameter set.
//
#include "engine/board.hpp"
#include "engine/evaluation.hpp"
#include "engine/move_lookup.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

extern int KING_DANGER_WEIGHT[7];
extern int KING_DANGER_SCALE, KING_DANGER_MIN_ATTACKERS, KING_DANGER_OFFSET,
           KING_DANGER_DEFENDER_W, KING_DANGER_WEAK_W, KING_DANGER_CHECK_W,
           KING_DANGER_NO_QUEEN_CUT;

namespace {

struct Row { Board board; int label; bool comp; };

std::vector<Row> load(const std::string& path) {
    std::vector<Row> rows;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> parts;
        std::stringstream ss(line);
        std::string p;
        while (std::getline(ss, p, ';')) parts.push_back(p);
        if (parts.size() < 3) continue;
        auto trim = [](std::string s) {
            const size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
            return (a == std::string::npos) ? std::string() : s.substr(a, b - a + 1);
        };
        Row r;
        if (!r.board.setFromFEN(trim(parts[0]))) continue;
        r.label = 0; r.comp = false;
        for (size_t i = 1; i < parts.size(); ++i) {
            const std::string t = trim(parts[i]);
            if (t.rfind("sf ", 0) == 0) r.label = std::atoi(t.c_str() + 3);
            else if (t.rfind("tag ", 0) == 0) r.comp = (t.substr(4) == "comp");
        }
        rows.push_back(r);
    }
    return rows;
}

// ctl is reported whole and also over the "open" positions, the ones whose
// label is inside the +-1000 cap. 89 of the 688 ctl positions are decisive
// (Stockfish at the cap), and a term that pushes a won position further past
// +1000 improves the clamped error there while doing nothing for play. The
// constraint binds on the open ones, because those are the ordinary positions
// the control set exists to protect.
struct Score { double comp, ctl, ctlOpen; int flips; };

Score score(const std::vector<Row>& rows) {
    double sc = 0, sl = 0, so = 0; int nc = 0, nl = 0, no = 0, flips = 0;
    const bool clamp = !std::getenv("KSTUNE_NOCLAMP");
    for (const Row& r : rows) {
        // Same clamp the labels carry: Stockfish caps at +-1000, so an
        // evaluation past that is not "more wrong" for being further past.
        int ours = evaluate_details(r.board).total;
        if (clamp) ours = std::max(-1000, std::min(1000, ours));
        const double err = std::fabs((double)ours - (double)r.label);
        if (r.comp) {
            sc += err; ++nc;
            if ((ours >= 100 && r.label <= -100) || (ours <= -100 && r.label >= 100)) ++flips;
        } else {
            sl += err; ++nl;
            if (std::abs(r.label) < 1000) { so += err; ++no; }
        }
    }
    return { nc ? sc / nc : 0, nl ? sl / nl : 0, no ? so / no : 0, flips };
}

struct Param { const char* name; int* ptr; int lo, hi, step; };

} // namespace

int main(int argc, char** argv) {
    initMoveLookupTables();
    const std::string path = (argc > 1) ? argv[1] : "tests/data/evalerr.epd";
    const std::vector<Row> rows = load(path);
    if (rows.empty()) { std::printf("no positions in %s\n", path.c_str()); return 1; }

    Param params[] = {
        {"SCALE",         &KING_DANGER_SCALE,         0, 2000, 10},
        {"MIN_ATTACKERS", &KING_DANGER_MIN_ATTACKERS, 1,   4,  1},
        {"OFFSET",        &KING_DANGER_OFFSET,        0,  60,  2},
        {"DEFENDER_W",    &KING_DANGER_DEFENDER_W,    0,  12,  1},
        {"WEAK_W",        &KING_DANGER_WEAK_W,        0,  12,  1},
        {"CHECK_W",       &KING_DANGER_CHECK_W,       0,  12,  1},
        {"NO_QUEEN_CUT",  &KING_DANGER_NO_QUEEN_CUT,  0,  40,  2},
        {"W[PAWN]",       &KING_DANGER_WEIGHT[PAWN],   0,  8,  1},
        {"W[KNIGHT]",     &KING_DANGER_WEIGHT[KNIGHT], 0, 10,  1},
        {"W[BISHOP]",     &KING_DANGER_WEIGHT[BISHOP], 0, 10,  1},
        {"W[ROOK]",       &KING_DANGER_WEIGHT[ROOK],   0, 12,  1},
        {"W[QUEEN]",      &KING_DANGER_WEIGHT[QUEEN],  0, 16,  1},
    };
    const int NP = (int)(sizeof(params) / sizeof(params[0]));

    // The constraint. ctl may not end up more than this much worse than it
    // started; every previous attempt at this term would have failed it.
    // KSTUNE_CTL_TOL overrides it, so the trade can be traced at several
    // budgets; a negative value demands that ctl improve too.
    const double CTL_TOLERANCE = std::getenv("KSTUNE_CTL_TOL")
        ? std::atof(std::getenv("KSTUNE_CTL_TOL")) : 2.0;

    // KSTUNE_EVAL=1 scores one parameter set, read from the environment
    // (KD_SCALE, KD_MIN_ATTACKERS, KD_OFFSET, KD_DEFENDER_W, KD_WEAK_W,
    // KD_CHECK_W, KD_NO_QUEEN_CUT), and stops. For checking a recorded
    // configuration against the current corpus and clamp.
    if (std::getenv("KSTUNE_EVAL")) {
        auto env = [](const char* k, int* dst) { if (const char* v = std::getenv(k)) *dst = std::atoi(v); };
        env("KD_SCALE", &KING_DANGER_SCALE);
        env("KD_MIN_ATTACKERS", &KING_DANGER_MIN_ATTACKERS);
        env("KD_OFFSET", &KING_DANGER_OFFSET);
        env("KD_DEFENDER_W", &KING_DANGER_DEFENDER_W);
        env("KD_WEAK_W", &KING_DANGER_WEAK_W);
        env("KD_CHECK_W", &KING_DANGER_CHECK_W);
        env("KD_NO_QUEEN_CUT", &KING_DANGER_NO_QUEEN_CUT);
        const Score s = score(rows);
        std::printf("comp %.1f  ctl %.1f  ctl-open %.1f  flips %d\n", s.comp, s.ctl, s.ctlOpen, s.flips);
        return 0;
    }

    Score base = score(rows);
    const double ctlCeiling = base.ctlOpen + CTL_TOLERANCE;
    std::printf("%zu positions. baseline  comp %.1f  ctl %.1f  ctl-open %.1f  flips %d\n",
                rows.size(), base.comp, base.ctl, base.ctlOpen, base.flips);
    std::printf("constraint: ctl-open <= %.1f\n\n", ctlCeiling);

    // The term is off at SCALE 0 and nothing else matters until it is on. A
    // greedy descent from a gentle switch-on cannot reach a region the
    // constraint allows only at larger scales, and there is one: along SCALE
    // alone, ctl-open falls before it rises. So the start is the SCALE that
    // minimises ctl-open, found by a plain sweep, and the descent runs from
    // there. With a positive tolerance the sweep changes nothing; with a
    // negative one it is what makes the search feasible at all.
    Score cur = base;
    int bestScale = 0;
    for (int sc = 10; sc <= params[0].hi; sc += params[0].step) {
        KING_DANGER_SCALE = sc;
        const Score s = score(rows);
        if (s.ctlOpen < cur.ctlOpen - 0.05) { cur = s; bestScale = sc; }
    }
    KING_DANGER_SCALE = bestScale;
    cur = score(rows);
    if (cur.ctlOpen > ctlCeiling) {
        // Even the ctl-optimal scale breaks the ceiling: raise the attacker
        // threshold and try again before giving up.
        KING_DANGER_MIN_ATTACKERS = 3;
        cur = score(rows);
    }
    std::printf("start     comp %.1f  ctl %.1f  ctl-open %.1f  flips %d   (SCALE %d, MIN_ATTACKERS %d)\n\n",
                cur.comp, cur.ctl, cur.ctlOpen, cur.flips, KING_DANGER_SCALE, KING_DANGER_MIN_ATTACKERS);

    // Coordinate descent. A step must improve comp AND respect the ctl
    // ceiling. Repeated until a full pass over every parameter changes nothing.
    for (int pass = 1; pass <= 12; ++pass) {
        bool moved = false;
        for (int i = 0; i < NP; ++i) {
            Param& p = params[i];
            for (int dir : {+1, -1}) {
                while (true) {
                    const int old = *p.ptr;
                    const int next = old + dir * p.step;
                    if (next < p.lo || next > p.hi) break;
                    *p.ptr = next;
                    const Score s = score(rows);
                    if (s.comp < cur.comp - 0.05 && s.ctlOpen <= ctlCeiling) {
                        cur = s; moved = true;
                        std::printf("  pass %2d  %-14s %4d -> %4d   comp %.1f  ctl %.1f  ctl-open %.1f  flips %d\n",
                                    pass, p.name, old, next, s.comp, s.ctl, s.ctlOpen, s.flips);
                    } else { *p.ptr = old; break; }
                }
            }
        }
        if (!moved) { std::printf("\nconverged after pass %d\n", pass); break; }
    }

    std::printf("\nresult    comp %.1f  ctl %.1f  ctl-open %.1f  flips %d   (baseline comp %.1f  ctl %.1f  ctl-open %.1f  flips %d)\n",
                cur.comp, cur.ctl, cur.ctlOpen, cur.flips, base.comp, base.ctl, base.ctlOpen, base.flips);
    std::printf("\n  -DKING_DANGER_SCALE_PCT=%d -DKING_DANGER_MIN_ATTACKERS_N=%d "
                "-DKING_DANGER_OFFSET_N=%d -DKING_DANGER_DEFENDER_W_N=%d "
                "-DKING_DANGER_WEAK_W_N=%d -DKING_DANGER_CHECK_W_N=%d "
                "-DKING_DANGER_NO_QUEEN_CUT_N=%d\n",
                KING_DANGER_SCALE, KING_DANGER_MIN_ATTACKERS, KING_DANGER_OFFSET,
                KING_DANGER_DEFENDER_W, KING_DANGER_WEAK_W, KING_DANGER_CHECK_W,
                KING_DANGER_NO_QUEEN_CUT);
    std::printf("  KING_DANGER_WEIGHT = { 0, 0, %d, %d, %d, %d, %d }\n",
                KING_DANGER_WEIGHT[PAWN], KING_DANGER_WEIGHT[KNIGHT], KING_DANGER_WEIGHT[BISHOP],
                KING_DANGER_WEIGHT[ROOK], KING_DANGER_WEIGHT[QUEEN]);
    return 0;
}
