# Known defects — 2026-08-15

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

**Entries keep their numbers when fixed.** Code comments and `HANDOFF.md` cite
them by number, so a fixed entry is marked, not deleted or renumbered.

---

## The baseline these are measured against

See `HANDOFF.md` § *Measured playing strength*. In short: **Lichess rapid 2198,
rd=121, 16-6-2** as of 2026-08-14 00:04, all of it on the build at `62d1043`.
Every move in those games came from this engine — every book and tablebase
source in `lichess/config.yml` is `enabled: false`.

---

## 1. The search cannot see the game's move history — **FIXED 2026-08-14**

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

**Note the symmetry:** the engine also could not *seek* a repetition when it was
losing. This was not only a way to throw away wins; it was a missing way to save
losses.

### What the fix does

`findBestMoveIterativeDeepening` now takes the positions the game already
visited, and every caller that plays a game supplies them: the UCI `position`
handler (`uci.cpp`), the match harness (`tests/match.cpp`), and the GUI through
`GameManager::repetitionHistory()`. The history stops at the last irreversible
move, because nothing across a capture or a pawn push can recur — which also
keeps it short enough for the linear scan the search does per node.

The search then applies **two** rules rather than one, and the difference
between them is the whole point:

- **Inside the tree, one match is a draw.** Both sides are choosing moves, so a
  line that reaches a position twice can normally reach it a third time. This is
  the pre-existing behaviour, deliberately left alone.
- **Against the game's past, one match is not.** It makes the current position
  only the *second* occurrence, and a second occurrence is not a draw. Scoring
  it 0 would make the engine decline winning lines and claim draws that do not
  exist — trading this bug for a worse one. Two prior occurrences make this the
  third, which is the one that ends the game.

### Verification

- `tests/uci_smoke.py` replays CTGzqoeY to move 13 and asserts the engine does
  not answer `c6a8`. It asserts *not the draw* rather than a specific
  replacement, so a future evaluation change cannot fail it spuriously. Checked
  against the pre-fix binary first: it fails there, which is the only way to
  know a regression test is wired to the thing it claims to guard.
- **The bench signature did not move** (still 1 465 771). That is the expected
  result, not a surprise: bench passes no history, so the new rule can never
  fire, and the search it measures is unchanged. The fix adds knowledge the
  engine did not have rather than altering how it searches a position with no
  past.
- Before: `bestmove c6a8`, score +515. After: `bestmove c6c3`, score +481 —
  it takes the pawn and keeps the win instead of checking into the draw.

Not yet gated. The change is inert without history, so no A/B match can measure
it at all — the only instrument that can is games against real opponents.

---

## 2. Colour-blind constants added to a colour-relative score — **FIXED 2026-08-14**

**PLAN.md 4.1.** Two addends in `e.total` were the same for both sides, so both
handed White a bonus for nothing. Only one of them was in the backlog; the
mirror-symmetry test found the other.

- **`tempoBonus`** was declared `float 0.01f`. Summed into an otherwise integer
  `e.total`, it promoted the whole expression to float, and truncation toward
  zero turned −5 into −4. It was also a constant rather than a bonus to the side
  to move, so it never measured tempo at all.
- **`(int)(gamePhaseFactor * 1.5f)`** added 1 centipawn to White in any position
  with roughly a full opening's material. The game phase is a property of the
  position, identical for both sides — a legitimate *weight*, which is how it is
  still used for the king piece-square blend, and meaningless as a *term*.

Both are gone. `e.total` is now integer throughout and made only of terms that
negate when the board is mirrored.

An honest tempo bonus applied to the side to move remains a real idea, and is
deliberately **not** included here: it changes evaluation, so it needs its own
gate rather than a free ride on a bug fix.

---

## 3. King safety is asymmetric in a symmetric position — **FIXED 2026-08-14**

**PLAN.md 4.2.** The starting position is mirror symmetric, so every
white-perspective term had to be 0. `kingSafety` was **−4**.

The cause was `|x - 3|` as distance from the centre. A board has eight ranks, so
its centre lies *between* 3 and 4; `|x - 3|` charges 4 at one edge and 3 at the
other. White's king on rank 7 was therefore penalised one more than Black's
identical king on rank 0 — in every position, from move one, in every game the
engine has ever played. The same flaw applied to files, where it made a king on
a1 score differently from one on h1.

Fixed by measuring to the nearer of the two central coordinates
(`centreDistance` in `evaluation.cpp`), which is symmetric under both
reflections. Range is now 0..6 instead of 0..7, so king safety is slightly
smaller in magnitude; the multiplier is a tuning question and a separate, gated
one.

### The mirror-symmetry test

`tests/evalref.cpp` now reflects every position top to bottom, swaps the
colours, and asserts that every term comes back exactly negated. It is the
strongest cheap invariant an evaluation has, and unlike the reference
comparison beside it, **it cannot be regenerated into agreement** — there is no
file to rewrite. It says the evaluation is *wrong*, not merely *changed*.

Run against the unfixed build it reports:

```
FAILED: 4691 of 4691 positions evaluate asymmetrically
        Terms at fault: total(4691) kingSafety(4499)
```

Every other term was already symmetric, which is why the fix touched only these
two. The starting position now evaluates to exactly 0 in every column.

### What it cost

Reviewed before regenerating either reference, as the diff is the only thing
those tests are for:

- **`evalref`**: 23 590 of 23 603 positions moved, and only `kingSafety` and
  `total` changed — the other 21 terms are untouched. Every `kingSafety` delta
  is a multiple of 4 (the term's own multiplier), and every `total` delta equals
  its `kingSafety` delta minus the removed game-phase constant, minus one more
  where the old float truncated a negative sum toward zero. No unexplained
  movement.
- **`bench`**: **1 465 771 → 1 725 755 nodes**, +17.7%, and **no best move
  changed** in any of the twelve positions. The increase concentrates in the
  opening positions and reverses in the endgames.

That node cost is the part to be honest about. A correct evaluation is not
automatically a stronger one at equal *time*: +17.7% nodes at a fixed depth is
roughly a fifth of a ply given up on the clock. The plausible cause is that a
symmetric evaluation returns exact ties more often, and proving equality costs
alpha-beta more than proving superiority — but that is a hypothesis, not a
measurement.

**Gated 2026-08-14, after the fact.** 3 360 games at `-N 100000`, driving the
two binaries over UCI (the capability entry 8 exists for): **+6.1 Elo, 95% CI
[−3.9, +16.1]** for the corrected evaluation over `56c41d9`. The interval spans
zero, so no gain is demonstrated — but the fixes were shipped on correctness
grounds and a bad number would not have reversed them. What the gate does rule
out is a loss.

It measured quality per node and says nothing about the +17.7% node cost, since
a node budget pays both sides the same nodes.

---

## 4. Quiescence has no depth bound — **FIXED 2026-08-14**

**PLAN.md 3.1, still unimplemented.** `quiescence()` (`search.cpp:166`) takes a
`ply` but never caps it. When in check it generates *all* legal moves rather
than captures (`search.cpp:184`), which is correct — stand-pat is illegal in
check — but it means a long forcing sequence of checks recurses without limit.

No game had demonstrably blown up on this, which is why it sat below the proven
defects. But the failure mode is a search that overruns its budget in a sharp
position, and an overrun is a forfeit.

**Fixed** with a `QS_MAX_DEPTH` of 8 plies past the horizon, behind a `qbound`
toggle that defaults **on** — a repair rather than a feature, on the same
reasoning that put `ttAging` on, and still a toggle so the repair can be
measured rather than assumed.

The diff is the right shape for a horizon bound: six of the twelve bench
positions are byte-identical, and the two pathological ones absorb nearly all of
it — kiwipete −26.0%, tactical −23.4%, everything else between 0 and −1.4%. No
best move changed. Bench **1 759 990 → 1 464 599, −16.8%**.

**It also settles the open question from the Phase 4 gate.** Those evaluation
fixes cost +20.1% nodes, and an equal-nodes gate divides that out by
construction, so whether they were affordable on a clock was unanswered. They
now cost nothing: 1 464 599 against the 1 465 771 that preceded them. The
quiescence bound paid the whole bill, and no timed gate was needed to find that
out — a bench run of a few seconds did it.

Delta pruning, the other half of PLAN 3.1, is implemented alongside it but
defaults **off**: it is a feature, it changed a best move, and it ships only if
a gate says so. See PLAN 3.1.

---

## 5. "Defended" does not mean defended — **FIXED 2026-08-14**

**PLAN.md 4.3.** The undefended-pieces term counted a piece as defended if any
friendly piece stood on an *adjacent square*. That is a different property from
the one the term is named after: it scored a knight beside its own rook as
defended when neither could recapture on the other's square, and a rook defended
down an open file as undefended because the defender was five squares away.
What it measured was how clumped the pieces were.

Replaced by `attackedBy[own][sq]`, which was already built by
`forEachAttackedSquare` for the threat term, so the fix also deleted a
nine-square scan per piece.

**Diff review.** Only `undefended` and `total` moved, on 18 994 of 23 603
positions, and `total` moved by exactly the `undefended` delta on every one of
them — checked rather than assumed, along with every delta being a multiple of
the term's own 5. Mirror symmetry still holds.

**bench: 1 725 755 → 1 759 990** (+2.0%), with two best moves changed:

- `open-sicil` `d7d5` → `b8c6`. An improvement, and a resolution: this is
  `1.e4 c5 2.Nf3`, where `2...d5 3.exd5 Qxd5` costs Black time and `2...Nc6` is
  the main line. `d7d5` appeared when `seeordering` was gated on and was
  recorded then as unexplained (`PLAN.md` 3.2). It is gone.
- `midgame-2` `c1c2` → `c3b5`. Unremarkable. White is a bishop down in that
  position (`material` −335, correctly), and at depth 9 the engine plays `f3d2`
  from both builds — the depth-6 difference is shuffling in a lost position.

Ungated, on the same reasoning as 2 and 3, and it should be measured in the same
match rather than a separate one.

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
pgrep -x chessbot && echo "in a game — wait"
```

`pgrep -x` on the command name, not `pgrep -f` on a pattern — see 9. This entry
recommended the `-f` form until 2026-08-15, which is its own small instance of
the same lesson.

The position was also losing on the board (`19.Qd4` walked the queen onto the
a1-h8 diagonal against `Bf6`, met by `Bxd4` with no recapture), so the forfeit
did not cost a won game. It cost 120 rating points that a normal loss would not
have.

### SIGINT does not finish the current game — 2026-08-15

`lichess-bot`'s handler looks reassuring: the first SIGINT only sets
`stop.terminated = True`, and a second sets `force_quit`. Reading that, it is
natural to conclude the first one is graceful and lets the live game finish.
**It does not.** Signalled during `Crimsy_Bot vs JDoss_BOT`
([gUH04Bb7](https://lichess.org/gUH04Bb7)), the bot stopped playing the game and
exited, leaving our clock running with nobody to answer.

The game survived only because the opponent was slow and it was noticed within
minutes; restarting `lichess-bot` made it reconnect and resume the game in
progress. It still cost **eight minutes of clock** — 12.2 down to 4.4 — in a
rated 900+10 game against a 2161. It was one slow reply away from being a second
time forfeit.

**And the check afterwards proves nothing.** The obvious way to confirm it was
safe is to look for the engine process after signalling:

```bash
kill -INT "$botpid"; sleep 3
pgrep -x chessbot || echo "no game was running"     # WRONG
```

That reports "no game" every time, because the engine exits *as a consequence*
of the signal. It measures the effect of the action, not the precondition for
it. The reading was taken on 2026-08-15 and was believed.

The signal to wait on is the **game's result on Lichess**, which no local
process can fake:

```bash
curl -s "https://lichess.org/game/export/$GAME?moves=false&tags=true" | grep '^\[Result'
# [Result "*"] means still playing
```

Then stop the bot only once `pgrep -x chessbot` finds nothing *before* any
signal is sent — that gap is what "between games" actually means.

The general form of the mistake is worth keeping, because it is not really
about `lichess-bot`: **reading an implementation is not the same as having
tested it.** `ROADMAP.md` already says a documented command that has never been
run is a guess. Inferring behaviour from source is the same guess wearing better
clothes.

---

## 8. The harness cannot gate an evaluation change — **FIXED 2026-08-14**

Not an engine defect — an instrument one, and it surfaced only when Phase 4
became the first evaluation change this project has tried to measure.

`tests/match.cpp` plays two `SearchOptions` configurations **inside one
process**, and `g_evalCache` (`evaluation.cpp`) is a single process-global array
it never clears between them. A position scored under configuration A is served
from that cache to configuration B. Every gate run so far compared *search*
changes — `nullmove`, `lmr`, `aspiration`, `seeordering`, `seepruning`,
`ttaging` — none of which touch evaluation, so the shared cache was harmless. It
is not harmless now, and `PLAN.md` 5.4 (lazy evaluation) will meet the same wall.

There is also no way to point the harness at two *binaries*, which is the other
way this is normally done.

One correction to the original wording: the shared cache is *latent*, not
active. `evaluate_details()` reads no options at all, so both sides compute the
same evaluation today and the cache is harmless. It would have become harmful
the moment anyone added an eval toggle to gate Phase 4 — which is the obvious
way to attempt it, and would have failed silently.

### The fix

`tests/match --engineA <path> --engineB <path>` drives engine *binaries* over
UCI instead of searching in-process. Two processes share no eval cache, no
transposition table and no globals, so the problem is gone structurally rather
than patched around. It also means a gate compares the binary that actually
ships rather than a flag approximating it — and nothing has to keep superseded
code alive just so a match can see it. Build the two commits and pass both
paths.

`go nodes <n>` was added to the UCI layer to go with it (`uci.cpp`), because a
cross-binary gate cannot use the in-process node budget, and without it every
such gate would have had to be a timed one — the expensive, unshardable kind.

Verified by playing an external binary against the in-process engine: same
build both sides, games completing by mate and adjudication, 2-0-2 over four
games.

**One thing the driver got wrong first, because it is easy to miss.** A UCI
engine picks its own default hash — this one takes 256 MB — and the driver did
not set it. A twelve-shard gate runs two external engines per shard, so it
quietly asked for 6 GB of transposition table on a 7.7 GB machine and took WSL
down twice on 2026-08-14, the second time 44 minutes into a run. `start()` now
takes a `hashMb` and sends `setoption name Hash` during the handshake, defaulted
to the 32 MB a side that the in-process path uses, so the two modes are
comparable as well as bounded: 46 MB resident per engine, measured. Sharded
memory is roughly `2 x shards x (hash + 15 MB)` — budget it before raising the
shard count. `tests/uci_smoke.py` covers `go nodes`, and takes a `CHESSBOT` override
so a build that is not `./chessbot` can be smoke-tested — needed whenever the
Lichess bot is live and relinking `./chessbot` would swap the engine mid-game.

---

## 9. Commands that were never run: `shard-gate.sh`, CI, and a silent parser

`HANDOFF.md` carried, as its top next step, a command that exits immediately:

```
$ ./tests/shard-gate.sh 14 120 -t 3000 --optA seepruning=on --optB seepruning=off
refusing: shard only a node-limited gate (-N); see header
```

The script is right and the instruction was wrong — its header explains that
shards under a time budget compete for the CPU and pool into a time control
nobody chose. `-t` is also a budget per *move*, so 3 000 ms was roughly nineteen
days for the volume the other gates used. Corrected on 2026-08-14 to sequential
`tests/match` at a time control sized from measured throughput.

The same day, two more of the same shape turned up.

**CI had been failing since 2026-08-11 — twenty commits.** `e2b2269` (08-10)
added the smoke step `./tests/match 2 4 20260810`; `9961b55` (08-11) made both
sides default to the shipped configuration and taught the harness to refuse an
A/B that differs in nothing. The guard is right and the step was not updated
with it, so every push mailed a failure. Fixed in `3fc24e0` by asking for the
old comparison explicitly: `-n 2 -d 4 -s 20260810 --hb off`.

**The positional parser ignored everything after the third argument.** That is
how the CI fix was nearly wrong twice: `./tests/match 2 4 <seed> --hb off`
dropped the `--hb` in silence, and `./tests/match 100 6 <seed> -N 100000` would
have quietly run a depth match instead of a node-budgeted one — a
misconfigured measurement that runs and reports. It now refuses and prints the
flag-form equivalent.

**`pgrep -f 'tests/match'` matches its own command line.** It is the check
`CLAUDE.md` and `HANDOFF.md` both recommend before `make tests`, and on
2026-08-14 it produced three false positives in a single session: a gate that
was not running, a poll loop that could never terminate, and a watcher reported
alive after it had exited. Use `pgrep -x` on the command name.

**It came back three more times on 2026-08-15**, which is why this is written
out rather than left as a one-line warning. `pgrep -f 'lichess-bot\.py'`
reported the bot alive after it had exited — the match was the shell running the
check, whose command line contained the string. Acting on that would have meant
signalling the wrong process, and a script that looked up the PID that way was
written before the problem was spotted.

`pgrep -x` fixes the process check but not the general case, because sometimes
the full command line is genuinely what you need — to tell one Python process
from another, say. Then the pattern has to exclude the shells:

```bash
ps -eo pid,args --no-headers | grep 'lichess-bot\.py' \
  | grep -v 'bash -c' | grep -v grep | awk '{print $1}'
```

The same trap catches `ps | grep` output being read by eye: a listing of
"`chessbot` processes" on 2026-08-15 showed four, three of which were shell
commands that merely mentioned the path. **Before believing any process listing,
check whether the tooling doing the looking is in it.**

The lesson is the cheap one, and it cost a day to learn three times: a
documented command that has never been run is a guess. The `shard-gate.sh` one
sat at the top of `HANDOFF.md` and was carried forward through two separate
edits before anyone tried it.

---

## 10. `mate 0` read as a win for the side that had just been mated — **FIXED 2026-08-15**

Not an engine defect — an instrument one, in the UCI score parser that the gate
harness and the review tool share.

`captureScore` (`uci_engine.hpp`) mapped `score mate n` to `30000 - 2n` whenever
`n >= 0`. But a UCI engine reports `score mate 0` for a position that is
*already* checkmate, so that branch handed **+30000 to the side to move — the
side that had just been mated**. Every mate in the archive was therefore scored
as a 2 000-centipawn swing away from the player who delivered it.

Confirmed against the analysing engine rather than inferred:

```
position fen R6k/5ppp/8/8/8/8/8/6K1 b - - 0 1
info depth 0 score mate 0
```

Stalemate, for contrast, reports `score cp 0` and was never affected.

**The gates are not affected**, which is worth stating precisely because the
parser is shared with `tests/match`. The harness tests for checkmate itself
before asking an engine to move (`match.cpp:195-199`), so a mated position never
reaches `bestMove()` and `mate 0` never reaches the adjudicator. `tools/review`
searches all *n+1* positions of a game, including the terminal one, and did.

### What it cost the numbers

This is the part to be honest about, because the corrupted figures were
published as a baseline. The whole-archive profile in `REVIEW.md` was measured
with this bug live, and every game the bot won by mate carried a phantom
2 000-centipawn blunder attributed to the winning move.

Over the 62-game archive, average centipawn loss falls from **51.9 to 20.8**,
and the correction is proportional to how often the bot delivered mate:

| opponent band | avg cp loss, buggy | corrected |
|---|---|---|
| under 1500 | 86.4 | **14.2** |
| 1500-1900 | 64.3 | **19.3** |
| 1900-2100 | 45.9 | **24.3** |
| 2100-2300 | 27.5 | **22.0** |
| 2300+ | 29.2 | **29.2** |

The 2300+ row is unchanged because the bot has never mated an opponent in that
band — which is the check that the mechanism is understood rather than merely
plausible.

That gradient is also what made "accuracy is flat across opponent strength" look
true when the sweep was first read. It is not flat; it declines monotonically.
See `ROADMAP.md` 6.1.

**A reference measured through a broken instrument is worse than no reference**,
because it gets quoted. `REVIEW.md`'s 92.6% archive accuracy and its
won/drawn/lost table predate this fix and have not been regenerated.

---

## 11. The clock is hoarded, not spent — **half fixed 2026-08-16, +78 Elo**

**There were two leaks, not one.** This entry described the allocation being too
small, which is real and still unfixed. It missed the larger half: the engine
did not spend even the allocation it made.

### The second leak, found 2026-08-16

Simulating this entry's own formula over a 900+10 game predicts 1 252 seconds
spent. The real game it was checked against spent **1 015**. The formula was not
the whole story, and the gap was measured directly — five positions at a 90s+1s
clock, comparing what `parseGo` allocated against wall time actually used:

```
position 1: allocated 3500 ms, used 1890 ms  ( 54%)
position 2: allocated 3500 ms, used 2016 ms  ( 58%)
position 3: allocated 3500 ms, used 2855 ms  ( 82%)
position 4: allocated 3500 ms, used 2811 ms  ( 80%)
position 5: allocated 3500 ms, used 3502 ms  (100%)
                                   overall     75%
```

The cause is in `search.cpp`, not `uci.cpp`. Iterative deepening refused to
*begin* an iteration unless the whole predicted iteration fitted in what was
left — a sound rule on its own terms, since a partial iteration is discarded and
its time buys nothing. But the prediction is 2.3× the last iteration, so the
rule abandons up to that much of every move's budget. One deadline was serving
two questions: "may I still be searching?" (wants the hard limit — an overrun is
a forfeit) and "should I begin another iteration?" (wants a smaller one).

### The fix

Split them. `SearchLimits.moveTimeMs` is the target that governs *beginning* an
iteration; `SearchLimits.hardTimeMs` — three times the target, still bounded by
the existing quarter-of-the-clock cap — governs abandoning one already running.
The search may now begin an iteration it is not certain to finish and keep the
result if it lands.

| | before | after |
|---|---|---|
| budget used | 72% | **167%** |
| mean depth over five positions | 8.8 | **10.0** |

**Gated at `--tc 30+0.33`, 200 games: +78 Elo, 95% CI [+40, +117]**, 61.0%,
**zero time forfeits**. The forfeit count is the half that was worth
distrusting: the change spends 1.67× more clock per move, and an overrun on a
clock costs a game rather than an interval. Simulation said it self-stabilises —
below about 35 seconds left the `remaining/4` cap makes it spend less than the
increment and the clock recovers — and 200 games agreed.

**`softtime` was on by default from 2026-08-16 and was turned back off on
2026-08-17, after it forfeited a rated game** — `Axiom_BOT vs Crimsy_Bot`,
[4OP39tbH](https://lichess.org/4OP39tbH), −11.

The gate is not wrong. The defect is that its result does not transfer to the
control the bot plays, and the cause is one line:

```cpp
hard = min(budget * 3, remaining / 4);
```

A *ratio* means different things at different clocks:

| control | remaining | budget | hard | overshoot permitted |
|---|---|---|---|---|
| `--tc 30+0.33` | 30 s | 1.2 s | 3.5 s | **2.3 s** |
| 900+10 | 898 s | 34.9 s | 104.8 s | **69.9 s** |

At the gated control a 3x overshoot is two seconds and 200 games found no
forfeits. At 900+10 it is seventy, and the engine takes it — **73 s on move one**
of that game, then 1.3x to 3.8x the target on every move after, until the clock
ran out. The engine never exceeded its stated hard limit; the hard limit was
absurd.

**The lesson is not that spending clock is dangerous.** It is that a parameter
expressed as a ratio was validated at one time control and shipped for another
thirty times longer. The 1.67x figure that a whole-game simulation rested on was
itself measured at a 90s+1s clock, so the simulation was answering a question
nobody had asked. "Gated at `--tc 30+0.33`, the bot plays 900+10" was written
down as an open item at the time it shipped, and shipping proceeded anyway.

**The repair, when it is re-gated:** bound the overshoot absolutely as well as
proportionally — `min(budget + increment, budget * 3, cap)`. One increment is
self-financing, since it arrives next move, and at short controls it collapses
to roughly today's behaviour. Re-gate **at a control resembling 900+10**, which
is the step that was skipped.

Verified on the shipped default: on the position that forfeited, the engine now
spends 0.72x, 0.64x and 0.94x of its budget at 898 s, 300 s and 26 s remaining.

**Why this survived every gate before it.** The defect is invisible to a node
budget by construction: `-N` pays both sides the same nodes, so an engine that
wastes *time* looks identical to one that does not. It needed `--tc`, which is
why building the instrument first was worth a day.

### The allocation itself: gated 2026-08-17, and it stays as it is

`timealloc` counts the moves down — `remaining / max(80 - moveNumber, 30) +
increment` — instead of dividing by a constant forever and banking half the
increment. **200 games at `--tc 30+0.33`: +14 Elo, 95% CI [-22, +50]**, zero
forfeits. The interval spans zero. It is kept as a toggle, off, exactly as
`seepruning` and `deltapruning` are.

**What the attempt found is worth more than the result.** Simulating it first
showed that once `softtime` is on, *every* allocation formula converges to the
same total — about 97% of the clock — because the `remaining/4` cap and the
increment dominate. There is no more time to extract. This entry's original
premise, that the allocation is too small, stopped being true the moment the
first half shipped.

What the formula still controls is *where* the time goes, and the first 900+10
game on the new build showed that is genuinely skewed:

| moves | seconds each |
|---|---|
| 1-10 | **44.2** (78 s on move 2) |
| 11-30 | 29.1 |
| 31-50 | 15.4 |
| 51-70 | **4.3** |

It spends the clock in the opening and plays the endgame at a second a move.
Flattening that is what `timealloc` does, and 200 games could not show it was
worth anything. Resolving +14 needs roughly four times the sample — about
twenty hours, since `--tc` cannot be sharded — which is the honest reason this
stops here rather than a claim that the distribution does not matter.

The original analysis of the allocation follows, and remains accurate about
*how* the formula behaves; it is only its conclusion — that fixing it is worth
Elo — that is now measured and unproven.

---

### Original entry (the allocation, still unfixed)

Noticed from watching games on 2026-08-15, and the impression was the opposite
of the fact: the bot looked like it was always about to lose on time. It is the
*opponents* who run low. Over the 19 games in the bot log with clock traces:

| | |
|---|---|
| our per-game **minimum** clock, median | **472 s** of a 900 s start |
| games where we fell below 60 s | **0** |
| games where the opponent did | 3, the worst at 10.9 s |
| our lowest ever | 252.7 s, and that was the outage in 7, not the engine |

**The cause is `parseGo` (`uci.cpp:154`).**

```cpp
int moves = (movestogo > 0) ? movestogo : 30;
long budget = remaining / moves + increment / 2;
```

Two independent leaks:

- **It divides whatever is left by 30 on every move, forever.** There is no
  estimate of moves remaining, so the allocation decays geometrically instead of
  being spent: 35 s on move 1 of 900+10, ~21 s once 472 s remain, less after
  that. Spending settles where `remaining/30 + inc/2 == inc`, i.e. at
  **`15 × increment`** — about 150 s for a 10 s increment. The clock converges to
  a floor rather than being used, which is precisely the 250-470 s floors in the
  games.
- **It banks half the increment.** With a 10 s increment, spending the whole
  10 s every move keeps the clock level indefinitely. `increment / 2` gives away
  5 s a move to buy nothing.

The result is a game finished with more than half the thinking time unused. On
this engine that is not a rounding error: every gate that paid did so by buying
*quality per node* (`checkext` +23.0 Elo at 9.2% more nodes), and unspent time
is exactly that, unbought.

### The instrument cannot see it, which is why it survived

This is the same shape as 8, and it is the reason this is filed rather than
fixed. `tests/match`'s `-t <ms>` is a budget **per move**, and
`UciEngine::bestMove` only ever sends `go nodes`, `go movetime` or `go depth`.
The in-process path takes the same `SearchLimits.moveTimeMs`. **Neither path has
ever sent `wtime`, `btime`, `winc` or `binc`**, so the clock branch of
`parseGo` — the code with the defect in it — is not reached by any test, gate or
smoke run in this repository. It only ever runs on Lichess.

So a fix cannot be gated as things stand: at a fixed per-move budget, time
management is *definitionally* invisible, because the harness has already made
the decision the manager exists to make.

### The instrument now exists — 2026-08-15

`./tests/match --tc <base>[+<inc>]`, in seconds. Each side gets a clock that
runs down, `UciEngine` sends `wtime/btime/winc/binc`, and a side that oversteps
loses the game on time. It refuses to combine with `-t`/`-N` (whichever bound
first would make the time manager's decision for it), refuses without
`--engineA/--engineB` (the in-process path takes a budget it is given and never
chooses), and is not shardable — `shard-gate.sh` already turns away anything
that is not node-limited, which covers it without a new rule.

Verified, in the order that matters:

- **The engine now reaches the branch.** `go wtime 30000 btime 30000 winc 1000
  binc 1000` produces a 602 ms search against the ~1 500 ms that formula
  allocates. Before this, nothing in the repository had ever sent those tokens.
- **A forfeit is actually detected**, which is the half worth distrusting: a
  test that has never failed is not known to work. Forced at `--tc 0.05+0`,
  where even the 10 ms floor plus process overhead outruns the clock, and both
  games ended `B wins time forfeit`. At a sane control nothing forfeits, which
  is why the pathological one had to be constructed on purpose.

So the fix is now gateable. **It is still not written**, deliberately. A time
manager that spends more is not obviously better — it could as easily walk into
the overrun the `remaining / 4` cap exists to prevent — and the whole reason to
build the instrument first was to stop this being decided by argument. Gate it
at a control resembling the one the bot actually plays; 900+10 is ~15 minutes a
side, so a meaningful match needs either a scaled-down control or a lot of
patience, and that trade-off is itself worth measuring before committing to a
long run.

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
