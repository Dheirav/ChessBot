#include "uci.hpp"

#include "board.hpp"
#include "move_lookup.hpp"
#include "movegen.hpp"
#include "search.hpp"
#include "transposition_table.hpp"

#include <atomic>
#include <cctype>
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

// UCI move format is plain long algebraic: "e2e4", "e7e8q". Move::toString()
// writes promotions as "e7e8=Q", which is for humans and is baked into the
// bench signature, so the UCI spelling lives here instead of changing it.
std::string toUci(const Move& m) {
    if (m.from < 0 || m.to < 0) return "0000";
    std::string s;
    s += (char)('a' + (m.from % 8));
    s += (char)('8' - (m.from / 8));
    s += (char)('a' + (m.to % 8));
    s += (char)('8' - (m.to / 8));
    if (m.flag == PROMOTION) {
        switch (m.promotionPiece.type()) {
            case QUEEN:  s += 'q'; break;
            case ROOK:   s += 'r'; break;
            case BISHOP: s += 'b'; break;
            case KNIGHT: s += 'n'; break;
            default: break;
        }
    }
    return s;
}

// Resolve a UCI move string against the legal move list. Matching against
// generated moves rather than parsing into a Move directly is what makes
// castling, en passant and promotion fall out correctly: the generator already
// knows which flag each move carries.
bool parseUciMove(const Board& board, const std::string& text, Move& out) {
    MoveList legal = generateLegalMoves(board, board.activeColor);
    for (const Move& m : legal) {
        if (toUci(m) == text) { out = m; return true; }
    }
    return false;
}

// "position [startpos | fen <6 fields>] [moves <m1> <m2> ...]"
void handlePosition(std::istringstream& is) {
    std::string token;
    is >> token;

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
        g_board.makeMove(m);
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
              << " pv " << toUci(best) << std::endl;
}

// Turn "go" arguments into a budget. A clock (wtime/btime) is divided rather
// than spent: with no movestogo, assume the game has a reasonable number of
// moves left, and keep a small reserve so a slow iteration cannot forfeit.
SearchLimits parseGo(std::istringstream& is) {
    SearchLimits limits;
    long wtime = 0, btime = 0, winc = 0, binc = 0, movetime = 0;
    int movestogo = 0, depth = 0;
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
        else if (token == "infinite")  infinite = true;
    }

    if (depth > 0) limits.maxDepth = depth;

    if (infinite) {
        limits.moveTimeMs = 0;              // ends only on "stop"
        if (depth <= 0) limits.maxDepth = 64;
    } else if (movetime > 0) {
        limits.moveTimeMs = movetime;
    } else if (wtime > 0 || btime > 0) {
        bool white = (g_board.activeColor == COLOR_WHITE);
        long remaining = white ? wtime : btime;
        long increment = white ? winc : binc;
        int moves = (movestogo > 0) ? movestogo : 30;
        long budget = remaining / moves + increment / 2;
        // Never commit more than a fraction of what is left: an overrun here is
        // a forfeit, and losing on time beats any depth gained.
        long cap = remaining / 4;
        if (budget > cap) budget = cap;
        if (budget < 10) budget = 10;
        limits.moveTimeMs = budget;
    }
    // Nothing specified: fall back to a depth-limited search rather than
    // thinking forever.
    if (limits.moveTimeMs == 0 && !infinite && depth <= 0) limits.maxDepth = 8;

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

    g_searchThread = std::thread([limits, searchBoard]() mutable {
        g_searchOptions.quiet = true;   // the search's own logging is not UCI
        g_searchInfo = onSearchInfo;
        Move best = findBestMoveIterativeDeepening(searchBoard, limits, g_stop, *g_tt);
        g_searchInfo = nullptr;
        std::cout << "bestmove " << toUci(best) << std::endl;
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
            // through standard tooling instead of tests/match.cpp. Defaults
            // here must match SearchOptions: the two SEE uses are off until
            // their gates pass (PLAN.md 3.2).
            std::cout << "option name Hash type spin default 256 min 1 max 4096\n";
            std::cout << "option name NullMove type check default true\n";
            std::cout << "option name LMR type check default true\n";
            std::cout << "option name Aspiration type check default true\n";
            std::cout << "option name SeeOrdering type check default false\n";
            std::cout << "option name SeePruning type check default false\n";
            std::cout << "option name TtAging type check default true\n";
            std::cout << "uciok" << std::endl;
        } else if (command == "isready") {
            std::cout << "readyok" << std::endl;
        } else if (command == "ucinewgame") {
            stopSearch();
            g_tt->clear();
            g_board.setFromFEN(Board::INITIAL_FEN);
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
