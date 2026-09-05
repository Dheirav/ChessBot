// Dump the hand-crafted evaluation for a list of FENs, one per line.
//
//   tools/evaldump < fens.txt > hce.txt
//
// Exists so a trained net can be compared against the evaluation it is meant to
// replace, on identical positions, without writing any accumulator code. That
// comparison is the go/no-go for the invasive part of the NNUE work: if the net
// cannot beat this on held-out positions, nothing downstream is worth starting.
#include "engine/board.hpp"
#include "engine/evaluation.hpp"
#include "engine/move_lookup.hpp"
#include <cstdio>
#include <iostream>
#include <string>

int main() {
    initMoveLookupTables();
    Board b;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        // setFromFEN, never parseFEN: parseFEN leaves the zobrist hash stale and
        // evaluate() is keyed on it, so every position would collide on the
        // default hash and return whatever the eval cache stored first.
        if (!b.setFromFEN(line)) { std::printf("0\n"); continue; }
        std::printf("%d\n", evaluate(b));   // White's point of view
    }
    return 0;
}
