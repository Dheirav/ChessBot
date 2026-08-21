// How wrong is the evaluation? — scored against positions it got wrong in real games.
//
// tests/evalref answers "did the evaluation change?" and "is it mirror-
// symmetric?". Neither asks whether it is *right*, so until now the only
// instrument that could answer that was a match: hours per verdict, and a
// verdict about games rather than about the evaluation. This scores the static
// evaluation against Stockfish over positions harvested from the archive by
// tools/eval-corpus.py, in about a second.
//
//   ./tests/evalerror                     score the corpus
//   ./tests/evalerror --worst 20          and list the 20 largest errors
//   ./tests/evalerror --save <file>       record these numbers as the baseline
//   ./tests/evalerror --check <file>      fail if mean error got worse
//
// Read the two tags separately and never average them together:
//
//   comp  positions where material says one player is winning and the position
//         says the other is — the compensation BUGS.md 13 is about. A fix aims
//         at this number.
//   ctl   ordinary positions. A term that improves comp by wrecking ctl has
//         not improved the evaluation, it has moved the error somewhere the
//         corpus was not looking. This column is the one that catches that.
//
// What this cannot tell you: whether a smaller error wins games. Agreement
// with Stockfish is not Elo, and 6.4 is the standing proof that a term can
// look right and lose (ROADMAP.md). Use this to iterate in seconds and a gate
// to decide — in that order, because the gate is the expensive one.
#include "engine/board.hpp"
#include "engine/evaluation.hpp"
#include "engine/move_lookup.hpp"
#include "engine/search.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Row {
    std::string fen, tag, id;
    int sf = 0;
};

struct Stats {
    long n = 0;
    double absErr = 0;
    double signedErr = 0;
    long signFlips = 0;      // we say one side is better by a pawn, truth says the other
    double mean() const { return n ? absErr / n : 0.0; }
    double bias() const { return n ? signedErr / n : 0.0; }
};

bool parseLine(const std::string& line, Row& r) {
    if (line.empty() || line[0] == '#') return false;
    std::string::size_type semi = line.find(" ; ");
    if (semi == std::string::npos) return false;
    r.fen = line.substr(0, semi);
    std::istringstream rest(line.substr(semi + 3));
    std::string field;
    while (std::getline(rest, field, ';')) {
        std::istringstream f(field);
        std::string key, value;
        f >> key >> value;
        if (key == "sf") r.sf = std::atoi(value.c_str());
        else if (key == "tag") r.tag = value;
        else if (key == "id") r.id = value;
    }
    return !r.fen.empty() && !r.tag.empty();
}

}  // namespace

int main(int argc, char** argv) {
    std::string path = "tests/data/evalerr.epd";
    std::string saveTo, checkAgainst;
    int worst = 0;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--worst") && i + 1 < argc) worst = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--save") && i + 1 < argc) saveTo = argv[++i];
        else if (!std::strcmp(argv[i], "--check") && i + 1 < argc) checkAgainst = argv[++i];
        else if (argv[i][0] != '-') path = argv[i];
        else { std::printf("unknown option: %s\n", argv[i]); return 2; }
    }

    initMoveLookupTables();

    std::ifstream in(path);
    if (!in) {
        std::printf("cannot read %s\n", path.c_str());
        std::printf("build it with: tools/eval-corpus.py\n");
        return 2;
    }

    Stats comp, ctl;
    std::vector<std::pair<int, Row>> errors;
    std::string line;
    long skipped = 0;
    while (std::getline(in, line)) {
        Row r;
        if (!parseLine(line, r)) continue;
        Board board;
        // setFromFEN, not parseFEN: parseFEN fills the squares and leaves the
        // Zobrist hash stale, and evaluate() is keyed on that hash. Loading
        // positions the other way makes every one of them collide on the
        // default hash and return whatever the eval cache stored first — which
        // reads as an evaluation of exactly 0 for every position in the file.
        if (!board.setFromFEN(r.fen)) { ++skipped; continue; }
        const int ours = evaluate(board);          // white's point of view, centipawns
        const int err = ours - r.sf;
        Stats& s = (r.tag == "comp") ? comp : ctl;
        ++s.n;
        s.absErr += std::abs(err);
        s.signedErr += err;
        if ((ours >= 100 && r.sf <= -100) || (ours <= -100 && r.sf >= 100)) ++s.signFlips;
        errors.emplace_back(std::abs(err), r);
    }
    if (comp.n + ctl.n == 0) { std::printf("no positions read from %s\n", path.c_str()); return 2; }

    std::printf("evaluation error against Stockfish, over %ld positions", comp.n + ctl.n);
    if (skipped) std::printf(" (%ld unparseable, skipped)", skipped);
    std::printf("\n\n");
    std::printf("  %-6s %7s %12s %12s %14s\n", "tag", "count", "mean |err|", "bias", "sign flips");
    std::printf("  %-6s %7ld %12.1f %12.1f %10ld (%4.1f%%)\n", "comp", comp.n, comp.mean(), comp.bias(),
                comp.signFlips, comp.n ? 100.0 * comp.signFlips / comp.n : 0.0);
    std::printf("  %-6s %7ld %12.1f %12.1f %10ld (%4.1f%%)\n", "ctl", ctl.n, ctl.mean(), ctl.bias(),
                ctl.signFlips, ctl.n ? 100.0 * ctl.signFlips / ctl.n : 0.0);
    std::printf("\n  a sign flip is the evaluation calling a position won that is lost, or the\n");
    std::printf("  reverse, both by at least a pawn. On comp it is the defect itself.\n");

    if (worst > 0) {
        std::sort(errors.begin(), errors.end(),
                  [](const std::pair<int, Row>& a, const std::pair<int, Row>& b) { return a.first > b.first; });
        std::printf("\n  %d largest errors\n", worst);
        for (int i = 0; i < worst && i < (int)errors.size(); ++i) {
            const Row& r = errors[i].second;
            Board board; board.setFromFEN(r.fen);
            std::printf("   %6d  sf %5d  ours %5d  %-5s %-14s %s\n",
                        errors[i].first, r.sf, evaluate(board), r.tag.c_str(), r.id.c_str(), r.fen.c_str());
        }
    }

    if (!saveTo.empty()) {
        std::ofstream out(saveTo);
        out << "# baseline written by tests/evalerror --save; regenerate deliberately\n";
        out << "comp " << comp.n << " " << comp.mean() << " " << comp.signFlips << "\n";
        out << "ctl "  << ctl.n  << " " << ctl.mean()  << " " << ctl.signFlips  << "\n";
        std::printf("\nbaseline written to %s\n", saveTo.c_str());
    }

    if (!checkAgainst.empty()) {
        std::ifstream base(checkAgainst);
        if (!base) { std::printf("\ncannot read baseline %s\n", checkAgainst.c_str()); return 2; }
        bool worseSomewhere = false;
        std::string tag;
        long n, flips;
        double mean;
        while (base >> tag) {
            if (tag[0] == '#') { std::getline(base, line); continue; }
            base >> n >> mean >> flips;
            const Stats& s = (tag == "comp") ? comp : ctl;
            const double delta = s.mean() - mean;
            std::printf("\n  %-5s mean |err| %.1f -> %.1f (%+.1f)", tag.c_str(), mean, s.mean(), delta);
            // A tolerance, because the corpus is finite and rounding is not a
            // regression. Anything past it is the evaluation getting worse
            // somewhere, and the point of the file is to say so out loud.
            if (delta > 1.0) { std::printf("  WORSE"); worseSomewhere = true; }
        }
        std::printf("\n");
        if (worseSomewhere) { std::printf("\nFAILED: the evaluation is further from the truth than the baseline\n"); return 1; }
        std::printf("\nPASSED: no worse than the baseline\n");
    }
    return 0;
}
