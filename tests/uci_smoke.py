#!/usr/bin/env python3
"""UCI protocol smoke test.

Checks that the engine speaks enough of the protocol for cutechess-cli, Arena
and the rest of the standard tooling to drive it. Not a strength test: it
verifies handshake, position setup, the three ways to ask for a move, mate
reporting, and that a search can be interrupted.

A protocol bug is invisible to every other test in this repo — the engine plays
perfectly well through its own GUI while being unusable by any external tool —
so it needs a guard of its own.

Run:  make test-uci
"""

import subprocess
import sys
import time

ENGINE = ["./chessbot", "--uci"]
failures = []


def check(name, condition, detail=""):
    if condition:
        print(f"  {name:<34} ok")
    else:
        print(f"  {name:<34} FAILED  {detail}")
        failures.append(name)


class Engine:
    def __init__(self):
        self.p = subprocess.Popen(
            ENGINE, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL, text=True, bufsize=1)

    def send(self, cmd):
        self.p.stdin.write(cmd + "\n")
        self.p.stdin.flush()

    def until(self, token, timeout=60):
        """Read until a line starting with `token`. Returns all lines read."""
        lines = []
        deadline = time.time() + timeout
        while time.time() < deadline:
            line = self.p.stdout.readline()
            if not line:
                break
            line = line.rstrip()
            lines.append(line)
            if line.startswith(token):
                return lines
        raise SystemExit(f"TIMEOUT waiting for {token!r}. Got:\n" + "\n".join(lines))


def main():
    e = Engine()
    print("UCI smoke test")

    e.send("uci")
    out = e.until("uciok")
    check("handshake", any(l.startswith("id name") for l in out))
    # The search heuristics are exposed as options so that A/B testing can run
    # through standard tooling rather than tests/match.cpp.
    check("options advertised",
          all(any(f"option name {o}" in l for l in out)
              for o in ("Hash", "NullMove", "LMR", "Aspiration")))

    e.send("isready")
    check("isready", e.until("readyok")[-1] == "readyok")

    # Fixed depth.
    e.send("position startpos")
    e.send("go depth 6")
    out = e.until("bestmove")
    check("go depth reports info", any(l.startswith("info depth 6") for l in out))
    check("go depth returns a move", out[-1].split()[1] not in ("0000", "invalid"),
          out[-1])

    # Fixed time. The engine must come back inside the budget; returning early
    # is fine and expected, since it will not start an iteration it cannot
    # finish.
    e.send("position startpos moves e2e4 e7e5")
    e.send("go movetime 1500")
    t0 = time.time()
    out = e.until("bestmove")
    elapsed = time.time() - t0
    check("go movetime respects budget", elapsed < 2.5, f"{elapsed:.2f}s")

    # Clock-based: the engine has to derive its own budget from wtime/btime.
    e.send("position startpos")
    e.send("go wtime 10000 btime 10000 winc 100 binc 100")
    t0 = time.time()
    e.until("bestmove")
    elapsed = time.time() - t0
    check("go wtime derives a budget", elapsed < 4.0, f"{elapsed:.2f}s")

    # Mate must be reported as a distance, not a centipawn value.
    e.send("position fen 6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1")
    e.send("go depth 5")
    out = e.until("bestmove")
    check("mate reported as 'score mate'", any("score mate 1" in l for l in out))
    check("mate move is Ra8#", out[-1] == "bestmove a1a8", out[-1])

    # An interruptible infinite search is what "stop" exists for.
    e.send("position startpos")
    e.send("go infinite")
    time.sleep(1.0)
    e.send("stop")
    out = e.until("bestmove", timeout=15)
    check("infinite search stops on demand",
          out[-1].startswith("bestmove") and out[-1].split()[1] != "0000")

    # Promotions use UCI spelling (e7e8q), not the human "e7e8=Q".
    e.send("position fen 8/P6k/8/8/8/8/7K/8 w - - 0 1 moves a7a8q")
    e.send("go depth 4")
    out = e.until("bestmove")
    check("promotion move accepted", not any("illegal move" in l for l in out))

    # An unknown command must be ignored, not fatal.
    e.send("nonsense command")
    e.send("isready")
    check("unknown command ignored", e.until("readyok")[-1] == "readyok")

    e.send("quit")
    try:
        e.p.wait(timeout=10)
        check("quit exits cleanly", e.p.returncode == 0, f"rc={e.p.returncode}")
    except subprocess.TimeoutExpired:
        check("quit exits cleanly", False, "did not exit")
        e.p.kill()

    if failures:
        print(f"\nFAILED: {len(failures)} check(s): {', '.join(failures)}")
        return 1
    print("\nPASSED: all UCI checks")
    return 0


if __name__ == "__main__":
    sys.exit(main())
