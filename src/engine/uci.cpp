#include "uci.hpp"

#include "board.hpp"
#include "move_lookup.hpp"
#include "movegen.hpp"
#include "search.hpp"
#include "transposition_table.hpp"

#include <atomic>
#include <cctype>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

// --- Engine state, one instance per process ---
Board g_board;
std::unique_ptr<TranspositionTable> g_tt;
std::atomic<bool> g_stop{false};
std::thread g_searchThread;
int g_hashMb = 256;


// Resolve a UCI move string against the legal move list. Matching against
// generated moves rather than parsing into a Move directly is what makes
// castling, en passant and promotion fall out correctly: the generator already
// knows which flag each move carries.
bool parseUciMove(const Board& board, const std::string& text, Move& out) {
    MoveList legal = generateLegalMoves(board, board.activeColor);
    for (const Move& m : legal) {
        if (toUciMove(m) == text) { out = m; return true; }
    }
    return false;
}

// Positions this game has already visited, rebuilt from every "position"
// command. A GUI re-sends the whole move list each move rather than telling the
// engine what was played, so this is the only place the engine ever learns that
// its position has a past — which is why the search was blind to repetitions
// until it was collected here (BUGS.md 1).
std::vector<uint64_t> g_gameHistory;

// How many plies the game has actually been played for, counted as `position`
// replays them.
//
// The time manager needs it and had no way to know it. A UCI engine is handed a
// clock and a position, never a move number, so an allocation of the form
// `remaining / N` with N constant treats move 3 and move 53 identically — and
// since `remaining` shrinks, the allocation decays geometrically. Measured on
// the first 900+10 game after the soft/hard split landed: 44 seconds a move over
// the first ten moves, 4.3 over the last twenty, and the endgame that decided
// the game played at one second a move.
//
// g_gameHistory cannot supply this: it is deliberately cleared at every capture
// and pawn move, because no position across an irreversible move can recur.
int g_pliesPlayed = 0;

// "position [startpos | fen <6 fields>] [moves <m1> <m2> ...]"
void handlePosition(std::istringstream& is) {
    std::string token;
    is >> token;

    // Rebuilt from scratch on every command. A "position" line is a complete
    // statement of the game, not a delta, so accumulating across calls would
    // add the same positions again on every move.
    g_gameHistory.clear();
    g_pliesPlayed = 0;

    if (token == "startpos") {
        g_board.setFromFEN(Board::INITIAL_FEN);
        is >> token; // consume "moves" if present
    } else if (token == "fen") {
        std::string fen, part;
        for (int i = 0; i < 6 && (is >> part); ++i) {
            if (part == "moves") { token = part; break; }
            fen += (fen.empty() ? "" : " ") + part;
        }
        if (!g_board.setFromFEN(fen)) {
            std::cout << "info string bad fen, position unchanged" << std::endl;
            return;
        }
        if (token != "moves") is >> token;
    } else {
        return;
    }

    if (token != "moves") return;
    std::string moveText;
    while (is >> moveText) {
        Move m;
        if (!parseUciMove(g_board, moveText, m)) {
            std::cout << "info string illegal move in position command: "
                      << moveText << std::endl;
            return;
        }
        const uint64_t before = g_board.getHash();
        g_board.makeMove(m);
        recordGamePosition(g_gameHistory, before, g_board);
        ++g_pliesPlayed;
    }
}

// UCI reports mates as a distance in moves, not a centipawn value.
void printScore(int score) {
    if (std::abs(score) > SEARCH_MATE_SCORE - 1000) {
        int pliesToMate = SEARCH_MATE_SCORE - std::abs(score);
        int movesToMate = (pliesToMate + 1) / 2;
        std::cout << "score mate " << (score > 0 ? movesToMate : -movesToMate);
    } else {
        std::cout << "score cp " << score;
    }
}

void onSearchInfo(int depth, int score, uint64_t nodes, long elapsedMs,
                  const Move& best) {
    std::cout << "info depth " << depth << " ";
    printScore(score);
    std::cout << " nodes " << nodes
              << " nps " << (elapsedMs > 0 ? (nodes * 1000ULL / (uint64_t)elapsedMs) : nodes)
              << " time " << elapsedMs
              << " pv " << toUciMove(best) << std::endl;
}

// Turn "go" arguments into a budget. A clock (wtime/btime) is divided rather
// than spent: with no movestogo, assume the game has a reasonable number of
// moves left, and keep a small reserve so a slow iteration cannot forfeit.
SearchLimits parseGo(std::istringstream& is) {
    SearchLimits limits;
    long wtime = 0, btime = 0, winc = 0, binc = 0, movetime = 0;
    int movestogo = 0, depth = 0;
    uint64_t nodes = 0;
    bool infinite = false;

    std::string token;
    while (is >> token) {
        if      (token == "wtime")     is >> wtime;
        else if (token == "btime")     is >> btime;
        else if (token == "winc")      is >> winc;
        else if (token == "binc")      is >> binc;
        else if (token == "movestogo") is >> movestogo;
        else if (token == "movetime")  is >> movetime;
        else if (token == "depth")     is >> depth;
        else if (token == "nodes")     is >> nodes;
        else if (token == "infinite")  infinite = true;
    }

    if (depth > 0) limits.maxDepth = depth;

    // A node budget is independent of the clock: whichever binds first ends the
    // search. It is here because it is what makes an *external* match
    // reproducible — a gate driving two binaries over UCI cannot use
    // tests/match's in-process node budget, and a millisecond is worth whatever
    // the machine had spare. Without this, every cross-binary gate would have
    // to be a timed one.
    if (nodes > 0) limits.maxNodes = nodes;

    if (infinite) {
        limits.moveTimeMs = 0;              // ends only on "stop"
        if (depth <= 0) limits.maxDepth = 64;
    } else if (movetime > 0) {
        limits.moveTimeMs = movetime;
    } else if (wtime > 0 || btime > 0) {
        bool white = (g_board.activeColor == COLOR_WHITE);
        long remaining = white ? wtime : btime;
        long increment = white ? winc : binc;
        // How many more moves to plan for.
        //
        // A constant divisor treats move 3 and move 53 alike, and since
        // `remaining` shrinks the allocation decays geometrically — the clock
        // gets spent where it matters least. The first 900+10 game after the
        // soft/hard split spent 44 s a move over its first ten moves and 4.3 s
        // over its last twenty, and drew an endgame it played at one second a
        // move.
        //
        // timeAlloc counts down instead: plan for a game of about eighty moves,
        // never assuming fewer than thirty left. The floor is what stops the
        // allocation collapsing in a long game — with an increment there is
        // always another move, so "moves remaining" must never reach zero.
        //
        // The increment is income, not savings. Spending it in full holds the
        // clock level; halving it gives away half of that for nothing.
        int moves;
        if (g_searchOptions.timeAlloc) {
            moves = (movestogo > 0) ? movestogo
                                    : std::max(80 - g_pliesPlayed / 2, 30);
        } else {
            moves = (movestogo > 0) ? movestogo : 30;
        }
        long budget = g_searchOptions.timeAlloc
                          ? remaining / moves + increment
                          : remaining / moves + increment / 2;
        // Never commit more than a fraction of what is left: an overrun here is
        // a forfeit, and losing on time beats any depth gained.
        long cap = remaining / 4;
        if (budget > cap) budget = cap;
        if (budget < 10) budget = 10;
        limits.moveTimeMs = budget;

        // Spend the budget instead of merely allocating it (BUGS.md 11).
        //
        // `budget` is a target, not a boundary: the cost of passing it slightly
        // is a few seconds off a clock with hundreds on it, while the cost of
        // stopping short of it is a whole iteration's worth of depth, thrown
        // away every move. Only overrunning the *clock* is fatal, and that is
        // what `cap` guards.
        //
        // So the search is given room to finish an iteration it has started —
        // three times the target — bounded by the same quarter-of-the-clock cap
        // the target itself respects. It rarely uses it: the soft limit still
        // governs whether an iteration begins, and this only decides what
        // happens to one already running.
        if (g_searchOptions.softTime) {
            // Bounded absolutely as well as proportionally, which is the
            // repair for the forfeit on 2026-08-17.
            //
            // `budget * 3` alone is a *ratio*, and a ratio means different
            // things at different clocks: 2 seconds of overshoot at
            // --tc 30+0.33, where it was gated, and seventy at 900+10, where
            // the engine took them and lost on time. One increment is the
            // bound that does travel -- overshooting by it is self-financing,
            // because the increment arrives on the next move, so a move that
            // runs one increment long costs the clock nothing over the game.
            //
            // The multiple is kept as well, for the case an increment is zero
            // or tiny: with no increment the bound would otherwise be the
            // budget itself and the soft/hard split would do nothing at all.
            long hard = budget + increment;
            if (hard > budget * 3) hard = budget * 3;
            if (hard > cap) hard = cap;
            if (hard < budget) hard = budget;
            limits.hardTimeMs = hard;
        }
    }
    // Nothing specified: fall back to a depth-limited search rather than
    // thinking forever. A node budget counts as something specified.
    if (limits.moveTimeMs == 0 && !infinite && depth <= 0 && nodes == 0)
        limits.maxDepth = 8;

    return limits;
}

void stopSearch() {
    g_stop = true;
    if (g_searchThread.joinable()) g_searchThread.join();
}

void handleGo(std::istringstream& is) {
    stopSearch();  // a previous search must be finished before starting another

    SearchLimits limits = parseGo(is);
    g_stop = false;

    // Copy the position here, on the command thread, rather than inside the
    // search. Every command that mutates g_board stops the search first, so
    // this is already safe — but copying at the point of the decision means it
    // cannot become unsafe if that ordering is ever relaxed.
    Board searchBoard = g_board.copyForSearch();

    // History copied into the thread for the same reason the board is: the next
    // "position" command rebuilds it, and it must not do so under a live search.
    std::vector<uint64_t> history = g_gameHistory;

    g_searchThread = std::thread([limits, searchBoard, history]() mutable {
        g_searchOptions.quiet = true;   // the search's own logging is not UCI
        g_searchInfo = onSearchInfo;
        Move best = findBestMoveIterativeDeepening(searchBoard, limits, g_stop, *g_tt, history);
        g_searchInfo = nullptr;
        std::cout << "bestmove " << toUciMove(best) << std::endl;
    });
}

void handleSetOption(std::istringstream& is) {
    // "setoption name <id> [value <x>]", where <id> may contain spaces.
    std::string token, name, value;
    is >> token;                       // "name"
    while (is >> token && token != "value") {
        name += (name.empty() ? "" : " ") + token;
    }
    while (is >> token) {
        value += (value.empty() ? "" : " ") + token;
    }

    auto isTrue = [](const std::string& v) { return v == "true" || v == "1"; };

    if (name == "Hash") {
        int mb = std::atoi(value.c_str());
        if (mb >= 1) {
            g_hashMb = mb;
            g_tt = std::make_unique<TranspositionTable>((size_t)g_hashMb);
        }
        return;
    }

    // Search heuristics go through the same named lookup the match harness
    // uses, so a feature added there is switchable over UCI without a second
    // edit here. UCI names are capitalised; the lookup keys are not.
    std::string key;
    for (char c : name) key += (char)std::tolower((unsigned char)c);
    if (!setSearchOption(g_searchOptions, key, isTrue(value))) {
        std::cout << "info string unknown option: " << name << std::endl;
    }
}

}  // namespace

int uciLoop() {
    initMoveLookupTables();
    g_tt = std::make_unique<TranspositionTable>((size_t)g_hashMb);
    g_board.setFromFEN(Board::INITIAL_FEN);

    // GUIs expect a response before the next command is sent, and a buffered
    // stream that only flushes when full will deadlock against that.
    std::cout.setf(std::ios::unitbuf);

    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream is(line);
        std::string command;
        is >> command;

        if (command == "uci") {
            std::cout << "id name ChessBot 1.1\n";
            std::cout << "id author Dheirav\n";
            // The search heuristics are exposed so that A/B testing can run
            // through standard tooling instead of tests/match.cpp.
            //
            // The advertised defaults are read out of a default-constructed
            // SearchOptions rather than written here, because the hand-copied
            // version of this list went stale the moment a gate flipped one:
            // it still said SeeOrdering was off after the engine shipped it on,
            // which tells a GUI the opposite of what the engine does.
            std::cout << "option name Hash type spin default 256 min 1 max 4096\n";
            const SearchOptions defaults;
            for (size_t i = 0; i < SEARCH_OPTION_COUNT; ++i) {
                std::cout << "option name " << SEARCH_OPTIONS[i].uciName
                          << " type check default "
                          << (defaults.*(SEARCH_OPTIONS[i].field) ? "true" : "false")
                          << "\n";
            }
            std::cout << "uciok" << std::endl;
        } else if (command == "isready") {
            std::cout << "readyok" << std::endl;
        } else if (command == "ucinewgame") {
            stopSearch();
            g_tt->clear();
            g_board.setFromFEN(Board::INITIAL_FEN);
            g_gameHistory.clear();
            g_pliesPlayed = 0;
        } else if (command == "position") {
            stopSearch();
            handlePosition(is);
        } else if (command == "go") {
            handleGo(is);
        } else if (command == "stop") {
            stopSearch();
        } else if (command == "setoption") {
            stopSearch();
            handleSetOption(is);
        } else if (command == "quit") {
            stopSearch();
            break;
        }
        // Unknown commands are ignored, as the protocol requires.
    }

    stopSearch();
    return 0;
}
