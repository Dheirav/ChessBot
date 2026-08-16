// The engine as a bare UCI binary, for gating.
//
// `./chessbot --uci` is the same engine, and using it here would be the obvious
// thing. Two reasons not to.
//
// It links SFML and carries the GUI, so building it relinks the binary the
// Lichess bot spawns per game — which silently deploys an ungated engine into
// the next rated game (docs/TODO.md, "Before touching anything").
//
// And a gate runs two engine processes per shard, so fourteen shards put
// twenty-eight processes named `chessbot` on the machine. `pgrep -x chessbot`
// is how this project asks "is a rated game live"; it has already produced
// false positives three times (BUGS.md 9), and a gate that makes that check lie
// for six hours is how someone rebuilds the engine mid-game.
//
// Any arguments are accepted and ignored, including the `--uci` that
// UciEngine::start() passes by default: this binary has no other mode.
#include "engine/move_lookup.hpp"
#include "engine/uci.hpp"

int main() {
    // Without this every lookup table is empty and move generation returns no
    // moves, which reads as "every game ended immediately" rather than as an
    // error.
    initMoveLookupTables();
    return uciLoop();
}
