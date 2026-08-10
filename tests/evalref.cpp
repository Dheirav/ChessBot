// Evaluation regression test.
//
// Dumps every EvalDetails field for a large, deterministic set of positions and
// compares against a stored reference. This is the test that makes rewriting
// evaluation safe: a change that is meant to preserve behaviour must reproduce
// the file byte for byte, and a change that is meant to alter behaviour shows
// you exactly which terms moved and by how much.
//
//   ./tests/evalref            compare against tests/data/evalref.txt
//   ./tests/evalref --regen    rewrite the reference (only when a change is
//                              intended and has been reviewed)
//
// Positions come from seeded random games, so the set covers openings,
// middlegames, endgames, checks, promotions and lopsided material without
// anyone curating it. The generator is a fixed xorshift64 and the move choice
// depends only on the legal move list, so the same binary always produces the
// same positions on any platform.

#include "engine/board.hpp"
#include "engine/movegen.hpp"
#include "engine/move_lookup.hpp"
#include "engine/evaluation.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static const char* REF_PATH = "tests/data/evalref.txt";

// Number of random games and the ply cap per game. 200 x up to 120 plies lands
// a little under 24k positions, which was enough last time to catch every
// unintended evaluation change during a full rewrite. Raising it costs file
// size and nothing else — the whole run takes well under a second.
static const int NUM_GAMES = 200;
static const int MAX_PLIES = 120;
static const uint64_t SEED = 0x5CBE7A1F2026ULL;

static uint64_t rngState;
static void rngSeed(uint64_t s) { rngState = s ? s : 1; }
static uint64_t rngNext() {
    rngState ^= rngState << 13;
    rngState ^= rngState >> 7;
    rngState ^= rngState << 17;
    return rngState;
}

// One CSV line per position: FEN first, then every EvalDetails field in
// declaration order. Keep this in sync with EvalDetails — a field added
// without a column here is a field the test cannot protect.
static void writeLine(std::ostream& out, const Board& board) {
    EvalDetails e = evaluate_details(board);
    out << board.getFEN()
        << ',' << e.total
        << ',' << e.material
        << ',' << e.mobility
        << ',' << e.kingSafety
        << ',' << e.centerControl
        << ',' << e.bishopPair
        << ',' << e.doubledPawn
        << ',' << e.isolatedPawn
        << ',' << e.passedPawn
        << ',' << e.backwardPawn
        << ',' << e.connectedPawn
        << ',' << e.pawnChain
        << ',' << e.rooksOpenFile
        << ',' << e.rooksSemiOpenFile
        << ',' << e.rooks7thRank
        << ',' << e.pst
        << ',' << e.outpost
        << ',' << e.trapped
        << ',' << e.kingActivity
        << ',' << e.threats
        << ',' << e.undefended
        << ',' << e.space
        << ',' << e.drawish
        << '\n';
}

static void generate(std::ostream& out) {
    rngSeed(SEED);
    for (int game = 0; game < NUM_GAMES; ++game) {
        Board board;
        for (int ply = 0; ply < MAX_PLIES; ++ply) {
            MoveList moves = generateLegalMoves(board, board.activeColor);
            if (moves.empty()) break;              // checkmate or stalemate
            if (board.halfmoveClock >= 100) break; // fifty-move rule

            writeLine(out, board);
            board.makeMove(moves[rngNext() % moves.size()]);
        }
    }
}

// Report the first few differing lines with the column name, rather than just
// "files differ" — the point of storing every term separately is to be told
// which term moved.
static int compare(const std::string& produced) {
    static const char* COLS[] = {
        "fen", "total", "material", "mobility", "kingSafety", "centerControl",
        "bishopPair", "doubledPawn", "isolatedPawn", "passedPawn",
        "backwardPawn", "connectedPawn", "pawnChain", "rooksOpenFile",
        "rooksSemiOpenFile", "rooks7thRank", "pst", "outpost", "trapped",
        "kingActivity", "threats", "undefended", "space", "drawish"
    };
    static const int NUM_COLS = (int)(sizeof(COLS) / sizeof(COLS[0]));

    std::ifstream ref(REF_PATH);
    if (!ref) {
        std::cerr << "FAILED: no reference at " << REF_PATH << "\n"
                  << "        Run 'make evalref-regen' on a build you trust.\n";
        return 1;
    }

    auto split = [](const std::string& line) {
        std::vector<std::string> f;
        size_t start = 0;
        for (size_t i = 0; i <= line.size(); ++i) {
            if (i == line.size() || line[i] == ',') {
                f.push_back(line.substr(start, i - start));
                start = i + 1;
            }
        }
        return f;
    };

    std::istringstream cur(produced);
    std::string refLine, curLine;
    long lineNo = 0, mismatches = 0;
    const long REPORT_LIMIT = 10;

    while (true) {
        bool haveRef = (bool)std::getline(ref, refLine);
        bool haveCur = (bool)std::getline(cur, curLine);
        if (!haveRef && !haveCur) break;
        ++lineNo;

        if (haveRef != haveCur) {
            std::cerr << "FAILED: reference has "
                      << (haveRef ? "more" : "fewer")
                      << " positions than this build produced (diverges at line "
                      << lineNo << ").\n"
                      << "        The position set itself changed — check the "
                         "move generator or the RNG, not the evaluation.\n";
            return 1;
        }
        if (refLine == curLine) continue;

        ++mismatches;
        if (mismatches <= REPORT_LIMIT) {
            std::vector<std::string> r = split(refLine), c = split(curLine);
            std::cerr << "line " << lineNo << ":\n";
            if (!r.empty()) std::cerr << "  fen " << r[0] << "\n";
            size_t n = r.size() < c.size() ? r.size() : c.size();
            for (size_t i = 1; i < n; ++i) {
                if (r[i] == c[i]) continue;
                const char* name = (i < (size_t)NUM_COLS) ? COLS[i] : "?";
                std::cerr << "  " << name << ": " << r[i] << " -> " << c[i] << "\n";
            }
        }
    }

    if (mismatches) {
        std::cerr << "\nFAILED: " << mismatches << " of " << lineNo
                  << " positions changed";
        if (mismatches > REPORT_LIMIT)
            std::cerr << " (first " << REPORT_LIMIT << " shown)";
        std::cerr << ".\n"
                  << "        If this change was intended, review the terms "
                     "above and run 'make evalref-regen'.\n";
        return 1;
    }

    std::cout << "PASSED: evaluation unchanged across " << lineNo
              << " positions\n";
    return 0;
}

// evaluate() memoizes into a hash-keyed cache; evaluate_details() always
// computes. Everything above compares evaluate_details() against the reference,
// so none of it would notice the cache returning a wrong answer.
//
// This walks the same positions twice: the first pass fills the cache, the
// second is served from it, and both must equal a freshly computed total. A key
// collision, a torn read, or a stale entry surviving a position change all show
// up here.
static int checkEvalCache() {
    rngSeed(SEED);
    long checked = 0, mismatches = 0;

    for (int game = 0; game < 40; ++game) {
        Board board;
        for (int ply = 0; ply < MAX_PLIES; ++ply) {
            MoveList moves = generateLegalMoves(board, board.activeColor);
            if (moves.empty() || board.halfmoveClock >= 100) break;

            int expected = evaluate_details(board).total;
            int first = evaluate(board);   // populates the cache
            int second = evaluate(board);  // served from the cache
            ++checked;
            if (first != expected || second != expected) {
                if (++mismatches <= 5) {
                    std::cerr << "eval cache mismatch at " << board.getFEN() << "\n"
                              << "  evaluate_details: " << expected
                              << "  evaluate (cold): " << first
                              << "  evaluate (warm): " << second << "\n";
                }
            }
            board.makeMove(moves[rngNext() % moves.size()]);
        }
    }

    if (mismatches) {
        std::cerr << "FAILED: evaluate() disagreed with evaluate_details() on "
                  << mismatches << " of " << checked << " positions\n";
        return 1;
    }
    std::cout << "PASSED: eval cache agrees with a fresh computation across "
              << checked << " positions\n";
    return 0;
}

int main(int argc, char** argv) {
    // Without this every lookup table is empty and move generation silently
    // returns no moves — which looks like "every game ended immediately"
    // rather than like an error.
    initMoveLookupTables();

    bool regen = (argc > 1 && std::strcmp(argv[1], "--regen") == 0);

    if (regen) {
        std::ofstream out(REF_PATH, std::ios::binary);
        if (!out) {
            std::cerr << "cannot write " << REF_PATH
                      << " (run from the repository root)\n";
            return 1;
        }
        generate(out);
        out.close();
        std::cout << "wrote " << REF_PATH << "\n";
        return 0;
    }

    std::ostringstream produced;
    generate(produced);
    int rc = compare(produced.str());
    if (checkEvalCache() != 0) rc = 1;
    return rc;
}
