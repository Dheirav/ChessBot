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
#include <filesystem>
#include <fstream>
#include <iterator>
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

// One row of the HTML report. Collected during the pass that prints the text
// report, so the two can never disagree about what a move was worth.
struct HtmlMove {
    std::string san, bestSan, label, terms, uci;
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
// One game as a record the page can load. Kept separate from the page itself so
// an archive of seventy games is seventy of these inside one document, rather
// than seventy documents each carrying its own copy of the stylesheet and the
// same twelve piece images -- which measured 3.3 MB against 0.4 MB.
std::string gameRecord(const PgnGame& game, const std::vector<HtmlMove>& moves,
                       const std::string& startFen, int startEval,
                       const std::string& engine, int depth,
                       const double acc[2], const double cpAvg[2],
                       const int count[2], bool flip) {
    // Counted here rather than passed in: the picker needs one number per game
    // and this is the only place that has the moves and the labels together.
    int blunders = 0;
    for (const HtmlMove& m : moves)
        if (m.label == "Blunder" && m.white != flip) ++blunders;
    std::string j = "[";
    for (size_t i = 0; i < moves.size(); ++i) {
        const HtmlMove& m = moves[i];
        char buf[1024];
        std::snprintf(buf, sizeof buf,
            "%s{\"n\":%zu,\"w\":%s,\"san\":\"%s\",\"ev\":%d,"
            "\"wp\":%.1f,\"cp\":%d,\"best\":\"%s\",\"label\":\"%s\","
            "\"terms\":\"%s\",\"uci\":\"%s\"}",
            i ? "," : "", i, m.white ? "true" : "false", m.san.c_str(),
            m.evalAfter, m.wpLoss, m.cpLoss,
            m.bestSan.c_str(), m.label.c_str(), m.terms.c_str(), m.uci.c_str());
        j += buf;
    }
    j += "]";

    char head[2560];
    std::snprintf(head, sizeof head,
        "{\"white\":\"%s\",\"black\":\"%s\",\"result\":\"%s\",\"engine\":\"%s\","
        "\"depth\":%d,\"startFen\":\"%s\",\"startEval\":%d,"
        "\"accW\":%.1f,\"accB\":%.1f,\"cpW\":%.1f,\"cpB\":%.1f,\"nW\":%d,\"nB\":%d,"
        "\"flip\":%s,\"blunders\":%d}",
        game.tags.white.c_str(), game.tags.black.c_str(), game.tags.result.c_str(),
        engine.c_str(), depth, startFen.c_str(), startEval,
        count[0] ? acc[0] : 0.0, count[1] ? acc[1] : 0.0,
        count[0] ? cpAvg[0] : 0.0, count[1] ? cpAvg[1] : 0.0, count[0], count[1],
        flip ? "true" : "false", blunders);

    return std::string("{\"head\":") + head + ",\"moves\":" + j + "}";
}

std::string htmlReport(const std::string& gamesJson, const std::string& title) {
    std::string html = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>__TITLE__</title>
<style>
:root{
  --bg:#eceff3; --panel:#fff; --sunk:#f5f7f9; --ink:#0f1319; --dim:#5a6472; --faint:#8b95a2;
  --line:#dde3ea; --accent:#a8712c;
  --sq-light:#d6dde5; --sq-dark:#6a7889; --sq-mark:#d7a13f;
  --blunder:#b02a1f; --mistake:#a96a09; --inacc:#7f7108; --ok:#3f6f45;
  --shade:rgba(15,19,25,.055); --shade2:rgba(15,19,25,.10);
  --wp-fill:#fcfcfa; --wp-edge:#0f1319; --bp-fill:#131820; --bp-edge:rgba(255,255,255,.34);
  --bar-w:#f2f3f0; --bar-b:#2b3138;
}
@media (prefers-color-scheme:dark){:root:not([data-theme="light"]){
  --bg:#0d1013; --panel:#161a1f; --sunk:#11151a; --ink:#e9edf2; --dim:#98a3b1; --faint:#6f7a87;
  --line:#252c34; --accent:#d9a05b;
  --sq-light:#7c8795; --sq-dark:#4a5560; --sq-mark:#e0ad55;
  --blunder:#e35c4d; --mistake:#dd9a37; --inacc:#c3ad32; --ok:#6fa871;
  --shade:rgba(233,237,242,.06); --shade2:rgba(233,237,242,.12);
  --wp-fill:#f6f7f4; --wp-edge:#090c10; --bp-fill:#0a0d12; --bp-edge:rgba(255,255,255,.72);
  --bar-w:#e8eae6; --bar-b:#1b2026;
}}
:root[data-theme="dark"]{
  --bg:#0d1013; --panel:#161a1f; --sunk:#11151a; --ink:#e9edf2; --dim:#98a3b1; --faint:#6f7a87;
  --line:#252c34; --accent:#d9a05b;
  --sq-light:#7c8795; --sq-dark:#4a5560; --sq-mark:#e0ad55;
  --blunder:#e35c4d; --mistake:#dd9a37; --inacc:#c3ad32; --ok:#6fa871;
  --shade:rgba(233,237,242,.06); --shade2:rgba(233,237,242,.12);
  --wp-fill:#f6f7f4; --wp-edge:#090c10; --bp-fill:#0a0d12; --bp-edge:rgba(255,255,255,.72);
  --bar-w:#e8eae6; --bar-b:#1b2026;
}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);
  font:15px/1.55 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;-webkit-font-smoothing:antialiased}
.wrap{max-width:980px;margin:0 auto;padding:30px 20px 56px}
.mono{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;font-variant-numeric:tabular-nums}

/* Header: the two names carry the page, the conditions sit under them in the
   fixed pitch a scoresheet uses. */
.picker{display:flex;align-items:center;gap:12px;margin-bottom:16px}
.picker select{flex:1;min-width:0;font:inherit;font-size:13.5px;padding:7px 10px;
  border:1px solid var(--line);border-radius:7px;background:var(--panel);color:var(--ink)}
.gcount{color:var(--faint);font-size:12px;letter-spacing:.06em;text-transform:uppercase}
header{display:flex;align-items:flex-end;gap:16px;flex-wrap:wrap;
  padding-bottom:14px;border-bottom:2px solid var(--ink);margin-bottom:22px}
h1{font:600 30px/1.15 ui-serif,Georgia,"Times New Roman",serif;margin:0;
  letter-spacing:-.015em;text-wrap:balance}
h1 .vs{color:var(--faint);font-style:italic;font-size:22px;padding:0 6px}
.cond{margin-left:auto;text-align:right;font-size:12px;color:var(--dim);line-height:1.7}
.cond b{display:block;font-size:19px;color:var(--ink);font-weight:600}

.cols{display:grid;grid-template-columns:minmax(300px,450px) minmax(280px,1fr);
  gap:20px;align-items:start}
@media(max-width:900px){.cols{grid-template-columns:1fr}}
.card{background:var(--panel);border:1px solid var(--line);border-radius:10px}
.stack{display:flex;flex-direction:column;gap:14px}

/* Board with an evaluation bar beside it, the arrangement every analysis board
   uses: the bar answers "who is winning" before you have read anything. */
.play{display:grid;grid-template-columns:14px minmax(0,1fr);gap:11px;padding:13px}
.bar{border-radius:4px;overflow:hidden;background:var(--bar-b);position:relative}
.bar i{position:absolute;left:0;right:0;bottom:0;background:var(--bar-w);
  transition:height .18s ease}
.bar.flip i{bottom:auto;top:0}
.boardbox{container-type:inline-size;border-radius:5px;overflow:hidden;
  box-shadow:0 1px 3px rgba(0,0,0,.14)}
/* minmax(0,1fr), not 1fr: a bare 1fr keeps an automatic minimum of its content,
   so a glyph larger than its share silently stretches that rank and the board
   stops being a grid of equal squares. */
#board{display:grid;grid-template-columns:repeat(8,minmax(0,1fr));
  grid-template-rows:repeat(8,minmax(0,1fr));aspect-ratio:1;user-select:none;overflow:hidden}
#board div{position:relative;display:flex;align-items:center;justify-content:center;
  font-size:8.6cqi;line-height:1;overflow:hidden}
.lt{background:var(--sq-light)}.dk{background:var(--sq-dark)}
/* Solid glyphs for both sides: the outline set for White disappears on a light
   square in most system fonts. Outlined through tokens, no shadow -- a shadow
   at this size just muddies the shape. */
/* paint-order matters more than the stroke width here: -webkit-text-stroke is
   centred on the glyph outline, so without this the stroke eats half the fill
   and a white piece reads as a dark one -- the two sides became almost
   indistinguishable in the light theme. Stroke behind fill keeps both solid. */
#board img{width:86%;height:86%;object-fit:contain;
  filter:drop-shadow(0 1px 1px rgba(0,0,0,.28));pointer-events:none}
.wp,.bp{paint-order:stroke fill}
.wp{color:var(--wp-fill);-webkit-text-stroke:1.6px var(--wp-edge)}
.bp{color:var(--bp-fill);-webkit-text-stroke:1.6px var(--bp-edge)}
.hl::after{content:"";position:absolute;inset:0;box-shadow:inset 0 0 0 3px var(--sq-mark)}
/* Coordinates on the edge squares: a board being read from, rather than played
   on, has to let you check a square against the notation beside it. */
.co{position:absolute;font:600 2.5cqi/1 ui-monospace,SFMono-Regular,Menlo,monospace;opacity:.7}
.co.f{right:5%;bottom:3%}.co.r{left:5%;top:3%}
.lt .co{color:var(--sq-dark)}.dk .co{color:var(--sq-light)}
.nav{display:flex;gap:6px;padding:0 14px 13px;align-items:center}
button{font:inherit;font-size:14px;padding:5px 11px;border:1px solid var(--line);
  border-radius:6px;background:var(--panel);color:var(--ink);cursor:pointer;line-height:1.3}
button:hover{background:var(--shade)}
button:focus-visible,.ply:focus-visible{outline:2px solid var(--accent);outline-offset:1px}
.ply-count{margin-left:auto;color:var(--faint);font-size:12.5px}

/* The move under inspection, given the room the explanation needs. */
.detail{padding:15px 17px;min-height:118px;border-left:3px solid var(--line)}
.detail.Blunder{border-left-color:var(--blunder)}
.detail.Mistake{border-left-color:var(--mistake)}
.detail.Inaccuracy{border-left-color:var(--inacc)}
.dhead{display:flex;align-items:baseline;gap:10px;flex-wrap:wrap;margin-bottom:9px}
.dmove{font:600 22px/1.15 ui-monospace,SFMono-Regular,Menlo,monospace;letter-spacing:-.02em}
.badge{font-size:11px;font-weight:700;letter-spacing:.08em;text-transform:uppercase;
  padding:3px 9px;border-radius:99px;color:#fff}
.b-Blunder{background:var(--blunder)}.b-Mistake{background:var(--mistake)}
.b-Inaccuracy{background:var(--inacc)}
.b-Good,.b-Excellent,.b-Best{background:transparent;color:var(--ok);
  border:1px solid currentColor}
.deval{margin-left:auto;font-size:14px;color:var(--dim)}
.dline{font-size:13.5px;color:var(--dim);margin-top:5px}
.dline b{color:var(--ink);font-weight:650}
.terms{display:flex;gap:6px;flex-wrap:wrap;margin-top:9px}
.term{font-size:12px;padding:3px 9px;border-radius:5px;background:var(--sunk);
  border:1px solid var(--line);color:var(--ink)}
.term em{font-style:normal;color:var(--dim)}

.summary{display:grid;grid-template-columns:1fr 1fr;border-bottom:1px solid var(--line)}
.summary div{padding:13px 15px}
.summary div+div{border-left:1px solid var(--line)}
.summary span{font-size:11px;color:var(--dim);letter-spacing:.07em;text-transform:uppercase}
.summary b{display:block;margin-top:2px;font:600 26px/1.1 ui-monospace,SFMono-Regular,Menlo,monospace;
  font-variant-numeric:tabular-nums;letter-spacing:-.02em}
.summary i{font-style:normal;font-size:11.5px;color:var(--faint)}

.sheet{max-height:64vh;overflow-y:auto;padding:6px}
.row{display:grid;grid-template-columns:32px 1fr 1fr;gap:3px;align-items:stretch}
.no{color:var(--faint);font-size:12px;padding:5px 5px 0 0;text-align:right;
  font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-variant-numeric:tabular-nums}
.ply{display:flex;align-items:baseline;gap:7px;padding:4px 8px;border-radius:5px;
  border:0;background:transparent;color:var(--ink);cursor:pointer;text-align:left;width:100%;
  font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:13.5px}
.ply:hover{background:var(--shade)}
.ply.sel{background:var(--shade2);box-shadow:inset 2px 0 0 var(--accent)}
.ply .d{margin-left:auto;font-size:11px;color:var(--faint);font-variant-numeric:tabular-nums}
.ply.Blunder,.ply.Mistake{font-weight:700}
.ply.Blunder{color:var(--blunder)}.ply.Mistake{color:var(--mistake)}
.ply.Inaccuracy{color:var(--inacc)}
.ply.Blunder .d,.ply.Mistake .d,.ply.Inaccuracy .d{color:inherit;opacity:.85}

.graph{margin-top:16px;padding:14px 17px 8px}
.glab{display:flex;justify-content:space-between;font-size:11px;color:var(--faint);
  letter-spacing:.06em;text-transform:uppercase;margin-bottom:7px}
svg{display:block;width:100%;height:150px;overflow:visible}
.note{color:var(--faint);font-size:12px;margin-top:24px;line-height:1.7;max-width:72ch}
@media (prefers-reduced-motion:no-preference){.ply,button{transition:background-color .12s}}
</style></head><body><div class="wrap">
<div id="picker" class="picker" style="display:none">
  <span class="gcount mono" id="gcount"></span>
  <select id="pick" aria-label="Choose a game"></select>
</div>
<header>
  <h1 id="ttl"></h1>
  <div class="cond mono" id="cond"></div>
</header>
<div class="cols">
  <div class="stack">
    <div class="card">
      <div class="play">
        <div class="bar"><i id="bar" style="height:50%"></i></div>
        <div class="boardbox"><div id="board"></div></div>
      </div>
      <div class="nav">
        <button onclick="go(-999)" title="Start">&#8676;</button>
        <button onclick="go(-1)" title="Previous">&#8592;</button>
        <button onclick="go(1)" title="Next">&#8594;</button>
        <button onclick="go(999)" title="End">&#8677;</button>
        <button onclick="flipBoard()" title="Flip the board (F)" id="flipbtn">&#8645;</button>
        <span class="ply-count mono" id="pos"></span>
      </div>
    </div>
    <div class="card detail" id="detail"></div>
  </div>
  <div class="card">
    <div class="summary" id="acc"></div>
    <div class="sheet" id="list"></div>
  </div>
</div>
<div class="card graph">
  <div class="glab"><span>evaluation, white's point of view</span><span id="glab2"></span></div>
  <svg id="g" viewBox="0 0 600 150" preserveAspectRatio="none"></svg>
</div>
<p class="note" id="note"></p>
</div>
<script>
const GAMES=__GAMES__, P=__PIECES__;
const HAVE_IMG=Object.keys(P).length===12;
let gi=0, H=GAMES[0].head, M=GAMES[0].moves, POS=[];
const S={k:"♚",q:"♛",r:"♜",b:"♝",n:"♞",p:"♟"};
let cur=-1, flipped=!!H.flip;
const winPct=cp=>50+50*(2/(1+Math.exp(-0.00368208*cp))-1);

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
const label=m=>m.san+(m.label=="Blunder"?"??":m.label=="Mistake"?"?":m.wp>=5?"?!":"");

function detail(){
  const d=document.getElementById("detail");
  d.className="card detail"+(cur>=0&&M[cur].wp>=5?" "+M[cur].label:"");
  if(cur<0){
    d.innerHTML='<div class="dline">Starting position. Click any move, or use the '+
      '<b>&larr;</b> and <b>&rarr;</b> keys.</div>';return;
  }
  const m=M[cur];
  let h='<div class="dhead"><span class="dmove">'+((cur>>1)+1)+(m.w?". ":"… ")+label(m)+'</span>'+
        '<span class="badge b-'+m.label+'">'+m.label+'</span>'+
        '<span class="deval mono">'+fmt(m.ev)+'</span></div>';
  if(m.wp>=5){
    h+='<div class="dline">Gave up <b>'+m.wp.toFixed(1)+'%</b> win probability'+
       (m.cp?' — '+m.cp+' centipawns':'')+(m.best?'. Best was <b>'+m.best+'</b>':'')+'.</div>';
    if(m.terms==="no term accounts for it")
      h+='<div class="dline"><b>No evaluation term accounts for it.</b> ChessBot cannot '+
         'see what this move lost.</div>';
    else if(m.terms){
      h+='<div class="terms">'+m.terms.split(", ").map(t=>{
        const i=t.lastIndexOf(" ");
        return '<span class="term"><em>'+t.slice(0,i)+'</em> '+t.slice(i+1)+'</span>';
      }).join("")+'</div>';
    }
  } else h+='<div class="dline">Nothing given up here.</div>';
  d.innerHTML=h;
}
function render(){
  const m=cur<0?null:M[cur], ev=m?m.ev:H.startEval;
  board(POS[cur+1], m?m.uci:"");
  document.getElementById("bar").style.height=winPct(ev).toFixed(1)+"%";
  document.querySelector(".bar").classList.toggle("flip",flipped);
  document.getElementById("pos").textContent=
    (flipped?H.black:H.white)+" below · "+(cur+1)+" / "+M.length;
  document.querySelectorAll(".ply").forEach(e=>e.classList.toggle("sel",+e.dataset.i===cur));
  const s=document.querySelector(".ply.sel"); if(s)s.scrollIntoView({block:"nearest"});
  document.getElementById("glab2").textContent=fmt(ev);
  detail(); marker();
}
function go(d){ cur=d===-999?-1:d===999?M.length-1:Math.max(-1,Math.min(M.length-1,cur+d)); render(); }
function flipBoard(){ flipped=!flipped; render(); }
document.addEventListener("keydown",e=>{
  if(e.key==="ArrowLeft")go(-1);else if(e.key==="ArrowRight")go(1);
  else if(e.key==="Home")go(-999);else if(e.key==="End")go(999);
  else if(e.key==="f"||e.key==="F")flipBoard();else return;
  e.preventDefault();
});

function buildSheet(){
  const list=document.getElementById("list"); list.innerHTML="";
  for(let i=0;i<M.length;i+=2){
    const row=document.createElement("div"); row.className="row";
    row.innerHTML='<span class="no">'+((i>>1)+1)+'</span>';
    for(const j of [i,i+1]){
      if(j>=M.length){row.insertAdjacentHTML("beforeend","<span></span>");continue}
      const m=M[j], b=document.createElement("button");
      b.className="ply "+(m.wp>=5?m.label:""); b.dataset.i=j;
      b.innerHTML='<span>'+label(m)+'</span><span class="d">'+
        (m.wp>=5?"-"+m.wp.toFixed(1)+"%":fmt(m.ev))+'</span>';
      b.onclick=()=>{cur=j;render()};
      row.appendChild(b);
    }
    list.appendChild(row);
  }
}

const W=600,GH=150,CAP=700;
let pts=[];
const gx=i=>i*W/Math.max(1,pts.length-1);
const gy=v=>GH/2-Math.max(-CAP,Math.min(CAP,v))/CAP*(GH/2-6);
function graph(){
  let d="M"+gx(0)+","+gy(pts[0]);
  pts.forEach((v,i)=>{if(i)d+="L"+gx(i)+","+gy(v)});
  let marks="";
  M.forEach((m,i)=>{ if(m.wp>=10) marks+='<circle cx="'+gx(i+1)+'" cy="'+gy(m.ev)+'" r="3" fill="var(--'+
    (m.label=="Blunder"?"blunder":"mistake")+')"/>'; });
  document.getElementById("g").innerHTML=
    '<defs><linearGradient id="f" x1="0" y1="0" x2="0" y2="1">'+
    '<stop offset="0" stop-color="currentColor" stop-opacity=".20"/>'+
    '<stop offset="1" stop-color="currentColor" stop-opacity=".02"/></linearGradient></defs>'+
    '<path d="'+d+'L'+W+','+(GH/2)+'L0,'+(GH/2)+'Z" fill="url(#f)"/>'+
    '<line x1="0" y1="'+(GH/2)+'" x2="'+W+'" y2="'+(GH/2)+'" stroke="currentColor" stroke-opacity=".38" stroke-dasharray="3 3"/>'+
    '<path d="'+d+'" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linejoin="round"/>'+
    marks+'<circle id="mk" r="4" fill="var(--accent)" stroke="var(--panel)" stroke-width="1.5"/>';
}
function marker(){
  const c=document.getElementById("mk"); if(!c)return;
  const i=cur+1; c.setAttribute("cx",gx(i)); c.setAttribute("cy",gy(pts[i]));
}
function loadGame(n){
  gi=n; H=GAMES[gi].head; M=GAMES[gi].moves; flipped=!!H.flip; cur=-1;
  POS=[unpack(H.startFen)];
  for(const m of M) POS.push(applyMove(POS[POS.length-1].slice(),m.uci));
  pts=[H.startEval].concat(M.map(m=>m.ev));
  document.getElementById("ttl").innerHTML=H.white+'<span class="vs">vs</span>'+H.black;
  document.getElementById("cond").innerHTML=
    '<b>'+H.result+'</b>'+M.length+' moves · '+H.engine.split("/").pop()+' depth '+H.depth;
  document.getElementById("acc").innerHTML=
    '<div><span>White accuracy</span><b>'+H.accW.toFixed(1)+'%</b><i>'+H.cpW.toFixed(1)+' cp avg loss</i></div>'+
    '<div><span>Black accuracy</span><b>'+H.accB.toFixed(1)+'%</b><i>'+H.cpB.toFixed(1)+' cp avg loss</i></div>';
  buildSheet(); graph(); render();
  if(GAMES.length>1)document.getElementById("pick").value=gi;
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
  "compare games reviewed the same way and nothing else. The named terms describe what moved "+
  "in ChessBot’s own evaluation, which is not why "+H.engine.split("/").pop()+" scored the move "+
  "that way — where the two disagree, that disagreement is the useful part.";
loadGame(0);
</script></body></html>
)HTML";

    auto sub = [&](const std::string& key, const std::string& val) {
        const size_t p = html.find(key);
        if (p != std::string::npos) html.replace(p, key.size(), val);
    };
    sub("__TITLE__", title);
    sub("__GAMES__", gamesJson);
    sub("__PIECES__", pieceImages());
    return html;
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
    std::string annotateOut, htmlOut, jsonOut, archiveOut;
    bool explain = false, flip = false;
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
        else if (a == "--explain")    explain = true;
        else if (a[0] != '-') { if (pgnPath.empty()) pgnPath = a; archiveIn.push_back(a); }
        else { std::printf("unknown option: %s\n", a.c_str()); return 1; }
    }
    if (pgnPath.empty()) {
        std::printf("usage: review <game.pgn> [--engine <path>] [--engine-arg <a>]\n"
                    "                 [--depth N] [--hash MB] [--annotate out.pgn]\n"
                    "                 [--html out.html] [--json out.json] [--flip|--me <player>]\n"
                    "                 [--explain]\n"
                    "  a file holding several games is reviewed in full, into one page\n"
                    "       review --archive all.html g1.json g2.json ...\n"
                    "  default engine is /usr/games/stockfish; ChessBot needs --engine-arg --uci\n");
        return 1;
    }

    // Assembling an archive reads records that --json already produced, so it
    // costs no analysis: adding one game to a 70-game archive re-reviews one
    // game, not seventy.
    if (!archiveOut.empty()) {
        std::string games = "[";
        int n = 0;
        for (const std::string& in : archiveIn) {
            std::ifstream f(in);
            if (!f) { std::printf("cannot read %s\n", in.c_str()); return 1; }
            const std::string rec((std::istreambuf_iterator<char>(f)),
                                   std::istreambuf_iterator<char>());
            if (rec.empty()) continue;
            if (n++) games += ",";
            games += rec;
        }
        games += "]";
        if (!n) { std::printf("no game records given\n"); return 1; }
        std::ofstream out(archiveOut);
        if (!out.is_open()) { std::printf("cannot write %s\n", archiveOut.c_str()); return 1; }
        char t[64];
        std::snprintf(t, sizeof t, "%d reviewed games", n);
        out << htmlReport(games, t);
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
            r.san = san[i];
            r.bestSan = (bestSan[i].empty() ? best[i] : bestSan[i]);
            if (r.bestSan == r.san) r.bestSan.clear();
            r.label = label;
            r.terms = termsText;
            r.uci = toUciMove(game.moves[i]);
            r.evalAfter = (i % 2 == 0) ? -score[i + 1] : score[i + 1];
            r.wpLoss = wpLoss;
            r.cpLoss = cpLoss;
            r.white = white;
            rows.push_back(r);
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
                                     enginePath, depth, accArr, cpArr, count, flipThis));
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
