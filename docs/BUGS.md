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

### The stop that lands a game late — 2026-08-21

`bot-stop.sh --games 1` armed during a game stopped the bot **after the next
game**, not after the one on the board. Two things caused it, and both come
from the same place: with a bare SIGINT the only safe moment is a gap between
games, so the script could not signal the game it meant — it had to wait for
that game to end and then find a gap, and it counted PGN files in
`game_records/` to know when the game had ended.

Counting records is a proxy for "games finished" and it slips both ways:

- **no record is written for a game the bot was killed out of** — `nFYSG2BI`
  (2026-08-17) is in the logs and has no file, so a stop armed during it would
  have waited through the following game;
- **a game the bot reconnects to has its record written twice** — `kvCboOh4`
  logged `Game over` at 22:41 and again at 23:00 on 2026-08-20, with the file
  written at the first one, so the count had already moved before the game
  you meant had finished.

The fix is not a better proxy. `lichess-bot` has the mechanism this needs:
**`quit_after_all_games_finish: true`**, which makes the first SIGINT mean
"play this game out, accept nothing new, then exit" (`close_pool` joins the
game process; the main loop stops handling events). It is now set in
`lichess/config.yml`, and `bot-stop.sh` signals **during** the last game rather
than hunting for a gap after it. A second SIGINT is still force-quit, so the
script never sends one on that path.

Two traps come with it. The option is read at startup, so **editing
`config.yml` does not reach a running bot** — `bot-stop.sh --status` reports
`inexact` when the file was edited after the process started, and falls back to
the old gap-hunting behaviour rather than signalling into a game that will not
survive it. And per the lesson above, the option is read behaviour, not tested
behaviour: after a graceful signal into a live game, the script waits ten
seconds and checks that the **engine is still there**. That direction of the
check is sound — presence cannot be manufactured by the signal — and if the
engine is gone with no game record, it says so loudly and tells you to restart
the bot immediately to reconnect.

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

**`softtime` is on by default from 2026-08-20**, after a second gate at a
control shaped like the one the bot plays: **+42 Elo, 95% CI [+6, +79]** over
200 games at `--tc 120+1.33`, **zero time forfeits**.

It shipped once before, on 2026-08-16, and was reverted the next day after
forfeiting a rated game — `Axiom_BOT vs Crimsy_Bot`,
[4OP39tbH](https://lichess.org/4OP39tbH), −11. That failure is worth keeping
because the gate was not wrong; the *parameter* was.

The hard bound was a ratio:

```cpp
hard = min(budget * 3, remaining / 4);
```

| control | remaining | budget | hard | overshoot permitted |
|---|---|---|---|---|
| `--tc 30+0.33` | 30 s | 1.2 s | 3.5 s | **2.3 s** |
| 900+10 | 898 s | 34.9 s | 104.8 s | **69.9 s** |

At the control it was gated on, three times the budget is two seconds and 200
games found no forfeits. At 900+10 the same expression permits seventy, and the
engine took them — **73 s on move one**, then 1.3× to 3.8× the target every move
until the clock ran out. It never exceeded its stated hard limit. The hard limit
was absurd.

**The bound is now absolute as well as proportional:**

```cpp
hard = min(budget + increment, budget * 3, cap);
```

One increment is the bound that travels between time controls, because
overshooting by it is self-financing — the increment arrives on the next move,
so a move that runs one increment long costs the clock nothing across the game.
The multiple survives only for the zero-increment case, where `budget + 0` would
collapse the split to nothing.

**It costs about half the gain — +78 down to +42 — and that is the right
trade.** The engine now uses less of the extra time than it did when free to
overshoot by seventy seconds. A forfeit is a whole game.

Two things were done differently the second time, and both were the point:

- **The bound was verified at 900+10 clocks before any gate ran.** On the
  position from the forfeited game the engine spends 44.8 s against a 44.9 s
  bound, where the old code took 73 s. Simulated to 140 moves it settles at 40 s
  remaining and never flags.
- **The gate ran at `--tc 120+1.33`**, the same 90:1 shape as 900+10 rather than
  a thirtieth of it, so a failure that only appears at long time controls had
  somewhere to appear. It did not.

**The general lesson, which is not about clocks.** A parameter expressed as a
ratio was validated at one time control and shipped for another thirty times
longer, and the whole-game simulation that said it was safe was fed a 1.67×
constant measured at a 90s+1s clock. "Gated at `--tc 30+0.33`, the bot plays
900+10" was written down as an open item at the time it shipped, and it shipped
anyway. A ratio is not a bound; it is a bound only once something fixes its
scale.

**One caveat that a run of this length cannot settle.** A `--tc` gate measures
wall time, so a machine that suspends corrupts it — the first attempt at this
run was abandoned six games in for exactly that reason, and under WSL2 the only
symptom was the elapsed figure looking impossible. The accepted run went 17 hours
without a gap.

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

## 12. Operational: a restart *during* a game rate-limits itself into a loop

**2026-08-21, `YV7XiTWZ` vs `TomokoNN` (2028), rated 900+10.** The bot played
move 12 normally at 08:46:26. Two seconds later it re-entered
`start_lichess_bot()` — its own restart cycle, in the same process (pid
unchanged) — and logged `Welcome Crimsy_Bot!` again. On the way back in it
re-opened the game stream for a game it was already in:

```
08:46:31  GET /api/bot/game/stream/YV7XiTWZ  ->  429 Too Many Requests
08:46:31  Giving up play_game(...) after 1 tries
```

`play_game` gives up on the 429 and is retried immediately, with no backoff.
Each retry rejoins the game, replays whatever move list it managed to read, and
posts a move that was already played — `a2e6` and `f8e8`, both **400 Bad
Request** — then logs `Game over` and starts again, spawning an engine process
per iteration. **From 08:46:31 until the recovery at 08:57:10 it never made a
move** — ten and a half minutes of wall clock, of which about five came off our
own clock, which runs only on our turn: 8.5 minutes down to 3.3.

Three things are worth keeping from it.

**The log is not the state; Lichess is.** The log says `Game over` twenty-one
times for a game that was still running, and the bot was replaying move 16
while the real game was at move 18. The authoritative reading needs no
guessing:

```bash
curl -s -H "Authorization: Bearer $LICHESS_BOT_TOKEN" \
     "https://lichess.org/api/account/playing?nb=5"
# gameId, isMyTurn, lastMove, fen, secondsLeft
```

That is the same lesson as 7 — the game's state on Lichess is the one thing no
local process can fake — with a query that answers it directly.

**A second SIGINT is not a reliable kill once
`quit_after_all_games_finish: true` is set.** Two, four seconds apart, did not
bring it down within seven seconds: the main loop had already handed off to
`close_pool`'s `pool.join()`, waiting on a game process that kept restarting
itself. `SIGTERM` ended it. `bot-stop.sh` deliberately never sends a second
signal on the graceful path, so a bot stuck like this is a manual `SIGTERM` and
should be — the second signal exists to abandon a game, and abandoning is the
thing being avoided.

**Do not restart while a game is live.** Re-opening a stream Lichess has just
served is what draws the 429, and the recovery path replays a stale board into
it. The restart afterwards was itself rate-limited on `/api/stream/event`
(`retry in 15 seconds`) before it got in at 08:55:44, rejoined, and played
`18...Bg4` at 08:57:10 with a 200. **`bot-stop.sh` exists so that a stop, and
therefore a restart, lands between games** — this is the cost of one that does
not.

The engine is not at fault: every move it returned was legal in the position it
was handed. The game was won, 0-1, `Termination "Normal"`, +3 — which is worth
saying plainly, because a nine-minute stall that ends in a win is exactly the
kind of incident that gets remembered as harmless.

---

## 13. The evaluation cannot see compensation — reproducible, 2026-08-21

`Crimsy_Bot vs agentc313 (2314)`, [gtB9qan7](https://lichess.org/gtB9qan7), rated
900+10. Position after 16...Rb6:

```
2q2rk1/p2b1ppp/1r6/3Q4/1b1pP3/3B4/PPP2PPP/R1BK3R w - - 5 17
```

The engine played **17.Qxd4**, winning a pawn. It is now **+3 in material** and
scores itself **+1.65** (its own eval, depth 10, in the game). Stockfish 16 at
depth 16 scores the same position **−3.09** — a gap of **4.7 pawns** with no
tactic in it. Nothing hangs; no material changes hands for the next fifteen
moves. What Black has is compensation: the bishop pair raking the position,
`17...Rd8` onto the file White's own king stands on, and a White king stuck on
d1 with the rooks still on a1 and h1 and no castling rights. The evaluation
scores all of that as zero, so it takes the pawn and calls the position good.

**An earlier version of this entry claimed this position reproduces on demand,
and that was wrong** — see 14. The command it recommended truncated the search,
so the answer it printed came from about one ply of work. Searched properly the
engine plays `Bf4` at depth 6, `h3` at 10 and `f3` at 14: it does **not** insist
on `Qxd4`. What it does insist on is the assessment. At depth 14 it scores this
position **+151** where Stockfish scores it **−309**, so the 4.6-pawn
disagreement is real and measured; only the "one reproducible blunder" framing
was an artifact of a broken instrument.

It is the same family as the `19.Qd4` blunder in 7: the queen goes pawn-hunting
onto its own king's file. That entry read it as a one-off. It is not.

### What five losses to 2100+ opposition look like — 2026-08-21

14 rated games that day, **9W-0D-5L**, 2189 → 2177. Every loss was to 2100+; the
record against **2200+ was 0-0-4** (PlayMarius 2239, Bongaclang 2404, DeepBecky
2394, agentc313 2314). Reviewed at Stockfish depth 16, over the bot's own moves:

| opponent band | games | accuracy | mean CPL | blunders | mistakes | errors/100 moves |
|---|---|---|---|---|---|---|
| 2200+ | 4 | 95.5% | 24.5 | 3 | 7 | **3.3** |
| 2000–2200 | 2 | 95.3% | 16.6 | 0 | 2 | 1.2 |
| under 2000 | 8 | 96.8% | 11.7 | **0** | **0** | **0.0** |

Zero errors in 268 moves below 2000. Restricting to positions still in the
balance (|eval| ≤ 1.00), so that "already winning" cannot flatter the easy
games, the same shape holds: **12.9** errors per 100 moves against 2200+ (4 in
31), 1.2 against 2000–2200 (1 in 80), 0 below 2000 (0 in 59). The 31 is small,
and small *because* those games left the balance early — read it as direction,
not as a rate.

**This corrects the 2026-08-16 finding** that the losses to 2300+ contained
"zero blunders between them — outplayed, not caught out". On this build, at
900+10, they contain three blunders and eight mistakes. None were time
pressure: each error had 21–43s of thought behind it and 150–770s left on the
clock.

### Two explanations that were tested and are wrong

**Not partial iterations, and mostly not phantoms either.** Re-run with a real
UCI client (the first attempt used the broken command in 14, and its answer was
wrong in both directions):

| position | played | our depth 10 | our depth 14 |
|---|---|---|---|
| `ZlTEweWc` 20 | `Nxd8` (−222) | **`Nxd8`** | **`Nxd8`** |
| `ZlTEweWc` 22 | `Bg1` (−382) | **`Bg1`** | `Ne6+` |
| `gtB9qan7` 17 | `Qxd4` (−334) | `f3` | `a3` |
| `4a75bE3F` 28 | `Rb6` (−257) | **`Rb6`** | `Rf7` |
| `J5MmngQ7` 5 | `Ng5` (−172) | `Be7` | `Be7` |

**Three of the five reproduce at depth 10**, which is the depth the engine
actually reaches in a rated game — its mean over these games was 12.3 plies and
it reported depth 10 and 11 on two of these very moves. Two of those three are
cured by depth 14. One, `Nxd8`, survives it.

That splits the failure into three kinds, and they want different fixes:

- **`Nxd8` — reproducible at every depth tried**, and it holds to depth 16.
  This is the reproducible lead the entry originally claimed `Qxd4` was, and
  unlike that one it survives being measured properly:

  ```
  r2r4/pN3pkp/Qb6/3qn1p1/3Pn3/4BP2/PP2P1PP/R3KB1R w KQ - 3 20
  played Nxd8 (takes a rook), Stockfish prefers fxe4, cost 222cp
  our search: d12 Nxd8 +597   d16 Nxd8 +444      static eval +381
  Stockfish:                            −149
  ```

  White grabs the rook on d8 while his king sits on e1 and Black's queen on d5,
  knights on e4 and e5 and bishop on b6 all bear on it. The engine scores that
  **+3.81 static, +4.44 at depth 16**, where the truth is −1.49. Six pawns of
  attack, priced at nothing.

  **And the king-exposure term added on 2026-08-21 does not fire here** — 381
  with it off, 381 with it on at full scale. Its "stranded" condition wants the
  castling rights already gone, and White still has `KQ`; its open-file
  condition wants a file at the king with no pawn of ours, and d4, e2 and f3
  are all occupied. The three facts that term charges are not the three facts
  that matter in the position the whole entry is about. What matters here is
  enemy pieces bearing on the king's neighbourhood — which is exactly the
  attacker-count term `ROADMAP.md` 6.4 built and rejected, on an instrument
  (self-play, no external opponent, no evaluation corpus) that could not have
  seen it work.
- **`Bg1` and `Rb6` — depth-limited.** The move the engine played is the move
  it prefers at the depth it had; four more plies rejects it. These are bought
  with speed, not with evaluation terms.
- **`Qxd4` and `Ng5` — not reproducible at fixed depth.** Something in the live
  path (transposition state carried across moves is the remaining candidate)
  chose a move that a clean search at either depth does not.

**A fixed-depth or fixed-node gate can see the first three**, which corrects
the claim this entry used to make in the other direction. The obvious suspect
for the third kind was iterative deepening returning a move from an iteration
the clock cut short. **It does not**: `search.cpp:825`
updates `bestMove` only when `completedDepth && !searchAborted(shouldStop)`,
and otherwise keeps the previous depth's move and breaks. What is left is that
those choices depended on the exact transposition-table state the game had
built, which changes the move at the same nominal depth and cannot be
reconstructed without replaying load-dependent timed searches. The practical
consequence is narrower than this entry first claimed: a fixed-node gate cannot
see the two that do not reproduce, but it can see the three that do.

**Not systematic overconfidence.** In the Bongaclang loss the engine scored
itself **+6.92** at move 21 and **+3.20** at move 22 while Stockfish had it at
−1.69 and −5.60, with its king walking (Kd1, Kc2) into knights (Nc4, Nxb2+) —
the king-safety hole doing exactly what the file says it should. But across all
14 games the bias does not generalise:

| band | moves | mean signed error | mean absolute error |
|---|---|---|---|
| 2200+ | 263 | +21 | **224** |
| 2000–2200 | 130 | +19 | 127 |
| under 2000 | 233 | **+115** | 171 |

The evaluation is *less accurate* against strong opponents, not systematically
optimistic about them — it is most optimistic against weak ones, where being
five pawns up makes the disagreement harmless. Quote the absolute column, not
the signed one.

### Why this matters for the king-safety question

`ROADMAP.md` 6.4 closed king safety as negative over four gates, and recorded
the one thing those gates could not rule out: **self-play may be unable to see
the term at all, because both sides share this engine's disinclination to
attack**, and testing it needs a gauntlet against a stronger attacking opponent
the harness cannot run. That gauntlet now exists — 2200+ bots on Lichess, since
`opponent_max_rating` was raised to 2500 on 2026-08-21 — and the first four
games in it went 0-4 with one textbook king hunt among them. That is not proof
the term belongs in the evaluation. It is the first evidence from a source
self-play cannot provide.

---

## 14. Piping a `go` command into the engine truncates the search — **FIXED 2026-08-23**

```bash
printf 'uci\nisready\nposition startpos\ngo depth 8\n' | ./chessbot --uci | tail -1
# bestmove a2a3
```

`a2a3` is not what this engine plays at depth 8. It is what it plays after
about one ply, because `printf | engine` closes stdin the instant the `go` line
is written: the UCI loop reads EOF, tears the process down, and the search
thread reports whatever it had. Hold stdin open and the same command searches
properly:

```bash
{ printf 'uci\nisready\nposition startpos\ngo depth 8\n'; sleep 6; } | ./chessbot --uci | grep '^info'
# info depth 8 score cp 31 ... pv b1c3
```

**It fails silently and it fails plausibly.** There is no error, no warning, and
the move that comes back is legal and often reasonable-looking — so a sweep
across `go depth 6` through `go depth 16` returns a column of answers that look
like a depth sweep and are all the same one-ply guess. That is exactly how it
got into 13: the claim that one blunder "reproduces at every depth" was five
truncated searches agreeing with each other, and it was published here before
anyone noticed that a depth-8 search does not open with a2a3.

Use a real UCI client for anything that matters. `python-chess` is already in
the tree's orbit (`lichess-bot`'s venv, and `tools/review` drives Stockfish the
same way):

```python
import chess, chess.engine
eng = chess.engine.SimpleEngine.popen_uci("./chessbot-uci.sh")
info = eng.analyse(board, chess.engine.Limit(depth=14))   # info["pv"], info["score"]
eng.quit()
```

The general form is 9's, which this file has now made twice: **a command that
was never run is a guess, and a command whose output was never sanity-checked
is the same guess with evidence-shaped decoration.** `a2a3` was on screen the
whole time.

### Fixed 2026-08-23 — the engine no longer abandons the search on EOF

`uciLoop`'s `while (std::getline(std::cin, line))` fell through to
`stopSearch()`, which sets `g_stop` and joins — so stdin closing killed a live
search and reported whatever it held. It now distinguishes the two ways out of
that loop: an explicit `quit` still stops immediately, while **EOF lets a
bounded search finish and report what it actually found.**

`go infinite` keeps the old behaviour and must, since by definition nothing
else would ever end it — a pipe would hang forever. That is why the flag
`g_searchUnbounded` exists rather than a plain join.

Verified against the reproduction at the top of this entry:

```
printf 'uci\nisready\nposition startpos\ngo depth 8\n' | ./chessbot --uci | tail -1
bestmove b1c3      # was a2a3; matches the held-open control exactly
```

and `go infinite` piped still exits rather than hanging. Bench signature
unchanged at 793,823, all eleven tests pass including `uci_smoke.py`.

**The advice above still stands.** A real UCI client is still the right tool,
because holding stdin open is not the only way an ad-hoc harness can lie. What
changed is that the most common shortcut no longer fails *silently*.

---

## 15. Operational: the host's network drops mid-game, and that is every forfeit — 2026-08-23

**All four time-forfeit losses this account has ever taken are this, and none of
them are the engine, the clock, or a restart.** The pattern was found on
2026-08-23 after two forfeits in one day, and it reclassifies the earlier two.

| game | date | opponent | connection errors in that game's log |
|---|---|---|---|
| [`4OP39tbH`](https://lichess.org/4OP39tbH) | 08-17 | 1986 | **93** |
| [`gNn9H2iE`](https://lichess.org/gNn9H2iE) | 08-21 | 1822 | **84** |
| [`7kgNwYF5`](https://lichess.org/7kgNwYF5) | 08-23 | 2159 | **24** |
| [`GlZN6Jnv`](https://lichess.org/GlZN6Jnv) | 08-23 | 2225 | **34 in seven minutes** |

The failure is local, and the log names it:

```
Failed to resolve 'lichess.org' ([Errno -3] Temporary failure in name resolution)
Failed to establish a new connection: [Errno 101] Network is unreachable
```

**`GlZN6Jnv` is the clean specimen.** The engine returned `b3c2` and the bot
tried to POST it at 16:43:57 local. Every attempt failed; `backoff` gave up
after eight tries at 16:44:55, the game process restarted, recomputed the same
move, and gave up again — three times, at 16:44:47, 16:46:21 and 16:47:56.
Our clock ran the whole time, because it runs on our turn: **202s → 121s →
flag**. Nothing was wrong with the move. It never left the machine.

**This is not 12.** 12 is a restart re-opening a stream and being 429'd; the
cure there is to stop between games. Today's process had not been restarted —
it came up at 13:58 and the game began at 16:18 — and Lichess never answered at
all, which is the opposite of rate limiting. Attributing a forfeit to a restart
because restarts happened nearby is exactly the mistake that was made on 08-21
and again in `MEASUREMENTS.md` earlier on 08-23, both times corrected by
counting connection errors instead of coincidences.

**And it is not 11.** `7kgNwYF5` looked like the time manager giving out in a
62-move game — the clock decayed 898s → 61s and it flagged. But the decay was
ordinary play, and the last event in the log is `f4f5` failing to post eight
times. Read the clock trace and you get the wrong answer; read the failures and
you get the right one.

### The scale of it

Over the ~30 hours to 2026-08-23 20:09 local, **twelve distinct outages**:

```
08-22 22:27 -> 22:31   3.6 min
08-23 16:43 -> 16:50   6.3 min
```

plus ten shorter than 90 seconds. **937 `Network is unreachable` against 413
`NameResolutionError`** — so this is the interface going away, not merely DNS,
and a static `resolv.conf` would not have saved either game. `/etc/resolv.conf`
is a symlink to `/mnt/wsl/resolv.conf`, regenerated by WSL, last written 20:04
on 08-23 — inside one of the outage windows.

A 900+10 game survives a 30-second outage. It does not survive six minutes with
202s on the clock, and no amount of engine work changes that.

### What would actually help, in order

1. **Fix the host network — applied 2026-08-23, pending a restart.**
   `networkingMode=mirrored` is now in `C:\Users\dheir_ii8c\.wslconfig`
   (previous file kept as `.wslconfig.bak`), which drops the NAT and shares the
   Windows host's interfaces directly. **`.wslconfig` is read only when the WSL
   VM boots, so this does nothing until `wsl --shutdown` from PowerShell and a
   fresh start.** Verify afterwards with `ip route` — a mirrored VM no longer
   routes through `10.255.255.254` — and by watching for a week of bot logs
   with no `NameResolutionError`. If drops continue, the next lever is
   `dnsTunneling=true` in the same file, deliberately left off for now so that
   one variable moves at a time.
2. **Make the move POST retry for the length of the game rather than eight
   tries.** In `GlZN6Jnv` the bot spent its remaining clock recomputing a move
   it had already found. Persistent retry would not have saved that game — the
   outage outlasted the clock — but it would have saved `7kgNwYF5`, where the
   outage was 48 seconds.
3. **Do not read a forfeit as a chess problem** until the game's log has been
   grepped for `ConnectionError`. Three of these four were misattributed at
   first, twice to a restart and once to the time manager.

**Do not spend engine work on this.** The rating cost is real — four forfeits,
and probably some of the six unfinished records that Lichess scored as losses
(`MEASUREMENTS.md`) — but every one of them is a network event on this box.

---

## 16. The machine starved the engine for twenty-one hours — 2026-08-24

**The engine ran at roughly a third of its speed from 2026-08-22 19:00 to
2026-08-23 16:00, and nothing in the repo noticed.** Found on 08-24 while
analysing play, from the bot's own `info` lines in `lichess_bot_auto_logs/` —
median nps over searches longer than two seconds:

| day | median knps |
|---|---|
| 08-16 | 875 |
| 08-17 | 792 |
| 08-18 | 878 |
| 08-20 | 745 |
| 08-21 | 848 |
| 08-22 | 692 |
| **08-23** | **257** |
| 08-24 | 813 |

Hourly, the floor is 139-182 knps for most of that window against a normal
band of 700-880. The recovery lands exactly on the `wsl --shutdown` that
deployed `networkingMode=mirrored` (15), which makes a stale WSL VM the leading
suspect and *not* a demonstrated cause. **A restart curing it is a workaround,
not a diagnosis.**

### Why this outranks the search queue

At this project's own doubling-is-70-Elo rule of thumb, a 3x slowdown is on the
order of **100 Elo**, sustained, for a day. `razoring` — the largest search win
ever gated here — is +39.1. **A recurrence costs more than anything on the
queue is worth**, so the cause is worth finding before the next feature.

### What it invalidates

**The `razoring` + `revfutility` field test.** Those 34 games were recorded in
`MEASUREMENTS.md`'s fifth reading as −1.45σ against band expectation, read as
an excursion regressing toward noise. They were largely played inside this
window, at a third speed, alongside the two network forfeits of 15. That is not
a noisy measurement of the pruning; it is a measurement of a crippled machine.
The pruning is not implicated and is not exonerated — **there is no valid field
reading of it yet.**

This is the third time in two days that a machine fault has been read as a
chess result: twice as a forfeit misattributed to the clock and to a restart
(15), and now once as a strength deficit. The pattern is worth naming.
**Environmental faults are silent, and they arrive wearing the costume of the
thing you were measuring.**

### The check that would have caught it — now exists, 2026-08-24

Nothing in the suite watched nps in live games. `tests/bench` measures speed on
demand and passed throughout, because it is run on a quiet machine when someone
thinks to run it.

`tools/nps-health.py` is the missing instrument: median nps per day out of the
bot's own logs, with the baseline computed as the median of the daily medians
so it adapts to the hardware instead of hard-coding a number that will rot.

```
  2026-08-22  n= 2840    691.9
  2026-08-23  n= 3028    257.5  <-- DEGRADED
  2026-08-24  n= 3429    796.2
```

`--quiet` prints one line and exits non-zero when the latest day is below 60%
of baseline, and **`lichess/bot-stop.sh --status` now prints that line** — the
one screen anybody looks at before touching the bot. A tool nobody runs would
have failed exactly the way the missing check did.

**Read it before trusting any reading taken from real games.** A band table
does not know how fast the engine was when it played.

---

## 17. Gates measure the engine with an eighth of the table it plays with — 2026-08-25

`tests/match.cpp:762` is `TranspositionTable ttA(32), ttB(32)` — **32 MB per
side, hardcoded, with no flag to override it.** `lichess/config.yml` sets
`Hash: 256`. So every gate this project has run measured the engine at **839k
slots** while rated games are played at **6 710k**.

Measured on the shipped build, `go depth 10`, five middlegame positions:

| position | 32 MB (gate) | 256 MB (play) | difference |
|---|---|---|---|
| 1 | 5 889 704 | 4 650 136 | **−21.0%** |
| 2 | 3 205 337 | 3 114 473 | −2.8% |
| 3 | 5 289 041 | 5 151 710 | −2.6% |
| 4 | 6 790 668 | 6 451 322 | −5.0% |
| 5 | 2 635 070 | 2 563 271 | −2.7% |
| **total** | **23 809 820** | **21 930 912** | **−7.9%** |

**This does not invalidate any past gate.** Both sides get 32 MB, so every
comparison was fair, and a fair comparison is what a gate is for. What it means
is narrower and still worth knowing: the engine being compared is about 8% less
node-efficient than the one that plays, and the two are in *qualitatively*
different regimes rather than merely different sizes.

### The regime difference, which is the real point

- **In a gate**, `-N 100000` means one search is 100k nodes against 839k slots.
  A single search fits in the table with room to spare; pressure builds only
  across the moves of a game, which is what `TtAging` handles.
- **In play**, the median search is **9.9M nodes against 6.7M slots**
  (`MEASUREMENTS.md`, 2026-08-24). A single search overflows the table one and
  a half times over.

**Gates never exercise the regime where one search thrashes the table**, and
that is the regime the bot lives in.

### Where this could bite, stated as a hypothesis and not a result

Anything that reduces the number of distinct positions searched also reduces
table pressure — which is the entire pruning family, and the family this
project has been shipping: `seeordering` +25.6, `checkext` +23.0, `razoring`
+39.1, `revfutility` +18.4. Under gate conditions the table is comparatively
roomy, so relieving pressure is worth *less* there than in play; under play
conditions it could be worth more. The sign of the error is not obvious and
**none of it has been measured** — it is a reason to be curious, not a reason
to doubt those numbers.

The concrete near-miss that found this: shrinking `TTEntry` to 16 bytes is
worth ~1.4% of nodes at gate size and ~0 at play size. Gated the normal way it
would likely have measured as a small positive that does not exist in real
games, and nothing in the methodology would have caught it. See branch
`tt-16byte`, parked for that reason.

### What to do about it

Not obvious, which is why this is written down rather than fixed.

Raising the gate to 256 MB is the intuitive answer and does not fit: 14 shards
× 2 sides × 256 MB is **7 GB against WSL's 8 GB** (`.wslconfig` `memory=8GB`).
It would mean trading shard count for realism — fewer, slower gates — and shard
count is what makes a gate affordable at all.

The cheap first step is to make the size **explicit and settable** rather than
buried as a literal: a `--hash` flag on `tests/match`, defaulting to today's 32
so no past result changes meaning. Then a change suspected of interacting with
table pressure can be gated at both sizes and the difference looked at, instead
of the question being invisible.

---

## 18. The bench signature measures depth 6; the engine plays at depth 10 — 2026-08-25

**A change's node cost at depth 6 does not predict its node cost where the bot
lives, and can understate it eightfold.** Found by measuring the Texel-tuned
evaluation (accepted at **+25.2 Elo [+15.7, +34.7]** on equal nodes, `GATES.md`)
against the shipped one at each depth, same position, node counts from the
engine's own `info` lines:

| depth | shipped | tuned | extra | by doubling-is-70-Elo |
|---|---|---|---|---|
| **6** — what bench measures | 170 939 | 182 987 | **+7.0%** | ~7 Elo |
| 7 | 313 828 | 357 908 | +14.0% | ~13 |
| 8 | 709 301 | 852 225 | +20.1% | ~19 |
| 9 | 1 383 284 | 1 835 667 | +32.7% | ~29 |
| **10** — where the bot plays | 3 114 473 | 4 962 280 | **+59.3%** | **~47 Elo** |
| 11 | 7 693 747 | 10 122 730 | +31.6% | ~28 |

Confirmed across three middlegame positions at depth 10: **+59.3%, +45.9%,
+53.8%**. And `nps` is unchanged between the two builds (680k vs 661k), which is
what a constants-only change should do — the cost is entirely in nodes-to-depth,
not in the price of a node.

### Why this cost a revert

The tune was gated, merged and deployed on the strength of +25.2 per node and a
bench figure of +15.1% read at depth 6, which was reasoned about as "roughly
−14 Elo on the clock". At depth 10 the same change is plausibly **−28 to −47**,
which could make it a net loss in real games. It was live on the Lichess bot for
about two hours before this was measured.

**Nothing about the gate was wrong.** +25.2 is a true statement about quality per
node. What was wrong was inferring the price from a depth the engine never
plays at.

### What to do about it

- **Never price a node cost from bench alone.** Bench's depth 6 is a *signature*
  — its job is to prove the tree did not change, and it is excellent at that.
  It is not a cost model.
- **Measure at play depth.** Run both binaries at `go depth 10` on a few
  middlegame positions and compare node counts. It takes under a minute and it
  is the number that matters.
- The general form is 17's, and this file has now made it twice in a week: the
  instrument and the thing being measured were in different regimes. There the
  table was 32 MB in gates and 256 MB in play; here the depth is 6 in bench and
  10-12 in games. **Check what regime your instrument is in before believing
  what it says about the engine that plays.**

The tuned weights are kept on branch `eval-texel-tune` and are not lost. What
they need is a `--tc` gate, which is now worth its cost: the question is no
longer an 11-Elo difference that ±36 could not resolve, but a possible swing of
forty or more, which it can.

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
- **King safety counts no attackers at all** — a queen, rook and knight around
  the king score what an empty board does. This is a real and still-present
  defect, and it is filed here rather than above because **fixing it was
  measured and does not win games**: four gates over 10 080 games returned
  +1.3, +2.2, −11.0 and −216.9. `ROADMAP.md` 6.4 is the write-up and
  `evaluation.cpp` carries the numbers beside the absence. Anyone scanning this
  file for evaluation defects should find it here and then not build it again.
  Note the one thing the gates could not rule out: self-play may be unable to
  see a king-safety term at all, because both sides share this engine's
  disinclination to attack. Testing that needs a gauntlet against a stronger
  attacking opponent, which the harness cannot yet run.
- **The four losses to 2300+ opposition were at 600+5, not 900+10.** They are
  the whole of the "0-0-3 against 2300+" ceiling this project has quoted, they
  were all played on 2026-08-12 while the provisional rating was still 3000, and
  they were at a third less time per move than the bot normally plays. The
  ceiling is therefore measured at a control the engine does not play, on a
  build four generations old. Not a defect — but not the evidence it reads as
  either. **Superseded 2026-08-21**: the current build has now played 2200+
  opposition at 900+10 and went 0-4, with three blunders and eight mistakes
  across the five losses to 2100+ — see 13. The ceiling is real; it is the old
  evidence for it that was thin.
