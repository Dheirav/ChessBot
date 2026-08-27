# Handoff — 2026-08-27

Current state, what is in flight, and what to pick up. This is the file to read
first; it is meant to be rewritten as state changes, unlike `BACKLOG.md`, which
is a frozen archive of the 2026-08-10 profiling session.

**One number, one home.** A result is written once, in the place that cannot
drift from the code, and everywhere else links to it. Verdicts on toggles live
on the toggle in `search.hpp` or `evaluation.cpp`; pooled gate results live in
`GATES.md`; external readings live in `MEASUREMENTS.md`. This rule was written
on 2026-08-22 after an audit found one verdict restated in ten files, four of
twelve queue rows pointing at finished work, and a stale line here that sent a
session off to redo a gate five days after it had run. **The toggle comments
were right when three documents were wrong** — that is the pattern the rule
generalises.

| document | what it is | current? |
|---|---|---|
| `HANDOFF.md` | this file — where things stand | **yes, keep it that way** |
| `TODO.md` | every open item, with what blocks it and what it costs | **yes, keep it that way** |
| `README.md` | features, UCI, build, controls | yes |
| `src/README.md` | architecture | yes |
| `tests/README.md` | test suite and gate methodology | yes |
| `BUGS.md` | known defects, ordered by what fixing them is worth | yes |
| `ROADMAP.md` | what to do next and why — the layer above `PLAN.md` | yes |
| `REVIEW.md` | the game-review tool, and what it is honestly good for | yes |
| `PLAN.md` | phased plan, with status lines per item | mostly — status lines are current |
| `GATES.md` | every pooled gate result, append-only | **yes — the only home for these numbers** |
| `MEASUREMENTS.md` | every external strength reading, append-only | **yes — the only home for these** |
| `BACKLOG.md` | 2026-08-10 findings | **archive; §2.1 is wrong, §4 and §7 still good** |

---

## Where the engine is

Phases 0, 1 and 2 are complete, with both Phase 1 gates cleared: **+246 Elo**
for the depth raise and **+140 Elo** for the search heuristics at equal time.
The engine plays to a clock, speaks UCI, and has eleven tests in CI.

Phase 3 is in progress. What is wired in:

| feature | default | status |
|---|---|---|
| null-move pruning, LMR, aspiration windows | **on** | accepted by gate 1.5, +140 Elo |
| TT aging | **on** | **accepted**, +11.5 Elo [+3.2, +19.8] |
| `seeordering` | **on** | **accepted**, +25.6 Elo [+16.1, +35.2] — on by default since 2026-08-13 |
| `seepruning` | off | **resolved, stays off** — +2.2 [−7.2, +11.6] at equal nodes, +4 [−7, +14] on the clock |
| `qbound` | **on** | quiescence capped 8 plies past the horizon; −16.8% nodes, no best move changed. Ungated repair |
| `checkext` | **on** | **accepted**, +23.0 Elo [+13.3, +32.7] — on by default since 2026-08-14 |
| `lmp` | **on** | **accepted 2026-08-26, +13.1 Elo [+3.5, +22.8]** over 3 360 games. Cuts 45.0% of nodes at bench 6. Endgames were checked rather than assumed: no move changed over twelve endgame positions at depth 12, and pure pawn endings are bit-identical because the `hasNonPawnMaterial` guard exempts them |
| `razoring` | **on** | **accepted 2026-08-22, +39.1 Elo [+28.4, +49.9]** over 2 400 games. Margin 500cp, sized off the evaluation's measured error rather than textbook 100–150 — 3.1 lost 50 Elo making the same bet inside the evaluation's own noise |
| `revfutility` | **on** | **accepted 2026-08-22, +18.4 Elo [+7.8, +29.1]** measured *on top of* `razoring`, +12.3 alone. Margin 300cp per ply |
| `deltapruning` | off | −50.0 at 200cp; **closed 2026-08-21 at +0.9 [−5.8, +7.7]** on the current build (the older +7.1 was a pre-6.2 engine) |
| `softtime` | **on** | **accepted, +42 Elo [+6, +79]**, 200 games at `--tc 120+1.33`, **zero forfeits** — on by default since 2026-08-20. Shipped once before at +78 from a 30-second control and reverted after a rated-game forfeit; the hard bound is now absolute (`budget + increment`) rather than a ratio. `BUGS.md` 11 |
| `iid` | off | **gated, stays off** — −0.1 Elo [−4.9, +4.7] over 3 360 games. The tightest null here: iterative deepening already fills the table at the depths IID fires at |

All three gates ran 2026-08-13 at 14 shards × 120 pairs (3 360 games each,
`-N 100000`). TT aging defaults on because it is a repair, not a feature — the
table was depth-preferred and ageless, so positions from moves already played
could only be displaced by something deeper still, and the live search got a
shrinking share of the table. The gate has now confirmed that default rather
than merely assuming it.

**Phase 4's evaluation fixes are gated and stay in.** 3 360 games at
`-N 100000`, two binaries over UCI: **+6.1 Elo, 95% CI [−3.9, +16.1]**. The
interval spans zero, so this does not demonstrate a gain — but it was never
going to be reverted on a bad number, and what it does rule out is a *loss*:
the corrected evaluation is at least as good per node as the broken one, and
probably slightly better. Note carefully what it cannot say. The fixes cost
+20.1% nodes at bench 6, and a node budget pays both sides the same nodes, so
this measured quality per node and deliberately divided out the price. Whether
the corrected evaluation is worth its extra nodes *on a clock* is not answered
here.

**`seepruning` is settled: it stays off.** The argument for it was that it buys
*speed* per node — it cuts the most nodes of anything in Phase 3, −41.1% at
bench 6 — so a node budget, which pays both sides the same nodes, divides out
exactly the thing it is good for. That argument was right to make and it did not
survive the measurement. A timed gate on 2026-08-14 returned **+4 Elo, 95% CI
[−7, +14]** over 3 360 games at `-t 100`, an interval that overlaps the
equal-nodes result almost exactly. Two instruments, two intervals containing
zero.

The one caveat worth carrying: `-t 100` is 100 ms per move, and the Lichess bot
plays 900+10, where it spends 15-25 *seconds*. Nothing here rules out
`seepruning` mattering at long time controls. Testing that is not affordable —
see the throughput note under Next.

### Speed, and the tools that found it

The engine is **~2.5× faster than it was on 2026-08-14**, at an identical
search — every one of these was verified by the bench signature staying
byte-identical, so none needed a gate:

| change | gain |
|---|---|
| inline the `Piece` accessors (`PLAN` 5.6) | 1.87× |
| inline `Move`'s constructors and `operator==` | 1.04× |
| pin-aware legality filter (`PLAN` 5.5) | 1.28× |
| count mobility instead of collecting it (`PLAN` 5.7) | 1.04× |

`make profile` exists now and found all of them. The largest was five lines, and
no amount of reasoning about algorithms would have suggested it — **re-profile
before optimising anything**, since the 2026-08-10 profile has been wrong twice
about where the time goes.

`make review` builds `tools/review` (`docs/REVIEW.md`): it reads a PGN — one
game or a whole export — analyses it with any UCI engine, and writes a text
report, an annotated PGN, or a self-contained HTML page. Stockfish 16 is at
`/usr/games/stockfish`; ChessBot needs `--engine-arg --uci`.

```bash
./tools/review-archive.sh                     # every archived game, one file
./tools/review-open.sh --latest               # newest game, opened
./tools/review g.pgn --html o.html --me you   # someone else's export
./tools/archive-profile.py --compare <stamp>  # accuracy before/after a build
```

It carries a known **3% phantom-loss floor** from successive searches
disagreeing, so a lone inaccuracy under ~5 win% is not evidence of anything.

The bench signature is **436,293 nodes** at depth 6, since late move pruning
shipped 2026-08-26 (**+13.1 Elo [+3.5, +22.8]**, −45.0% nodes). Before it,
793,823. The Texel-tuned weights took it to 913,669 on 2026-08-25 and were
reverted the same day when the node price turned out to be **+7% at depth 6 and
+59% at depth 10**, which is where the bot plays — `BUGS.md` 18. 793,823 held
since `razoring` and
`revfutility` on 08-22 (−27.0% from 1,086,693), and before those 6.2's removal
of the hanging-piece penalty on 2026-08-15 took it 1,599,675 → 1,323,943 →
1,086,693, −32.1% across that day. Any change claiming to preserve search behaviour must reproduce
it exactly. It has moved six times this
week — 2,056,371 until `seeordering` was turned on (2026-08-13); then 1,465,771,
1,725,755 and 1,759,990 as the three Phase 4 evaluation fixes landed
(2026-08-14); then back down to 1,464,599 when the quiescence bound landed the
same day, which paid the whole of Phase 4's +20.1% back. Older documents quoting an earlier figure are describing
the baseline of their day, not a regression.

---

## Measured playing strength — the external baseline

**Current: 2152 Lichess rapid (rd ±45) over 294 rated games**, as of
2026-08-27. The bot plays rated 900+10 against other bots, seeking opponents up
to 2500 since `opponent_max_rating` was raised on 2026-08-21.

**The rating is falling on purpose: 2190 → 2160 → 2130 across three days.** The
per-band scores have not moved — the crossover is still 2100-2150 — but nine of
the fourteen games this account has ever played against 2300+ were played on
08-21 and 08-22, and it scores 7% there. Widening the pool made the rating
honest, and honest is lower. `MEASUREMENTS.md` has the fourth reading in full,
including the reconciliation that puts **13% of every loss on restarts and
aborts rather than on play** (`BUGS.md` 7 and 12).

**Accuracy by opponent, over 200 reviewed games:** 96.3% under 1500 scoring
99%, down to 94.3% against 2300+ scoring **zero**. Two percentage points of
accuracy is the whole distance between beating everyone and beating no one,
which is what "getting stronger" has to mean here — consistent small gains, not
one large fix. `MEASUREMENTS.md` has the table.

**Every reading, and the three write-ups that established the baseline, are in
`MEASUREMENTS.md`.** The short version: self-play Elo does not convert to
Lichess rating, the 6.2 work was externally validated on 2026-08-16, and the
rating fell from 2190 to 2130 in the two days after the opponent cap was
raised — which is the rating becoming honest rather than the engine getting
worse.

**Settled 2026-08-24: `razoring` and `revfutility` are clear in the field.**
Thirty games on a healthy machine land at **+0.11σ** against band expectation
with zero forfeits (34 games and −0.32σ on the looser cut — the verdict does
not depend on which). The earlier −1.96σ and −1.45σ were `BUGS.md` 16, the
speed collapse, not the pruning. `MEASUREMENTS.md`'s seventh reading has both
cuts and why there are two. What the void window said at the time, for the
record: 17-2-15, worth 18.0 points against a field expected to yield
21.1: **−1.45σ**, down from the −1.96σ that ten games showed. The whole
remaining deficit is in 2100-2300 (2.0 against 5.1 expected), and **two of
those eight losses are time forfeits with no chess in them** — credit them at
band expectation and the aggregate is −0.97σ. Keep counting, but stop treating
this as a pruning question.

**Two machine faults, not one, and both wore a chess costume.** The forfeits
below are `BUGS.md` 15; the speed collapse is 16, found a day later in the same
logs. Three readings in two days were first attributed to the engine and turned
out to be the environment — twice a forfeit blamed on the clock and on a
restart, once a strength deficit blamed on pruning. **Check nps and grep for
`ConnectionError` before reading any result off real games.**

**The forfeits are the finding, and they are not the engine.** All four
forfeit losses this account has ever taken are the host's network dropping
mid-game — `BUGS.md` 15, added 2026-08-23. Twelve outages in thirty hours, two
of them minutes long, and the clock runs on our turn throughout. Three of the
four were first misread as a restart or as the time manager. **Grep a forfeit's
log for `ConnectionError` before calling it a chess problem.**

## In flight — 2026-08-27

**Read this section first; the rest of the file is older than it.**

### State right now

| | |
|---|---|
| **bot** | **DOWN.** Restart it — nothing blocks that |
| **`./chessbot`** | late-move-pruning build, bench **436,293** |
| **rating** | **2152**, rd ±45, prog 0, over 294 rated games |
| **git** | `main` clean and pushed; `eval-texel-tune` and `tt-16byte` parked |
| **`.wslconfig`** | edited to `processors=8`, **not yet applied** — needs `wsl --shutdown` |

### What shipped 2026-08-26

**Late move pruning, +13.1 Elo [+3.5, +22.8]** over 3 360 games,
`shard-20260826-181028/`. Cuts 45% of nodes at bench 6. On by default.

Its cost is known and measured, which is why the next item exists: over 120
positions sampled from the game archive, LMP changes the chosen move in **49**
of them, and when a depth-11 search adjudicates those disagreements it endorses
**LMP's move 13 times and the shipped move 20**. So LMP gives up judgement on
roughly one position in six and buys enough depth to more than pay for it. The
gate already priced that; the point is that a less aggressive setting might pay
more.

### What is parked, and why — do not re-derive this

**`eval-texel-tune`** holds Texel-tuned evaluation weights measuring **+25.2
Elo [+15.7, +34.7]** per node over 3 360 games. They were merged and reverted
the same day. **The reason is `BUGS.md` 18 and it is the most useful thing in
this session:** the node price is +7% at bench's depth 6 and **+59% at depth
10**, which is where the bot plays, so at the clock it is plausibly a net loss.
The tune is not wrong and the gate is not wrong; the inference from a depth-6
bench figure was.

Diagnosed further and still parked: the whole cost is aspiration-window
re-searches (with `Aspiration` off the two builds search within 1% of each
other), because the tuned weights move the score more between iterations — 22%
of iterations exceed the ±50 window against 14% for shipped. Four window
policies were swept across 8 positions and **the current one is the best of
them**, so there is no cheap fix. Do not spend another evening on it.

**`tt-16byte`** holds a correct, tested 16-byte `TTEntry`. It gains ~1.4% of
nodes at gate size and ~0 at play size, so it is not worth its bench-signature
cost alone. It is Phase 0 of Lazy SMP if that is ever built.

**The earlier reading this paragraph used to carry, kept because the next two
paragraphs argue from it:** 2190 (rd ±45) over 173 rated games as of
2026-08-21 08:30. The 19 rated games between 2026-08-20 20:00 and then went
17W 2D 0L for +28 rating: 3/4 against 2100+, clean sweeps below that, no time
forfeits, and the lowest our clock reached in any game was 41s of 900+10 —
the softtime change (`+42 Elo`, commit `ffc6e2b`) holding up in real games.

**Then the opponent pool was widened and the picture changed.**
`opponent_max_rating` was 2200 against a 2190 bot — matchmaking could not reach
the band that defines the ceiling — and was raised to 2500 on 2026-08-21. The
14 rated games that followed went **9W-0D-5L, 2189 → 2177**, and the record
against 2200+ was **0-0-4**. Reviewed at Stockfish depth 16, the bot makes
**3.3 errors per 100 moves against 2200+ and zero in 268 moves below 2000**;
`BUGS.md` 13 has the tables, the one blunder that reproduces on demand
(`gtB9qan7`, a queen grabbing a pawn into 4.7 pawns of unseen compensation),
and the two explanations that were tested and are wrong. It also corrects the
2026-08-16 reading that these losses contained no blunders — on this build they
do.

**Do not `make` the engine without checking first** — it relinks `./chessbot`,
and the bot spawns a fresh engine per game, so the next rated game would
silently get an ungated binary. `make review` and `make tests` do not touch it.

To stop it, use the scheduler — do not signal it by hand:

```bash
./lichess/bot-stop.sh              # menu; --games 1 makes the game on the
                                   # board now the last one
./lichess/bot-stop.sh --status     # state, and whether stops are exact
```

**This bot's stops are exact.** It started after
`quit_after_all_games_finish: true` was set in `lichess/config.yml`, and it
confirmed so itself at startup: *"When quitting, lichess-bot will first wait
for all running games to finish."* So `--games 1` ends on the game being
played. `--status` says which mode is in force; any bot started before that
config change falls back to gap-hunting and stops late (`BUGS.md` 7).

Restarting, once it is down:

```bash
cd /home/dheirav/Code/lichess-bot && nohup ./venv/bin/python lichess-bot.py \
  --config /home/dheirav/Code/ChessBot/lichess/config.yml -v > /tmp/bot.log 2>&1 &
```

`pgrep -x`, and `ps` with the wrappers filtered — **not** `pgrep -f`, which
matches the shell running the check and produced false positives three separate
times on 2026-08-15 (`BUGS.md` 9). `./lichess/run.sh` is the documented
launcher but needs `LICHESS_BOT_TOKEN` exported in the calling shell; it also
builds first, so it deploys whatever is in the working tree — do not use it on
top of unverified changes.

**A bare SIGINT does not finish the game in progress** (`BUGS.md` 7). It cost
eight minutes of clock in a rated game on 2026-08-15 and nearly a second
forfeit. Only a bot started with `quit_after_all_games_finish: true` may be
signalled during a game, and only once — the second signal is force-quit.

**And do not restart while a game is live** (`BUGS.md` 12). On 2026-08-21
lichess-bot restarted itself mid-game, re-opened the game stream, was 429'd,
and span for ten minutes replaying a stale board into 400s without making a
move — five minutes off our clock. When the log and the game disagree, Lichess
is the state:

```bash
curl -s -H "Authorization: Bearer $LICHESS_BOT_TOKEN" \
     "https://lichess.org/api/account/playing?nb=5"   # isMyTurn, fen, secondsLeft
```

**Shipped 2026-08-23, unbuilt and ungated by design — none of them touch the
search.** Bench signature unchanged at 793,823, all eleven tests pass:

- **EOF no longer truncates a live search** (`BUGS.md` 14, now fixed). A piped
  `go depth 8` returned a one-ply move dressed as a depth-8 one; it now returns
  `b1c3`, matching the held-open control. `go infinite` still aborts on EOF, or
  a pipe would hang.
- **`Threads`, `Ponder` and `Move Overhead` are advertised** — a GUI's default
  config previously set options the engine never announced, which python-chess
  raises on. `EXTERNAL_RATING.md` blocker 4, closed.
- **`Move Overhead` is a real term**, default 100 ms, subtracted from the clock
  before the budget is sized. The engine-side complement to `BUGS.md` 15: it
  covers ordinary round-trip latency, not the outages.

**Recently closed, each recorded where it cannot drift from the code:**

- **6.2 — the `threats` deletion, +155.0 Elo.** Externally validated on
  2026-08-16 (`MEASUREMENTS.md`). Read `ROADMAP.md` 6.2 before quoting any of
  it: the gain was never accuracy, it was silence.
- **The clock: +78 Elo, then `softtime` at +42.** `BUGS.md` 11 and the toggles
  in `search.hpp`. The allocation formula is closed too, at +14 [−22, +50].
- **King safety, six negative results.** Four gates in `ROADMAP.md` 6.4, then a
  rebuild on 2026-08-21 at −33.1 (`BUGS.md` 13). The reasoning lives in
  `evaluation.cpp` beside the absence.
- **`deltapruning`, `IID`** — closed at +0.9 and −0.1, on their toggles.

**The instruments these left behind are the lasting part**, and `TODO.md` §2b
lists them: `tests/engine`, per-side `setoption` forwarding, `tests/evalref
--opt`, `tests/evaltrace`, `tests/gate-progress.sh`, `tests/gate-pause.sh`, and
since 2026-08-21 `tests/evalerror`, `tools/eval-corpus.py` and
`tests/gauntlet.sh`.

**No gate is running.** The `softtime` re-gate finished 2026-08-20 at +42 Elo
[+6, +79] with zero forfeits over 200 games at `--tc 120+1.33`, and is shipped.
The Lichess bot is playing (see *In flight* above), so anything that wants the
machine — a `--tc` gate, `tools/review-archive.sh` — either waits for it or
takes a share of the **four** CPUs away from rated games — `nproc` is 4, not 16 (`.wslconfig` `processors=4`), so concurrent work starves the bot far faster than the old figure suggested.

Logs from finished gates are kept; re-pool any with
`./tests/pool-shards.sh <dir>/`.

**The review archive is current as of 2026-08-21.** `~/reviews/records/` holds
185 of the 186 archived games; the one gap is `HYFGxKGA`, an abandoned game
with no moves in it, which `tools/review` correctly refuses. Re-run after new
games — it only reviews what is missing:

```bash
JOBS=8 ./tools/review-archive.sh   # with the bot down; it is Stockfish per game
```

**The profile moved the right way across 6.2**, over the bot's own moves at
Stockfish depth 16:

| | games | moves | Best | Excellent | Good | Inaccuracy | Mistake | Blunder |
|---|---|---|---|---|---|---|---|---|
| before 2026-08-16 | 87 | 3 546 | 61.1% | 20.7% | 12.1% | 4.7% | 1.2% | 0.3% |
| from 2026-08-16 | 98 | 4 527 | 62.9% | 18.5% | 13.3% | 3.8% | 1.3% | 0.2% |

Read it as a description, not a measurement: the two eras played different
opponents at different ratings, so this is not a controlled comparison and
nothing here is Elo. What it does say is that the top three labels went 93.9% →
94.7% and inaccuracies fell by a fifth, on the same instrument, which is
consistent with — not evidence for — the gated result.

**Every gate this engine has been measured by is in `GATES.md`**, append-only,
with the rules those numbers depend on. It is the only place they live in full:
a verdict is stated once here, next to the thing it decides, and links there for
the number.

**`deltapruning` is closed — 2026-08-21.** Re-measured on the current build:
**+0.9 Elo [−5.8, +7.7]** over 3 360 games at `-N 100000`, 14 shards × 120
pairs, `SEED_BASE=20260821`. The 2026-08-14 result (+7.1 [−2.9, +17.2]) was
**not pooled with it and must not be**: that run measured a pre-6.2 engine, one
`threats` deletion and two clock fixes ago, so it is a different experiment
rather than fewer games of the same one. The point estimate collapsed from +7.1
to +0.9, which is the answer — the old number was a property of that engine,
not of this one.

The clock side was checked too, cheaply, because `-N` cannot see per-node cost:
`./tests/bench 6` reaches the same depth in **~2.7% less wall time** with the
toggle on (1938/1921/2055 ms vs 2001/2008/2067 ms), so the 6.8% node saving is
real but the per-capture check eats most of it. At the usual doubling-is-70-Elo
rule of thumb that is worth +2–3 Elo — inside the interval already measured,
and far below what a timed match could resolve. **A `-t` gate was deliberately
not run**: timed matches cannot be sharded, 200 of them took 17 hours and
returned ±36 Elo, so matching today's ±6.7 would cost weeks to answer a
1-Elo question. It stays off, which is the default, so no code changed.

---

## Next, in order — 2026-08-27

**This list is current. The `ROADMAP.md` discussion below it is the older
reasoning that produced it, kept because the arguments still hold.**

1. **Restart the bot.** Nothing blocks it. It is on a build 13 Elo stronger per
   node and the 2100-2300 band — the one that decides the rating — is still the
   thinnest evidence in the project.

2. **Apply `processors=8`.** `.wslconfig` is already edited; it needs
   `wsl --shutdown` from PowerShell. Measured justification: the host is 8
   cores / 16 logical, WSL had 4, so a saturated WSL could never exceed 25% of
   the machine while native Windows used **0.6 of 16 logical processors**. Gates
   are the bottleneck on every remaining item and they halve, ~110 min to ~60.

3. **Gate `LMP_MAX_DEPTH = 2`** (`search.cpp`, currently 3). Evidence-backed by
   the 13-20 adjudication above: pruning less should hand back judgement while
   keeping most of the node saving. One gate.

4. **Singular extensions**, then **probcut**. +20-40 each on the Phase 7 prior.
   Same recipe every time: implement behind its own toggle, verify bench is
   *bit-identical* with the toggle off, `./tests/bench 6 --opt <f>=on` to see
   the tree, then `shard-gate.sh` at equal nodes.

5. **Ponder** (+30-50). Real engine work — `go ponder`, `ponderhit`,
   `bestmove X ponder Y`. It is currently advertised as a no-op. Threading plus
   the time manager is the combination behind `BUGS.md` 11, so gate it on
   `--tc` before it ever plays a rated game.

6. **Lazy SMP.** Only worth its +200-280 prior *after* step 2; at 4 threads it
   was a fraction of that. Needs `tt-16byte` merged first.

7. **Opening book, last, deliberately.** `lichess-bot` already has polyglot
   support in `config.yml` (lines 16-30), so it is config-only and five
   minutes. It is last because it is **the only item that costs measurement
   quality**: `MEASUREMENTS.md` records that every move is the engine's own, and
   a book makes the rating measure "engine + book" and breaks comparability with
   every past reading. Do the measured work first. When it does go in, use
   `max_depth: 8` and record the date as a break in the series.

**Closed, do not reopen without new information:** evaluation *terms*
(`ROADMAP.md` 6.3, blindness measured at 4.3% over 651 criticised moves — there
is nothing for a new term to be for), and the Texel weights above.

### Two things about this machine that cost hours if forgotten

**It is shared.** `res_ai` and `BrassBot` run on the same four CPUs, sometimes
driven by other Claude sessions, and they start without warning. A gate that
should take 110 minutes took 4 hours once and was abandoned twice. **Check
before starting anything long** — and check for *all* of them, not just the bot:

```bash
ps -eo pid,pcpu,comm,args --sort=-pcpu | head
```

**Match on `comm`, never `pgrep -f`.** `BUGS.md` 9 says this and it still
caught this session out four times in one day — twice reporting the exact
opposite of the truth, because the shell running the check matches its own
pattern. A node-limited gate stays *correct* under contention, so a slow gate is
only slow; but a wrong answer about whether the bot is running is how
`./chessbot` gets relinked mid-game.

---

**`ROADMAP.md` is the answer to this question.** In one line: **the evaluation
is the ceiling and no phase of `PLAN.md` addresses it**, so `ROADMAP.md`
proposes a Phase 6 and puts it first.

**6.1 ran on 2026-08-15 and came back negative.** The review was pointed at all
62 games (`--explain`, Stockfish 16 at depth 14, 2 575 of the bot's own moves)
to collect the losses no evaluation term accounts for. There are four, one of
which is search noise. **The terms move on 97% of this engine's own errors, so
there is no blind-spot list and 6.3 is deferred.** One afternoon retired a
phase, which is what the cheap instrument was for.

It found two things on the way, and both are now the work:

- **`BUGS.md` 10**, in the review tool's own scoring: `score mate 0` was read as
  a win for the side that had just been mated. Fixed. It had corrupted every
  published archive number in the direction that flattered them — see the entry,
  and note that `REVIEW.md`'s profile has been regenerated on the corrected
  parser (94.9%, not the 92.6% older documents quote).
- **`threats` was the largest-magnitude term in the evaluation, larger than
  material**, because `hangingPiecePenalty` charged the *full piece value* for
  anything merely attacked and undefended. Rebuilding it on `see()` gated at
  **+121.2 Elo**; then gating the divisor it carried showed the score still
  climbing as the charge shrank, all the way to charging nothing. **The term is
  now deleted, worth a further +155.0 Elo [+144.3, +166.0]**, and the engine is
  32.1% fewer nodes and 21.6% faster on the clock than it was this morning.

  Read `ROADMAP.md` 6.2 before quoting any of it. The short version: **the gain
  was never accuracy, it was silence** — a static score cannot know whether a
  threatened piece will be saved, and the search settles it a ply later anyway.
  The controls are recorded there too (baseline against itself: 1 120 games,
  exactly 50.00%), and all of it is **self-play Elo, which does not convert to
  Lichess rating**.

**`TODO.md` is the queue**, with sizes and blockers for everything open. The
old item 1 — the uninterrupted night of rated games — **is done and came back
positive**; the third measurement above is its write-up. What matters most now,
in the order I would take it:

1. **The compensation blindness** (`BUGS.md` 13), and **the two instruments
   for it now exist** (2026-08-21). Note 13's own correction: the "one
   reproducible blunder" it used to lead with was an artifact of the truncated
   search in `BUGS.md` 14. The evaluation gap is real and measured; the single
   position is not the lead it looked like.

   `./tests/evalerror` scores the evaluation against Stockfish over 1 051
   positions harvested from the reviewed archive, in about a second. Today it
   reads **182 cp** mean error on ordinary positions with 1.5% sign flips — a
   healthy static evaluation — and **544 cp with 57.9% sign flips** on the
   compensation set. That gap is the defect, and it is now a number that moves
   when a fix works instead of a night of games. `make evalerror-baseline`
   records the bar; `tests/README.md` says why the two tags are read
   separately.

   `./tests/gauntlet.sh` plays a fixed external opponent instead of ourselves,
   because self-play cannot see this family: both sides get the term and
   neither attacks, which is the caveat `ROADMAP.md` 6.4 recorded against its
   own four negative king-safety gates. Verified working against Stockfish on
   2026-08-21; `OPP_NODES` is the handicap that keeps the opponent a fixed
   ruler.

   **Two terms now exist and both are off** in `evaluation.cpp`, each behind
   its own scale constant, and off is exact (`evalref` unchanged over 23 603
   positions, bench still 1,086,693, `evalerror` identical to baseline):

   - `KING_EXPOSURE_SCALE` — a king that has lost castling rights and still
     sits on the centre files, plus files at the king with no pawn of its own.
   - `KING_DANGER_SCALE` — enemy pieces bearing on the squares around the
     king, weighted by piece and squared. This is the shape `ROADMAP.md` 6.4
     rejected, rebuilt because 6.4 judged it on self-play alone.

   Priced on the corpus, in minutes rather than nights:

   | setting | comp | ctl |
   |---|---|---|
   | both off (baseline) | 543.7 | 181.9 |
   | exposure 100% | 533.5 | 180.8 |
   | danger 300% | 516.1 | 182.7 |
   | danger 600% | 500.7 | **194.3** |
   | **exposure 100% + danger 300%** | **506.3** | 183.0 |

   Read the `ctl` column: at danger 600% the term is buying `comp` by wrecking
   ordinary positions, which is the failure mode the control set exists to
   catch. The candidate is **exposure 100% + danger 300%** — 37cp off `comp`
   for 1.1cp on `ctl`. That 1.1 would fail `make test-evalerror` against the
   current baseline, which is the instrument doing its job: the trade is real
   and a gate has to decide whether it is worth it.

   **Gated 2026-08-21 and rejected: −33.1 Elo [−43.2, −23.0]** over 3 360
   games, `shard-20260821-220901/`. The gauntlet run alongside it could not
   separate the candidate from the baseline (30.6% against 32.5% over 80
   games), so it neither confirms nor contradicts — at that sample its interval
   is ±80 Elo.

   **Three things this settles.**

   *The term is out.* Both scales stay 0.

   *6.4's caveat is not vindicated.* The whole reason to rebuild the
   attacker-count term was that self-play might be structurally unable to see
   it. Self-play saw it immediately, and saw it as harmful. That does not prove
   the caveat wrong in general, but it is the first real test of it and it went
   the other way.

   *The corpus anti-predicted the result*, and this is the lasting lesson.
   `evalerror` scored the term as 37cp better on exactly the positions the
   engine loses games from, and the engine got 33 Elo weaker. Use the corpus to
   kill hypotheses cheaply and to locate error; never as a proxy for strength.
   `tests/README.md` carries the table.

   **What the corpus says the target really is.** Over the 363 compensation
   positions the side ahead in material is ahead by **+195cp**, we price the
   position at **+153**, and the truth is **−391** — we discount a material
   edge by 42 centipawns where Stockfish discounts it by 586. But **282cp of
   that 544 is dynamics**: Stockfish's *own* depth-1 evaluation is that far
   from its depth-16 evaluation on the same positions, so no term can remove
   it. The addressable part is our 572 against a world-class 282, and the
   honest goal is halving that distance rather than driving `comp` to zero.

   **Two hypotheses died on the flagship position** (`BUGS.md` 13's `Nxd8`,
   static +381 against a truth of −149): the exposure term does not fire there
   at all, because the castling rights are intact and every file at the king
   has a pawn on it; and the danger term scores it 4cp, because exactly one
   black piece currently attacks the king zone — the queen, the second knight
   and the bishop are all *aimed* at it and blocked. The danger there is
   latent, and a static count of current attacks cannot see latent danger.
   Our own search does not see it either: depth 16 still says +444.

   Three of the five largest errors that day **do** reproduce at depth 10, the
   depth the engine reaches in a rated game, so a node- or depth-limited gate
   can see them (`BUGS.md` 13 has the table). Two of those three are cured by
   depth 14, which makes them speed rather than evaluation; one, `Nxd8` in
   `ZlTEweWc`, survives every depth tried and is the reproducible lead.

   The clock fix that used to head this list **shipped**: +78 Elo, then
   `softtime` at +42 (`BUGS.md` 11). **The allocation formula is closed too** —
   this line used to say it still wanted a gate, and it had already had one:
   `timeAlloc` gated 2026-08-17 at **+14 Elo [−22, +50]** over 200 games and
   stays off, with the reason it stops there recorded on the toggle in
   `search.hpp`. Resolving +14 needs four times the games, about twenty hours
   of unshardable `--tc` wall clock, and the upside is bounded anyway: after
   `softTime` shipped, every allocation formula converges to spending about 97%
   of the clock, so this only redistributes time that is already being spent.
2. **Keep the bot on the widened pool.** The 2200+ gauntlet is the one source
   of evidence self-play cannot give, and `ROADMAP.md` 6.4 names exactly that
   as what its four negative king-safety gates could not rule out. Four games
   is not a sample; forty is.
3. **The general Texel tune** — the rest of 6.2, now over an evaluation whose
   largest term has stopped shouting, and now with a reason to believe a gate
   on it will mean something.
4. **`ROADMAP.md` Phase 7 — the road to 3000.** Written 2026-08-22 after an
   audit of this list found four dead items in it. **Three of those absences
   have since been filled** — `razoring` and `revfutility` on 08-22, late move
   pruning on 08-26 — so as of 2026-08-27 the engine is **2152**, still
   single-threaded, and still has no singular extensions, no probcut, no ponder,
   no book and no tablebases. WSL is set to eight CPUs pending a shutdown.
   Those absences are measured; the Elo attached to each of them in
   Phase 7 is a prior from general practice and has to be earned here. Order is
   pruning suite → Lazy SMP → NNUE, by Elo per unit of effort.

   The strategic line, which six negative gates now support: **the evaluation
   is the ceiling and hand-crafting it is the slow road.** King safety cost
   10 080 games across four gates and 3 360 more on 2026-08-21, for +1.3, +2.2,
   −11.0, −216.9 and −33.1. NNUE is the item that ends that line of work.

Still open in the existing plan: 3.4 (futility/razoring — **read 3.1's result
first**, the same bet swung 57 Elo on one constant), 3.6 (retune LMR,
deliberately last), 5.4 (lazy evaluation). **3.5 (IID) and `deltapruning` are
both settled and both stay off** — −0.1 [−4.9, +4.7] and +0.9 [−5.8, +7.7]
respectively; the deltapruning write-up sits with the gate table above.


---

## Rules that cost real time to learn

- **Gate on nodes, not milliseconds.** `-N 100000` is the standing budget. A
  timed match is not reproducible from its seed and silently depends on machine
  load. Use `-t` only when the change's value is *speed per node* rather than
  *quality per node* — a faster evaluation is invisible at equal nodes.
- **Never `--sprt` under sharding.** A stopping rule applied per shard stops
  each on its own favourable noise. Fixed N per shard, pooled after, is valid.
  `shard-gate.sh` refuses it.
- **Look at the tree before spending a day on a match.**
  `./tests/bench 6 --opt <feature>=on` prints the node signature with one option
  flipped, in seconds. It caught a real SEE ordering bug before a gate ran on it.
- **A and B must differ in exactly one thing**, which is why every heuristic has
  its own named toggle rather than one combined switch.
- **Verify by identity where you can.** Move generation and evaluation are
  checked exactly by `test-perft` and `test-evalref`; only things that change
  the search tree need a match.
- **Do not start with the "obvious" optimizations.** Bitboard move generation
  (~1.02× ceiling), pin-aware movegen (1.24×) and replacing the old string board
  fields (~1.05×) are measured, and the first is a dead end — see `BACKLOG.md`
  §4.

---

## Things a new reader gets wrong

- **There is one move generator and it is mailbox-based** (`movegen.cpp`). The
  bitboard module is complete and perft-verified but deliberately **not
  connected**; it exists for SEE and pin-aware generation, not to make movegen
  faster. `BACKLOG.md` §2.1 says it is broken — that is stale, ignore it.
- **`generateLegalMoves` mutates the board it is given** and restores it. The
  search depends on that to avoid a copy per node.
- **`TTEntry.depth` is `int8_t` on purpose.** Entry size divides into
  `ENTRIES_PER_MB`, so widening it changes the table length, the index
  distribution, and every node count the search produces.
- **Test binaries link `$(ENGINE_OBJ)`; do not go back to listing sources.**
  Until 2026-08-13 each test rule handed every engine `.cpp` to one `g++` call,
  and `-MMD` with many sources but a single `-o` rewrites the `.d` once per
  translation unit — so `tests/bench.d` described only the last file compiled
  and named no headers. Editing `search.hpp` then printed *"Nothing to be done
  for 'tests'"* and left ten stale binaries testing the previous engine: a green
  suite proving nothing. If you ever see that message after a header edit,
  suspect this first. (A full clean build is now ~7 s, down from recompiling 21
  sources ten times over.)
- **UCI advertises its defaults from a default-constructed `SearchOptions`.**
  Do not hand-write `option name ... default ...` lines; the hand-written list
  said `SeeOrdering default false` for as long as it took someone to notice.
- **`test-evalref` and `test-bench` fail on any change they cover, by design.**
  Review the diff, then `make evalref-regen` / `make bench-regen`. Regenerating
  without reading the diff throws away the only thing they do.
- **The project is MIT licensed** since 2026-08-15 (`48183d1`). It was an empty
  file before that, which meant default copyright — worth knowing if you read
  any document written earlier that says so.
  `lichess-bot` remains a separate AGPL-3.0 project and is deliberately not
  vendored here; see `lichess/README.md`.
