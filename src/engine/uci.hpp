#pragma once

// UCI (Universal Chess Interface) front end.
//
// Reads UCI commands from stdin and writes responses to stdout until "quit".
// This is what lets the engine be driven by cutechess-cli, Arena, Banksia and
// the rest of the standard tooling — which is a far better testbed than the
// bespoke harness in tests/match.cpp: opening books, gauntlets, concurrency
// across cores and standard SPRT all come for free.
//
// Entered with `./chessbot --uci`. The GUI is unaffected.
int uciLoop();
