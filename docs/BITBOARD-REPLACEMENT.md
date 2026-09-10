# The bitboard replacement

Status: B0 to B6 are built and verified. B7, connecting it, is not started, and
nothing in the shipped engine calls any of it yet.

## What this is

A second, complete engine core built on bitboards, living beside the mailbox
engine and wired to nothing. It gets built and verified in full while the
mailbox engine keeps playing, and only when it is proven equivalent does
anything switch over. Nothing in `chessbot`, the UCI layer or the GUI changes
until the last phase.

This is a full replacement rather than a staged in-place migration because a
staged migration means the engine spends weeks in a state where half the hot
path reads one representation and half reads another, every conversion between
them is a chance to be subtly wrong, and there is no single moment where you can
say the new thing is correct. A disconnected build has that moment: the new core
either reproduces the old core's search tree exactly or it does not.

## Why now, and what the payoff actually is

`bitboard.hpp` says the ceiling is about 1.02x, and `CLAUDE.md` repeats it. That
number came from `BACKLOG.md` 4.0, which measured move generation at about 2
percent of search time. It is stale and it is wrong, because it only counted the
generator called from the search. The gprof profile at depth 9 says:

| symbol                    | share  | what it really is                        |
|---------------------------|--------|------------------------------------------|
| `evaluate_details`        | 29.51% | the evaluation                           |
| `countPseudoLegalMoves`   | 14.99% | mobility, called *from* the evaluation   |
| TT resize                 |  9.41% | startup, not the search                  |
| `Board::isSquareAttacked` |  6.38% | square attack tests                      |
| `evaluate`                |  4.94% | the evaluation again                     |
| `generatePseudoLegalImpl` |  4.78% | the generator in the search              |
| `generateLegalCaptures`   |  3.67% | quiescence                               |
| `toBitboardState`         |  2.39% | 1.4M mailbox to bitboard conversions     |

Mobility is the item that changes the picture. It is 15 percent of the search
and it is a mailbox move count run once per piece per evaluation, when on
bitboards it is a `popcount` of an attack set that SEE and king safety already
need anyway. Add the attack tests, the two generators and the conversions that
disappear entirely when there is nothing to convert from, and roughly 25 percent
of the profile is addressable rather than 2 percent.

That is the honest case: not "bitboards are faster" in the abstract, but that
the specific things this engine spends its time on are the things a mailbox
board is worst at.

## The verification standard

The mailbox engine's bench is **463,295 nodes at depth 6**. That number is a
function of every decision the search makes: the move order at every node, the
evaluation at every leaf, the TT hits, the pruning margins. If the bitboard core
searches the same position to the same depth and visits exactly 463,295 nodes
and returns the same principal variation, then every layer beneath it agrees
with the mailbox engine on every input the search actually gave it.

So the acceptance test is exact tree equality, not "close enough" and not "plays
about as well". Any divergence is a bug to find, not a difference to accept. It
is a demanding standard on purpose: it is the only one that can be checked in
seconds instead of gated over hours, and it means the switch itself is provably
not an Elo event.

Each phase below has its own check so a failure lands on the layer that caused
it rather than surfacing as a wrong node count at the end.

## Where it stands

Run `make test-bbequiv` for the current state of every check below.

| phase | what           | check                                        | result |
|-------|----------------|----------------------------------------------|--------|
| B0    | `Position`     | keys, clocks, FEN, 186,380 make/unmake       | pass   |
| B1    | legal movegen  | perft x6, move sets over 5,982 positions     | pass   |
| B2    | staged/tactical| capture sets, captures+quiets partition all  | pass   |
| B3    | SEE            | 24,695 captures against `see()`              | pass   |
| B4    | evaluation     | 23 terms, exact integers, 5,982 positions    | pass   |
| B5    | move ordering  | ordered sequence over 5,982 positions         | pass   |
| B6    | search         | node counts and best moves, 12 bench positions| pass   |

Speed so far, same machine, same positions:

| component                     | mailbox    | bitboard    | ratio |
|-------------------------------|------------|-------------|-------|
| generate + make/unmake (perft)| 59.8 Mnps  | 129.0 Mnps  | 2.16x |
| evaluation                    | 0.52 M/s   | 1.13 M/s    | 2.17x |
| whole search, depth 8         | 526 knps   | 850 knps    | 1.62x |

The search figure is the one that decides the question, and it is a clean
comparison rather than an estimate: both sides visit 2,210,916 nodes, the same
number, so the ratio is time for identical work. `make bbspeed` reproduces all
three and refuses to print a ratio if the node counts disagree.

Three things came out of building it that were not the point of it:

**The old bitboard generator was slower than the mailbox one**, at 35.4 Mnps
against 56.3. It decided legality by making every pseudo-legal move and testing
the king square, which is the same expensive thing `movegen.cpp` does, so it
paid for a bitboard and did a mailbox's work. That is where the 1.02x ceiling
came from, and it was a fact about that implementation rather than about
bitboards.

**`popcount`, `lsb` and `msb` were out of line in `bitboard.cpp`.** With no
link-time optimisation in this build every call was a real cross-module call,
and `lsb` also carried a live `assert` because `NDEBUG` is not set. The move
generator calls `lsb` once per generated move. Inlining them into the header is
worth about 9 percent of the generator on its own, and it is exactly the trap
`piece.hpp` already documents for the `Piece` accessors.

**The bench signature depends on an unspecified libstdc++ detail.** Both
ordering sorts key on the score alone, so moves the ordering rates identically,
which is most quiet moves, come out in whatever order introsort leaves them.
That order is deterministic for a given input but nothing specifies it, and the
engine has been living on it: making the sort stable instead, which is the least
surprising alternative, costs 11.7 percent more nodes at depth 6, and ordering
ties by (from, to) costs 17.2 percent. So the arbitrary permutation the standard
library happens to produce is better than either principled alternative, and the
signature would not survive a libstdc++ upgrade.

This mattered here because two generators that emit in different orders cannot
produce the same tree unless ties are broken by something order-independent.
The answer is `SearchOptions::orderTieBreak`, off by default: on, both cores use
a total order and are provably identical, which is what B5 and B6 run under; off,
the shipped engine is untouched and bench still reads 463,295. It is a real
ordering change with a real cost, so it is a toggle to be gated on its own rather
than something this work smuggles in.

**Mobility counts an en passant move for the wrong side.** `movegen.cpp` tests
`to == board.enPassantSquare` without asking whose turn it is, so after a double
push the moving side is credited with a phantom en passant capture for any of
its own pawns attacking that square. The bitboard version reproduces it
deliberately, because this phase is measuring whether the two agree and fixing
the numbers at the same time would answer neither question. It belongs in
`BUGS.md` as its own item.

## Phases

**B0. `Position`.** The state the search needs, which `BitboardState` does not
carry: Zobrist hash, halfmove clock, fullmove number and the repetition history.
Plus FEN in and out.
*Check:* FEN round-trip, and the Zobrist key matches the mailbox `Board`'s on
every position in the corpus. The eval cache is keyed on that hash, so a
disagreement here silently poisons everything above it.

**B1. Legal generation without make/unmake.** `generateBitboardLegal` currently
makes every pseudo-legal move and tests the king square, which is the same
expensive thing the mailbox generator does. Replace it with pin and checker
masks, which `blockersForKing` and `checkers` already provide. Fixed-size move
list, no `std::vector`, no heap traffic per node.
*Check:* perft to depth 6 on the standard suite, and set equality with the
mailbox generator's output on every position in the corpus.

**B2. Staged and tactical generation.** Captures first, then quiets, matching
what `movegen.cpp`'s `TacticalFilter` and `generateLegalCaptures` produce.
*Check:* set equality against the mailbox equivalents.

**B3. SEE on `Position`.** `attackersTo` already takes a caller-supplied
occupancy, which is exactly the form the exchange loop wants.
*Check:* agrees with `tests/see`, and with mailbox `see()` on every capture in
the corpus.

**B4. Evaluation on `Position`.** The big one, and the one with the real payoff,
because mobility stops being a move count.
*Check:* exact integer equality with mailbox `evaluate()` on the corpus. Not
close, equal.

**B5. Move ordering.** Same history tables, same scores, same tie-breaks.
*Check:* identical move order at every node.

**B6. Search.** Same TT, same toggles, same pruning.
*Check:* bench is exactly 463,295 at depth 6, same PV, and the same at depths 7
and 8.

**B7. Connect.** Not started.

Two things to settle first, because they are the reason B6's proof is narrower
than it looks. The bitboard core is proven identical to the mailbox core *with
`orderTieBreak` on*. Shipped with it off, the two generators' different emission
orders make the trees differ among equally-scored moves, so connecting is an Elo
event and needs a gate like any other. The alternatives are to gate
`orderTieBreak` on its own first, or to accept the gate on the switch itself.

Also not ported, deliberately: the threading, time management, the aspiration
retry loop above the root and the UCI reporting. Those sit above the node
function and are representation-independent, so they stay in search.cpp and get
pointed at the new core rather than duplicated. `bbSearchRoot` reproduces only
what the bench configuration exercises, which is what B6 needed.

**B7 (original note).** Swap the entry point. The mailbox core stays in the tree behind
a flag for one release so a regression has something to bisect against.

## What could make this not worth finishing

Worth writing down now, while it is still cheap to stop:

- If B4 cannot reach exact equality without reproducing mailbox quirks in the
  bitboard code, the evaluation is carrying accidental behaviour that the
  replacement would have to inherit to pass. That is a reason to fix the
  evaluation first, not to weaken the test.
- If B6 lands and the measured speedup is under about 1.15x, the profile was
  wrong again and the remaining phases are not worth the risk. Measure before
  connecting, not after.
