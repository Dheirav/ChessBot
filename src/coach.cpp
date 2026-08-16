#include "coach.hpp"

#include "engine/board.hpp"
#include "engine/evaluation.hpp"
#include "engine/legal_move_validator.hpp"
#include "engine/move_lookup.hpp"
#include "engine/movegen.hpp"
#include "engine/pgn.hpp"
#include "engine/search.hpp"
#include "engine/transposition_table.hpp"

#include <atomic>
#include <unistd.h>
#include <chrono>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

// One position in the game, kept so `undo` can step back.
//
// Storing whole boards rather than the moves and unmaking them: a coached game
// is tens of moves long, a Board is small, and unmakeMove needs the UndoInfo
// that makeMove returned, which is exactly the bookkeeping this avoids. The
// search is where that trade matters, and this is not the search.
struct Ply {
    Board board;                     // position *before* the move
    std::vector<uint64_t> history;   // repetition history before the move
    std::string san;                 // the move, as it was played
};

const char* PIECE_GLYPH[3][7] = {
    {" ", " ", " ", " ", " ", " ", " "},
    {" ", "K", "P", "N", "B", "R", "Q"},   // white
    {" ", "k", "p", "n", "b", "r", "q"},   // black
};

void printBoard(const Board& board, bool whiteAtBottom) {
    std::cout << "\n";
    for (int r = 0; r < 8; ++r) {
        // Board index 0 is a8, so rank 8 is row 0. Flipping for a Black player
        // means walking both axes backwards, which is what makes the board read
        // from the side it is being played from.
        int row = whiteAtBottom ? r : 7 - r;
        std::cout << "  " << (whiteAtBottom ? 8 - r : r + 1) << " ";
        for (int f = 0; f < 8; ++f) {
            int file = whiteAtBottom ? f : 7 - f;
            const Piece& p = board.squares[row * 8 + file];
            std::cout << " " << PIECE_GLYPH[p.color()][p.type()];
        }
        std::cout << "\n";
    }
    std::cout << "    ";
    for (int f = 0; f < 8; ++f) std::cout << " " << (char)('a' + (whiteAtBottom ? f : 7 - f));
    std::cout << "\n\n";
}

// Centipawns as a person reads them, always from the side being advised.
//
// evaluate() is white-perspective, which is the convention for published
// analysis and the wrong one here: someone playing Black wants "+0.4" to mean
// they are better, not worse.
std::string formatScore(int cp, bool humanIsWhite) {
    const int shown = humanIsWhite ? cp : -cp;
    std::ostringstream out;
    if (std::abs(shown) > SEARCH_MATE_SCORE - 1000) {
        const int plies = SEARCH_MATE_SCORE - std::abs(shown);
        const int moves = (plies + 1) / 2;
        out << (shown > 0 ? "mate in " : "mated in ") << moves;
    } else {
        out << (shown >= 0 ? "+" : "") << (shown / 100.0);
    }
    return out.str();
}

// "checkmate", "stalemate", "" — the game is over only for the first two.
const char* gameOver(Board& board) {
    MoveList legal = generateLegalMoves(board, board.activeColor);
    if (!legal.empty()) return "";
    return LegalMoveValidator::isInCheck(board, board.activeColor) ? "checkmate"
                                                                  : "stalemate";
}

void printHelp() {
    std::cout <<
        "  Type a move in the notation you would write it: e4, Nf3, O-O, exd5,\n"
        "  e8=Q. Long form works too (e2e4). Enter alone plays the suggestion.\n"
        "\n"
        "    board   redraw       fen     print the position as FEN\n"
        "    undo    take back one move   eval    static evaluation\n"
        "    moves   list legal moves     help    this\n"
        "    quit\n\n";
}

}  // namespace

int coachLoop(bool humanIsWhite, long thinkMs) {
    initMoveLookupTables();

    // The search narrates itself to stdout by default, which is right for the
    // GUI's side panel and for a bench run, and ruinous here: this mode *is*
    // stdout, and forty lines of "Depth 7 complete" between the question and
    // the answer is not a conversation.
    g_searchOptions.quiet = true;

    Board board;
    std::vector<uint64_t> history;
    std::vector<Ply> played;
    TranspositionTable tt(64);
    std::atomic<bool> never{false};

    const PieceColor humanSide = humanIsWhite ? COLOR_WHITE : COLOR_BLACK;

    std::cout << "\n=== ChessBot — play-along ===\n"
              << "You are " << (humanIsWhite ? "White" : "Black")
              << ". Thinking " << (thinkMs / 1000.0) << "s per suggestion.\n\n";
    printHelp();
    printBoard(board, humanIsWhite);

    std::string suggestion;   // SAN of the last suggestion, for a bare Enter

    while (true) {
        const char* over = gameOver(board);
        if (*over) {
            const bool humanMated = (board.activeColor == humanSide);
            std::cout << "  " << over;
            if (std::string(over) == "checkmate")
                std::cout << " — " << (humanMated ? "you lose." : "you win!");
            std::cout << "\n";
            return 0;
        }

        const bool humanToMove = (board.activeColor == humanSide);

        if (humanToMove) {
            // Suggest before asking, so the prompt is an answer to something.
            // Erase the "thinking" line only when something is watching it.
            // Piped into a file or a pipe, a carriage return is a character
            // rather than a cursor movement, and the two lines run together.
            const bool tty = isatty(fileno(stdout));
            std::cout << "  thinking..." << std::flush;
            const auto t0 = std::chrono::steady_clock::now();
            SearchLimits limits;
            limits.moveTimeMs = thinkMs;
            Board searchBoard = board.copyForSearch();
            Move best = findBestMoveIterativeDeepening(searchBoard, limits, never, tt, history);
            const long ms = (long)std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count();

            if (best.from < 0) {
                std::cout << (tty ? "\r                \r" : "\n") << "  no move found.\n";
                suggestion.clear();
            } else {
                suggestion = toSan(board, best);
                // Score the position the suggestion leads to, from White's
                // point of view, then flip it for display.
                Board after = board.copyForSearch();
                after.makeMove(best);
                std::cout << (tty ? "\r                \r" : "\n")
                          << "  >>> play  " << suggestion
                          << "   (" << formatScore(evaluate(after), humanIsWhite)
                          << ", " << (ms / 1000.0) << "s)\n";
            }
        }

        std::cout << (humanToMove ? "  you> " : "  opponent> ") << std::flush;
        std::string line;
        if (!std::getline(std::cin, line)) { std::cout << "\n"; return 0; }

        // Trim
        const size_t b = line.find_first_not_of(" \t\r\n");
        const size_t e = line.find_last_not_of(" \t\r\n");
        line = (b == std::string::npos) ? "" : line.substr(b, e - b + 1);

        if (line == "quit" || line == "exit") return 0;
        if (line == "help") { printHelp(); continue; }
        if (line == "board") { printBoard(board, humanIsWhite); continue; }
        if (line == "eval") {
            std::cout << "  static: " << formatScore(evaluate(board), humanIsWhite) << "\n";
            continue;
        }
        if (line == "fen") {
            // No FEN writer exists in the engine — parseFEN is one-way — so say
            // so rather than print something that is not a FEN.
            std::cout << "  not available: the engine parses FEN but does not write it.\n";
            continue;
        }
        if (line == "moves") {
            MoveList legal = generateLegalMoves(board, board.activeColor);
            std::cout << " ";
            for (const Move& m : legal) std::cout << " " << toSan(board, m);
            std::cout << "\n";
            continue;
        }
        if (line == "undo") {
            if (played.empty()) { std::cout << "  nothing to undo.\n"; continue; }
            board = played.back().board;
            history = played.back().history;
            std::cout << "  took back " << played.back().san << ".\n";
            played.pop_back();
            printBoard(board, humanIsWhite);
            continue;
        }

        // A bare Enter on your own turn plays the suggestion. On the
        // opponent's it means nothing, because there is nothing to assume.
        if (line.empty()) {
            if (humanToMove && !suggestion.empty()) line = suggestion;
            else continue;
        }

        Move m = fromSan(board, line);
        if (m.from < 0) {
            // fromSan matches against the legal move list, so a rejection means
            // illegal or misspelt and cannot mean "legal but unrecognised".
            std::cout << "  not a legal move here: \"" << line
                      << "\"  (type `moves` to see them)\n";
            continue;
        }

        played.push_back({board, history, toSan(board, m)});
        const uint64_t before = board.getHash();
        board.makeMove(m);
        recordGamePosition(history, before, board);
        printBoard(board, humanIsWhite);
    }
}
