# Known defects — 2026-08-14

Bugs the engine is known to have, ordered by what fixing them is worth. Each
entry names the evidence, so a fix can be checked against the thing that found
it rather than against an opinion.

This list exists because the first 24 rated games on Lichess found defects that
no test in the suite covers. `test-perft` proves move generation, `test-evalref`
proves evaluation has not changed, `test-bench` proves the tree has not changed
— none of them can notice that the engine draws a won game or evaluates a
symmetric position asymmetrically. Real games are the only instrument that has
caught these.

Fix order is not the order they were found. It is roughly expected Elo per hour
of work, with one exception noted at 3.

---

## The baseline these are measured against

See `HANDOFF.md` § *Measured playing strength*. In short: **Lichess rapid 2198,
rd=121, 16-6-2** as of 2026-08-14 00:04, all of it on the build at `62d1043`.
Every move in those games came from this engine — every book and tablebase
source in `lichess/config.yml` is `enabled: false`.

---

## 1. The search cannot see the game's move history

**Cost: proven. One drawn win against a 1404, −48 rating in a single game.**

`Crimsy_Bot vs sargon-2ply`, [CTGzqoeY](https://lichess.org/CTGzqoeY). The
engine was a rook and a pawn up, its own eval reading **+5.16**, and it drew by
threefold repetition — playing the repeating move itself:

```
9.Qxa8+ Qd8 10.Qc6+ Qd7 11.Qa8+ Qd8 12.Qc6+ Qd7 13.Qa8+  ½–½
```

That third `Qa8+` is the engine's choice. The position after it had already
occurred twice.

**Cause.** `search.cpp:299` does detect repetition, but only against
`pathHashes`, and `pathHashes` is a fresh local vector seeded with nothing but
the current root position (`search.cpp:558`). `uci.cpp`'s `position ... moves
...` handler replays the game onto the board but records no hash along the way.
So the search sees repetitions it creates *inside its own tree* and is blind to
every position the game has actually visited. From the root it just sees
`Qa8+` → +5.16, and it will see that forever.

**Fix.** Collect the zobrist key after each move replayed in the `position`
handler and hand that history to the search as the initial `pathHashes`. Count
occurrences rather than treating any repeat as a draw, so the engine can tell a
second occurrence from a third.

**Note the symmetry:** the engine also cannot *seek* a repetition when it is
losing. This is not only a way to throw away wins; it is a missing way to save
losses.

**Verification.** Changes the tree, so `test-bench` will fail and the signature
will move. That is correct, not a regression — read the diff before regenerating.
Add a test that replays the CTGzqoeY move list and asserts the engine does not
play `Qa8+` at move 13.

---

## 2. `tempoBonus` is a float, and it truncates the whole evaluation

**PLAN.md 4.1.** `evaluation.cpp:235` declares it `float 0.01f`; summed into
`e.total` at `:588` it promotes the sum to float, and truncation toward zero
turns −5 into −4. The result is a one-centipawn asymmetry favouring Black that
has nothing to do with tempo.

Fix first, not because it is worth much on its own, but because it corrupts the
reading of every other evaluation term — including 3 below. Make it an honest
`int` applied to the side to move, or delete it and let a match decide.

---

## 3. King safety is asymmetric in a symmetric position

**PLAN.md 4.2.** The starting position is mirror symmetric, so every
white-perspective term must be exactly 0. `kingSafety` is **−4**. Material,
mobility, PST and centre control are all correctly 0, so this is specific to the
king-safety term and not a general orientation error.

This is a colour-dependent error in *every* position the engine evaluates, not
just the start. It is listed third only because 2 must land first for the
number to be readable.

**While fixing:** add a mirror-symmetry check to `tests/evalref.cpp` — flip
colours and ranks, assert every term negates exactly. It is the strongest cheap
invariant an evaluation has, it needs no reference file, and it would have
caught this the day the term was written.

---

## 4. Quiescence has no depth bound

**PLAN.md 3.1, still unimplemented.** `quiescence()` (`search.cpp:166`) takes a
`ply` but never caps it. When in check it generates *all* legal moves rather
than captures (`search.cpp:184`), which is correct — stand-pat is illegal in
check — but it means a long forcing sequence of checks recurses without limit.

No game has demonstrably blown up on this yet, which is why it sits below the
proven defects. But the failure mode is a search that overruns its budget in a
sharp position, and an overrun is a forfeit. The cheap fix is a ply cap plus
delta pruning, both of which PLAN 3.1 already specifies.

---

## 5. "Defended" does not mean defended

**PLAN.md 4.3.** The undefended-pieces term counts a piece as defended if any
friendly piece stands on an *adjacent square*, which measures how pawn-chain-ish
a position is, not whether anything is actually protected. `attackedBy[own][sq]`
is already built by `forEachAttackedSquare` for the threat term and is exactly
the right predicate. Nearly free at runtime.

---

## 6. Play is deterministic, so repeated pairings replay the same game

The engine met `sargon-3ply` twice. The two games are **identical for 36 plies**
— through move 18 — and diverge only at move 19, where clock variation changed
the depth reached. There is no opening book (`polyglot.enabled: false`, and no
book files exist) and no randomisation among equal-scoring root moves.

Two consequences, and the second is the one that matters:

- Any opponent that finds one refutation gets to use it every single game.
- **Results against a given opponent are correlated, not independent samples.**
  The 24-game record is worth less than 24 games of evidence, and the more the
  bot plays the same small pool of opponents, the worse that gets.

The cheap fix is a small random tiebreak among root moves within a few
centipawns of the best. That changes the tree, so it needs the same bench
discipline as 1 — and, unlike everything above it, it makes the engine
irreproducible unless the randomness is seeded and the seed is logged. Weigh
that against the gate methodology before committing to it.

---

## 7. Operational: restarting the bot mid-game forfeits a rated game

Not an engine defect, but the single most expensive mistake made so far.

`Crimsy_Bot vs MostlyHuman1900`, [zXKXX7WO](https://lichess.org/zXKXX7WO),
`Termination "Time forfeit"`, **−120** — the largest rating loss on the account.
The bot was killed mid-game during the 2026-08-13 rebuild. Its clock ran out
because the process was gone.

Stop the bot **between** games, never during one. Check for a live game first:

```bash
pgrep -f 'chessbot --uci' && echo "in a game — wait"
```

The position was also losing on the board (`19.Qd4` walked the queen onto the
a1-h8 diagonal against `Bf6`, met by `Bxd4` with no recapture), so the forfeit
did not cost a won game. It cost 120 rating points that a normal loss would not
have.

---

## Things that look like bugs and are not

- **Two games against `ficheallrs` show `Termination "Abandoned"` after
  `1.Nf3 *`.** Our bot made its move; the opponent never replied. No rating
  effect, nothing to fix.
- **The four losses on 2026-08-12 were all as White**, which looked like a
  colour-dependent defect and would have paired suspiciously with 3. It is not:
  matchmaking simply gave the engine 2274-3042 opponents while it had White and
  1200-1800 opponents while it had Black. Over 24 games the split is 5 losses as
  White and 1 as Black against an average opponent 344 points stronger with
  White. The signal disappears once the field is accounted for.
- **The engine never resigns** (`draw_or_resign.resign_enabled: false`). That is
  a config choice, not a defect. Playing on in lost positions costs nothing but
  the opponent's time.
