// Fit the king-danger parameters against the evaluation-error corpus.
//
//   make kstune && ./tools/kstune [corpus.epd ...]
//
// With no arguments it reads tests/data/evalerr.epd and, when it exists,
// tests/data/evalerr-self.epd: the positions a candidate's own search reached
// (tools/self-corpus.py). The main corpus alone was tuned against on
// 2026-09-14 and the result lost 33 Elo to positions that corpus does not
// contain; `tag self` is held to its baseline exactly as ctl is.
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
           KING_DANGER_NO_QUEEN_CUT, KING_DANGER_STM_PCT;
extern int PASSED_RANK[7];
extern int PASSED_FREE_PCT, PASSED_KING_PCT, PASSED_EG_PCT;

namespace {

// comp is the objective; ctl and self are the two sets the term must not
// damage. self is the search's own positions (tools/self-corpus.py), the ones
// the main corpus cannot contain and the 2026-09-14 gate was lost on.
enum Tag { COMP, CTL, SELF };
struct Row { Board board; int label; Tag tag; };

void load(const std::string& path, std::vector<Row>& rows) {
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
        r.label = 0; r.tag = CTL;
        for (size_t i = 1; i < parts.size(); ++i) {
            const std::string t = trim(parts[i]);
            if (t.rfind("sf ", 0) == 0) r.label = std::atoi(t.c_str() + 3);
            else if (t.rfind("tag ", 0) == 0)
                r.tag = (t.substr(4) == "comp") ? COMP : (t.substr(4) == "self") ? SELF : CTL;
        }
        rows.push_back(r);
    }
}

// ctl is reported whole and also over the "open" positions, the ones whose
// label is inside the +-1000 cap. 89 of the 688 ctl positions are decisive
// (Stockfish at the cap), and a term that pushes a won position further past
// +1000 improves the clamped error there while doing nothing for play. The
// constraint binds on the open ones, because those are the ordinary positions
// the control set exists to protect.
struct Score { double comp, ctl, ctlOpen, self; int flips; };

Score score(const std::vector<Row>& rows) {
    double sc = 0, sl = 0, so = 0, ss = 0; int nc = 0, nl = 0, no = 0, ns = 0, flips = 0;
    const bool clamp = !std::getenv("KSTUNE_NOCLAMP");
    for (const Row& r : rows) {
        // Same clamp the labels carry: Stockfish caps at +-1000, so an
        // evaluation past that is not "more wrong" for being further past.
        int ours = evaluate_details(r.board).total;
        if (clamp) ours = std::max(-1000, std::min(1000, ours));
        const double err = std::fabs((double)ours - (double)r.label);
        if (r.tag == COMP) {
            sc += err; ++nc;
            if ((ours >= 100 && r.label <= -100) || (ours <= -100 && r.label >= 100)) ++flips;
        } else if (r.tag == SELF) {
            ss += err; ++ns;
        } else {
            sl += err; ++nl;
            if (std::abs(r.label) < 1000) { so += err; ++no; }
        }
    }
    return { nc ? sc / nc : 0, nl ? sl / nl : 0, no ? so / no : 0, ns ? ss / ns : 0, flips };
}

// The constraint: neither held set may be worse than its baseline plus the
// tolerance. Both are measured against the term-off evaluation, so "worse"
// means the term did damage there, whatever the absolute level.
bool held(const Score& s, const Score& base, double tol) {
    return s.ctlOpen <= base.ctlOpen + tol && s.self <= base.self + tol;
}
// How much of the tolerance is used up. A step that leaves comp where it is
// and buys slack here is worth taking, because the slack is what lets a later
// step on another parameter lower comp; a descent that only ever accepts
// comp improvements cannot move a parameter whose whole job is to protect the
// held sets, which is exactly what the tempo discount is.
double slack(const Score& s, const Score& base) {
    return (s.ctlOpen - base.ctlOpen) + (s.self - base.self);
}

struct Param { const char* name; int* ptr; int lo, hi, step; };

} // namespace

int main(int argc, char** argv) {
    initMoveLookupTables();
    // Every argument is a corpus file; the default is the main corpus plus the
    // self-play half when it exists.
    std::vector<Row> rows;
    if (argc > 1) { for (int i = 1; i < argc; ++i) load(argv[i], rows); }
    else { load("tests/data/evalerr.epd", rows); load("tests/data/evalerr-self.epd", rows); }
    if (rows.empty()) { std::printf("no positions\n"); return 1; }
    int nSelf = 0; for (const Row& r : rows) if (r.tag == SELF) ++nSelf;

    // KSTUNE_SET picks the family: "king" (default) or "passed". The other
    // family stays at its shipped values, so each fit is one term against the
    // corpus with everything else held, and the report says which.
    const std::string set = std::getenv("KSTUNE_SET") ? std::getenv("KSTUNE_SET") : "king";
    std::vector<Param> params;
    if (set == "king") params = {
        {"SCALE",         &KING_DANGER_SCALE,         0, 2000, 10},
        {"MIN_ATTACKERS", &KING_DANGER_MIN_ATTACKERS, 1,   4,  1},
        {"OFFSET",        &KING_DANGER_OFFSET,        0,  60,  2},
        {"DEFENDER_W",    &KING_DANGER_DEFENDER_W,    0,  12,  1},
        {"WEAK_W",        &KING_DANGER_WEAK_W,        0,  12,  1},
        {"CHECK_W",       &KING_DANGER_CHECK_W,       0,  12,  1},
        {"NO_QUEEN_CUT",  &KING_DANGER_NO_QUEEN_CUT,  0,  40,  2},
        {"STM_PCT",       &KING_DANGER_STM_PCT,       0, 100,  5},
        {"W[PAWN]",       &KING_DANGER_WEIGHT[PAWN],   0,  8,  1},
        {"W[KNIGHT]",     &KING_DANGER_WEIGHT[KNIGHT], 0, 10,  1},
        {"W[BISHOP]",     &KING_DANGER_WEIGHT[BISHOP], 0, 10,  1},
        {"W[ROOK]",       &KING_DANGER_WEIGHT[ROOK],   0, 12,  1},
        {"W[QUEEN]",      &KING_DANGER_WEIGHT[QUEEN],  0, 16,  1},
    };
    else if (set == "passed") params = {
        {"RANK_1",   &PASSED_RANK[1],   0, 120,  2},
        {"RANK_2",   &PASSED_RANK[2],   0, 120,  2},
        {"RANK_3",   &PASSED_RANK[3],   0, 160,  2},
        {"RANK_4",   &PASSED_RANK[4],   0, 200,  2},
        {"RANK_5",   &PASSED_RANK[5],   0, 300,  4},
        {"RANK_6",   &PASSED_RANK[6],   0, 400,  4},
        {"FREE_PCT", &PASSED_FREE_PCT,  0, 200,  5},
        {"KING_PCT", &PASSED_KING_PCT,  0, 100,  2},
        {"EG_PCT",   &PASSED_EG_PCT,    0, 300,  5},
    };
    else { std::printf("unknown KSTUNE_SET %s\n", set.c_str()); return 1; }
    const int NP = (int)params.size();

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
        env("KD_STM_PCT", &KING_DANGER_STM_PCT);
        const Score s = score(rows);
        std::printf("comp %.1f  ctl %.1f  ctl-open %.1f  self %.1f  flips %d\n", s.comp, s.ctl, s.ctlOpen, s.self, s.flips);
        return 0;
    }

    Score base = score(rows);
    std::printf("%zu positions (%d self). baseline  comp %.1f  ctl %.1f  ctl-open %.1f  self %.1f  flips %d\n",
                rows.size(), nSelf, base.comp, base.ctl, base.ctlOpen, base.self, base.flips);
    std::printf("constraint: ctl-open <= %.1f, self <= %.1f\n\n",
                base.ctlOpen + CTL_TOLERANCE, base.self + CTL_TOLERANCE);

    Score cur = base;
    if (set == "king") {
        // The term is off at SCALE 0 and nothing else matters until it is on. A
        // greedy descent from a gentle switch-on cannot reach a region the
        // constraint allows only at larger scales, and there is one: along SCALE
        // alone, ctl-open falls before it rises. So the start is the SCALE that
        // minimises the held sets' excess, found by a plain sweep, and the
        // descent runs from there.
        int bestScale = 0;
        double bestHeld = 0;
        for (int sc = 10; sc <= params[0].hi; sc += params[0].step) {
            KING_DANGER_SCALE = sc;
            const Score s = score(rows);
            const double h = (s.ctlOpen - base.ctlOpen) + (s.self - base.self);
            if (h < bestHeld - 0.05) { cur = s; bestScale = sc; bestHeld = h; }
        }
        KING_DANGER_SCALE = bestScale;
        cur = score(rows);
        if (!held(cur, base, CTL_TOLERANCE)) {
            KING_DANGER_MIN_ATTACKERS = 3;
            cur = score(rows);
        }
    }
    std::printf("start     comp %.1f  ctl %.1f  ctl-open %.1f  self %.1f  flips %d   (set %s)\n\n",
                cur.comp, cur.ctl, cur.ctlOpen, cur.self, cur.flips, set.c_str());

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
                    const bool better = s.comp < cur.comp - 0.05;
                    const bool freer  = s.comp <= cur.comp && slack(s, base) < slack(cur, base) - 0.5;
                    if ((better || freer) && held(s, base, CTL_TOLERANCE)) {
                        cur = s; moved = true;
                        std::printf("  pass %2d  %-14s %4d -> %4d   comp %.1f  ctl %.1f  ctl-open %.1f  self %.1f  flips %d\n",
                                    pass, p.name, old, next, s.comp, s.ctl, s.ctlOpen, s.self, s.flips);
                    } else { *p.ptr = old; break; }
                }
            }
        }
        if (!moved) { std::printf("\nconverged after pass %d\n", pass); break; }
    }

    std::printf("\nresult    comp %.1f  ctl %.1f  ctl-open %.1f  self %.1f  flips %d   (baseline comp %.1f  ctl %.1f  ctl-open %.1f  self %.1f  flips %d)\n",
                cur.comp, cur.ctl, cur.ctlOpen, cur.self, cur.flips, base.comp, base.ctl, base.ctlOpen, base.self, base.flips);
    if (set == "king") {
        std::printf("\n  -DKING_DANGER_SCALE_PCT=%d -DKING_DANGER_MIN_ATTACKERS_N=%d "
                    "-DKING_DANGER_OFFSET_N=%d -DKING_DANGER_DEFENDER_W_N=%d "
                    "-DKING_DANGER_WEAK_W_N=%d -DKING_DANGER_CHECK_W_N=%d "
                    "-DKING_DANGER_NO_QUEEN_CUT_N=%d -DKING_DANGER_STM_PCT_N=%d\n",
                    KING_DANGER_SCALE, KING_DANGER_MIN_ATTACKERS, KING_DANGER_OFFSET,
                    KING_DANGER_DEFENDER_W, KING_DANGER_WEAK_W, KING_DANGER_CHECK_W,
                    KING_DANGER_NO_QUEEN_CUT, KING_DANGER_STM_PCT);
        std::printf("  -DKING_DANGER_W_PAWN_N=%d -DKING_DANGER_W_KNIGHT_N=%d -DKING_DANGER_W_BISHOP_N=%d "
                    "-DKING_DANGER_W_ROOK_N=%d -DKING_DANGER_W_QUEEN_N=%d\n",
                    KING_DANGER_WEIGHT[PAWN], KING_DANGER_WEIGHT[KNIGHT], KING_DANGER_WEIGHT[BISHOP],
                    KING_DANGER_WEIGHT[ROOK], KING_DANGER_WEIGHT[QUEEN]);
    } else {
        std::printf("\n  -DPASSED_RANK_1_N=%d -DPASSED_RANK_2_N=%d -DPASSED_RANK_3_N=%d "
                    "-DPASSED_RANK_4_N=%d -DPASSED_RANK_5_N=%d -DPASSED_RANK_6_N=%d\n"
                    "  -DPASSED_FREE_PCT_N=%d -DPASSED_KING_PCT_N=%d -DPASSED_EG_PCT_N=%d\n",
                    PASSED_RANK[1], PASSED_RANK[2], PASSED_RANK[3], PASSED_RANK[4], PASSED_RANK[5], PASSED_RANK[6],
                    PASSED_FREE_PCT, PASSED_KING_PCT, PASSED_EG_PCT);
    }
    return 0;
}
