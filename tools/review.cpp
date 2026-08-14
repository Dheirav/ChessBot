// Game review: read a PGN, analyse every position, report what each move cost.
//
//   ./tools/review game.pgn [--engine <path>] [--depth N] [--hash MB]
//
// The analysis engine is a parameter, not this engine (docs/REVIEW.md). A
// review is only worth reading if the thing doing the reviewing is stronger
// than the players being reviewed, and at Lichess rapid ~2065 ChessBot is a
// peer of its own games rather than an oracle over them. Point this at
// Stockfish for output to trust; point it at ./chessbot to see what ChessBot
// thinks, which is a different and much narrower question.
//
// **One search per position, not two.** The plan called for searching each
// position and then the position after the move played — but the position after
// move i *is* position i+1, which the loop already visits. So n moves need n+1
// searches rather than 2n. Everything below depends on that identity holding,
// which is why the engine is fed the game as a move list from the start rather
// than being handed positions independently.

#include "engine/move_lookup.hpp"
#include "engine/board.hpp"
#include "engine/evaluation.hpp"
#include "engine/movegen.hpp"
#include "engine/pgn.hpp"
#include "engine/uci_engine.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

// Mate scores are not centipawns and must not be subtracted as if they were:
// "mate in 3" minus "mate in 5" is not 200 of anything. Clamping puts them on
// a scale where the arithmetic is meaningless but bounded, so one missed mate
// cannot swamp a whole game's average.
constexpr int MATE_CLAMP = 1000;

int clampScore(int s) {
    if (s >  MATE_CLAMP) return  MATE_CLAMP;
    if (s < -MATE_CLAMP) return -MATE_CLAMP;
    return s;
}

// Centipawns are the wrong unit to judge a move in, and reviewing all 49 of
// this bot's games proved it: average centipawn loss came out at 83.0 in games
// it *won* and 29.8 in games it *lost*. That is not a paradox, it is the metric
// failing. In a decided position a 300-centipawn slip changes nothing — going
// from +900 to +600 still wins — while the same 300 from level is the game. Raw
// loss scores them identically, so a game full of winning positions looks like
// a badly played one.
//
// Win probability has the compression built in: +900 to +600 is 6.4 percentage
// points, 0 to -300 is 25.1. Everything below is judged in those points, which
// is the same choice Lichess and Chess.com make and for the same reason.
double winPercent(int cp) {
    return 50.0 + 50.0 * (2.0 / (1.0 + std::exp(-0.00368208 * cp)) - 1.0);
}

// Per-move accuracy from the win probability given up. Lichess's curve: it is
// one defensible mapping of many, and the reason to adopt an existing one is
// that a curve invented here would inevitably be tuned until the numbers
// flattered whoever was being reviewed.
double accuracy(double wpLoss) {
    const double a = 103.1668 * std::exp(-0.04354 * wpLoss) - 3.1669;
    return a < 0.0 ? 0.0 : (a > 100.0 ? 100.0 : a);
}

// Numeric Annotation Glyphs, the machine-readable half of an annotation. Only
// the criticisms are emitted: "!" on a merely-best move is noise, and every
// viewer already highlights the engine's preference.
const char* nagFor(double wpLoss) {
    if (wpLoss >= 20.0) return "$4";   // blunder, shown as ??
    if (wpLoss >= 10.0) return "$2";   // mistake, shown as ?
    if (wpLoss >=  5.0) return "$6";   // dubious, shown as ?!
    return "";
}

const char* classify(double wpLoss) {
    if (wpLoss >= 20.0) return "Blunder";
    if (wpLoss >= 10.0) return "Mistake";
    if (wpLoss >=  5.0) return "Inaccuracy";
    if (wpLoss >=  2.0) return "Good";
    if (wpLoss >=  0.5) return "Excellent";
    return "Best";
}

// Walk a principal variation to its end and return the position it reaches.
// Stops early on any move that is not legal, which should not happen with a
// well-behaved engine but must not be allowed to corrupt the board if it does.
// How far down a principal variation to walk before comparing.
//
// Far enough that a tactic has resolved, and no further. Two lines from
// *different* moves diverge, and the deeper they run the more a term diff
// measures that divergence rather than what the move cost — a 55-centipawn
// inaccuracy was being "explained" by a 397-point swing in threats, which is
// two different positions talking, not an explanation.
constexpr size_t PV_ATTRIBUTION_PLIES = 6;

Board atEndOfPv(const Board& from, const std::vector<std::string>& pv) {
    Board b = from.copyForSearch();
    size_t used = 0;
    for (const std::string& mv : pv) {
        if (used++ >= PV_ATTRIBUTION_PLIES) break;
        bool played = false;
        Board probe = b.copyForSearch();
        for (const Move& cand : generateLegalMoves(probe, probe.activeColor)) {
            if (toUciMove(cand) == mv) { b.makeMove(cand); played = true; break; }
        }
        if (!played) break;
    }
    return b;
}

// Which named evaluation terms differ between two positions, largest first.
//
// This explains a move *in the vocabulary of this engine's evaluation*, which
// is not the same as explaining why the analysing engine scored it that way.
// When the two disagree — Stockfish says a move lost 500 centipawns and every
// term here is unchanged — that disagreement is the finding: it means this
// evaluation is blind to whatever decided the position. Reported as "no term
// accounts for it" rather than dressed up.
struct TermDelta { const char* name; int delta; };

std::vector<TermDelta> termDiff(const Board& before, const Board& after) {
    const EvalDetails a = evaluate_details(before);
    const EvalDetails b = evaluate_details(after);
    const std::vector<TermDelta> all = {
        {"material", b.material - a.material},
        {"mobility", b.mobility - a.mobility},
        {"king safety", b.kingSafety - a.kingSafety},
        {"centre control", b.centerControl - a.centerControl},
        {"bishop pair", b.bishopPair - a.bishopPair},
        {"doubled pawns", b.doubledPawn - a.doubledPawn},
        {"isolated pawns", b.isolatedPawn - a.isolatedPawn},
        {"passed pawns", b.passedPawn - a.passedPawn},
        {"backward pawns", b.backwardPawn - a.backwardPawn},
        {"connected pawns", b.connectedPawn - a.connectedPawn},
        {"pawn chains", b.pawnChain - a.pawnChain},
        {"rooks on open files", b.rooksOpenFile - a.rooksOpenFile},
        {"rooks on semi-open", b.rooksSemiOpenFile - a.rooksSemiOpenFile},
        {"rooks on the 7th", b.rooks7thRank - a.rooks7thRank},
        {"piece placement", b.pst - a.pst},
        {"outposts", b.outpost - a.outpost},
        {"trapped pieces", b.trapped - a.trapped},
        {"king activity", b.kingActivity - a.kingActivity},
        {"threats", b.threats - a.threats},
        {"undefended pieces", b.undefended - a.undefended},
        {"space", b.space - a.space},
    };
    std::vector<TermDelta> out;
    for (const TermDelta& t : all) if (t.delta != 0) out.push_back(t);
    std::sort(out.begin(), out.end(), [](const TermDelta& x, const TermDelta& y) {
        return std::abs(x.delta) > std::abs(y.delta);
    });
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    initMoveLookupTables();

    std::string pgnPath, enginePath = "/usr/games/stockfish";
    int depth = 16, hashMb = 256;
    // Standard UCI engines take no arguments. ChessBot is the exception — its
    // default mode opens a window — so reviewing with it needs
    //   --engine ./chessbot --engine-arg --uci
    std::vector<std::string> engineArgs;
    std::string annotateOut;
    bool explain = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : ""; };
        if      (a == "--engine") enginePath = next();
        else if (a == "--depth")  depth = std::atoi(next());
        else if (a == "--hash")   hashMb = std::atoi(next());
        else if (a == "--engine-arg") engineArgs.push_back(next());
        else if (a == "--annotate")   annotateOut = next();
        else if (a == "--explain")    explain = true;
        else if (a[0] != '-')     pgnPath = a;
        else { std::printf("unknown option: %s\n", a.c_str()); return 1; }
    }
    if (pgnPath.empty()) {
        std::printf("usage: review <game.pgn> [--engine <path>] [--engine-arg <a>]\n"
                    "                 [--depth N] [--hash MB] [--annotate out.pgn] [--explain]\n"
                    "  default engine is /usr/games/stockfish; ChessBot needs --engine-arg --uci\n");
        return 1;
    }

    PgnGame game;
    std::string err;
    if (!readPgn(pgnPath, game, &err)) {
        std::printf("cannot read %s: %s\n", pgnPath.c_str(), err.c_str());
        return 1;
    }

    UciEngine engine;
    if (!engine.start(enginePath, hashMb, engineArgs)) {
        std::printf("cannot start engine: %s\n", enginePath.c_str());
        return 1;
    }
    engine.newGame();

    std::printf("%s vs %s  (%s)\n", game.tags.white.c_str(), game.tags.black.c_str(),
                game.tags.result.c_str());
    std::printf("%zu moves | %s at depth %d\n\n", game.moves.size(),
                enginePath.c_str(), depth);

    // Score of every position, from the point of view of the side to move in it.
    // SAN is collected alongside, because "Qa8+" is what a player recognises and
    // "c6a8" is what the wire format calls it.
    std::vector<std::string> uci;
    std::vector<std::string> san(game.moves.size());
    std::vector<std::string> bestSan(game.moves.size());
    std::vector<std::vector<std::string>> pv(game.moves.size() + 1);
    std::vector<Board> position(game.moves.size() + 1);
    {
        Board b;
        if (!game.tags.startFen.empty()) b.setFromFEN(game.tags.startFen);
        for (size_t i = 0; i <= game.moves.size(); ++i) {
            position[i] = b.copyForSearch();
            if (i < game.moves.size()) b.makeMove(game.moves[i]);
        }
    }
    {
        Board b;
        if (!game.tags.startFen.empty()) b.setFromFEN(game.tags.startFen);
        for (size_t i = 0; i < game.moves.size(); ++i) {
            san[i] = toSan(b, game.moves[i]);
            b.makeMove(game.moves[i]);
        }
    }
    std::vector<int> score(game.moves.size() + 1, 0);
    std::vector<std::string> best(game.moves.size() + 1);

    for (size_t i = 0; i <= game.moves.size(); ++i) {
        const std::string mv = engine.bestMove(uci, /*ms=*/0, /*nodes=*/0, depth);
        if (!engine.haveScore()) break;          // terminal position: nothing to search
        score[i] = clampScore(engine.lastScore());
        best[i] = mv;
        pv[i] = engine.lastPv();
        if (i < game.moves.size()) {
            // The engine answers in UCI; name it the way the game does.
            Board at;
            if (!game.tags.startFen.empty()) at.setFromFEN(game.tags.startFen);
            for (size_t k = 0; k < i; ++k) at.makeMove(game.moves[k]);
            for (const Move& cand : generateLegalMoves(at, at.activeColor)) {
                if (toUciMove(cand) == mv) { bestSan[i] = toSan(at, cand); break; }
            }
            uci.push_back(toUciMove(game.moves[i]));
        }
        std::fprintf(stderr, "\ranalysing %zu/%zu", i + 1, game.moves.size() + 1);
    }
    std::fprintf(stderr, "\r                              \r");

    // Loss for move i is the value the mover gave up. score[i+1] is from the
    // *opponent's* point of view, so it is negated to bring both onto the
    // mover's scale.
    long cpSum[2] = {0, 0};
    double accSum[2] = {0.0, 0.0};
    int count[2] = {0, 0};
    int tally[2][6] = {};     // Blunder, Mistake, Inaccuracy, Good, Excellent, Best

    auto bucket = [](const char* label) {
        if (!std::strcmp(label, "Blunder"))    return 0;
        if (!std::strcmp(label, "Mistake"))    return 1;
        if (!std::strcmp(label, "Inaccuracy")) return 2;
        if (!std::strcmp(label, "Good"))       return 3;
        if (!std::strcmp(label, "Excellent"))  return 4;
        return 5;
    };

    std::vector<MoveNote> notes(game.moves.size());

    for (size_t i = 0; i < game.moves.size(); ++i) {
        const int played = -score[i + 1];
        int cpLoss = score[i] - played;
        if (cpLoss < 0) cpLoss = 0;              // nothing beats the best move

        double wpLoss = winPercent(score[i]) - winPercent(played);
        if (wpLoss < 0.0) wpLoss = 0.0;

        const bool white = (i % 2 == 0);
        const int s = white ? 0 : 1;
        cpSum[s] += cpLoss;
        accSum[s] += accuracy(wpLoss);
        ++count[s];
        const char* label = classify(wpLoss);
        ++tally[s][bucket(label)];

        {
            // The eval tag is written from White's point of view and in pawns,
            // which is the convention Lichess and friends parse. score[] is
            // from the side to move, so Black's needs negating.
            const int fromWhite = white ? score[i] : -score[i];
            char buf[160];
            const std::string bs = bestSan[i].empty() ? best[i] : bestSan[i];
            if (wpLoss >= 5.0 && !bs.empty() && bs != san[i]) {
                std::snprintf(buf, sizeof buf, "[%%eval %.2f] %s, -%.1f win%%; best %s",
                              fromWhite / 100.0, label, wpLoss, bs.c_str());
            } else {
                std::snprintf(buf, sizeof buf, "[%%eval %.2f]", fromWhite / 100.0);
            }
            notes[i].comment = buf;
            notes[i].nag = nagFor(wpLoss);
        }

        if (wpLoss >= 5.0) {
            const std::string b = bestSan[i].empty() ? best[i] : bestSan[i];
            std::printf("%3zu.%s%-8s %-11s -%4.1f win%%  (-%d cp, best %s, eval %+d)\n",
                        i / 2 + 1, white ? " " : "..", san[i].c_str(),
                        label, wpLoss, cpLoss, b.c_str(), score[i]);

            // Attribution, at the end of each line rather than at the move.
            // A tactic's point is that the material changes several plies
            // later, so comparing the static evaluation of the two *starting*
            // positions would explain a hanging queen as a change in centre
            // control.
            if (explain) {
                const Board wouldBe = atEndOfPv(position[i], pv[i]);
                const Board didGo   = atEndOfPv(position[i + 1], pv[i + 1]);
                const std::vector<TermDelta> d = termDiff(wouldBe, didGo);
                if (d.empty() || std::abs(d[0].delta) < 20) {
                    std::printf("        no term accounts for it — this engine's "
                                "evaluation does not see what was lost\n");
                } else {
                    std::printf("       ");
                    for (size_t k = 0; k < d.size() && k < 3; ++k) {
                        if (std::abs(d[k].delta) < 20) break;
                        std::printf(" %s %+d", d[k].name, d[k].delta);
                    }
                    std::printf("  (white's point of view, at the end of each line)\n");
                }
            }
        }
    }

    static const char* NAMES[6] = {"blunder", "mistake", "inaccuracy",
                                   "good", "excellent", "best"};
    std::printf("\n%-8s %-10s %-9s %s\n", "", "accuracy", "avg cp", "moves");
    for (int s = 0; s < 2; ++s) {
        if (!count[s]) continue;
        std::printf("%-8s %-10.1f %-9.1f %d\n", s == 0 ? "White" : "Black",
                    accSum[s] / count[s], (double)cpSum[s] / count[s], count[s]);
    }
    std::printf("\n%-8s", "");
    for (int b = 0; b < 6; ++b) std::printf(" %-11s", NAMES[b]);
    std::printf("\n");
    for (int s = 0; s < 2; ++s) {
        if (!count[s]) continue;
        std::printf("%-8s", s == 0 ? "White" : "Black");
        for (int b = 0; b < 6; ++b) std::printf(" %-11d", tally[s][b]);
        std::printf("\n");
    }
    if (!annotateOut.empty()) {
        std::ofstream f(annotateOut);
        if (!f.is_open()) {
            std::printf("\ncannot write %s\n", annotateOut.c_str());
            return 1;
        }
        f << toPgn(game.moves, game.tags, notes);
        std::printf("\nannotated PGN written to %s\n", annotateOut.c_str());
    }
    return 0;
}
