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
#include "engine/search.hpp"   // setSearchOption, for --opt

#include <cctype>
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

// Every EvalDetails field in declaration order, as names and as values. Keep
// both in sync with EvalDetails — a field added without an entry here is a
// field the test cannot protect. They are a pair on purpose: the CSV writer,
// the diff reporter and the symmetry check all read them, so a new term becomes
// covered by all three at once or by none.
static const char* FIELD_NAMES[] = {
    "total", "material", "mobility", "kingSafety", "centerControl",
    "bishopPair", "doubledPawn", "isolatedPawn", "passedPawn",
    "backwardPawn", "connectedPawn", "pawnChain", "rooksOpenFile",
    "rooksSemiOpenFile", "rooks7thRank", "pst", "outpost", "trapped",
    "kingActivity", "threats", "undefended", "space", "drawish"
};
static const int NUM_FIELDS = (int)(sizeof(FIELD_NAMES) / sizeof(FIELD_NAMES[0]));

static void fieldsOf(const EvalDetails& e, int out[NUM_FIELDS]) {
    int i = 0;
    out[i++] = e.total;             out[i++] = e.material;
    out[i++] = e.mobility;          out[i++] = e.kingSafety;
    out[i++] = e.centerControl;     out[i++] = e.bishopPair;
    out[i++] = e.doubledPawn;       out[i++] = e.isolatedPawn;
    out[i++] = e.passedPawn;        out[i++] = e.backwardPawn;
    out[i++] = e.connectedPawn;     out[i++] = e.pawnChain;
    out[i++] = e.rooksOpenFile;     out[i++] = e.rooksSemiOpenFile;
    out[i++] = e.rooks7thRank;      out[i++] = e.pst;
    out[i++] = e.outpost;           out[i++] = e.trapped;
    out[i++] = e.kingActivity;      out[i++] = e.threats;
    out[i++] = e.undefended;        out[i++] = e.space;
    out[i++] = e.drawish;
}

// One CSV line per position: FEN first, then every field in declaration order.
static void writeLine(std::ostream& out, const Board& board) {
    int v[NUM_FIELDS];
    fieldsOf(evaluate_details(board), v);
    out << board.getFEN();
    for (int i = 0; i < NUM_FIELDS; ++i) out << ',' << v[i];
    out << '\n';
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
    // Column 0 is the FEN; the rest line up with FIELD_NAMES.
    auto columnName = [](size_t i) {
        return (i == 0) ? "fen"
             : (i <= (size_t)NUM_FIELDS) ? FIELD_NAMES[i - 1] : "?";
    };

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
                std::cerr << "  " << columnName(i) << ": "
                          << r[i] << " -> " << c[i] << "\n";
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

// --- Mirror symmetry ---
//
// Reflect a position top to bottom and swap the colours: White to move on rank
// 1 becomes Black to move on rank 8, with every piece, castling right and en
// passant square carried across. Chess is symmetric under that reflection, so
// every white-perspective evaluation term must come back exactly negated.
//
// This is the strongest cheap invariant an evaluation has. It needs no
// reference file, so it cannot go stale and cannot be regenerated into
// agreement — unlike the comparison above, which will happily bless a bug the
// moment someone runs --regen without reading the diff. It is also absolute
// rather than relative: it says the evaluation is *wrong*, not merely changed.
//
// It is here because it failed. King safety measured distance from the centre
// as |rank - 3|, and on an eight-rank board the centre lies between 3 and 4, so
// White's king on rank 7 was charged four while Black's on rank 0 was charged
// three. Every position, from move one, in every game the engine ever played.
static std::string mirrorFen(const std::string& fen) {
    std::vector<std::string> f;
    std::istringstream in(fen);
    std::string part;
    while (in >> part) f.push_back(part);
    if (f.size() != 6) return "";

    // Piece placement: reverse the rank order, swap each piece's colour.
    std::vector<std::string> ranks;
    size_t start = 0;
    for (size_t i = 0; i <= f[0].size(); ++i) {
        if (i == f[0].size() || f[0][i] == '/') {
            ranks.push_back(f[0].substr(start, i - start));
            start = i + 1;
        }
    }
    if (ranks.size() != 8) return "";
    std::string placement;
    for (int r = 7; r >= 0; --r) {
        for (char c : ranks[r]) {
            placement += std::isalpha((unsigned char)c)
                       ? (std::islower((unsigned char)c) ? (char)std::toupper(c)
                                                         : (char)std::tolower(c))
                       : c;
        }
        if (r > 0) placement += '/';
    }

    const std::string side = (f[1] == "w") ? "b" : "w";

    // Castling rights follow their owners, re-emitted in canonical order so the
    // mirrored FEN is comparable as text as well as parseable.
    std::string castling;
    if (f[2].find('k') != std::string::npos) castling += 'K';
    if (f[2].find('q') != std::string::npos) castling += 'Q';
    if (f[2].find('K') != std::string::npos) castling += 'k';
    if (f[2].find('Q') != std::string::npos) castling += 'q';
    if (castling.empty()) castling = "-";

    std::string ep = f[3];
    if (ep != "-" && ep.size() == 2) ep[1] = (char)('1' + '8' - ep[1]);

    return placement + ' ' + side + ' ' + castling + ' ' + ep + ' ' + f[4] + ' ' + f[5];
}

static int checkMirrorSymmetry() {
    rngSeed(SEED);
    long checked = 0, badPositions = 0;
    long perField[NUM_FIELDS] = {0};
    const long REPORT_LIMIT = 5;

    for (int game = 0; game < 40; ++game) {
        Board board;
        for (int ply = 0; ply < MAX_PLIES; ++ply) {
            MoveList moves = generateLegalMoves(board, board.activeColor);
            if (moves.empty() || board.halfmoveClock >= 100) break;

            const std::string fen = board.getFEN();
            const std::string flipped = mirrorFen(fen);
            Board other;
            if (!flipped.empty() && other.setFromFEN(flipped)) {
                int a[NUM_FIELDS], b[NUM_FIELDS];
                fieldsOf(evaluate_details(board), a);
                fieldsOf(evaluate_details(other), b);

                bool bad = false;
                for (int i = 0; i < NUM_FIELDS; ++i) {
                    if (a[i] != -b[i]) { ++perField[i]; bad = true; }
                }
                ++checked;
                if (bad && ++badPositions <= REPORT_LIMIT) {
                    std::cerr << "asymmetric: " << fen << "\n"
                              << "  mirrored: " << flipped << "\n";
                    for (int i = 0; i < NUM_FIELDS; ++i) {
                        if (a[i] == -b[i]) continue;
                        std::cerr << "  " << FIELD_NAMES[i] << ": " << a[i]
                                  << " vs " << b[i] << " (expected "
                                  << -a[i] << ")\n";
                    }
                }
            }
            board.makeMove(moves[rngNext() % moves.size()]);
        }
    }

    if (badPositions) {
        std::cerr << "\nFAILED: " << badPositions << " of " << checked
                  << " positions evaluate asymmetrically";
        if (badPositions > REPORT_LIMIT)
            std::cerr << " (first " << REPORT_LIMIT << " shown)";
        std::cerr << ".\n        Terms at fault:";
        for (int i = 0; i < NUM_FIELDS; ++i)
            if (perField[i]) std::cerr << ' ' << FIELD_NAMES[i]
                                       << '(' << perField[i] << ')';
        std::cerr << "\n        This is a bug in the evaluation, not a change "
                     "to it. Do not regenerate the reference.\n";
        return 1;
    }
    std::cout << "PASSED: evaluation is mirror-symmetric across "
              << checked << " positions\n";
    return 0;
}

int main(int argc, char** argv) {
    // Without this every lookup table is empty and move generation silently
    // returns no moves — which looks like "every game ended immediately"
    // rather than like an error.
    initMoveLookupTables();

    bool regen = (argc > 1 && std::strcmp(argv[1], "--regen") == 0);

    // --opt <name>=<on|off>, as tests/bench takes it.
    //
    // The reference file describes the default evaluation, so a modified one
    // cannot be compared against it — and is refused below. What *can* be
    // checked under any option set is mirror symmetry, which has no reference
    // file and cannot be regenerated into agreement. An evaluation toggle that
    // is not colour-symmetric makes the engine play White and Black by
    // different rules, and this is the only thing that would catch it before a
    // gate spent a day measuring the asymmetry instead of the feature.
    bool optionsChanged = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--opt") != 0 || i + 1 >= argc) continue;
        std::string spec = argv[++i];
        size_t eq = spec.find('=');
        if (eq == std::string::npos) {
            std::cerr << "--opt wants <name>=<on|off>, got '" << spec << "'\n";
            return 1;
        }
        std::string name = spec.substr(0, eq), value = spec.substr(eq + 1);
        if (!setSearchOption(g_searchOptions, name,
                             value == "on" || value == "true" || value == "1")) {
            std::cerr << "unknown search option '" << name << "'\n";
            return 1;
        }
        optionsChanged = true;
    }

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

    int rc = 0;
    if (optionsChanged) {
        std::cout << "--opt given: skipping the reference comparison, which "
                     "describes the default evaluation.\n"
                     "Checking the two invariants that hold under any options.\n";
    } else {
        std::ostringstream produced;
        generate(produced);
        rc = compare(produced.str());
    }
    if (checkEvalCache() != 0) rc = 1;
    if (checkMirrorSymmetry() != 0) rc = 1;
    return rc;
}
