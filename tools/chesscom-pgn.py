#!/usr/bin/env python3
"""Fetch a Chess.com game by link or id and write it as a PGN.

    tools/chesscom-pgn.py https://www.chess.com/game/187525839308
    tools/chesscom-pgn.py 187525839308 -o game.pgn

Standard library only, so it runs without a virtualenv. It reads the same JSON
the game page loads for a logged-out visitor, so it needs no account, and it
works for any finished game whose page you can open.

Why this does not use the public API instead: api.chess.com serves a player's
monthly archive, which needs the username and month, and a bare game link has
neither. The page's own JSON is keyed by the id alone.

Moves arrive in Chess.com's own encoding, two characters per ply, from-square
then to-square. Turning those into SAN is left to tools/uci2pgn, which uses the
engine's toSan() and refuses any move that is not legal where it is played, so
a decoding mistake stops with a ply number instead of producing a different game.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
HELPER = os.path.join(HERE, "uci2pgn")

# Index = square number (a1=0 .. h1=7, a2=8 .. h8=63). Characters past 63 only
# appear as the destination of a promotion.
ALPHABET = ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "0123456789!?{~}(^)[_]@#$")
PROMOTION_PIECES = "qnrbkp"


def square(i):
    return "abcdefgh"[i % 8] + str(i // 8 + 1)


def decode_moves(move_list):
    """Chess.com's move string -> coordinate moves ("e2e4", "e7e8q")."""
    if len(move_list) % 2:
        raise ValueError("move list has an odd number of characters")
    moves = []
    for i in range(0, len(move_list), 2):
        try:
            frm, to = ALPHABET.index(move_list[i]), ALPHABET.index(move_list[i + 1])
        except ValueError:
            raise ValueError("unknown character in move list at ply %d" % (i // 2 + 1))
        suffix = ""
        if to > 63:
            # A promotion packs the piece and the file step (-1, 0, +1, for a
            # capture left, a push, a capture right) into one character, because
            # the destination rank is implied by the pawn's own rank.
            code = to - 64
            suffix = PROMOTION_PIECES[code // 3]
            step = code % 3 - 1
            to = frm + (8 if frm >= 32 else -8) + step
        moves.append(square(frm) + square(to) + suffix)
    return moves


def parse_link(text):
    """(kind, id) from a link or bare id. kind is "live", "daily" or None."""
    m = re.search(r"/(?:game|analysis/game|live/game|live)/(live|daily)/(\d+)", text)
    if m:
        return m.group(1), m.group(2)
    m = re.search(r"/(?:game|analysis/game)/(\d+)", text) or re.fullmatch(r"\s*(\d+)\s*", text) \
        or re.search(r"[#?&]g=(\d+)", text)
    if m:
        return None, m.group(1)
    raise ValueError("no Chess.com game id in %r" % text)


def get_json(url):
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0", "Accept": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return json.load(r)
    except urllib.error.HTTPError as e:
        if e.code == 404:
            return None
        raise


def fetch(kind, game_id):
    kinds = [kind] if kind else ["live", "daily"]
    for k in kinds:
        data = get_json("https://www.chess.com/callback/%s/game/%s" % (k, game_id))
        # A missing game answers 404 with a body of "[]", not an error object.
        if isinstance(data, dict) and "game" in data:
            return k, data
    raise LookupError("Chess.com has no %s game %s (private, deleted, or the id is wrong)"
                      % ("/".join(kinds), game_id))


def clocks_ms(game, plies):
    """Remaining clock after each ply, in ms, or None when the game has none.

    Chess.com stores it as tenths of a second remaining. Daily games have no
    clock in that sense, so they simply get none.
    """
    stamps = game.get("moveTimestamps")
    if not stamps:
        return [None] * plies
    try:
        vals = [int(float(x)) * 100 for x in stamps.split(",")]
    except ValueError:
        return [None] * plies
    return (vals + [None] * plies)[:plies]


def tag(name, value):
    return '[%s "%s"]\n' % (name, str(value).replace("\\", "\\\\").replace('"', '\\"'))


def build_pgn(kind, data):
    game = data["game"]
    if game.get("type", "chess") != "chess":
        raise ValueError("only standard chess is supported, this is %r" % game.get("typeName"))
    h = game.get("pgnHeaders", {})
    fen = h.get("FEN") or game.get("initialSetup") or ""
    standard = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
    if fen == standard:
        fen = ""

    uci = decode_moves(game["moveList"])
    clocks = clocks_ms(game, len(uci))
    tokens = [m if c is None else "%s@%d" % (m, c) for m, c in zip(uci, clocks)]

    game_id = str(game.get("id", ""))
    link = "https://www.chess.com/game/%s/%s" % (kind, game_id)
    tc = str(h.get("TimeControl", ""))
    cmd = [HELPER,
           "--white", h.get("White", "?"), "--black", h.get("Black", "?"),
           "--result", h.get("Result", "*"), "--date", h.get("Date", ""),
           "--event", h.get("Event", "Chess.com"), "--site", "Chess.com", "--tc", tc]
    if fen:
        cmd += ["--fen", fen]
    if not os.access(HELPER, os.X_OK):
        raise RuntimeError("%s is not built. Run `make uci2pgn` (not while a gate is running)." % HELPER)
    run = subprocess.run(cmd, input=" ".join(tokens), capture_output=True, text=True)
    if run.returncode != 0:
        raise RuntimeError("the moves did not replay: " + run.stderr.strip())

    # The helper writes the seven-tag roster; the rest go after it. Order inside
    # the extras does not matter to a reader, but Elo before ECO is the
    # convention every export follows.
    extras = ""
    for name, key in (("WhiteElo", "WhiteElo"), ("BlackElo", "BlackElo"),
                      ("TimeControl", "TimeControl"), ("ECO", "ECO"),
                      ("Termination", "Termination")):
        if h.get(key) not in (None, ""):
            extras += tag(name, h[key])
    extras += tag("Link", link)
    head, sep, body = run.stdout.partition("\n\n")
    return head + "\n" + extras.rstrip("\n") + sep + body, game


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("game", help="Chess.com game link, or the numeric id")
    ap.add_argument("-o", "--output", help="write here instead of stdout")
    ap.add_argument("--id", action="store_true", help="print the game id and stop (for scripts)")
    args = ap.parse_args()
    try:
        kind, gid = parse_link(args.game)
        if args.id:
            print(gid)
            return 0
        kind, data = fetch(kind, gid)
        pgn, game = build_pgn(kind, data)
    except (ValueError, LookupError, RuntimeError, urllib.error.URLError) as e:
        print("chesscom-pgn: %s" % e, file=sys.stderr)
        return 1
    if args.output:
        with open(args.output, "w") as f:
            f.write(pgn)
        h = game.get("pgnHeaders", {})
        print("%s vs %s, %d plies -> %s" % (h.get("White"), h.get("Black"),
                                           game.get("plyCount", 0), args.output), file=sys.stderr)
    else:
        sys.stdout.write(pgn)
    return 0


if __name__ == "__main__":
    sys.exit(main())
