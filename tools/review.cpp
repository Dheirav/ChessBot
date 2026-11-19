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
#include "engine/see.hpp"
#include "engine/uci_engine.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <unordered_map>
#include <iterator>
#include <cstdio>
#include <cstdlib>
#include <cctype>
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

// --- Chess.com-style classes ------------------------------------------------
//
// `label` above is what this tool has always said, and it stays as it is:
// tools/archive-profile.py and the archive index count Blunder, Mistake and
// Inaccuracy from it, and those counts are a series compared across builds. A
// relabelling that moved moves between buckets would break the one baseline
// this project has. `cls` is the second opinion, in Chess.com's vocabulary, and
// it is a separate field for that reason.
//
// The loss-based classes use Chess.com's published expected-points limits
// (Excellent to 0.02, Good to 0.05, Inaccuracy to 0.10, Mistake to 0.20,
// Blunder above) read as win-probability points. Chess.com's win model depends
// on the player's rating and this one does not, so the boundaries agree and the
// moves landing on them will not always.
//
// Brilliant, Great, Miss and Book have no published numeric rule, only a
// description. What follows is this tool's reading of those descriptions and has
// not been validated against Chess.com's own output, so it is stated here
// instead of implied.

// How many classes there are, in the order the summary table shows them.
// Chess.com's order, which the report page also uses.
const char* const CLS_ORDER[10] = {"Brilliant", "Great", "Book", "Best", "Excellent",
                                   "Good", "Inaccuracy", "Mistake", "Miss", "Blunder"};

int pieceValue(PieceType t) {
    switch (t) {
        case PAWN:   return 1;
        case KNIGHT: case BISHOP: return 3;
        case ROOK:   return 5;
        case QUEEN:  return 9;
        default:     return 0;
    }
}

// Material from `side`'s point of view: its pieces minus the opponent's.
int materialBalance(const Board& b, PieceColor side) {
    int total = 0;
    for (int sq = 0; sq < 64; ++sq) {
        const Piece p = b.squares[sq];
        if (p.color() == COLOR_NONE) continue;
        total += (p.color() == side ? 1 : -1) * pieceValue(p.type());
    }
    return total;
}

// A position's identity for the opening book: placement, side to move and
// castling rights, and nothing else. The en passant square is dropped on
// purpose. It is recorded after every double push whether or not a capture is
// possible, so keeping it would make the same position reached by a different
// move order look like a different one, and transpositions are the whole reason
// to key by position and not by move list.
std::string bookKey(const Board& b) {
    const std::string fen = b.getFEN();
    size_t end = 0;
    for (int fields = 0; end < fen.size(); ++end)
        if (fen[end] == ' ' && ++fields == 3) break;
    return fen.substr(0, end);
}

// Opening theory, from the Lichess chess-openings list (CC0): ECO code, name
// and the move list that defines each. A position is "book" if some line in
// the list passes through it, and carries a name if some line ends there.
//
// Chess.com's Book comes from its own explorer database, which is not
// available. This is far smaller, so a sound but rarely played line will not be
// recognised, and the review says so by calling it a move and not a book move.
class OpeningBook {
public:
    bool load(const std::string& path) {
        std::ifstream f(path);
        if (!f) return false;
        std::string line;
        std::getline(f, line);                       // header
        while (std::getline(f, line)) {
            const size_t t1 = line.find('\t');
            const size_t t2 = (t1 == std::string::npos) ? t1 : line.find('\t', t1 + 1);
            if (t2 == std::string::npos) continue;
            const std::string eco = line.substr(0, t1);
            const std::string name = line.substr(t1 + 1, t2 - t1 - 1);
            std::istringstream moves(line.substr(t2 + 1));
            Board b;
            std::string tok, key;
            bool ok = true;
            while (ok && moves >> tok) {
                if (std::isdigit((unsigned char)tok[0]) && tok.find('.') != std::string::npos
                    && tok.find_first_not_of("0123456789.") == std::string::npos) continue;
                const Move m = fromSan(b, tok);
                if (m.from < 0) { ok = false; break; }
                b.makeMove(m);
                key = bookKey(b);
                known_.emplace(key, std::string());
            }
            // Ending a line names the position. The first name wins, which is the
            // shortest line sorted first in each file and so the most general.
            if (ok && !key.empty()) {
                std::string& slot = known_[key];
                if (slot.empty()) slot = eco + " " + name;
            }
        }
        return !known_.empty();
    }
    bool contains(const Board& b) const { return known_.count(bookKey(b)) != 0; }
    // Empty when the position is in book without being the end of a named line.
    std::string name(const Board& b) const {
        const auto it = known_.find(bookKey(b));
        return it == known_.end() ? std::string() : it->second;
    }
    bool empty() const { return known_.empty(); }
private:
    std::unordered_map<std::string, std::string> known_;
};

// What a move gives up in material if play continues down the engine's line,
// as the mover would count it: positive is gained, negative is given away.
//
// This is how a sacrifice is told from a trade. A move that drops a knight for
// a pawn reads -2 once the opponent has taken it; a move that drops a knight and
// takes one back reads 0, because the line the engine expects includes the
// recapture. The line is walked up to 12 plies and then any recapture on the last
// square is played, because a principal variation is cut at a fixed depth and a
// cut that lands between a capture and its answer would read as a sacrifice that
// nobody made. Six plies was tried first and read an ordinary exchange on move
// 13 of Byrne-Fischer as a sacrifice, because the line was cut before the
// material came back.
//
// It cannot see a sacrifice the opponent is right to decline, since the line
// then never loses anything. That is what ClsInput::offered is for.
int materialAlongLine(const Board& before, const Board& after, const Move& playedMove,
                      const std::vector<std::string>& pv) {
    const PieceColor mover = before.activeColor;
    const int start = materialBalance(before, mover);
    Board b = after.copyForSearch();
    // Seeded from the move played, so that a line which is empty or ends right
    // after it still knows an exchange is under way on that square.
    int lastTo = playedMove.to;
    bool lastWasCapture = playedMove.capturedPiece.color() != COLOR_NONE;
    for (size_t k = 0; k < pv.size() && k < 12; ++k) {
        bool played = false;
        for (const Move& cand : generateLegalMoves(b, b.activeColor)) {
            if (toUciMove(cand) != pv[k]) continue;
            lastWasCapture = cand.capturedPiece.color() != COLOR_NONE;
            lastTo = cand.to;
            b.makeMove(cand);
            played = true;
            break;
        }
        if (!played) break;
    }
    for (int guard = 0; guard < 6 && lastWasCapture && lastTo >= 0; ++guard) {
        Move cheapest;
        int cheapestValue = 1000;
        for (const Move& cand : generateLegalMoves(b, b.activeColor)) {
            if (cand.to != lastTo || cand.capturedPiece.color() == COLOR_NONE) continue;
            const int v = pieceValue(cand.movedPiece.type());
            if (v < cheapestValue) { cheapestValue = v; cheapest = cand; }
        }
        if (cheapest.from < 0) break;
        b.makeMove(cheapest);
    }
    return materialBalance(b, mover) - start;
}

struct ClsInput {
    double wpBefore;      // value of the best move, mover's win%
    double wpPlayed;      // value of the move played
    double wpLoss;
    bool   playedIsBest;
    bool   haveSecond;
    double wpSecond;      // value of the second-best move
    bool   book;
    int    materialDelta; // see materialAlongLine
    bool   recapture;     // the move retakes on the square just captured on
    bool   freeCapture;   // the move takes material outright (a static exchange wins at least 2 pawns)
    double opponentLoss;  // win% the opponent's previous move gave up
    bool   missedMate;
    // After this move the opponent can win at least a minor piece on a static
    // exchange. For a move the engine rates as costing nothing, that is a
    // sacrifice, whether or not accepting it is any good. This catches the ones
    // the best line declines: after 17...Be6 in Byrne-Fischer the engine's
    // answer is not to take the queen, so the line shows no material lost.
    bool   offered;
};

const char* clsFor(const ClsInput& in) {
    const char* base = in.playedIsBest      ? "Best"
                     : in.wpLoss >= 20.0    ? "Blunder"
                     : in.wpLoss >= 10.0    ? "Mistake"
                     : in.wpLoss >=  5.0    ? "Inaccuracy"
                     : in.wpLoss >=  2.0    ? "Good"
                                            : "Excellent";
    // Book comes first: a gambit is theory, and calling its first moves
    // "brilliant" because they give a pawn away would be wrong twice. A line
    // that is in the list but plainly bad is not rewarded as book.
    if (in.book && in.wpLoss < 10.0) return "Book";

    // A sacrifice (two points of material or more, so a piece for a pawn or the
    // exchange but not a pawn) that costs almost nothing, from a position that
    // is not already won and into one that is not bad. Chess.com's description
    // is exactly those three conditions; the numbers are this tool's.
    if (in.wpLoss < 2.0 && (in.materialDelta <= -2 || in.offered) &&
        in.wpBefore <= 85.0 && in.wpPlayed >= 40.0)
        return "Brilliant";

    // The only good move: best, with every alternative clearly worse, in a
    // position that is not lost anyway. Retaking a piece and picking up a piece
    // that was left hanging are the two "only moves" everyone finds, so both are
    // excluded, and a position with a single legal move has no second line and
    // cannot qualify.
    if (in.playedIsBest && in.haveSecond && !in.recapture && !in.freeCapture &&
        in.wpBefore - in.wpSecond >= 10.0 && in.wpBefore >= 30.0)
        return "Great";

    // Failing to use an opponent's error: they gave something up, the position
    // offered a clear win, and this move did not take it. A missed forced mate
    // counts even when what remains is still a big advantage. Blunders keep their
    // name, because a miss that loses the game is a blunder first.
    if (std::strcmp(base, "Blunder") != 0) {
        const bool inaccurate = !std::strcmp(base, "Inaccuracy") || !std::strcmp(base, "Mistake");
        if (inaccurate && in.opponentLoss >= 10.0 && in.wpBefore >= 70.0) return "Miss";
        if (in.missedMate && in.wpLoss >= 2.0) return "Miss";
    }
    return base;
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

// One row of the HTML report. Collected during the pass that prints the text
// report, so the two can never disagree about what a move was worth.
struct HtmlMove {
    std::string san, bestSan, label, terms, uci, bestUci;
    std::string cls;      // Chess.com-style class (see clsFor); label is untouched
    std::string opening;  // "B12 Caro-Kann Defense..." when this move ends a named line
    int ms = -1;        // milliseconds spent on this move, -1 when unknown
    int leftMs = -1;    // clock remaining after it
    int evalAfter;      // centipawns, White's point of view, after this move
    double wpLoss;
    int cpLoss;
    bool white;
};

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

// The board is drawn with the same piece images the GUI uses, inlined as data
// URIs. "Self-contained" is the constraint that decides this: a <img src> to a
// file beside the report breaks the moment the report is moved or mailed, and
// the whole point of one file is that it survives being sent to someone.
//
// 12 PNGs at 45x45 come to about 16 KB, roughly 21 KB once base64'd, on a
// report that is otherwise ~20 KB. That is a real cost and it buys the
// difference between a chess diagram and a row of text characters.
//
// If the images cannot be found the report still works: the drawing code falls
// back to Unicode glyphs, which is why this returns "{}" rather than failing.
std::string base64(const std::string& in) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    for (size_t i = 0; i < in.size(); i += 3) {
        const unsigned a = (unsigned char)in[i];
        const unsigned b = (i + 1 < in.size()) ? (unsigned char)in[i + 1] : 0;
        const unsigned c = (i + 2 < in.size()) ? (unsigned char)in[i + 2] : 0;
        const unsigned v = (a << 16) | (b << 8) | c;
        out += T[(v >> 18) & 63];
        out += T[(v >> 12) & 63];
        out += (i + 1 < in.size()) ? T[(v >> 6) & 63] : '=';
        out += (i + 2 < in.size()) ? T[v & 63] : '=';
    }
    return out;
}

std::string pieceImages() {
    // Relative to the executable, like the move tables, so running from another
    // directory does not silently produce a board with no pieces on it.
    std::error_code ec;
    std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    const std::filesystem::path base = ec ? std::filesystem::path(".") : exe.parent_path();
    const std::filesystem::path candidates[] = {
        base / ".." / "src" / "gui" / "assets" / "piece_images",
        base / "src" / "gui" / "assets" / "piece_images",
        std::filesystem::path("src") / "gui" / "assets" / "piece_images",
    };
    static const char* NAMES[12] = {"wK","wQ","wR","wB","wN","wP",
                                    "bK","bQ","bR","bB","bN","bP"};
    for (const std::filesystem::path& dir : candidates) {
        std::string out = "{";
        bool all = true;
        for (const char* n : NAMES) {
            std::ifstream f(dir / (std::string(n) + ".png"), std::ios::binary);
            if (!f) { all = false; break; }
            const std::string bytes((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
            if (bytes.empty()) { all = false; break; }
            if (out.size() > 1) out += ",";
            out += std::string("\"") + n + "\":\"data:image/png;base64," + base64(bytes) + "\"";
        }
        if (all) return out + "}";
    }
    return "{}";
}

// The report is one self-contained file: no CDN, no fonts, no images. A review
// is something you send to someone, and a page that fetches anything is a page
// that breaks on the machine you sent it to. Pieces are Unicode glyphs for the
// same reason — an image set would have to be embedded or fetched, and both are
// worse than a character that every system already has.
// Bumped whenever a record gains or changes a field. Cached records from an
// older tool are not wrong, they are incomplete -- when the clock and the
// best-move arrow landed, an archive rebuilt from cache silently produced 74
// games with neither and 6 with both, and nothing said so. A version the cache
// can compare against is the difference between a stale record and a stale
// record that announces itself.
constexpr int RECORD_VERSION = 3;

// Index metadata for one archived game, read from the PGN rather than the
// cached record.
//
// The index wants date, ratings and opening; a record carries none of them,
// because none of them costs analysis to obtain and the record is a cache of
// analysis. Adding them to the record would bump RECORD_VERSION and re-review
// every game in the archive with Stockfish to learn facts already sitting in a
// tag -- half an hour of a machine that is usually busy playing rated games.
//
// So the assembler reads the tags directly, and everything the index needs that
// *is* analysis -- accuracy, blunders, clock spent -- the page computes from the
// records it already has.
//
// Returns "null" when no PGN can be found, which the page renders as blanks
// rather than as an error: an archive assembled without --pgn-dir is still a
// working archive, just without the columns that come from tags.
std::string indexMeta(const std::string& pgnPath) {
    std::ifstream f(pgnPath);
    if (!f) return "null";
    std::string text((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
    auto tag = [&](const char* name) -> std::string {
        const std::string key = std::string("[") + name + " \"";
        const size_t p = text.find(key);
        if (p == std::string::npos) return "";
        const size_t s = p + key.size(), e = text.find('"', s);
        if (e == std::string::npos) return "";
        std::string v = text.substr(s, e - s);
        // The tag values used here are dates, integers and opening names; a
        // quote or backslash in one would break the JSON, and an opening name
        // is the plausible source of either.
        std::string out;
        for (char c : v) {
            if (c == '"' || c == '\\') out += '\\';
            out += c;
        }
        return out;
    };
    char buf[1024];
    std::snprintf(buf, sizeof buf,
        "{\"date\":\"%s\",\"time\":\"%s\",\"welo\":\"%s\",\"belo\":\"%s\","
        "\"eco\":\"%s\",\"opening\":\"%s\",\"term\":\"%s\",\"site\":\"%s\"}",
        tag("UTCDate").c_str(), tag("UTCTime").c_str(),
        tag("WhiteElo").c_str(), tag("BlackElo").c_str(),
        tag("ECO").c_str(), tag("Opening").c_str(),
        tag("Termination").c_str(), tag("Site").c_str());
    return buf;
}

// One game as a record the page can load. Kept separate from the page itself so
// an archive of seventy games is seventy of these inside one document, rather
// than seventy documents each carrying its own copy of the stylesheet and the
// same twelve piece images -- which measured 3.3 MB against 0.4 MB.
std::string gameRecord(const PgnGame& game, const std::vector<HtmlMove>& moves,
                       const std::string& startFen, int startEval,
                       const std::string& engine, int depth,
                       const double acc[2], const double cpAvg[2],
                       const int count[2], bool flip,
                       const std::string& opening, int multipv) {
    // Counted here rather than passed in: the picker needs one number per game
    // and this is the only place that has the moves and the labels together.
    int blunders = 0;
    for (const HtmlMove& m : moves)
        if (m.label == "Blunder" && m.white != flip) ++blunders;
    // Opening names come from a data file and may hold an apostrophe or, rarely,
    // a quote; either would end the JSON string early and blank the whole page.
    auto esc = [](const std::string& in) {
        std::string out;
        for (char c : in) { if (c == '"' || c == '\\') out += '\\'; out += c; }
        return out;
    };
    std::string j = "[";
    for (size_t i = 0; i < moves.size(); ++i) {
        const HtmlMove& m = moves[i];
        char buf[1536];
        std::snprintf(buf, sizeof buf,
            "%s{\"n\":%zu,\"w\":%s,\"san\":\"%s\",\"ev\":%d,"
            "\"wp\":%.1f,\"cp\":%d,\"best\":\"%s\",\"label\":\"%s\","
            "\"terms\":\"%s\",\"uci\":\"%s\",\"bu\":\"%s\",\"ms\":%d,\"left\":%d,"
            "\"cls\":\"%s\",\"op\":\"%s\"}",
            i ? "," : "", i, m.white ? "true" : "false", m.san.c_str(),
            m.evalAfter, m.wpLoss, m.cpLoss,
            m.bestSan.c_str(), m.label.c_str(), m.terms.c_str(), m.uci.c_str(),
            m.bestUci.c_str(), m.ms, m.leftMs,
            m.cls.c_str(), esc(m.opening).c_str());
        j += buf;
    }
    j += "]";

    char head[3072];
    std::snprintf(head, sizeof head,
        "{\"white\":\"%s\",\"black\":\"%s\",\"result\":\"%s\",\"engine\":\"%s\","
        "\"depth\":%d,\"startFen\":\"%s\",\"startEval\":%d,"
        "\"accW\":%.1f,\"accB\":%.1f,\"cpW\":%.1f,\"cpB\":%.1f,\"nW\":%d,\"nB\":%d,"
        "\"flip\":%s,\"blunders\":%d,\"tc\":\"%s\",\"opening\":\"%s\",\"mpv\":%d,"
        "\"welo\":\"%s\",\"belo\":\"%s\",\"v\":%d}",
        game.tags.white.c_str(), game.tags.black.c_str(), game.tags.result.c_str(),
        engine.c_str(), depth, startFen.c_str(), startEval,
        count[0] ? acc[0] : 0.0, count[1] ? acc[1] : 0.0,
        count[0] ? cpAvg[0] : 0.0, count[1] ? cpAvg[1] : 0.0, count[0], count[1],
        flip ? "true" : "false", blunders, game.tags.timeControl.c_str(),
        esc(opening).c_str(), multipv,
        esc(game.tags.whiteElo).c_str(), esc(game.tags.blackElo).c_str(), RECORD_VERSION);

    return std::string("{\"head\":") + head + ",\"moves\":" + j + "}";
}

std::string htmlReport(const std::string& gamesJson, const std::string& title,
                       const std::string& metaJson = "[]") {
    std::string html = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>__TITLE__</title>
<style>
/* Colour is spent in two places only, the class badges and the evaluation bar.
   Everything else is neutral, so a blunder is the one thing on the page that is
   red. The badge colours are shared by both themes and are always paired with
   the class name in text, so no meaning rides on hue alone. */
:root{
  --bg:#eeedea; --panel:#fff; --sunk:#f6f5f2; --ink:#1d1c1a; --dim:#5b5750; --faint:#6f6a62;
  --line:#e0ddd7; --accent:#2f7d4f;
  --sq-light:#d6dde5; --sq-dark:#6a7889; --sq-mark:#d7a13f;
  --arrow:#2f7d4f; --num:#000;
  --shade:rgba(29,28,26,.05); --shade2:rgba(29,28,26,.10);
  --wp-fill:#fcfcfa; --wp-edge:#0f1319; --bp-fill:#131820; --bp-edge:rgba(255,255,255,.34);
  --bar-w:#f2f1ee; --bar-b:#2b2a28;
  --ring:rgba(0,0,0,.2);
  --c-Brilliant:#26b5a8; --c-Great:#5b9bd8; --c-Book:#b48a62; --c-Best:#8bb43f; --c-Excellent:#5fb878;
  --c-Good:#b9a36f; --c-Inaccuracy:#e5c13f; --c-Mistake:#e8923a; --c-Miss:#d9779f; --c-Blunder:#e85a52;
  --on:#1a1917;
  --ease:cubic-bezier(.23,1,.32,1);
}
@media (prefers-color-scheme:dark){:root:not([data-theme="light"]){
  --bg:#262421; --panel:#302e2b; --sunk:#282623; --ink:#ecebe8; --dim:#b0aca5; --faint:#9a958d;
  --line:rgba(255,255,255,.09); --accent:#8bc176;
  --sq-light:#7c8795; --sq-dark:#4a5560; --sq-mark:#e0ad55; --arrow:#6fcf97; --num:#fff;
  --shade:rgba(255,255,255,.06); --shade2:rgba(255,255,255,.11);
  --wp-fill:#f6f7f4; --wp-edge:#090c10; --bp-fill:#0a0d12; --bp-edge:rgba(255,255,255,.72);
  --bar-w:#ecebe8; --bar-b:#1c1b19; --ring:rgba(255,255,255,.14);
}}
:root[data-theme="dark"]{
  --bg:#262421; --panel:#302e2b; --sunk:#282623; --ink:#ecebe8; --dim:#b0aca5; --faint:#9a958d;
  --line:rgba(255,255,255,.09); --accent:#8bc176;
  --sq-light:#7c8795; --sq-dark:#4a5560; --sq-mark:#e0ad55; --arrow:#6fcf97; --num:#fff;
  --shade:rgba(255,255,255,.06); --shade2:rgba(255,255,255,.11);
  --wp-fill:#f6f7f4; --wp-edge:#090c10; --bp-fill:#0a0d12; --bp-edge:rgba(255,255,255,.72);
  --bar-w:#ecebe8; --bar-b:#1c1b19; --ring:rgba(255,255,255,.14);
}
*{box-sizing:border-box}
html{color-scheme:light dark}
:root[data-theme="light"]{color-scheme:light}
:root[data-theme="dark"]{color-scheme:dark}
body{margin:0;background:var(--bg);color:var(--ink);
  font:15px/1.5 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;-webkit-font-smoothing:antialiased}
::selection{background:var(--shade2)}
*{scrollbar-width:thin;scrollbar-color:var(--shade2) transparent}
.wrap{max-width:1120px;margin:0 auto;padding:22px 20px 56px}
.mono{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;font-variant-numeric:tabular-nums}

.picker{display:flex;align-items:center;gap:12px;margin-bottom:14px}
.picker select{flex:1;min-width:0;font:inherit;font-size:13.5px;padding:7px 10px;
  border:1px solid var(--line);border-radius:8px;background:var(--panel);color:var(--ink)}
.gcount{color:var(--faint);font-size:12px;letter-spacing:.06em;text-transform:uppercase}
header.top{display:flex;align-items:baseline;gap:6px 16px;flex-wrap:wrap;margin-bottom:14px}
h1{font:600 20px/1.2 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;margin:0;text-wrap:balance}
h1 .vs{color:var(--faint);font-weight:400;padding:0 8px}
.cond{margin-left:auto;font-size:12.5px;color:var(--dim)}
.cond b{color:var(--ink);font-weight:600;margin-right:10px}

.layout{display:grid;grid-template-columns:minmax(320px,640px) minmax(320px,1fr);gap:20px;align-items:stretch}
@media(max-width:900px){.layout{grid-template-columns:1fr}}
.card{background:var(--panel);border:1px solid var(--line);border-radius:12px}

/* Player strips sit directly above and below the board, the way a game on a real
   board puts the opponent across from you. The clock is the clock at the move
   being viewed, not at the end of the game. */
.pstrip{display:flex;align-items:center;gap:9px;min-height:34px;padding:2px 2px}
.chip{width:12px;height:12px;border-radius:3px;flex:none;border:1px solid var(--ring)}
.chip.w{background:var(--wp-fill)}.chip.b{background:var(--bp-fill)}
.pname{font-weight:600;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.prate{color:var(--dim);font-size:13.5px}
.pclock{margin-left:auto;font-size:15px;padding:3px 10px;border-radius:6px;background:var(--panel);
  border:1px solid var(--line);min-width:64px;text-align:right}

/* Evaluation bar beside the board: it answers "who is winning" before anything
   has been read, and carries the number at the end that belongs to the leader. */
.play{display:grid;grid-template-columns:28px minmax(0,1fr);gap:8px}
.bar{border-radius:6px;overflow:hidden;background:var(--bar-b);position:relative}
.bar i{position:absolute;left:0;right:0;bottom:0;background:var(--bar-w);
  transition:height .2s var(--ease)}
.bar.flip i{bottom:auto;top:0}
.bar .n{position:absolute;left:0;right:0;text-align:center;font:600 10px/1 ui-monospace,SFMono-Regular,Menlo,monospace;
  padding:6px 0;letter-spacing:-.03em}
.boardbox{container-type:inline-size;border-radius:6px;overflow:hidden;position:relative}
.arrows{position:absolute;inset:0;pointer-events:none;width:100%;height:100%}
/* minmax(0,1fr), not 1fr: a bare 1fr keeps an automatic minimum of its content,
   so a glyph larger than its share silently stretches that rank and the board
   stops being a grid of equal squares. */
#board{display:grid;grid-template-columns:repeat(8,minmax(0,1fr));
  grid-template-rows:repeat(8,minmax(0,1fr));aspect-ratio:1;user-select:none;overflow:hidden}
#board div{position:relative;display:flex;align-items:center;justify-content:center;
  font-size:8.6cqi;line-height:1;overflow:hidden}
.lt{background:var(--sq-light)}.dk{background:var(--sq-dark)}
/* paint-order matters more than the stroke width: -webkit-text-stroke is centred
   on the glyph outline, so without this the stroke eats half the fill and a white
   piece reads as a dark one. Only used when the piece images are missing. */
#board img{width:86%;height:86%;object-fit:contain;
  filter:drop-shadow(0 1px 1px rgba(0,0,0,.28));pointer-events:none}
.wp,.bp{paint-order:stroke fill}
.wp{color:var(--wp-fill);-webkit-text-stroke:1.6px var(--wp-edge)}
.bp{color:var(--bp-fill);-webkit-text-stroke:1.6px var(--bp-edge)}
.hl::after{content:"";position:absolute;inset:0;box-shadow:inset 0 0 0 3px var(--sq-mark)}
.co{position:absolute;font:600 2.5cqi/1 ui-monospace,SFMono-Regular,Menlo,monospace;opacity:.7}
.co.f{right:5%;bottom:3%}.co.r{left:5%;top:3%}
.lt .co{color:var(--sq-dark)}.dk .co{color:var(--sq-light)}

.nav{display:flex;gap:6px;padding:8px 0 0;align-items:center;flex-wrap:wrap}
button{font:inherit;color:var(--ink);cursor:pointer;-webkit-tap-highlight-color:transparent}
.nb{display:inline-grid;place-items:center;min-width:40px;height:36px;padding:0 10px;font-size:13px;
  border:1px solid var(--line);border-radius:8px;background:var(--panel);
  transition:background-color .12s ease,transform .16s var(--ease)}
.nb svg{width:16px;height:16px;fill:currentColor;stroke:currentColor}
.nb svg.ln{fill:none}
.nb:active{transform:scale(.97)}
.nb.txt{font-size:12.5px;color:var(--dim)}
@media (hover:hover) and (pointer:fine){.nb:hover{background:var(--shade)}}
@media (pointer:coarse){.nb{min-height:44px;min-width:44px}}
button:focus-visible,.ply:focus-visible,.mom:focus-visible,summary:focus-visible{outline:2px solid var(--accent);outline-offset:2px}
.ply-count{margin-left:auto;color:var(--faint);font-size:12.5px;white-space:nowrap}

/* Class badge: a filled disc, the glyph set in near-black on every fill except
   where a measured contrast said otherwise. 1px ring so the pale fills keep an
   edge on a white page. */
.ic{display:inline-grid;place-items:center;width:1.45em;height:1.45em;border-radius:50%;flex:none;
  background:var(--c);color:var(--on);box-shadow:inset 0 0 0 1px var(--ring);font-size:15px;vertical-align:middle}
.ic b{font:800 .8em/1 system-ui,-apple-system,"Segoe UI",sans-serif;letter-spacing:-.06em;margin-top:-.04em}
.ic svg{width:.82em;height:.82em;fill:currentColor}
.ic svg.st{fill:none;stroke:currentColor;stroke-width:2.2;stroke-linecap:round;stroke-linejoin:round}
.c-Brilliant{--c:var(--c-Brilliant)}.c-Great{--c:var(--c-Great)}.c-Book{--c:var(--c-Book)}
.c-Best{--c:var(--c-Best)}.c-Excellent{--c:var(--c-Excellent)}.c-Good{--c:var(--c-Good)}
.c-Inaccuracy{--c:var(--c-Inaccuracy)}.c-Mistake{--c:var(--c-Mistake)}.c-Miss{--c:var(--c-Miss)}
.c-Blunder{--c:var(--c-Blunder)}

/* The move under inspection. Stepping through a game is keyboard-driven and
   repeated hundreds of times, so nothing here animates. */
/* The side column is pinned to the height of the board column rather than
   contributing its own: the summary is far taller than a board, and letting it
   decide the row height would leave the board floating above a page-long panel.
   It scrolls inside the pane instead. */
.side{position:relative;min-width:0}
.sidein{position:absolute;inset:0;display:flex;flex-direction:column;gap:12px;min-height:0}
.tabcard{flex:1;min-height:0;display:flex;flex-direction:column;overflow:hidden}
.tabcard .pane{flex:1;min-height:0}
@media(max-width:900px){
  .side{position:static}.sidein{position:static;display:block}
  .sidein>*+*{margin-top:12px}.tabcard .pane{max-height:70vh}}
.coach{padding:14px 16px;min-height:112px}
.chead{display:flex;align-items:center;gap:10px;flex-wrap:wrap}
.ctitle{font-size:17px;font-weight:600;line-height:1.3}
.ctitle .mv{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;letter-spacing:-.02em}
.ceval{margin-left:auto;font-size:14px;color:var(--dim)}
.cline{font-size:14px;color:var(--dim);margin-top:6px}
.cline b{color:var(--ink);font-weight:600}
.cline kbd{font:600 11.5px ui-monospace,SFMono-Regular,Menlo,monospace;padding:1px 6px;border-radius:4px;
  border:1px solid var(--line);background:var(--sunk);color:var(--ink)}
details.why{margin-top:8px}
details.why summary{cursor:pointer;color:var(--dim);font-size:13px;width:fit-content;padding:2px 0}
details.why[open] summary{color:var(--ink)}
.terms{display:flex;gap:6px;flex-wrap:wrap;margin-top:8px}
.term{font-size:12px;padding:3px 9px;border-radius:6px;background:var(--sunk);
  border:1px solid var(--line)}
.term em{font-style:normal;color:var(--dim)}

.tabs{display:flex;gap:2px;padding:4px;border-bottom:1px solid var(--line)}
.tab{flex:1;padding:8px 12px;border:0;border-radius:8px;background:transparent;font-size:14px;font-weight:600;
  color:var(--dim);min-height:36px}
.tab[aria-selected="true"]{background:var(--shade2);color:var(--ink)}
@media (hover:hover) and (pointer:fine){.tab:hover{background:var(--shade)}}
.pane{overflow-y:auto}
.pane[hidden]{display:none}
.pad{padding:14px 16px}

/* Accuracy: one figure per player under their own name. */
.accs{display:grid;grid-template-columns:1fr 1fr;gap:10px;padding:14px 16px 6px}
.acc1 .who{display:flex;align-items:center;gap:7px;font-size:13px;color:var(--dim);min-width:0}
.acc1 .who span{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.acc1 b{display:block;font:600 28px/1.15 ui-monospace,SFMono-Regular,Menlo,monospace;
  font-variant-numeric:tabular-nums;letter-spacing:-.03em;margin-top:2px}
.acc1 i{font-style:normal;font-size:12px;color:var(--faint)}
.acc1.r{text-align:right}.acc1.r .who{justify-content:flex-end}

/* Counts either side of the class name, so a row reads left to right as
   "White had n of these, here is what they are, Black had m". A class that
   never happened is dimmed rather than removed: the table keeps one shape. */
table.clsT{width:100%;border-collapse:collapse;margin:6px 0 0}
table.clsT td{padding:0 16px;height:34px;font-size:14px;border-top:1px solid var(--line)}
table.clsT td.n{width:22%;font-family:ui-monospace,SFMono-Regular,Menlo,monospace;
  font-variant-numeric:tabular-nums;font-weight:600}
table.clsT td.n:first-child{text-align:left}table.clsT td.n:last-child{text-align:right}
table.clsT td.k{text-align:center;color:var(--dim)}
table.clsT td.k>span{display:inline-flex;align-items:center;gap:8px}
table.clsT tr.z td{color:var(--faint);font-weight:400}
table.clsT tr.z .ic{opacity:.45}
.opn{margin:0;padding:10px 16px;font-size:13px;color:var(--dim);border-top:1px solid var(--line)}
.opn b{color:var(--ink);font-weight:600}
h3.sh{font-size:13px;font-weight:600;color:var(--dim);margin:0;padding:12px 16px 6px;border-top:1px solid var(--line)}
.moments{padding:0 8px 8px;display:flex;flex-direction:column}
.mom{display:flex;align-items:center;gap:10px;padding:7px 8px;border:0;border-radius:8px;background:transparent;
  text-align:left;width:100%;font-size:14px;min-height:36px}
.mom .mv{font-family:ui-monospace,SFMono-Regular,Menlo,monospace}
.mom .cn{color:var(--dim)}
.mom .ev{margin-left:auto;color:var(--faint);font-size:12.5px}
.mom.sel{background:var(--shade2)}
@media (hover:hover) and (pointer:fine){.mom:hover{background:var(--shade)}}
.none{padding:4px 16px 12px;color:var(--faint);font-size:13.5px;margin:0}
.gbox{padding:6px 16px 10px;border-top:1px solid var(--line)}
.glab{display:flex;justify-content:space-between;font-size:12px;color:var(--faint);margin:6px 0}
svg.gr{display:block;width:100%;height:132px;overflow:visible;cursor:pointer}
details.gbox summary{cursor:pointer;font-size:12px;color:var(--faint);padding:6px 0;display:flex;justify-content:space-between}
details.gbox[open] summary{margin-bottom:4px}
details.gbox svg.gr{height:84px}

.sheet{padding:6px}
.row{display:grid;grid-template-columns:34px 1fr 1fr;gap:3px;align-items:stretch}
.no{color:var(--faint);font-weight:600;font-size:12.5px;padding:8px 6px 0 0;text-align:right;
  font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-variant-numeric:tabular-nums}
.ply{display:flex;align-items:center;gap:7px;padding:5px 8px;border-radius:7px;min-height:34px;
  border:0;background:transparent;color:var(--ink);text-align:left;width:100%;
  font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:13.5px;
  transition:background-color .12s ease}
.ply .ic{font-size:13px}
.ply .d{margin-left:auto;font-size:11px;color:var(--faint);font-variant-numeric:tabular-nums}
.ply.sel{background:var(--shade2)}
@media (hover:hover) and (pointer:fine){.ply:hover{background:var(--shade)}}
.ply.bad{font-weight:700}
.ply.bad .d{color:var(--dim)}

.note{color:var(--faint);font-size:12.5px;margin-top:22px;line-height:1.7;max-width:76ch}
#index{display:none;margin-bottom:26px}
#index.on{display:block}
.idxbar{display:flex;align-items:baseline;gap:14px;margin-bottom:10px;flex-wrap:wrap}
.idxbar h2{font-size:15px;margin:0;font-weight:600}
.idxbar .sub{color:var(--faint);font-size:12px}
.idxwrap{overflow-x:auto;max-height:420px;overflow-y:auto;
  border:1px solid var(--line);border-radius:12px;background:var(--panel)}
table.idx{border-collapse:collapse;width:100%;font-size:12.5px}
table.idx th,table.idx td{padding:6px 10px;text-align:left;white-space:nowrap;
  border-bottom:1px solid var(--line)}
table.idx th{position:sticky;top:0;background:var(--panel);cursor:pointer;
  font-weight:600;user-select:none}
table.idx th:hover{color:var(--accent)}
table.idx th.num,table.idx td.num{text-align:right}
table.idx tbody tr{cursor:pointer}
table.idx tbody tr:hover{background:var(--shade)}
table.idx tbody tr.sel{background:var(--shade2)}
table.idx td.win{color:var(--c-Best)}table.idx td.loss{color:var(--c-Blunder)}
.bandtab{margin-top:14px;font-size:12.5px;border-collapse:collapse}
.bandtab td,.bandtab th{padding:4px 12px 4px 0;text-align:left}
.bandtab th{color:var(--faint);font-weight:500}
@media (prefers-reduced-motion:reduce){.bar i,.nb,.ply{transition:none}}
</style></head><body><div class="wrap">
<section id="index">
  <div class="idxbar">
    <h2>Archive</h2>
    <span class="sub" id="idxsub"></span>
    <span class="sub" style="margin-left:auto"><button class="nb txt" id="idxtoggle">hide</button></span>
  </div>
  <div class="idxwrap"><table class="idx" id="idxtable"></table></div>
  <table class="bandtab" id="bandtab"></table>
</section>
<div id="picker" class="picker" style="display:none">
  <span class="gcount mono" id="gcount"></span>
  <select id="pick" aria-label="Choose a game"></select>
</div>
<header class="top">
  <h1 id="ttl"></h1>
  <div class="cond" id="cond"></div>
</header>
<div class="layout">
  <div class="left">
    <div class="pstrip" id="ptop"></div>
    <div class="play">
      <div class="bar" id="barbox"><i id="bar" style="height:50%"></i><span class="n" id="barnum"></span></div>
      <div class="boardbox"><div id="board"></div>
        <svg class="arrows" id="arrow" viewBox="0 0 8 8" aria-hidden="true"></svg></div>
    </div>
    <div class="pstrip" id="pbot"></div>
    <div class="nav">
      <button class="nb" onclick="go(-999)" title="Start (Home)" aria-label="Go to the start"><svg viewBox="0 0 16 16"><path d="M3.5 3v10" stroke-width="1.8" stroke-linecap="round"/><path d="M13 3.2L6.5 8l6.5 4.8z" stroke="none"/></svg></button>
      <button class="nb" onclick="go(-1)" title="Previous move (Left)" aria-label="Previous move"><svg viewBox="0 0 16 16"><path d="M11.5 3.2L5 8l6.5 4.8z" stroke="none"/></svg></button>
      <button class="nb" onclick="go(1)" title="Next move (Right)" aria-label="Next move"><svg viewBox="0 0 16 16"><path d="M4.5 3.2L11 8l-6.5 4.8z" stroke="none"/></svg></button>
      <button class="nb" onclick="go(999)" title="End (End)" aria-label="Go to the end"><svg viewBox="0 0 16 16"><path d="M12.5 3v10" stroke-width="1.8" stroke-linecap="round"/><path d="M3 3.2L9.5 8 3 12.8z" stroke="none"/></svg></button>
      <button class="nb" onclick="flipBoard()" title="Flip the board (F)" aria-label="Flip the board"><svg class="ln" viewBox="0 0 16 16" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"><path d="M3 5.5h9.5M10 3l2.5 2.5L10 8"/><path d="M13 10.5H3.5M6 8l-2.5 2.5L6 13"/></svg></button>
      <button class="nb txt" onclick="jump(-1)" title="Previous key moment (P)">&lsaquo; Key moment</button>
      <button class="nb txt" onclick="jump(1)" title="Next key moment (N)">Key moment &rsaquo;</button>
      <span class="ply-count mono" id="pos"></span>
    </div>
  </div>
  <aside class="side"><div class="sidein">
    <div class="card coach" id="coach" aria-live="polite"></div>
    <div class="card tabcard">
      <div class="tabs" role="tablist">
        <button class="tab" role="tab" id="tab-sum" aria-controls="pane-sum" aria-selected="true">Summary</button>
        <button class="tab" role="tab" id="tab-mov" aria-controls="pane-mov" aria-selected="false">Moves</button>
      </div>
      <div class="pane" id="pane-sum" role="tabpanel" aria-labelledby="tab-sum">
        <div class="accs" id="acc"></div>
        <table class="clsT" id="clsT"></table>
        <p class="opn" id="opn"></p>
        <h3 class="sh">Key moments</h3>
        <div class="moments" id="moments"></div>
        <div class="gbox">
          <div class="glab"><span>Evaluation, White's point of view</span><span id="glab2"></span></div>
          <svg class="gr" id="g" aria-label="Evaluation graph. Click to jump to a move."></svg>
        </div>
        <details class="gbox" id="timecard" style="display:none">
          <summary><span>Clock remaining</span><span id="tlab"></span></summary>
          <svg class="gr" id="t"></svg>
        </details>
      </div>
      <div class="pane" id="pane-mov" role="tabpanel" aria-labelledby="tab-mov" hidden>
        <div class="sheet" id="list"></div>
      </div>
    </div>
  </div></aside>
</div>
<p class="note" id="note"></p>
</div>
<script>
const GAMES=__GAMES__, P=__PIECES__, META=__META__;
const HAVE_IMG=Object.keys(P).length===12;
let gi=0, H=GAMES[0].head, M=GAMES[0].moves, POS=[];
const S={k:"♚",q:"♛",r:"♜",b:"♝",n:"♞",p:"♟"};
let cur=-1, flipped=!!H.flip, tab="sum";
const winPct=cp=>50+50*(2/(1+Math.exp(-0.00368208*cp))-1);
const esc=t=>String(t).replace(/[&<>"]/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;"}[c]));

// Classes, in the order Chess.com's summary table lists them. Each is a glyph:
// the annotation symbols chess has always used for the five that have one, and a
// drawn shape for the rest.
const CLS=[
  {k:"Brilliant",g:"!!"},{k:"Great",g:"!"},{k:"Book",g:"book"},{k:"Best",g:"star"},
  {k:"Excellent",g:"check"},{k:"Good",g:"check"},{k:"Inaccuracy",g:"?!"},
  {k:"Mistake",g:"?"},{k:"Miss",g:"x"},{k:"Blunder",g:"??"}];
const SHAPE={
  star:'<svg viewBox="0 0 16 16" aria-hidden="true"><path d="M8 1.9l1.8 3.7 4 .6-2.9 2.8.7 4L8 11.1 4.4 13l.7-4L2.2 6.2l4-.6z"/></svg>',
  book:'<svg viewBox="0 0 16 16" aria-hidden="true"><path d="M2.2 4c1.9-.8 3.8-.7 5.8.5v8c-2-1.2-3.9-1.3-5.8-.5zM13.8 4c-1.9-.8-3.8-.7-5.8.5v8c2-1.2 3.9-1.3 5.8-.5z"/></svg>',
  check:'<svg class="st" viewBox="0 0 16 16" aria-hidden="true"><path d="M3.6 8.5l2.7 2.7 6-6.2"/></svg>',
  x:'<svg class="st" viewBox="0 0 16 16" aria-hidden="true"><path d="M4.6 4.6l6.8 6.8M11.4 4.6l-6.8 6.8"/></svg>'};
// Records written before the classes existed carry only `label`, whose names are
// a subset of these, so an old archive still renders.
const kind=m=>m.cls||m.label;
const BAD={Inaccuracy:1,Mistake:1,Miss:1,Blunder:1};
const KEY={Brilliant:1,Great:1,Miss:1,Mistake:1,Blunder:1};
const isKey=m=>!!KEY[kind(m)];
function icon(k){
  const c=CLS.find(x=>x.k===k); if(!c) return "";
  return '<span class="ic c-'+k+'">'+(SHAPE[c.g]||'<b>'+c.g+'</b>')+'</span>';
}
const NOUN={Brilliant:"a brilliant move",Great:"a great move",Book:"a book move",Best:"the best move",
  Excellent:"an excellent move",Good:"a good move",Inaccuracy:"an inaccuracy",
  Mistake:"a mistake",Miss:"a miss",Blunder:"a blunder"};

function sq(u){ if(!u||u.length<4)return[];
  const ix=t=>(8-(+t[1]))*8+(t.charCodeAt(0)-97);
  return[ix(u.slice(0,2)),ix(u.slice(2,4))]; }
// The FEN is unpacked into 64 squares first, so drawing order is a separate
// decision from parsing. Flipping is then a 180-degree rotation -- 63-i, both
// rank and file -- rather than a second parser that reads the board backwards.
function unpack(fen){
  const a=new Array(64).fill(""), rows=fen.split(" ")[0].split("/");
  for(let r=0;r<8;r++){let f=0;
    for(const ch of rows[r]){
      if(ch>="1"&&ch<="8")f+=+ch; else a[r*8+(f++)]=ch;
    }}
  return a;
}
// Positions are replayed rather than stored. A FEN per move is about 60 bytes
// against the 4 the move already costs, and the game is fully determined by its
// move list -- carrying both meant every report shipped the same information
// twice, which mattered once there were seventy of them.
//
// Only legal moves ever reach this, so it applies a move rather than validating
// one: the three that are not "pick it up and put it down" are castling (the
// king crosses two files and the rook jumps it), en passant (a pawn captures
// onto an empty square) and promotion (the fifth character of the UCI move).
function applyMove(a,u){
  const from=(8-(+u[1]))*8+(u.charCodeAt(0)-97), to=(8-(+u[3]))*8+(u.charCodeAt(2)-97);
  const p=a[from];
  a[from]="";
  if(p.toLowerCase()==="k" && Math.abs((from&7)-(to&7))===2){
    const rank=to&56, rf=(to&7)>4?rank+7:rank, rt=(to&7)>4?rank+5:rank+3;
    a[rt]=a[rf]; a[rf]="";
  }
  if(p.toLowerCase()==="p" && (from&7)!==(to&7) && !a[to]) a[(from&56)|(to&7)]="";
  a[to]=u.length>4?(p===p.toUpperCase()?u[4].toUpperCase():u[4].toLowerCase()):p;
  return a;
}
function board(a,uci){
  const mark=sq(uci), b=document.getElementById("board");
  b.innerHTML="";
  for(let i=0;i<64;i++){
    const idx=flipped?63-i:i;
    cell(b,idx>>3,idx&7,a[idx],mark,i);
  }
}
function cell(b,r,f,p,mark,at){
  const d=document.createElement("div");
  const white=p&&p===p.toUpperCase();
  const g=p?(HAVE_IMG?p.toUpperCase():S[p.toLowerCase()]||""):"";
  d.className=((r+f)%2?"dk":"lt")+(mark.includes(r*8+f)?" hl":"");
  if(g){
    if(HAVE_IMG){const im=new Image();im.src=P[(white?"w":"b")+g];im.alt="";d.appendChild(im)}
    else{const s=document.createElement("span");s.className=white?"wp":"bp";s.textContent=g;d.appendChild(s)}
  }
  // Coordinates belong to the edges of the *view*, not of the board, or they
  // end up in the middle of a flipped diagram.
  if(at>=56)d.insertAdjacentHTML("beforeend",'<span class="co f">'+"abcdefgh"[f]+'</span>');
  if(at%8===0)d.insertAdjacentHTML("beforeend",'<span class="co r">'+(8-r)+'</span>');
  b.appendChild(d);
}
const fmt=cp=>(cp>0?"+":"")+(cp/100).toFixed(2);
// A clamped mate is 1000 centipawns here, which is not a number a person should
// read as a pawn count.
const fmtBar=cp=>Math.abs(cp)>=1000?(cp>0?"+10+":"-10+"):(cp>0?"+":"")+(cp/100).toFixed(1);
const secs=ms=>ms>=60000?Math.floor(ms/60000)+"m "+Math.round(ms%60000/1000)+"s"
                        :(ms/1000).toFixed(ms<10000?1:0)+"s";
function clockText(ms){
  if(ms<0)ms=0;
  const t=Math.floor(ms/1000), h=Math.floor(t/3600), m=Math.floor(t/60)%60, s=t%60;
  if(h)return h+":"+String(m).padStart(2,"0")+":"+String(s).padStart(2,"0");
  if(ms<10000)return m+":"+String(s).padStart(2,"0")+"."+Math.floor(ms%1000/100);
  return m+":"+String(s).padStart(2,"0");
}
const baseMs=()=>{const n=parseInt(H.tc,10);return n>0?n*1000:null};
// The clock of one side as it stood at the move being viewed.
function clockAt(white){
  for(let i=cur;i>=0;i--) if(M[i].w===white&&M[i].left>=0) return M[i].left;
  return baseMs();
}
const label=m=>m.san+(m.label=="Blunder"?"??":m.label=="Mistake"?"?":m.wp>=5?"?!":"");

function strip(el,white){
  const name=white?H.white:H.black, elo=white?H.welo:H.belo, t=clockAt(white);
  el.innerHTML='<span class="chip '+(white?"w":"b")+'"></span><span class="pname">'+esc(name)+'</span>'+
    (elo?'<span class="prate">('+esc(elo)+')</span>':'')+
    (t==null?'':'<span class="pclock mono">'+clockText(t)+'</span>');
}

// The coach line for the move being viewed: what it was, then what was better.
function coach(){
  const d=document.getElementById("coach");
  if(cur<0){
    d.innerHTML='<div class="ctitle">Starting position</div>'+
      '<div class="cline">Step with <kbd>&larr;</kbd> <kbd>&rarr;</kbd>, jump between key moments with '+
      '<kbd>N</kbd> and <kbd>P</kbd>, flip the board with <kbd>F</kbd>.</div>';
    return;
  }
  const m=M[cur], k=kind(m);
  let h='<div class="chead">'+icon(k)+'<span class="ctitle"><span class="mv">'+((cur>>1)+1)+(m.w?". ":"... ")+
    esc(m.san)+'</span> is '+NOUN[k]+'</span><span class="ceval mono">'+fmt(m.ev)+'</span></div>';
  const best=m.best?' Best was <b>'+esc(m.best)+'</b>.':'';
  if(BAD[k]){
    h+='<div class="cline">'+(m.wp>=0.05?'Cost <b>'+m.wp.toFixed(1)+'%</b> win probability'+
       (m.cp?' ('+m.cp+' centipawns)':'')+'.':'')+(k==="Miss"?' The last move gave you a chance and this one did not take it.':'')+best+'</div>';
    if(m.terms==="no term accounts for it")
      h+='<details class="why"><summary>Why</summary><div class="cline"><b>No evaluation term accounts for it.</b> '+
         'ChessBot cannot see what this move lost.</div></details>';
    else if(m.terms)
      h+='<details class="why"><summary>Why, in ChessBot’s evaluation</summary><div class="terms">'+
         m.terms.split(", ").map(t=>{const i=t.lastIndexOf(" ");
           return '<span class="term"><em>'+esc(t.slice(0,i))+'</em> '+esc(t.slice(i+1))+'</span>';}).join("")+'</div></details>';
  } else if(k==="Book"){
    h+='<div class="cline">'+(m.op?esc(m.op):'A move from opening theory.')+'</div>';
  } else if(k==="Brilliant"){
    h+='<div class="cline">A sacrifice that holds the position.</div>';
  } else if(k==="Great"){
    h+='<div class="cline">The only move that keeps the position, with every alternative clearly worse.</div>';
  } else if(m.best){
    h+='<div class="cline">'+best.trim()+'</div>';
  }
  if(m.ms>=0)h+='<div class="cline">Took <b>'+secs(m.ms)+'</b>'+
    (m.left>=0?', leaving '+secs(m.left)+' on the clock':'')+'.</div>';
  d.innerHTML=h;
}

function arrow(u){
  const g=document.getElementById("arrow");
  if(!u||u.length<4){g.innerHTML="";return}
  const at=t=>{
    const idx=(8-(+t[1]))*8+(t.charCodeAt(0)-97), d=flipped?63-idx:idx;
    return [(d%8)+0.5,(d>>3)+0.5];
  };
  const [x1,y1]=at(u.slice(0,2)), [x2,y2]=at(u.slice(2,4));
  // Stop short of the destination so the arrowhead sits on the square rather
  // than covering the piece standing on it.
  const dx=x2-x1, dy=y2-y1, len=Math.hypot(dx,dy)||1;
  const ex=x2-dx/len*0.32, ey=y2-dy/len*0.32;
  g.innerHTML=
    '<defs><marker id="ah" markerWidth="3.2" markerHeight="3.2" refX="1.6" refY="1.6" orient="auto">'+
    '<path d="M0,0 L3.2,1.6 L0,3.2 z" fill="var(--arrow)"/></marker></defs>'+
    '<line x1="'+x1+'" y1="'+y1+'" x2="'+ex+'" y2="'+ey+'" stroke="var(--arrow)" '+
    'stroke-width="0.15" stroke-linecap="round" marker-end="url(#ah)" opacity=".85"/>';
}
function evalBar(ev){
  const box=document.getElementById("barbox"), n=document.getElementById("barnum");
  document.getElementById("bar").style.height=winPct(ev).toFixed(1)+"%";
  box.classList.toggle("flip",flipped);
  // The number sits at the end of the bar that belongs to whoever is ahead,
  // coloured to read against that side's fill.
  const whiteLeads=ev>=0, atBottom=whiteLeads===!flipped;
  n.textContent=fmtBar(ev);
  n.style.top=atBottom?"auto":"0"; n.style.bottom=atBottom?"0":"auto";
  n.style.color=whiteLeads?"var(--bar-b)":"var(--bar-w)";
}
function render(){
  const m=cur<0?null:M[cur], ev=m?m.ev:H.startEval;
  board(POS[cur+1], m?m.uci:"");
  arrow(m && BAD[kind(m)] ? m.bu : "");
  evalBar(ev);
  strip(document.getElementById("ptop"), flipped);
  strip(document.getElementById("pbot"), !flipped);
  document.getElementById("pos").textContent=(cur+1)+" / "+M.length;
  document.querySelectorAll(".ply").forEach(e=>e.classList.toggle("sel",+e.dataset.i===cur));
  document.querySelectorAll(".mom").forEach(e=>e.classList.toggle("sel",+e.dataset.i===cur));
  const s=document.querySelector(tab==="mov"?".ply.sel":".mom.sel"); if(s)s.scrollIntoView({block:"nearest"});
  document.getElementById("glab2").textContent=fmt(ev);
  coach(); marker();
}
function go(d){ cur=d===-999?-1:d===999?M.length-1:Math.max(-1,Math.min(M.length-1,cur+d)); render(); }
function flipBoard(){ flipped=!flipped; render(); }
// An eighty-move game has a handful of moves worth looking at. Stepping through
// a hundred and sixty plies to find them is the reading this tool exists to save.
function jump(dir){
  const bad=M.map((m,i)=>isKey(m)?i:-1).filter(i=>i>=0);
  if(!bad.length)return;
  const next=dir>0?bad.find(i=>i>cur):[...bad].reverse().find(i=>i<cur);
  cur = next===undefined ? (dir>0?bad[0]:bad[bad.length-1]) : next;
  render();
}
document.addEventListener("keydown",e=>{
  if(e.metaKey||e.ctrlKey||e.altKey)return;
  const t=e.target&&e.target.tagName;
  if(t==="SELECT"||t==="INPUT")return;
  if(e.key==="ArrowLeft")go(-1);else if(e.key==="ArrowRight")go(1);
  else if(e.key==="Home")go(-999);else if(e.key==="End")go(999);
  else if(e.key==="f"||e.key==="F")flipBoard();
  else if(e.key==="n"||e.key==="N")jump(1);
  else if(e.key==="p"||e.key==="P")jump(-1);else return;
  e.preventDefault();
});
function setTab(t){
  tab=t;
  for(const k of ["sum","mov"]){
    document.getElementById("tab-"+k).setAttribute("aria-selected",k===t);
    document.getElementById("pane-"+k).hidden=k!==t;
  }
  render();
}
document.getElementById("tab-sum").onclick=()=>setTab("sum");
document.getElementById("tab-mov").onclick=()=>setTab("mov");

function buildSheet(){
  const list=document.getElementById("list"); list.innerHTML="";
  for(let i=0;i<M.length;i+=2){
    const row=document.createElement("div"); row.className="row";
    row.innerHTML='<span class="no">'+((i>>1)+1)+'</span>';
    for(const j of [i,i+1]){
      if(j>=M.length){row.insertAdjacentHTML("beforeend","<span></span>");continue}
      const m=M[j], k=kind(m), b=document.createElement("button");
      const show=k!=="Best"&&k!=="Excellent"&&k!=="Good";
      b.className="ply"+(BAD[k]?" bad":""); b.dataset.i=j;
      b.setAttribute("aria-label",m.san+", "+k);
      b.innerHTML=(show?icon(k):"")+'<span>'+esc(m.san)+'</span><span class="d">'+
        (m.wp>=5?"-"+m.wp.toFixed(1)+"%":fmt(m.ev))+'</span>';
      b.onclick=()=>{cur=j;render()};
      row.appendChild(b);
    }
    list.appendChild(row);
  }
}

// Counts per class and side, from the moves themselves, so a record that predates
// a class simply shows zero for it.
function summary(){
  const cnt=w=>{const o={};for(const c of CLS)o[c.k]=0;for(const m of M)if(m.w===w)o[kind(m)]=(o[kind(m)]||0)+1;return o};
  const cw=cnt(true), cb=cnt(false);
  document.getElementById("acc").innerHTML=
    '<div class="acc1"><div class="who"><i class="chip w"></i><span>'+esc(H.white)+'</span></div><b>'+H.accW.toFixed(1)+'</b><i>accuracy, '+H.cpW.toFixed(1)+' cp average loss</i></div>'+
    '<div class="acc1 r"><div class="who"><span>'+esc(H.black)+'</span><i class="chip b"></i></div><b>'+H.accB.toFixed(1)+'</b><i>accuracy, '+H.cpB.toFixed(1)+' cp average loss</i></div>';
  document.getElementById("clsT").innerHTML=CLS.map(c=>{
    const a=cw[c.k]||0, b=cb[c.k]||0;
    return '<tr'+(a||b?"":' class="z"')+'><td class="n">'+a+'</td><td class="k"><span>'+icon(c.k)+c.k+'</span></td><td class="n">'+b+'</td></tr>';
  }).join("");
  let op=H.opening||""; for(const m of M) if(m.op) op=m.op;
  document.getElementById("opn").innerHTML=op?'<b>'+esc(op.slice(0,3))+'</b> '+esc(op.slice(4)):"";
  document.getElementById("opn").style.display=op?"":"none";
  const key=M.map((m,i)=>isKey(m)?i:-1).filter(i=>i>=0);
  document.getElementById("moments").innerHTML=key.length?key.map(i=>{
    const m=M[i], k=kind(m);
    return '<button class="mom" data-i="'+i+'">'+icon(k)+'<span class="mv">'+((i>>1)+1)+(m.w?". ":"... ")+esc(m.san)+
      '</span><span class="cn">'+k+'</span><span class="ev mono">'+fmt(m.ev)+'</span></button>';
  }).join(""):'<p class="none">No key moments. Nothing here was brilliant, great, missed or a mistake.</p>';
  document.querySelectorAll(".mom").forEach(b=>b.onclick=()=>{cur=+b.dataset.i;render()});
}

// Graphs are drawn at the width they are shown at, not squeezed from a fixed
// viewBox, so the markers stay round.
const GH=132,CAP=700;
let pts=[],GW=400;
const gx=i=>i*GW/Math.max(1,pts.length-1);
const gy=v=>GH/2-Math.max(-CAP,Math.min(CAP,v))/CAP*(GH/2-8);
function graph(){
  const svg=document.getElementById("g");
  GW=Math.max(200,Math.round(svg.getBoundingClientRect().width)||400);
  svg.setAttribute("viewBox","0 0 "+GW+" "+GH);
  let d="M"+gx(0)+","+gy(pts[0]);
  pts.forEach((v,i)=>{if(i)d+="L"+gx(i)+","+gy(v)});
  let marks="";
  M.forEach((m,i)=>{ if(isKey(m)) marks+='<circle cx="'+gx(i+1)+'" cy="'+gy(m.ev)+'" r="4" fill="var(--c-'+
    kind(m)+')" stroke="var(--panel)" stroke-width="1.5"/>'; });
  svg.innerHTML=
    '<path d="'+d+'L'+GW+','+(GH/2)+'L0,'+(GH/2)+'Z" fill="currentColor" fill-opacity=".10"/>'+
    '<line x1="0" y1="'+(GH/2)+'" x2="'+GW+'" y2="'+(GH/2)+'" stroke="currentColor" stroke-opacity=".35" stroke-dasharray="3 3"/>'+
    '<path d="'+d+'" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linejoin="round"/>'+
    marks+'<circle id="mk" r="5" fill="none" stroke="var(--ink)" stroke-width="2"/>';
  svg.onclick=e=>{
    const r=svg.getBoundingClientRect(), i=Math.round((e.clientX-r.left)/r.width*(pts.length-1));
    cur=Math.max(-1,Math.min(M.length-1,i-1)); render();
  };
  marker();
}
function marker(){
  const c=document.getElementById("mk"); if(!c)return;
  const i=cur+1; c.setAttribute("cx",gx(i)); c.setAttribute("cy",gy(pts[i]));
}
// Clock remaining, both sides, across the game. It is here because time is the
// one thing the position cannot tell you afterwards, and because this engine is
// known to hoard it (BUGS.md 11): the shape of these lines is that defect, or
// its absence, drawn from real games rather than from a self-play harness.
function timeGraph(){
  const card=document.getElementById("timecard");
  const have=M.some(m=>m.left>=0);
  card.style.display=have?"":"none";
  if(!have)return;
  const svg=document.getElementById("t");
  const TW=Math.max(200,Math.round(svg.getBoundingClientRect().width)||400), TH=84;
  svg.setAttribute("viewBox","0 0 "+TW+" "+TH);
  const base=baseMs()||0;
  const peak=Math.max(base,...M.map(m=>m.left>=0?m.left:0))||1;
  const tx=i=>i*TW/Math.max(1,M.length-1);
  const ty=v=>TH-4-(v/peak)*(TH-10);
  const line=w=>{
    let d="",started=false;
    M.forEach((m,i)=>{ if(m.w!==w||m.left<0)return;
      d+=(started?"L":"M")+tx(i)+","+ty(m.left); started=true; });
    return d;
  };
  const w=line(true), b=line(false);
  svg.innerHTML=
    (w?'<path d="'+w+'" fill="none" stroke="var(--ink)" stroke-width="1.7"/>':'')+
    (b?'<path d="'+b+'" fill="none" stroke="var(--faint)" stroke-width="1.7" stroke-dasharray="4 3"/>':'');
  const last=[...M].reverse().find(m=>m.left>=0);
  document.getElementById("tlab").textContent=
    (H.tc?H.tc+"s · ":"")+(last?secs(last.left)+" left at the end":"");
}

function loadGame(n){
  gi=n; H=GAMES[gi].head; M=GAMES[gi].moves; flipped=!!H.flip; cur=-1;
  POS=[unpack(H.startFen)];
  for(const m of M) POS.push(applyMove(POS[POS.length-1].slice(),m.uci));
  pts=[H.startEval].concat(M.map(m=>m.ev));
  document.getElementById("ttl").innerHTML=esc(H.white)+'<span class="vs">vs</span>'+esc(H.black);
  document.getElementById("cond").innerHTML=
    '<b>'+esc(H.result)+'</b>'+M.length+' moves · '+esc(H.engine.split("/").pop())+' depth '+H.depth;
  buildSheet(); summary(); graph(); timeGraph(); render();
  // A new game starts at the top of its panes, not wherever the last one was
  // scrolled to.
  document.querySelectorAll(".pane").forEach(e=>e.scrollTop=0);
  if(GAMES.length>1)document.getElementById("pick").value=gi;
  document.querySelectorAll("#idxtable tbody tr").forEach(tr=>
    tr.classList.toggle("sel", +tr.dataset.i===gi));
}
let rz; addEventListener("resize",()=>{clearTimeout(rz);rz=setTimeout(()=>{graph();timeGraph()},120)});
// --- Archive index -------------------------------------------------------
// Everything here is derived rather than stored. Accuracy, blunders and the
// clock all come out of the records the page already carries; date, ratings
// and opening come from META, read from the PGN tags at assembly time. Nothing
// needed re-analysing to build this table, which is why it exists.
function ourSide(g){ return g.head.flip ? false : true; }   // true = we are White
function rowStats(g){
  const white=ourSide(g), M=g.moves;
  let spent=0, left=null, mine=0;
  for(const m of M){
    if(m.w!==white) continue;
    mine++;
    if(typeof m.ms==="number" && m.ms>0) spent+=m.ms;
    if(typeof m.left==="number" && m.left>0) left=m.left;
  }
  const acc = white ? g.head.accW : g.head.accB;
  const cp  = white ? g.head.cpW  : g.head.cpB;
  // Result from our side, which is the only orientation worth reading a
  // 119-row table in.
  const r=g.head.result;
  const sc = r==="1/2-1/2" ? 0.5 : ((r==="1-0")===white ? 1 : 0);
  return {white,acc,cp,blunders:g.head.blunders,moves:mine,spent,left,sc};
}
const IDX = GAMES.map((g,i)=>{
  const m = META[i]||{}, st = rowStats(g);
  const opp = st.white ? g.head.black : g.head.white;
  const oppElo = +( st.white ? m.belo : m.welo ) || 0;
  const myElo  = +( st.white ? m.welo : m.belo ) || 0;
  const date=(m.date||"").replace(/\./g,"-"), time=(m.time||"").slice(0,5);
  return {i, id:m.id||"", date, time, when:date+" "+time,
          opp, oppElo, myElo, colour: st.white?"W":"B",
          res: st.sc===1?"win":st.sc===0?"loss":"draw", sc:st.sc,
          eco:m.eco||"", opening:(m.opening||"").split(":")[0],
          term:m.term||"", acc:st.acc, cp:st.cp, blunders:st.blunders,
          moves:st.moves, spent:st.spent, left:st.left};
});
const COLS=[
  {k:"when",   t:"date",     f:r=>r.when.trim()},
  {k:"colour", t:"",         f:r=>r.colour},
  {k:"opp",    t:"opponent", f:r=>r.opp},
  {k:"oppElo", t:"rating",   n:1, f:r=>r.oppElo||""},
  {k:"res",    t:"result",   f:r=>r.res, cls:r=>r.sc===1?"win":r.sc===0?"loss":""},
  {k:"opening",t:"opening",  f:r=>r.opening},
  {k:"moves",  t:"moves",    n:1, f:r=>r.moves},
  {k:"acc",    t:"accuracy", n:1, f:r=>r.moves?r.acc.toFixed(1)+"%":"—"},
  {k:"cp",     t:"cp loss",  n:1, f:r=>r.moves?r.cp.toFixed(1):"—"},
  {k:"blunders",t:"blund.",  n:1, f:r=>r.blunders||""},
  {k:"spent",  t:"time used",n:1, f:r=>r.spent?Math.round(r.spent/1000)+"s":""},
  {k:"left",   t:"left",     n:1, f:r=>r.left!=null?Math.round(r.left/1000)+"s":""},
];
let sortKey="when", sortDir=-1;
function drawIndex(){
  const rows=IDX.slice().sort((a,b)=>{
    const x=a[sortKey], y=b[sortKey];
    if(x===y) return a.i-b.i;
    if(typeof x==="number"&&typeof y==="number") return (x-y)*sortDir;
    return String(x).localeCompare(String(y))*sortDir;
  });
  let h="<thead><tr>";
  for(const c of COLS)
    h+='<th class="'+(c.n?"num":"")+'" data-k="'+c.k+'">'+c.t+
       (sortKey===c.k?(sortDir>0?" ↑":" ↓"):"")+"</th>";
  h+="</tr></thead><tbody>";
  for(const r of rows){
    h+='<tr data-i="'+r.i+'"'+(r.i===gi?' class="sel"':'')+">";
    for(const c of COLS){
      const extra=c.cls?c.cls(r):"";
      h+='<td class="'+(c.n?"num ":"")+extra+'">'+c.f(r)+"</td>";
    }
    h+="</tr>";
  }
  document.getElementById("idxtable").innerHTML=h+"</tbody>";
  document.querySelectorAll("#idxtable th").forEach(th=>th.onclick=()=>{
    const k=th.dataset.k;
    if(sortKey===k) sortDir=-sortDir; else { sortKey=k; sortDir=(k==="when")?-1:1; }
    drawIndex();
  });
  document.querySelectorAll("#idxtable tbody tr").forEach(tr=>tr.onclick=()=>{
    loadGame(+tr.dataset.i);
    document.getElementById("board").scrollIntoView({behavior:"smooth",block:"center"});
  });
}
// Strength by opponent band, the cut that has mattered every time this archive
// has been read. Bands match docs/HANDOFF.md so the two can be compared.
function drawBands(){
  const B=[[0,1500,"under 1500"],[1500,1900,"1500-1900"],[1900,2100,"1900-2100"],
           [2100,2300,"2100-2300"],[2300,9999,"2300+"]];
  let h="<tr><th>opponent</th><th>games</th><th>W-D-L</th><th>score</th><th>accuracy</th></tr>";
  let any=false;
  for(const [lo,hi,name] of B){
    const g=IDX.filter(r=>r.oppElo>=lo&&r.oppElo<hi);
    if(!g.length) continue;
    any=true;
    const w=g.filter(r=>r.sc===1).length, d=g.filter(r=>r.sc===0.5).length, l=g.filter(r=>r.sc===0).length;
    const scored=g.filter(r=>r.moves);
    const acc=scored.length?scored.reduce((s,r)=>s+r.acc,0)/scored.length:0;
    h+="<tr><td>"+name+"</td><td>"+g.length+"</td><td>"+w+"-"+d+"-"+l+"</td><td>"+
       (100*(w+d/2)/g.length).toFixed(0)+"%</td><td>"+acc.toFixed(1)+"%</td></tr>";
  }
  document.getElementById("bandtab").innerHTML = any ? h : "";
}
if(GAMES.length>1&&META.length===GAMES.length){
  document.getElementById("index").classList.add("on");
  const dated=IDX.filter(r=>r.date).length;
  document.getElementById("idxsub").textContent =
    GAMES.length+" games"+(dated<GAMES.length?"  ·  "+(GAMES.length-dated)+" without tags":"");
  const tog=document.getElementById("idxtoggle");
  tog.onclick=()=>{
    const t=document.querySelector(".idxwrap"), b=document.getElementById("bandtab");
    const hidden=t.style.display==="none";
    t.style.display=b.style.display=hidden?"":"none";
    tog.textContent=hidden?"hide":"show";
  };
  drawIndex(); drawBands();
}
if(GAMES.length>1){
  const sel=document.getElementById("pick");
  GAMES.forEach((g,i)=>{
    const o=document.createElement("option"); o.value=i;
    const bl=g.head.blunders, acc=g.head.flip?g.head.accB:g.head.accW;
    o.textContent=g.head.white+" vs "+g.head.black+"  ·  "+g.head.result+
      "  ·  "+acc.toFixed(1)+"%"+(bl?"  ·  "+bl+" blunder"+(bl>1?"s":""):"");
    sel.appendChild(o);
  });
  sel.onchange=()=>loadGame(+sel.value);
  document.getElementById("picker").style.display="";
  document.getElementById("gcount").textContent=GAMES.length+" games";
}
document.getElementById("note").textContent=
  "Accuracy depends on the analysing engine, its depth and the curve used, so these numbers "+
  "compare games reviewed the same way and nothing else. Best, Excellent, Good, Inaccuracy, Mistake "+
  "and Blunder use Chess.com’s published limits on a fixed win-probability curve. Brilliant, Great, "+
  "Miss and Book have no published rule, so they are this tool’s reading of Chess.com’s descriptions, "+
  "and Book comes from a small opening list. The named terms describe what moved in ChessBot’s own "+
  "evaluation, which is not why "+H.engine.split("/").pop()+" scored the move that way.";
loadGame(0);
</script></body></html>
)HTML";

    auto sub = [&](const std::string& key, const std::string& val) {
        const size_t p = html.find(key);
        if (p != std::string::npos) html.replace(p, key.size(), val);
    };
    sub("__TITLE__", title);
    sub("__GAMES__", gamesJson);
    sub("__META__", metaJson);
    sub("__PIECES__", pieceImages());
    return html;
}

}  // namespace

int main(int argc, char** argv) {
    // Answered before anything is initialised: a cache asks the binary what
    // format it writes, and the move-table loader prints to stdout, which would
    // otherwise end up in the answer.
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--record-version") {
            std::printf("%d\n", RECORD_VERSION);
            return 0;
        }

    initMoveLookupTables();

    std::string pgnPath, enginePath = "/usr/games/stockfish";
    int depth = 16, hashMb = 256;
    // Standard UCI engines take no arguments. ChessBot is the exception — its
    // default mode opens a window — so reviewing with it needs
    //   --engine ./chessbot --engine-arg --uci
    std::vector<std::string> engineArgs;
    std::string annotateOut, htmlOut, jsonOut, archiveOut, pgnDir;
    bool explain = false, flip = false;
    int multipv = 1;
    std::string bookPath;
    std::string me;
    std::vector<std::string> archiveIn;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : ""; };
        if      (a == "--engine") enginePath = next();
        else if (a == "--depth")  depth = std::atoi(next());
        else if (a == "--hash")   hashMb = std::atoi(next());
        else if (a == "--engine-arg") engineArgs.push_back(next());
        else if (a == "--annotate")   annotateOut = next();
        else if (a == "--html")       htmlOut = next();
        else if (a == "--flip")       flip = true;
        else if (a == "--me")         me = next();
        else if (a == "--json")       jsonOut = next();
        else if (a == "--archive")    archiveOut = next();
        else if (a == "--pgn-dir")    pgnDir = next();
        else if (a == "--explain")    explain = true;
        else if (a == "--multipv")    multipv = std::atoi(next());
        else if (a == "--book")       bookPath = next();
        else if (a[0] != '-') { if (pgnPath.empty()) pgnPath = a; archiveIn.push_back(a); }
        else { std::printf("unknown option: %s\n", a.c_str()); return 1; }
    }
    if (pgnPath.empty()) {
        std::printf("usage: review <game.pgn> [--engine <path>] [--engine-arg <a>]\n"
                    "                 [--depth N] [--hash MB] [--annotate out.pgn]\n"
                    "                 [--html out.html] [--json out.json] [--flip|--me <player>]\n"
                    "                 [--explain] [--multipv 2] [--book openings.tsv]\n"
                    "  --multipv 2 also finds each position's second-best move, which is what\n"
                    "       the Great class needs; it changes the scores slightly, so the archive\n"
                    "       profile does not use it\n"
                    "  a file holding several games is reviewed in full, into one page\n"
                    "       review --archive all.html g1.json g2.json ...\n"
                    "  default engine is /usr/games/stockfish; ChessBot needs --engine-arg --uci\n");
        return 1;
    }

    // Assembling an archive reads records that --json already produced, so it
    // costs no analysis: adding one game to a 70-game archive re-reviews one
    // game, not seventy.
    if (!archiveOut.empty()) {
        // A record is named <game-id>.json, and the PGN it came from ends in
        // " - <game-id>.pgn". That naming is the only link between the two, so
        // the map is built once here rather than by scanning the directory per
        // record.
        std::map<std::string, std::string> pgnById;
        if (!pgnDir.empty()) {
            for (const auto& e : std::filesystem::directory_iterator(pgnDir)) {
                const std::string name = e.path().filename().string();
                if (name.size() < 5 || name.substr(name.size() - 4) != ".pgn") continue;
                const std::string stem = name.substr(0, name.size() - 4);
                const size_t dash = stem.rfind(" - ");
                pgnById[dash == std::string::npos ? stem : stem.substr(dash + 3)] =
                    e.path().string();
            }
        }

        std::string games = "[", meta = "[";
        int n = 0, withMeta = 0;
        for (const std::string& in : archiveIn) {
            std::ifstream f(in);
            if (!f) { std::printf("cannot read %s\n", in.c_str()); return 1; }
            const std::string rec((std::istreambuf_iterator<char>(f)),
                                   std::istreambuf_iterator<char>());
            if (rec.empty()) continue;
            if (n++) { games += ","; meta += ","; }
            games += rec;

            const std::string stem = std::filesystem::path(in).stem().string();
            auto it = pgnById.find(stem);
            std::string m = (it == pgnById.end()) ? "null" : indexMeta(it->second);
            if (m != "null") ++withMeta;
            // The id travels with the metadata so a row can link to the game on
            // Lichess, which is the one thing this page cannot reconstruct.
            if (m != "null") m.insert(1, "\"id\":\"" + stem + "\",");
            meta += m;
        }
        games += "]"; meta += "]";
        if (!pgnDir.empty() && withMeta < n)
            std::printf("note: %d of %d records had no matching PGN in %s; "
                        "their index rows will be blank\n", n - withMeta, n, pgnDir.c_str());
        if (!n) { std::printf("no game records given\n"); return 1; }
        std::ofstream out(archiveOut);
        if (!out.is_open()) { std::printf("cannot write %s\n", archiveOut.c_str()); return 1; }
        char t[64];
        std::snprintf(t, sizeof t, "%d reviewed games", n);
        out << htmlReport(games, t, meta);
        std::printf("archive of %d games written to %s\n", n, archiveOut.c_str());
        return 0;
    }

    std::vector<PgnGame> games;
    std::string err;
    if (!readPgnAll(pgnPath, games, &err)) {
        std::printf("cannot read %s: %s\n", pgnPath.c_str(), err.c_str());
        return 1;
    }

    UciEngine engine;
    if (!engine.start(enginePath, hashMb, engineArgs)) {
        std::printf("cannot start engine: %s\n", enginePath.c_str());
        return 1;
    }
    if (multipv > 1) engine.setOption("MultiPV", std::to_string(multipv));

    // The opening book is data beside the binary, found the way the piece images
    // are. Without it the review still runs and simply has no Book class.
    OpeningBook book;
    {
        std::error_code ec;
        std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
        const std::filesystem::path base = ec ? std::filesystem::path(".") : exe.parent_path();
        const std::string path = bookPath.empty() ? (base / "data" / "openings.tsv").string() : bookPath;
        if (!book.load(path))
            std::fprintf(stderr, "note: no opening book at %s, so there is no Book class\n", path.c_str());
    }

    std::vector<std::string> records;
    std::string annotated;

    for (size_t gameIndex = 0; gameIndex < games.size(); ++gameIndex) {
    const PgnGame& game = games[gameIndex];
    // --me orients each game to that player without having to know, per game,
    // which colour they had. --flip still forces it for a one-off.
    const bool flipThis = flip || (!me.empty() && game.tags.black == me);
    engine.newGame();

    if (games.size() > 1)
        std::printf("=== game %zu of %zu ===\n", gameIndex + 1, games.size());
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
    std::vector<int> rawScore(game.moves.size() + 1, 0);     // before the mate clamp
    std::vector<int> secondScore(game.moves.size() + 1, 0);
    std::vector<char> haveSecond(game.moves.size() + 1, 0);
    std::vector<std::string> best(game.moves.size() + 1);

    for (size_t i = 0; i <= game.moves.size(); ++i) {
        const std::string mv = engine.bestMove(uci, /*ms=*/0, /*nodes=*/0, depth);
        if (!engine.haveScore()) break;          // terminal position: nothing to search
        rawScore[i] = engine.lastScore();
        score[i] = clampScore(engine.lastScore());
        if (engine.haveSecondScore()) {
            secondScore[i] = clampScore(engine.lastSecondScore());
            haveSecond[i] = 1;
        }
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
    int tally2[2][10] = {};   // CLS_ORDER
    std::string lastOpening;

    auto bucket = [](const char* label) {
        if (!std::strcmp(label, "Blunder"))    return 0;
        if (!std::strcmp(label, "Mistake"))    return 1;
        if (!std::strcmp(label, "Inaccuracy")) return 2;
        if (!std::strcmp(label, "Good"))       return 3;
        if (!std::strcmp(label, "Excellent"))  return 4;
        return 5;
    };

    // "900+10", "600", "-" (correspondence) or absent.
    int baseMs = -1, incMs = 0;
    {
        const std::string& tc = game.tags.timeControl;
        if (!tc.empty() && std::isdigit((unsigned char)tc[0])) {
            baseMs = std::atoi(tc.c_str()) * 1000;
            const size_t plus = tc.find('+');
            if (plus != std::string::npos) incMs = std::atoi(tc.c_str() + plus + 1) * 1000;
        }
    }

    std::vector<MoveNote> notes(game.moves.size());
    std::vector<HtmlMove> rows;
    // The HTML report shows the same attribution the text one does, so asking
    // for a page implies asking for the explanations behind it.
    const bool wantTerms = explain || !htmlOut.empty();

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

        std::string clsName, openingName;
        {
            const Move& mv = game.moves[i];
            ClsInput in;
            in.wpBefore = winPercent(score[i]);
            in.wpPlayed = winPercent(played);
            in.wpLoss = wpLoss;
            in.playedIsBest = (toUciMove(mv) == best[i]);
            in.haveSecond = haveSecond[i] != 0;
            in.wpSecond = in.haveSecond ? winPercent(secondScore[i]) : 0.0;
            in.book = !book.empty() && book.contains(position[i + 1]);
            // Only a move that cost nothing can be Brilliant, so the line is not
            // walked for the rest.
            in.materialDelta = (wpLoss < 2.0)
                ? materialAlongLine(position[i], position[i + 1], mv, pv[i + 1]) : 0;
            in.offered = false;
            if (wpLoss < 2.0) {
                // What this very move took is set against what the opponent can
                // take back, or every even trade would read as an offered piece.
                const int gained = pieceValue(mv.capturedPiece.type()) * 100;
                Board a = position[i + 1].copyForSearch();
                for (const Move& c : generateLegalMoves(a, a.activeColor))
                    if (c.capturedPiece.color() != COLOR_NONE && see(a, c) - gained >= 250) { in.offered = true; break; }
            }
            in.freeCapture = mv.capturedPiece.color() != COLOR_NONE && see(position[i], mv) >= 200;
            in.recapture = i > 0 && game.moves[i - 1].capturedPiece.color() != COLOR_NONE
                           && game.moves[i - 1].to == mv.to;
            in.opponentLoss = i > 0 ? rows[i - 1].wpLoss : 0.0;
            in.missedMate = rawScore[i] >= 29000 && -rawScore[i + 1] < 29000;
            clsName = clsFor(in);
            if (in.book) openingName = book.name(position[i + 1]);
            if (!openingName.empty()) lastOpening = openingName;
            for (int c = 0; c < 10; ++c)
                if (clsName == CLS_ORDER[c]) { ++tally2[s][c]; break; }
        }

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

        std::string termsText;
        if (wantTerms && wpLoss >= 5.0) {
            const Board wouldBe = atEndOfPv(position[i], pv[i]);
            const Board didGo   = atEndOfPv(position[i + 1], pv[i + 1]);
            const std::vector<TermDelta> d = termDiff(wouldBe, didGo);
            if (!d.empty() && std::abs(d[0].delta) >= 20) {
                char tb[64];
                for (size_t k = 0; k < d.size() && k < 3; ++k) {
                    if (std::abs(d[k].delta) < 20) break;
                    std::snprintf(tb, sizeof tb, "%s%s %+d",
                                  termsText.empty() ? "" : ", ", d[k].name, d[k].delta);
                    termsText += tb;
                }
            } else {
                termsText = "no term accounts for it";
            }
        }
        {
            HtmlMove r;
            // What the move cost in time. The clock in the file is what was
            // left *after* the move and after the increment was added, so the
            // time spent is the previous reading for that side, plus the
            // increment, minus this one.
            if (i < game.clockMs.size() && game.clockMs[i] >= 0) {
                r.leftMs = game.clockMs[i];
                const int prev = (i >= 2 && game.clockMs[i - 2] >= 0)
                               ? game.clockMs[i - 2] : baseMs;
                if (prev >= 0) {
                    const int spent = prev + incMs - game.clockMs[i];
                    r.ms = (spent >= 0) ? spent : -1;
                }
            }
            r.san = san[i];
            r.bestSan = (bestSan[i].empty() ? best[i] : bestSan[i]);
            r.bestUci = best[i];
            if (r.bestSan == r.san) { r.bestSan.clear(); r.bestUci.clear(); }
            if (r.bestUci == r.uci) r.bestUci.clear();
            r.label = label;
            r.cls = clsName;
            r.opening = openingName;
            r.terms = termsText;
            r.uci = toUciMove(game.moves[i]);
            r.evalAfter = (i % 2 == 0) ? -score[i + 1] : score[i + 1];
            r.wpLoss = wpLoss;
            r.cpLoss = cpLoss;
            r.white = white;
            rows.push_back(r);
        }

        if (clsName == "Brilliant" || clsName == "Great" || clsName == "Miss") {
            // Lines the criticised-move format cannot express: a good move, or a
            // miss that cost less than the 5 points that earn a criticism.
            std::printf("%3zu.%s%-8s %-11s (chess.com-style class; %s)\n",
                        i / 2 + 1, white ? " " : "..", san[i].c_str(), clsName.c_str(),
                        clsName == "Miss" ? "an opportunity was not taken"
                      : clsName == "Brilliant" ? "a sacrifice that holds" : "the only good move");
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
                if (termsText == "no term accounts for it") {
                    std::printf("        no term accounts for it — this engine's "
                                "evaluation does not see what was lost\n");
                } else {
                    std::printf("        %s  (white's point of view, at the end "
                                "of each line)\n", termsText.c_str());
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
    // The same moves in Chess.com's classes. Book, Brilliant and Great need data
    // this tool only has when the book file is present and, for Great, when run
    // with --multipv 2.
    std::printf("\n%-8s", "");
    for (int c = 0; c < 10; ++c) std::printf(" %-10s", CLS_ORDER[c]);
    std::printf("\n");
    for (int s = 0; s < 2; ++s) {
        if (!count[s]) continue;
        std::printf("%-8s", s == 0 ? "White" : "Black");
        for (int c = 0; c < 10; ++c) std::printf(" %-10d", tally2[s][c]);
        std::printf("\n");
    }
    if (!lastOpening.empty()) std::printf("\nopening: %s\n", lastOpening.c_str());
    {
        Board start;
        if (!game.tags.startFen.empty()) start.setFromFEN(game.tags.startFen);
        const double accW = count[0] ? accSum[0] / count[0] : 0.0;
        const double accB = count[1] ? accSum[1] / count[1] : 0.0;
        const double cpW = count[0] ? (double)cpSum[0] / count[0] : 0.0;
        const double cpB = count[1] ? (double)cpSum[1] / count[1] : 0.0;
        const double accArr[2] = {accW, accB};
        const double cpArr[2] = {cpW, cpB};
        records.push_back(gameRecord(game, rows, start.getFEN(), score[0],
                                     enginePath, depth, accArr, cpArr, count, flipThis,
                                     lastOpening, multipv));
    }
    if (!annotateOut.empty()) {
        if (!annotated.empty()) annotated += "\n\n";
        annotated += toPgn(game.moves, game.tags, notes);
    }
    if (games.size() > 1) std::printf("\n");
    }  // end per-game loop

    if (!jsonOut.empty()) {
        std::ofstream f(jsonOut);
        if (!f.is_open()) { std::printf("\ncannot write %s\n", jsonOut.c_str()); return 1; }
        // One record per game, so a multi-game file caches as one entry.
        f << (records.size() == 1 ? records[0] : "");
        if (records.size() != 1) {
            std::printf("\n--json writes a single game; %zu were read. Use --html.\n",
                        records.size());
            return 1;
        }
        std::printf("\nrecord written to %s\n", jsonOut.c_str());
    }
    if (!htmlOut.empty()) {
        std::ofstream f(htmlOut);
        if (!f.is_open()) { std::printf("\ncannot write %s\n", htmlOut.c_str()); return 1; }
        std::string all = "[";
        for (size_t k = 0; k < records.size(); ++k) all += (k ? "," : "") + records[k];
        all += "]";
        char title[128];
        if (records.size() == 1)
            std::snprintf(title, sizeof title, "%s vs %s",
                          games[0].tags.white.c_str(), games[0].tags.black.c_str());
        else
            std::snprintf(title, sizeof title, "%zu reviewed games", records.size());
        f << htmlReport(all, title);
        std::printf("\nreport written to %s (%zu game%s)\n", htmlOut.c_str(),
                    records.size(), records.size() == 1 ? "" : "s");
    }
    if (!annotateOut.empty()) {
        std::ofstream f(annotateOut);
        if (!f.is_open()) {
            std::printf("\ncannot write %s\n", annotateOut.c_str());
            return 1;
        }
        f << annotated;
        std::printf("\nannotated PGN written to %s\n", annotateOut.c_str());
    }
    return 0;
}

