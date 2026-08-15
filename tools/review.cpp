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

// One row of the HTML report. Collected during the pass that prints the text
// report, so the two can never disagree about what a move was worth.
struct HtmlMove {
    std::string san, bestSan, label, terms, fen, uci;
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

// The report is one self-contained file: no CDN, no fonts, no images. A review
// is something you send to someone, and a page that fetches anything is a page
// that breaks on the machine you sent it to. Pieces are Unicode glyphs for the
// same reason — an image set would have to be embedded or fetched, and both are
// worse than a character that every system already has.
std::string htmlReport(const PgnGame& game, const std::vector<HtmlMove>& moves,
                       const std::string& startFen, int startEval,
                       const std::string& engine, int depth,
                       const double acc[2], const double cpAvg[2],
                       const int count[2]) {
    std::string j = "[";
    for (size_t i = 0; i < moves.size(); ++i) {
        const HtmlMove& m = moves[i];
        char buf[1024];
        std::snprintf(buf, sizeof buf,
            "%s{\"n\":%zu,\"w\":%s,\"san\":\"%s\",\"fen\":\"%s\",\"ev\":%d,"
            "\"wp\":%.1f,\"cp\":%d,\"best\":\"%s\",\"label\":\"%s\","
            "\"terms\":\"%s\",\"uci\":\"%s\"}",
            i ? "," : "", i, m.white ? "true" : "false", m.san.c_str(),
            m.fen.c_str(), m.evalAfter, m.wpLoss, m.cpLoss,
            m.bestSan.c_str(), m.label.c_str(), m.terms.c_str(), m.uci.c_str());
        j += buf;
    }
    j += "]";

    char head[2048];
    std::snprintf(head, sizeof head,
        "{\"white\":\"%s\",\"black\":\"%s\",\"result\":\"%s\",\"engine\":\"%s\","
        "\"depth\":%d,\"startFen\":\"%s\",\"startEval\":%d,"
        "\"accW\":%.1f,\"accB\":%.1f,\"cpW\":%.1f,\"cpB\":%.1f,\"nW\":%d,\"nB\":%d}",
        game.tags.white.c_str(), game.tags.black.c_str(), game.tags.result.c_str(),
        engine.c_str(), depth, startFen.c_str(), startEval,
        count[0] ? acc[0] : 0.0, count[1] ? acc[1] : 0.0,
        count[0] ? cpAvg[0] : 0.0, count[1] ? cpAvg[1] : 0.0, count[0], count[1]);

    std::string html = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>__TITLE__</title>
<style>
/* Three theme states, not two: an explicit choice stamps data-theme, and the
   default "system" setting stamps nothing -- so bare :root carries a complete
   palette and the other two blocks redefine only tokens. */
:root{
  --bg:#f2f4f6; --panel:#fff; --ink:#14181c; --dim:#657080; --faint:#8b95a3;
  --line:#e1e6ec; --sq-light:#dfe4ea; --sq-dark:#7c8a99; --sq-mark:#d9a441;
  --blunder:#b3261e; --mistake:#b06a00; --inacc:#8a7400;
  --shade:rgba(20,24,28,.055); --shade2:rgba(20,24,28,.105);
  --wp-fill:#fbfbf9; --wp-edge:#1a1e22; --bp-fill:#1d2228; --bp-edge:rgba(255,255,255,.30);
}
@media (prefers-color-scheme:dark){:root:not([data-theme="light"]){
  --bg:#101315; --panel:#191d21; --ink:#e7eaee; --dim:#9aa4b0; --faint:#78828e;
  --line:#272d33; --sq-light:#6d7885; --sq-dark:#3b444d; --sq-mark:#e0b354;
  --blunder:#e2564a; --mistake:#dd9836; --inacc:#c7ae33;
  --shade:rgba(231,234,238,.07); --shade2:rgba(231,234,238,.13);
  --wp-fill:#f4f5f3; --wp-edge:#15181b; --bp-fill:#14181c; --bp-edge:rgba(255,255,255,.38);
}}
:root[data-theme="dark"]{
  --bg:#101315; --panel:#191d21; --ink:#e7eaee; --dim:#9aa4b0; --faint:#78828e;
  --line:#272d33; --sq-light:#6d7885; --sq-dark:#3b444d; --sq-mark:#e0b354;
  --blunder:#e2564a; --mistake:#dd9836; --inacc:#c7ae33;
  --shade:rgba(231,234,238,.07); --shade2:rgba(231,234,238,.13);
  --wp-fill:#f4f5f3; --wp-edge:#15181b; --bp-fill:#14181c; --bp-edge:rgba(255,255,255,.38);
}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);
  font:15px/1.55 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;
  -webkit-font-smoothing:antialiased}
.wrap{max-width:1120px;margin:0 auto;padding:26px 18px 52px}
header{margin-bottom:18px}
h1{font:600 24px/1.25 ui-serif,Georgia,"Times New Roman",serif;margin:0 0 5px;
  letter-spacing:-.01em;text-wrap:balance}
.mono{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;
  font-variant-numeric:tabular-nums}
.sub{color:var(--dim);font-size:12.5px}
.cols{display:grid;grid-template-columns:minmax(300px,430px) 1fr;gap:20px;align-items:start}
@media(max-width:880px){.cols{grid-template-columns:1fr}}
.card{background:var(--panel);border:1px solid var(--line);border-radius:9px}
.stack{display:flex;flex-direction:column;gap:14px}

/* The board. Both axes are stated: with columns alone, rows size to their
   content and a rank holding pieces grows taller than an empty one, which is
   exactly how the squares came out uneven. Glyph size is a share of the
   board's own width (cqi), not the viewport, so a square and the piece on it
   stay in proportion at every layout width. */
.boardbox{container-type:inline-size;border-radius:9px 9px 0 0;overflow:hidden}
#board{display:grid;grid-template-columns:repeat(8,1fr);grid-template-rows:repeat(8,1fr);
  aspect-ratio:1;user-select:none}
#board div{position:relative;display:flex;align-items:center;justify-content:center;
  font-size:9.4cqi;line-height:1}
.lt{background:var(--sq-light)}.dk{background:var(--sq-dark)}
/* Solid glyphs for both colours, coloured and outlined rather than relying on
   the outline set: the hollow white pieces vanish on a light square in most
   system fonts. */
.wp{color:var(--wp-fill);-webkit-text-stroke:1.1px var(--wp-edge);
  text-shadow:0 1px 1px rgba(0,0,0,.22)}
.bp{color:var(--bp-fill);-webkit-text-stroke:1.1px var(--bp-edge)}
.hl::after{content:"";position:absolute;inset:0;
  box-shadow:inset 0 0 0 3px var(--sq-mark)}

.nav{display:flex;gap:6px;padding:9px 11px;align-items:center;
  border-top:1px solid var(--line)}
button{font:inherit;font-size:14px;padding:4px 10px;border:1px solid var(--line);
  border-radius:6px;background:var(--panel);color:var(--ink);cursor:pointer;line-height:1.4}
button:hover{background:var(--shade)}
button:focus-visible,.ply:focus-visible{outline:2px solid var(--sq-mark);outline-offset:1px}

/* Detail for the selected move, given room here rather than crammed into the
   list -- the explanation is the reason this report exists. */
.detail{padding:13px 15px;min-height:104px}
.badge{display:inline-block;padding:1px 8px;border-radius:99px;font-size:11.5px;
  font-weight:650;letter-spacing:.04em;text-transform:uppercase;color:#fff}
.b-Blunder{background:var(--blunder)}.b-Mistake{background:var(--mistake)}
.b-Inaccuracy{background:var(--inacc)}
.b-Good,.b-Excellent,.b-Best{background:transparent;color:var(--faint);
  border:1px solid var(--line)}
.dhead{display:flex;align-items:center;gap:9px;flex-wrap:wrap}
.dmove{font:600 19px/1.2 ui-monospace,SFMono-Regular,Menlo,monospace}
.dloss{margin-left:auto;font-size:13px;color:var(--dim)}
.dterms{margin-top:9px;font-size:13px;color:var(--dim)}
.dterms b{color:var(--ink);font-weight:600}

.summary{display:flex;gap:24px;padding:13px 15px;border-bottom:1px solid var(--line);
  flex-wrap:wrap}
.summary div{font-size:11.5px;color:var(--dim);letter-spacing:.05em;text-transform:uppercase}
.summary b{display:block;font:600 20px/1.2 ui-monospace,SFMono-Regular,Menlo,monospace;
  color:var(--ink);font-variant-numeric:tabular-nums;text-transform:none;letter-spacing:0}
.summary i{font-style:normal;font-size:12px;color:var(--faint);text-transform:none;
  letter-spacing:0}

/* A scoresheet: one row per move, both plies on it, which is how a game is
   written down and half the rows of a ply-per-line list. */
.sheet{max-height:56vh;overflow-y:auto;padding:5px 6px}
.row{display:grid;grid-template-columns:38px 1fr 1fr;gap:4px;align-items:stretch}
.no{color:var(--faint);font-size:12.5px;padding:4px 4px 4px 0;text-align:right;
  font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-variant-numeric:tabular-nums}
.ply{display:flex;align-items:baseline;gap:6px;padding:4px 7px;border-radius:5px;
  border:0;background:transparent;color:var(--ink);cursor:pointer;text-align:left;
  font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:13.5px;width:100%}
.ply:hover{background:var(--shade)}
.ply.sel{background:var(--shade2)}
.ply .d{margin-left:auto;font-size:11.5px;color:var(--faint);font-variant-numeric:tabular-nums}
.ply.Blunder{color:var(--blunder);font-weight:650}
.ply.Mistake{color:var(--mistake);font-weight:650}
.ply.Inaccuracy{color:var(--inacc)}
.ply.Blunder .d,.ply.Mistake .d,.ply.Inaccuracy .d{color:inherit}

.graph{margin-top:16px;padding:13px 15px 9px}
svg{display:block;width:100%;height:112px;overflow:visible}
.note{color:var(--faint);font-size:12px;margin-top:22px;line-height:1.65;max-width:70ch}
@media (prefers-reduced-motion:no-preference){.ply,button{transition:background-color .12s}}
</style></head><body><div class="wrap">
<header>
  <h1 id="ttl"></h1>
  <div class="sub mono" id="sub"></div>
</header>
<div class="cols">
  <div class="stack">
    <div class="card">
      <div class="boardbox"><div id="board"></div></div>
      <div class="nav">
        <button onclick="go(-999)" title="start">&#8676;</button>
        <button onclick="go(-1)" title="previous">&#8592;</button>
        <button onclick="go(1)" title="next">&#8594;</button>
        <button onclick="go(999)" title="end">&#8677;</button>
        <span class="sub mono" style="margin-left:auto" id="pos"></span>
      </div>
    </div>
    <div class="card detail" id="detail"></div>
  </div>
  <div class="card">
    <div class="summary" id="acc"></div>
    <div class="sheet" id="list"></div>
  </div>
</div>
<div class="card graph"><span class="sub">evaluation &middot; white's point of view</span>
<svg id="g" viewBox="0 0 600 112" preserveAspectRatio="none"></svg></div>
<p class="note" id="note"></p>
</div>
<script>
const H=__HEAD__, M=__MOVES__;
const S={k:"♚",q:"♛",r:"♜",b:"♝",n:"♞",p:"♟"};
let cur=-1;

function sq(u){
  if(!u||u.length<4)return[];
  const ix=t=>(8-(+t[1]))*8+(t.charCodeAt(0)-97);
  return[ix(u.slice(0,2)),ix(u.slice(2,4))];
}
function board(fen,uci){
  const rows=fen.split(" ")[0].split("/"), b=document.getElementById("board"), mark=sq(uci);
  b.innerHTML="";
  for(let r=0;r<8;r++){let f=0;
    for(const ch of rows[r]){
      if(ch>="1"&&ch<="8"){for(let k=0;k<+ch;k++,f++)cell(b,r,f,"",false,mark)}
      else{cell(b,r,f++,S[ch.toLowerCase()]||"",ch===ch.toUpperCase(),mark)}
    }}
}
function cell(b,r,f,g,white,mark){
  const d=document.createElement("div");
  d.className=((r+f)%2?"dk":"lt")+(mark.includes(r*8+f)?" hl":"");
  if(g){const s=document.createElement("span");s.className=white?"wp":"bp";s.textContent=g;d.appendChild(s)}
  b.appendChild(d);
}
function fmt(cp){return (cp>0?"+":"")+(cp/100).toFixed(2)}
function label(m){return m.san+(m.label=="Blunder"?"??":m.label=="Mistake"?"?":m.wp>=5?"?!":"")}

function detail(){
  const d=document.getElementById("detail");
  if(cur<0){d.innerHTML='<div class="sub">Starting position. Click a move, or use the arrow keys.</div>';return}
  const m=M[cur];
  let h='<div class="dhead"><span class="dmove">'+((cur>>1)+1)+(m.w?". ":"… ")+label(m)+'</span>'+
        '<span class="badge b-'+m.label+'">'+m.label+'</span>'+
        '<span class="dloss mono">eval '+fmt(m.ev)+'</span></div>';
  if(m.wp>=5){
    h+='<div class="dterms">Gave up <b>'+m.wp.toFixed(1)+'%</b> win probability'+
       (m.cp?' ('+m.cp+' cp)':'')+(m.best?', best was <b>'+m.best+'</b>':'')+'.</div>';
    if(m.terms)h+='<div class="dterms">'+(m.terms=="no term accounts for it"
      ?'<b>No evaluation term accounts for it</b> — this engine cannot see what was lost.'
      :'ChessBot’s evaluation moved: <b>'+m.terms+'</b>')+'</div>';
  } else {
    h+='<div class="dterms">Nothing given up here.</div>';
  }
  d.innerHTML=h;
}
function render(){
  const m=cur<0?null:M[cur];
  board(m?m.fen:H.startFen, m?m.uci:"");
  document.getElementById("pos").textContent=(cur+1)+" / "+M.length;
  document.querySelectorAll(".ply").forEach(e=>e.classList.toggle("sel",+e.dataset.i===cur));
  const s=document.querySelector(".ply.sel"); if(s)s.scrollIntoView({block:"nearest"});
  detail(); marker();
}
function go(d){
  cur=d===-999?-1:d===999?M.length-1:Math.max(-1,Math.min(M.length-1,cur+d));
  render();
}
document.addEventListener("keydown",e=>{
  if(e.key==="ArrowLeft")go(-1);else if(e.key==="ArrowRight")go(1);
  else if(e.key==="Home")go(-999);else if(e.key==="End")go(999);else return;
  e.preventDefault();
});

document.getElementById("ttl").textContent=H.white+"  vs  "+H.black;
document.getElementById("sub").textContent=
  H.result+"  ·  "+M.length+" moves  ·  "+H.engine.split("/").pop()+" depth "+H.depth;
document.getElementById("acc").innerHTML=
  '<div>White accuracy<b>'+H.accW.toFixed(1)+'%</b><i>'+H.cpW.toFixed(1)+' cp avg loss</i></div>'+
  '<div>Black accuracy<b>'+H.accB.toFixed(1)+'%</b><i>'+H.cpB.toFixed(1)+' cp avg loss</i></div>';

const list=document.getElementById("list");
for(let i=0;i<M.length;i+=2){
  const row=document.createElement("div"); row.className="row";
  row.innerHTML='<span class="no">'+((i>>1)+1)+'.</span>';
  for(const j of [i,i+1]){
    if(j>=M.length){row.insertAdjacentHTML("beforeend","<span></span>");continue}
    const m=M[j], b=document.createElement("button");
    b.className="ply "+m.label; b.dataset.i=j;
    b.innerHTML='<span>'+label(m)+'</span>'+
      (m.wp>=5?'<span class="d">-'+m.wp.toFixed(1)+'%</span>':'<span class="d">'+fmt(m.ev)+'</span>');
    b.onclick=()=>{cur=j;render()};
    row.appendChild(b);
  }
  list.appendChild(row);
}

const W=600,Hh=112,CAP=800;
const pts=[H.startEval].concat(M.map(m=>m.ev));
const gx=i=>i*W/Math.max(1,pts.length-1);
const gy=v=>Hh/2-Math.max(-CAP,Math.min(CAP,v))/CAP*(Hh/2-5);
(function graph(){
  let d="M"+gx(0)+","+gy(pts[0]);
  pts.forEach((v,i)=>{if(i)d+="L"+gx(i)+","+gy(v)});
  document.getElementById("g").innerHTML=
    '<defs><linearGradient id="f" x1="0" y1="0" x2="0" y2="1">'+
    '<stop offset="0" stop-color="currentColor" stop-opacity=".22"/>'+
    '<stop offset="1" stop-color="currentColor" stop-opacity=".02"/></linearGradient></defs>'+
    '<path d="'+d+'L'+W+','+(Hh/2)+'L0,'+(Hh/2)+'Z" fill="url(#f)"/>'+
    '<line x1="0" y1="'+(Hh/2)+'" x2="'+W+'" y2="'+(Hh/2)+'" stroke="currentColor" stroke-opacity=".35"/>'+
    '<path d="'+d+'" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linejoin="round"/>'+
    '<circle id="mk" r="3.5" fill="var(--sq-mark)" style="display:none"/>';
})();
function marker(){
  const c=document.getElementById("mk"); if(!c)return;
  const i=cur+1;
  c.setAttribute("cx",gx(i)); c.setAttribute("cy",gy(pts[i]));
  c.style.display="";
}

document.getElementById("note").textContent=
  "Accuracy depends on the analysing engine, its depth and the curve used, so these "+
  "numbers compare games reviewed the same way and nothing else. The named terms "+
  "describe what moved in ChessBot’s own evaluation, which is not why "+
  H.engine.split("/").pop()+" scored the move that way — where the two disagree, "+
  "that disagreement is the useful part.";
render();
</script></body></html>
)HTML";

    auto sub = [&](const std::string& key, const std::string& val) {
        const size_t p = html.find(key);
        if (p != std::string::npos) html.replace(p, key.size(), val);
    };
    sub("__TITLE__", game.tags.white + " vs " + game.tags.black);
    sub("__HEAD__", head);
    sub("__MOVES__", j);
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
    std::string annotateOut, htmlOut;
    bool explain = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : ""; };
        if      (a == "--engine") enginePath = next();
        else if (a == "--depth")  depth = std::atoi(next());
        else if (a == "--hash")   hashMb = std::atoi(next());
        else if (a == "--engine-arg") engineArgs.push_back(next());
        else if (a == "--annotate")   annotateOut = next();
        else if (a == "--html")       htmlOut = next();
        else if (a == "--explain")    explain = true;
        else if (a[0] != '-')     pgnPath = a;
        else { std::printf("unknown option: %s\n", a.c_str()); return 1; }
    }
    if (pgnPath.empty()) {
        std::printf("usage: review <game.pgn> [--engine <path>] [--engine-arg <a>]\n"
                    "                 [--depth N] [--hash MB] [--annotate out.pgn]\n"
                    "                 [--html out.html] [--explain]\n"
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
            r.fen = position[i + 1].getFEN();
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
    if (!htmlOut.empty()) {
        std::ofstream f(htmlOut);
        if (!f.is_open()) {
            std::printf("\ncannot write %s\n", htmlOut.c_str());
            return 1;
        }
        Board start;
        if (!game.tags.startFen.empty()) start.setFromFEN(game.tags.startFen);
        const double accW = count[0] ? accSum[0] / count[0] : 0.0;
        const double accB = count[1] ? accSum[1] / count[1] : 0.0;
        const double cpW = count[0] ? (double)cpSum[0] / count[0] : 0.0;
        const double cpB = count[1] ? (double)cpSum[1] / count[1] : 0.0;
        const double accArr[2] = {accW, accB};
        const double cpArr[2] = {cpW, cpB};
        f << htmlReport(game, rows, start.getFEN(), score[0], enginePath, depth,
                        accArr, cpArr, count);
        std::printf("\nreport written to %s\n", htmlOut.c_str());
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
