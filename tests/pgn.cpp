// PGN export: SAN spelling, move numbering, and the whole document.
//
// SAN is where an export quietly goes wrong: the notation is unambiguous only
// if the writer does the disambiguation work, and a viewer given "Nf3" when it
// needed "Nbd2" rejects the game or, worse, replays a different one. Each case
// below is one of the rules that requires the board rather than the move.
#include "engine/board.hpp"
#include "engine/fen.hpp"
#include "engine/move_lookup.hpp"
#include "engine/movegen.hpp"
#include "engine/pgn.hpp"

#include <iostream>
#include <string>
#include <vector>

static int failures = 0;

static void check(bool ok, const std::string& what) {
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (!ok) ++failures;
}

static void checkEq(const std::string& got, const std::string& want,
                    const std::string& what) {
    check(got == want, what + " -> \"" + got + "\"" +
                       (got == want ? "" : " (wanted \"" + want + "\")"));
}

// Resolve a long-algebraic move ("e2e4", "e7e8q") against the legal moves, the
// way the UCI layer does, so the flags are the generator's rather than guessed.
static Move find(const Board& board, const std::string& uci) {
    MoveList legal = generateLegalMoves(board, board.activeColor);
    for (const Move& m : legal) {
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
        if (s == uci) return m;
    }
    std::cerr << "no legal move " << uci << " in " << board.getFEN() << std::endl;
    std::exit(1);
}

static Board fromFEN(const std::string& fen) {
    Board b;
    if (!b.setFromFEN(fen)) {
        std::cerr << "bad FEN: " << fen << std::endl;
        std::exit(1);
    }
    return b;
}

// SAN for one move played in one position.
static std::string san(const std::string& fen, const std::string& uci) {
    Board b = fromFEN(fen);
    return toSan(b, find(b, uci));
}

int main() {
    initMoveLookupTables();

    // --- the plain cases ----------------------------------------------------
    const std::string start = Board::INITIAL_FEN;
    checkEq(san(start, "e2e4"), "e4", "a pawn push is just its destination");
    checkEq(san(start, "g1f3"), "Nf3", "a piece move names the piece");

    // --- captures -----------------------------------------------------------
    checkEq(san("rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2", "e4d5"),
            "exd5", "a capturing pawn is named by the file it left");
    checkEq(san("rnbqkbnr/ppp1pppp/8/3P4/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 2", "d8d5"),
            "Qxd5", "a capturing piece takes an x");

    // --- en passant: a capture with an empty destination square -------------
    checkEq(san("rnbqkbnr/pppp1ppp/8/3Pp3/8/8/PPP1PPPP/RNBQKBNR w KQkq e6 0 3", "d5e6"),
            "dxe6", "en passant is spelled as a capture");

    // --- castling -----------------------------------------------------------
    checkEq(san("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1", "e1g1"),
            "O-O", "kingside castling");
    checkEq(san("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1", "e1c1"),
            "O-O-O", "queenside castling");

    // --- promotion ----------------------------------------------------------
    checkEq(san("6r1/4P3/8/8/8/8/8/K6k w - - 0 1", "e7e8q"),
            "e8=Q", "promotion names the piece chosen");
    checkEq(san("6r1/4P3/8/8/8/8/8/K6k w - - 0 1", "e7e8n"),
            "e8=N", "underpromotion too");
    // Pawn e7 takes the rook on f8 and promotes, checking the king on h8 along
    // the eighth rank.
    checkEq(san("5r1k/4P3/8/8/8/8/8/K7 w - - 0 1", "e7f8q"),
            "exf8=Q+", "a promotion that captures and checks");

    // --- disambiguation: the reason SAN needs the board ---------------------
    // Knights on b1 and f3 both reach d2: the file tells them apart.
    checkEq(san("4k3/8/8/8/8/5N2/8/1N2K3 w - - 0 1", "b1d2"),
            "Nbd2", "two knights disambiguate by file");
    // Rooks on a1 and a8 both reach a4: same file, so the rank does it.
    checkEq(san("R7/8/8/3k4/8/8/8/R3K3 w - - 0 1", "a1a4"),
            "R1a4", "same-file rooks disambiguate by rank");
    // Queens on a2, d2 and d8 all bear on d5: the mover on d2 shares its rank
    // with one and its file with the other, so neither half is enough.
    checkEq(san("3Q4/8/8/8/8/8/Q2Q4/4K2k w - - 0 1", "d2d5"),
            "Qd2d5+", "when file and rank both clash, the square is spelled out");
    // And a piece with no rival for the square is named plainly.
    checkEq(san("4k3/8/8/8/8/8/8/1N2K3 w - - 0 1", "b1d2"),
            "Nd2", "a lone knight is not disambiguated");

    // --- check and mate -----------------------------------------------------
    checkEq(san("4k3/8/8/8/8/8/8/4K2R w K - 0 1", "h1h8"), "Rh8+", "check gets a +");
    checkEq(san("4k3/R7/8/8/8/8/8/4K2R w K - 0 1", "h1h8"), "Rh8#", "mate gets a #");

    // --- a whole document ---------------------------------------------------
    {
        // Scholar's mate, which exercises numbering, a capture, and the mate.
        const char* line[] = {"e2e4", "e7e5", "f1c4", "b8c6", "d1h5", "g8f6", "h5f7"};
        Board b;
        std::vector<Move> moves;
        for (const char* uci : line) {
            Move m = find(b, uci);
            moves.push_back(m);
            b.makeMove(m);
        }

        PgnTags tags;
        tags.event = "Test";
        tags.date = "2026.08.11";
        tags.white = "ChessBot";
        tags.black = "Opponent";
        tags.result = "1-0";
        const std::string pgn = toPgn(moves, tags);

        check(pgn.find("[White \"ChessBot\"]") != std::string::npos, "the tag roster is written");
        check(pgn.find("[Result \"1-0\"]") != std::string::npos, "the result tag is written");
        check(pgn.find("1. e4 e5 2. Bc4 Nc6 3. Qh5 Nf6 4. Qxf7# 1-0") != std::string::npos,
              "the movetext reads as SAN, numbered, ending in the result");
        check(pgn.find("[FEN") == std::string::npos,
              "a game from the opening position carries no FEN tag");

        // Every line a PGN reader will see must fit in 80 columns.
        size_t start = 0;
        bool wrapped = true;
        while (start < pgn.size()) {
            const size_t nl = pgn.find('\n', start);
            const size_t len = (nl == std::string::npos ? pgn.size() : nl) - start;
            if (len > 80) wrapped = false;
            if (nl == std::string::npos) break;
            start = nl + 1;
        }
        check(wrapped, "no line exceeds 80 columns");
    }

    // --- a game that did not start at move 1 --------------------------------
    {
        // Black to move on move 23: the movetext has to open with the ellipsis
        // and keep counting from there, or every move is attributed to White.
        const std::string fen = "4k3/8/8/8/8/8/4P3/4K3 b - - 0 23";
        Board b = fromFEN(fen);
        std::vector<Move> moves;
        for (const char* uci : {"e8d8", "e2e4"}) {
            Move m = find(b, uci);
            moves.push_back(m);
            b.makeMove(m);
        }
        PgnTags tags;
        tags.startFen = fen;
        tags.result = "*";
        const std::string pgn = toPgn(moves, tags);

        check(pgn.find("[SetUp \"1\"]") != std::string::npos, "a set-up game says so");
        check(pgn.find("[FEN \"" + fen + "\"]") != std::string::npos, "and carries its FEN");
        check(pgn.find("23... Kd8 24. e4 *") != std::string::npos,
              "numbering follows the board, and Black-to-move opens with ...");
    }

    std::cout << (failures ? "\nFAILED: " + std::to_string(failures) + " checks\n"
                           : "\nPASSED: all PGN checks\n");
    return failures ? 1 : 0;
}
