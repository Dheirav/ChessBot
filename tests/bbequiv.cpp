// Equivalence harness for the bitboard replacement.
//
// docs/BITBOARD-REPLACEMENT.md says the acceptance standard is exact agreement
// with the mailbox engine, layer by layer, so that a failure lands on the layer
// that caused it instead of surfacing as a wrong node count at the end. This is
// where those checks live, and it grows a section per phase.
//
// Every check runs over the same corpus: positions from random legal games, so
// they are reachable, varied, and not chosen by whoever wrote the test to be
// the cases the code already handles.
//
#include "../src/engine/bb_position.hpp"
#include "../src/engine/bb_movegen.hpp"
#include "../src/engine/bb_see.hpp"
#include "../src/engine/bb_evaluation.hpp"
#include "../src/engine/bb_move_ordering.hpp"
#include "../src/engine/bb_search.hpp"
#include "../src/engine/search.hpp"
#include "../src/engine/transposition_table.hpp"
#include "../src/engine/move_ordering.hpp"
#include "../src/engine/evaluation.hpp"
#include "../src/engine/see.hpp"
#include "../src/engine/bitboard_attacks.hpp"
#include "../src/engine/board.hpp"
#include "../src/engine/movegen.hpp"
#include "../src/engine/move_lookup.hpp"
#include <cstdio>
#include <vector>
#include <string>
#include <algorithm>
#include <set>
#include <sstream>
#include <iostream>

namespace {

struct Rng {
    uint64_t s = 0x5CBE7A1F2026ULL;
    uint64_t operator()() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
};

// Positions from random legal games. The same seed and walk as
// bitboard_test.cpp, so the two harnesses cover the same ground.
std::vector<std::string> buildCorpus(int games, int maxPly) {
    std::vector<std::string> out;
    Rng rng;
    for (int g = 0; g < games; ++g) {
        Board board;
        for (int ply = 0; ply < maxPly; ++ply) {
            MoveList moves = generateLegalMoves(board, board.activeColor);
            if (moves.empty() || board.halfmoveClock >= 100) break;
            out.push_back(board.getFEN());
            board.makeMove(moves[rng() % moves.size()]);
        }
    }
    return out;
}

// A move as a comparable string. Long algebraic plus the promotion piece, which
// is everything that distinguishes one legal move from another.
std::string squareName(int sq) {
    std::string s;
    s += char('a' + sq % 8);
    s += char('1' + (7 - sq / 8));
    return s;
}
std::string describe(const BitboardMove& m) {
    std::string s = squareName(m.from) + squareName(m.to);
    if (m.flag == BBM_PROMOTION) s += "nbrq"[m.promotionType == BB_KNIGHT ? 0 :
                                             m.promotionType == BB_BISHOP ? 1 :
                                             m.promotionType == BB_ROOK   ? 2 : 3];
    return s;
}
std::string describe(const Move& m) {
    std::string s = squareName(m.from) + squareName(m.to);
    const PieceType promo = m.promotionPiece.type();
    if (m.flag == PROMOTION && promo != NONE)
        s += "nbrq"[promo == KNIGHT ? 0 : promo == BISHOP ? 1 : promo == ROOK ? 2 : 3];
    return s;
}

// A BitboardMove as the mailbox Move that means the same thing, so the two
// implementations of a function can be handed the same move rather than two
// moves that are believed to be the same.
Move toMailboxMove(const Position& pos, const BitboardMove& m) {
    const PieceColor us = toMailboxColor(pos.sideToMove);
    const PieceColor them = (us == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
    Piece moved(us, toMailboxType(m.moved));
    Piece captured = (m.captured == BB_NONE) ? Piece()
                                             : Piece(them, toMailboxType(m.captured));
    MoveFlag flag = NORMAL;
    switch (m.flag) {
        case BBM_CAPTURE:    flag = CAPTURE; break;
        case BBM_PROMOTION:  flag = PROMOTION; break;
        case BBM_EN_PASSANT: flag = EN_PASSANT; break;
        case BBM_CASTLE:     flag = CASTLING; break;
        default:             flag = NORMAL; break;
    }
    Piece promo = (m.flag == BBM_PROMOTION) ? Piece(us, toMailboxType(m.promotionType))
                                            : Piece();
    return Move(m.from, m.to, moved, captured, flag, promo);
}

int failures = 0;
void report(const char* what, int bad, long checked) {
    std::printf("  %-42s %8ld checked  %s", what, checked,
                bad == 0 ? "ok\n" : "");
    if (bad) { std::printf("%d MISMATCH\n", bad); ++failures; }
}

} // namespace

int main() {
    initMoveLookupTables();
    initBitboardAttacks();

    const std::vector<std::string> corpus = buildCorpus(60, 100);
    std::printf("Bitboard replacement equivalence, %zu positions\n", corpus.size());

    // ---------------------------------------------------------------- B0
    // The keys must be the mailbox engine's keys, not merely self-consistent
    // ones: the two cores share a transposition table during verification, so
    // a different key means a different tree for reasons that have nothing to
    // do with the code under test.
    std::printf("\nB0  Position: keys, clocks, FEN\n");
    int badHash = 0, badPawn = 0, badFen = 0, badClock = 0, badIncr = 0, badUnmake = 0;
    long checked = 0, incrChecked = 0;

    for (const std::string& fen : corpus) {
        Board board;
        if (!board.setFromFEN(fen)) { ++badFen; continue; }
        ++checked;

        const Position viaBoard = toPosition(board);
        if (viaBoard.hash != board.getHash() && badHash < 5) {
            std::printf("    hash %016llx vs board %016llx\n    %s\n",
                        (unsigned long long)viaBoard.hash,
                        (unsigned long long)board.getHash(), fen.c_str());
            ++badHash;
        }
        for (int sq = 0; sq < 64; ++sq) {
            if (viaBoard.squares[sq].value != board.squares[sq].value) {
                if (badHash < 5) std::printf("    square %d differs\n    %s\n", sq, fen.c_str());
                ++badHash;
                break;
            }
        }
        if (viaBoard.pawnHash != board.getPawnHash() && badPawn < 5) {
            std::printf("    pawn hash mismatch\n    %s\n", fen.c_str());
            ++badPawn;
        }

        // Loading through Position's own parser must land in the same place as
        // loading through Board's, which is what lets the search be handed a
        // FEN rather than a converted Board once it is connected.
        Position pos;
        pos.setFromFEN(fen);
        if (pos.hash != board.getHash() && badHash < 5) {
            std::printf("    FEN-loaded hash mismatch\n    %s\n", fen.c_str());
            ++badHash;
        }
        if (pos.halfmoveClock != board.halfmoveClock ||
            pos.fullmoveNumber != board.fullmoveNumber) {
            if (badClock < 5) {
                std::printf("    clocks %d/%d vs board %d/%d\n    %s\n",
                            pos.halfmoveClock, pos.fullmoveNumber,
                            board.halfmoveClock, board.fullmoveNumber, fen.c_str());
            }
            ++badClock;
        }
        if (pos.toFEN() != board.getFEN()) {
            if (badFen < 5) {
                std::printf("    FEN round-trip\n      got  %s\n      want %s\n",
                            pos.toFEN().c_str(), board.getFEN().c_str());
            }
            ++badFen;
        }

        // The incremental key is built from the move, so it can drift from the
        // board it claims to describe. Recomputing after every move is the only
        // thing that catches a delta that is wrong for one move shape.
        BitboardMoveList moves;
        generateBitboardLegal(pos, pos.sideToMove, moves);
        for (const BitboardMove& m : moves) {
            const Position before = pos;
            const PositionUndo u = pos.makeMove(m);
            ++incrChecked;
            // consistent() checks the keys, the occupancies and the square
            // array together, so one failure here means the position and its
            // description of itself have come apart somewhere.
            if (!pos.consistent()) {
                if (badIncr < 5) {
                    std::printf("    state inconsistent after %d->%d flag %d\n    %s\n",
                                m.from, m.to, (int)m.flag, fen.c_str());
                }
                ++badIncr;
            }
            pos.unmakeMove(u);
            if (!pos.consistent() ||
                pos.hash != before.hash || pos.pawnHash != before.pawnHash ||
                pos.halfmoveClock != before.halfmoveClock ||
                pos.fullmoveNumber != before.fullmoveNumber ||
                pos.occupancyAll != before.occupancyAll) {
                if (badUnmake < 5) {
                    std::printf("    unmake did not restore after %d->%d\n    %s\n",
                                m.from, m.to, fen.c_str());
                }
                ++badUnmake;
            }
        }
    }

    report("zobrist key matches mailbox", badHash, checked);
    report("pawn key matches mailbox", badPawn, checked);
    report("FEN round-trip", badFen, checked);
    report("halfmove and fullmove clocks", badClock, checked);
    // Null move too: it changes the key without moving a piece, so an error
    // here would show up only as transposition table noise.
    {
        int badNull = 0;
        long nullChecked = 0;
        for (const std::string& fen : corpus) {
            Board board;
            if (!board.setFromFEN(fen)) continue;
            Position pos;
            pos.setFromFEN(fen);
            const Position before = pos;
            const Position::NullUndo u = pos.makeNullMove();
            ++nullChecked;
            Board mirror = board;
            const auto mu = mirror.makeNullMove();
            if (pos.hash != mirror.getHash() || !pos.consistent()) ++badNull;
            pos.unmakeNullMove(u);
            mirror.unmakeNullMove(mu);
            if (pos.hash != before.hash || pos.enPassantSquare != before.enPassantSquare ||
                pos.halfmoveClock != before.halfmoveClock ||
                pos.sideToMove != before.sideToMove) ++badNull;
        }
        report("null move key matches mailbox", badNull, nullChecked);
    }
    report("state consistent after make", badIncr, incrChecked);
    report("unmake restores exactly", badUnmake, incrChecked);


    // ---------------------------------------------------------------- B1
    // Legal generation from masks instead of make-and-test. Two independent
    // checks: perft, which catches a rule that is wrong everywhere, and set
    // equality against movegen.cpp, which catches a rule that is wrong in
    // positions perft's four openings never reach.
    std::printf("\nB1  Legal generation without make/unmake\n");
    {
        struct PerftCase { const char* name; const char* fen; int depth; uint64_t expected; };
        static const PerftCase PERFT[] = {
            {"startpos",  "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 5, 4865609},
            {"kiwipete",  "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 4, 4085603},
            {"position3", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 5, 674624},
            {"position4", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 4, 422333},
            {"position5", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 4, 2103487},
            {"position6", "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 4, 3894594},
        };
        struct Perft {
            static uint64_t run(Position& pos, int depth) {
                BBMoveList moves;
                bbGenerate(pos, moves);
                if (depth <= 1) return (uint64_t)moves.size();
                uint64_t nodes = 0;
                for (const BitboardMove& m : moves) {
                    const PositionUndo u = pos.makeMove(m);
                    nodes += run(pos, depth - 1);
                    pos.unmakeMove(u);
                }
                return nodes;
            }
        };
        for (const PerftCase& c : PERFT) {
            Position pos;
            pos.setFromFEN(c.fen);
            const uint64_t got = Perft::run(pos, c.depth);
            const bool ok = (got == c.expected);
            std::printf("  perft %-10s depth %d: %10llu  expected %10llu  %s\n",
                        c.name, c.depth, (unsigned long long)got,
                        (unsigned long long)c.expected, ok ? "ok" : "MISMATCH");
            if (!ok) ++failures;
        }

        // Set equality with the generator the engine actually uses. Compared as
        // sets rather than sequences because generation order is a B5 question;
        // here the claim is only that the two agree on which moves exist.
        int badSet = 0;
        long setChecked = 0;
        for (const std::string& fen : corpus) {
            Board board;
            if (!board.setFromFEN(fen)) continue;
            Position pos;
            pos.setFromFEN(fen);

            std::set<std::string> mine, theirs;
            BBMoveList mv;
            bbGenerate(pos, mv);
            for (const BitboardMove& m : mv) mine.insert(describe(m));
            for (const Move& m : generateLegalMoves(board, board.activeColor))
                theirs.insert(describe(m));
            ++setChecked;

            if (mine != theirs) {
                if (badSet < 5) {
                    std::printf("    move set differs\n    %s\n", fen.c_str());
                    for (const std::string& s2 : mine)
                        if (!theirs.count(s2)) std::printf("      extra   %s\n", s2.c_str());
                    for (const std::string& s2 : theirs)
                        if (!mine.count(s2)) std::printf("      missing %s\n", s2.c_str());
                }
                ++badSet;
            }
        }
        report("move set matches movegen.cpp", badSet, setChecked);
    }


    // ---------------------------------------------------------------- B2
    // The quiescence set. movegen.cpp defines it as captures, en passant and
    // promotions, where a quiet promotion counts because the flag is PROMOTION
    // whether or not anything was taken. The second check is the one that
    // matters for staging: captures and quiets must partition the legal moves
    // exactly, because the staged search generates them separately and would
    // silently lose any move that falls between the two.
    std::printf("\nB2  Staged and tactical generation\n");
    {
        int badCaps = 0, badPartition = 0;
        long capsChecked = 0;
        for (const std::string& fen : corpus) {
            Board board;
            if (!board.setFromFEN(fen)) continue;
            Position pos;
            pos.setFromFEN(fen);
            ++capsChecked;

            std::set<std::string> mine, theirs;
            BBMoveList caps, quiets, all;
            bbGenerate(pos, caps, BB_GEN_CAPTURES);
            bbGenerate(pos, quiets, BB_GEN_QUIETS);
            bbGenerate(pos, all, BB_GEN_ALL);
            for (const BitboardMove& m : caps) mine.insert(describe(m));

            MoveList theirCaps;
            generateLegalCaptures(board, board.activeColor, theirCaps);
            for (const Move& m : theirCaps) theirs.insert(describe(m));

            if (mine != theirs) {
                if (badCaps < 5) {
                    std::printf("    capture set differs\n    %s\n", fen.c_str());
                    for (const std::string& s2 : mine)
                        if (!theirs.count(s2)) std::printf("      extra   %s\n", s2.c_str());
                    for (const std::string& s2 : theirs)
                        if (!mine.count(s2)) std::printf("      missing %s\n", s2.c_str());
                }
                ++badCaps;
            }

            std::set<std::string> union_, allSet;
            for (const BitboardMove& m : caps) union_.insert(describe(m));
            size_t beforeQuiets = union_.size();
            for (const BitboardMove& m : quiets) union_.insert(describe(m));
            for (const BitboardMove& m : all) allSet.insert(describe(m));
            const bool disjoint = (union_.size() == beforeQuiets + (size_t)quiets.size());
            if (union_ != allSet || !disjoint) {
                if (badPartition < 5)
                    std::printf("    captures+quiets do not partition all moves\n    %s\n", fen.c_str());
                ++badPartition;
            }
        }
        report("capture set matches movegen.cpp", badCaps, capsChecked);
        report("captures and quiets partition all", badPartition, capsChecked);
    }


    // ---------------------------------------------------------------- B3
    // SEE decides which captures quiescence searches and how captures sort, so
    // a disagreement here moves node counts without moving anything visible.
    // Checked on every legal capture in the corpus rather than on hand-picked
    // exchanges, because the cases that separate the two implementations are
    // the ones with an x-ray behind a spent piece, and nobody writes those by
    // hand.
    std::printf("\nB3  Static exchange evaluation\n");
    {
        int badSee = 0;
        long seeChecked = 0;
        for (const std::string& fen : corpus) {
            Board board;
            if (!board.setFromFEN(fen)) continue;
            Position pos;
            pos.setFromFEN(fen);

            BBMoveList caps;
            bbGenerate(pos, caps, BB_GEN_CAPTURES);
            for (const BitboardMove& m : caps) {
                const Move mm = toMailboxMove(pos, m);
                const int mine = bbSee(pos, m);
                const int theirs = see(board, mm);
                ++seeChecked;
                if (mine != theirs) {
                    if (badSee < 8)
                        std::printf("    see %s: bitboard %d mailbox %d\n    %s\n",
                                    describe(m).c_str(), mine, theirs, fen.c_str());
                    ++badSee;
                }
            }
        }
        report("agrees with see.cpp on every capture", badSee, seeChecked);
    }


    // ---------------------------------------------------------------- B4
    std::printf("\nB4  Evaluation\n");
    {
        int badMob = 0;
        long mobChecked = 0;
        for (const std::string& fen : corpus) {
            Board board;
            if (!board.setFromFEN(fen)) continue;
            Position pos;
            pos.setFromFEN(fen);
            for (int c = 0; c < 2; ++c) {
                const PieceColor mc = (c == 0) ? COLOR_WHITE : COLOR_BLACK;
                const BitboardColor bc = (c == 0) ? BB_WHITE : BB_BLACK;
                const int mine = bbCountPseudoLegal(pos, bc);
                const int theirs = countPseudoLegalMoves(board, mc, false);
                ++mobChecked;
                if (mine != theirs) {
                    if (badMob < 8)
                        std::printf("    mobility %s: bitboard %d mailbox %d\n    %s\n",
                                    c == 0 ? "white" : "black", mine, theirs, fen.c_str());
                    ++badMob;
                }
            }
        }
        report("pseudo-legal count matches movegen.cpp", badMob, mobChecked);

        // The one that matters: every term, on every position, to the
        // centipawn. Reported per term as well as in total, because a total
        // that differs says nothing about where, and the terms are what get
        // converted to bitboards one at a time next.
        int badEval = 0;
        long evalChecked = 0;
        struct TermDiff { const char* name; int EvalDetails::* field; int bad; };
        TermDiff terms[] = {
            {"total",        &EvalDetails::total, 0},
            {"material",     &EvalDetails::material, 0},
            {"mobility",     &EvalDetails::mobility, 0},
            {"kingSafety",   &EvalDetails::kingSafety, 0},
            {"centerControl",&EvalDetails::centerControl, 0},
            {"bishopPair",   &EvalDetails::bishopPair, 0},
            {"doubledPawn",  &EvalDetails::doubledPawn, 0},
            {"isolatedPawn", &EvalDetails::isolatedPawn, 0},
            {"passedPawn",   &EvalDetails::passedPawn, 0},
            {"backwardPawn", &EvalDetails::backwardPawn, 0},
            {"connectedPawn",&EvalDetails::connectedPawn, 0},
            {"pawnChain",    &EvalDetails::pawnChain, 0},
            {"rooksOpenFile",&EvalDetails::rooksOpenFile, 0},
            {"rooksSemiOpen",&EvalDetails::rooksSemiOpenFile, 0},
            {"rooks7thRank", &EvalDetails::rooks7thRank, 0},
            {"pst",          &EvalDetails::pst, 0},
            {"outpost",      &EvalDetails::outpost, 0},
            {"trapped",      &EvalDetails::trapped, 0},
            {"kingActivity", &EvalDetails::kingActivity, 0},
            {"threats",      &EvalDetails::threats, 0},
            {"undefended",   &EvalDetails::undefended, 0},
            {"space",        &EvalDetails::space, 0},
            {"drawish",      &EvalDetails::drawish, 0},
        };
        for (const std::string& fen : corpus) {
            Board board;
            if (!board.setFromFEN(fen)) continue;
            Position pos;
            pos.setFromFEN(fen);
            const EvalDetails mine = bbEvaluateDetails(pos);
            const EvalDetails theirs = evaluate_details(board);
            ++evalChecked;
            bool any = false;
            for (TermDiff& t : terms)
                if (mine.*(t.field) != theirs.*(t.field)) { ++t.bad; any = true; }
            if (any) {
                if (badEval < 4) {
                    std::printf("    eval differs\n    %s\n", fen.c_str());
                    for (const TermDiff& t : terms)
                        if (mine.*(t.field) != theirs.*(t.field))
                            std::printf("      %-14s bitboard %6d  mailbox %6d\n",
                                        t.name, mine.*(t.field), theirs.*(t.field));
                }
                ++badEval;
            }
            if (bbEvaluate(pos) != evaluate(board)) ++badEval;
        }
        for (const TermDiff& t : terms)
            if (t.bad) std::printf("    term %-14s differs on %d positions\n", t.name, t.bad);
        report("evaluation matches evaluation.cpp exactly", badEval, evalChecked);

    }


    // ---------------------------------------------------------------- B5
    // Move ordering shapes the tree more than anything else in the search, so
    // this is checked as an exact sequence rather than as a set: the two
    // orderers must produce the same moves in the same positions.
    //
    // Empty tables would make the test far too easy -- almost every quiet move
    // would score zero and the comparison would only exercise the tie-break --
    // so both orderers are first driven with the same killers and the same
    // history updates, taken from the moves of the corpus itself.
    // Ordering ties are settled by std::sort unless orderTieBreak is on, and
    // an unstable sort's answer depends on the input order -- which is exactly
    // where the two generators differ. B5 and B6 therefore run with the toggle
    // on, which is what makes an exact comparison possible at all. The shipped
    // engine leaves it off; see the note in search.hpp.
    g_searchOptions.orderTieBreak = true;

    std::printf("\nB5  Move ordering\n");
    {
        MoveOrderer mailboxOrderer;
        BBMoveOrderer bbOrderer;

        int badOrder = 0;
        long orderChecked = 0;
        int fenIndex = 0;
        for (const std::string& fen : corpus) {
            Board board;
            if (!board.setFromFEN(fen)) continue;
            Position pos;
            pos.setFromFEN(fen);
            const int depth = 1 + (fenIndex % 8);
            ++fenIndex;

            MoveList theirMoves = generateLegalMoves(board, board.activeColor);
            BBMoveList myMoves;
            bbGenerate(pos, myMoves);
            if (theirMoves.empty()) continue;

            // Feed both tables the same updates, so they are ordering with the
            // same knowledge rather than both ordering from zero.
            const Move& seed = theirMoves[fenIndex % theirMoves.size()];
            mailboxOrderer.updateHistory(seed, depth);
            mailboxOrderer.updateKillerMove(seed, depth);
            for (const BitboardMove& m : myMoves) {
                if (describe(m) != describe(seed)) continue;
                bbOrderer.updateHistory(m, depth, pos.sideToMove);
                bbOrderer.updateKillerMove(m, depth);
            }

            // The transposition table move, which is the first thing either
            // orderer looks at.
            const Move ttTheirs = theirMoves[(fenIndex * 7) % theirMoves.size()];
            BitboardMove ttMine = BBMoveOrderer::none();
            for (const BitboardMove& m : myMoves)
                if (describe(m) == describe(ttTheirs)) ttMine = m;

            mailboxOrderer.orderMoves(theirMoves, board, depth, ttTheirs);
            bbOrderer.orderMoves(myMoves, pos, depth, ttMine);
            ++orderChecked;

            if ((size_t)myMoves.size() != theirMoves.size()) { ++badOrder; continue; }
            for (int i = 0; i < myMoves.size(); ++i) {
                if (describe(myMoves[i]) != describe(theirMoves[i])) {
                    if (badOrder < 4) {
                        std::printf("    order differs at %d\n    %s\n", i, fen.c_str());
                        for (int j = 0; j < myMoves.size() && j < 12; ++j)
                            std::printf("      %2d  bitboard %-6s  mailbox %s\n", j,
                                        describe(myMoves[j]).c_str(),
                                        describe(theirMoves[j]).c_str());
                    }
                    ++badOrder;
                    break;
                }
            }
        }
        report("ordered sequence matches MoveOrderer", badOrder, orderChecked);
    }


    // ---------------------------------------------------------------- B6
    // The acceptance test for the whole replacement.
    //
    // Same positions and same table size as tests/bench, each search given a
    // fresh table so the two cores cannot see each other's entries. If the node
    // counts agree, then every layer below agreed on every input the search
    // actually gave it, which is a far stronger statement than any of the
    // per-layer checks above make on their own.
    std::printf("\nB6  Search\n");
    {
        static const struct { const char* name; const char* fen; } POSITIONS[] = {
            {"startpos",   "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"},
            {"open-ital",  "r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 0 1"},
            {"open-sicil", "rnbqkbnr/pp1ppppp/8/2p5/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 2"},
            {"kiwipete",   "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"},
            {"midgame-1",  "r1bq1rk1/pp2bppp/2n1pn2/2pp4/3P1B2/2PBPN2/PP1N1PPP/R2Q1RK1 w - - 0 1"},
            {"midgame-2",  "2rq1rk1/pb1nbppp/1p2pn2/3p4/3P4/1BN1PN2/PP2QPPP/2RR2K1 w - - 0 1"},
            {"tactical",   "r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1"},
            {"promo-race", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"},
            {"rook-endg",  "8/8/8/4k3/8/8/4K3/R7 w - - 0 1"},
            {"pawn-endg",  "8/1p3pp1/7p/5P1P/2k3P1/8/2K2P2/8 w - - 0 1"},
            {"queen-endg", "6k1/5ppp/8/8/8/8/5PPP/3Q2K1 w - - 0 1"},
            {"zugzwang",   "8/8/p1p5/1p5p/1P5p/8/PPP2K1p/4R1rk w - - 0 1"},
        };
        const int depth = 6;
        // The search narrates every iteration and the table announces every
        // resize; both would drown the comparison.
        g_searchOptions.quiet = true;
        std::streambuf* savedOut = std::cout.rdbuf();
        std::ostringstream swallowed;
        std::cout.rdbuf(swallowed.rdbuf());
        uint64_t mailboxTotal = 0, bbTotal = 0;
        int badNodes = 0, badMove = 0;
        std::printf("  %-12s %12s %12s  %-6s %-6s\n",
                    "position", "mailbox", "bitboard", "best", "best");
        for (const auto& p : POSITIONS) {
            std::atomic<bool> stop{false};

            Board board;
            board.setFromFEN(p.fen);
            TranspositionTable ttA(64);
            // g_searchNodes is the count for the search just finished, not a
            // running total: it is reset when a search starts.
            Move theirBest = findBestMoveIterativeDeepening(board, depth, stop, ttA);
            const uint64_t theirNodes = g_searchNodes;

            Position pos;
            pos.setFromFEN(p.fen);
            TranspositionTable ttB(64);
            std::vector<uint64_t> path;
            const BBSearchResult mine = bbSearchRoot(pos, depth, ttB, stop, path);

            mailboxTotal += theirNodes;
            bbTotal += mine.nodes;
            const bool nodesOk = (theirNodes == mine.nodes);
            const bool moveOk = (describe(mine.best) == describe(theirBest));
            if (!nodesOk) ++badNodes;
            if (!moveOk) ++badMove;
            std::printf("  %-12s %12llu %12llu  %-6s %-6s %s%s\n", p.name,
                        (unsigned long long)theirNodes, (unsigned long long)mine.nodes,
                        describe(theirBest).c_str(), describe(mine.best).c_str(),
                        nodesOk ? "" : "NODES ", moveOk ? "" : "MOVE");
        }
        std::cout.rdbuf(savedOut);
        std::printf("  %-12s %12llu %12llu\n", "total",
                    (unsigned long long)mailboxTotal, (unsigned long long)bbTotal);
        report("node counts match the mailbox search", badNodes, 12);
        report("best moves match the mailbox search", badMove, 12);
    }

    std::printf("\n%s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
