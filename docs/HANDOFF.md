# Handoff — 2026-08-17

Current state, what is in flight, and what to pick up. This is the file to read
first; it is meant to be rewritten as state changes, unlike `BACKLOG.md`, which
is a frozen archive of the 2026-08-10 profiling session.

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
| `deltapruning` | off | −50.0 [−60.3, −39.7] at 200cp; **+7.1 [−2.9, +17.2] at 900cp** — spans zero, so stays off |
| `softtime` | **off** | gated **+78 Elo [+40, +117]** at `--tc 30+0.33`, shipped 2026-08-16, **reverted 2026-08-17 after a rated-game time forfeit** — the `budget * 3` hard limit permits a 2 s overshoot at the gated control and a **70 s** one at 900+10. `BUGS.md` 11 has the repair; re-gate at a realistic control |

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

The bench signature is **1,086,693 nodes** at depth 6, after 6.2 removed the
hanging-piece penalty on 2026-08-15 (1,599,675 → 1,323,943 → 1,086,693, −32.1%
across the day). Any change claiming to preserve search behaviour must reproduce
it exactly. It has moved six times this
week — 2,056,371 until `seeordering` was turned on (2026-08-13); then 1,465,771,
1,725,755 and 1,759,990 as the three Phase 4 evaluation fixes landed
(2026-08-14); then back down to 1,464,599 when the quiescence bound landed the
same day, which paid the whole of Phase 4's +20.1% back. Older documents quoting an earlier figure are describing
the baseline of their day, not a regression.

---

## Measured playing strength — the external baseline

Every gate in this project so far has been the engine against itself. Self-play
says whether a change helped; it cannot say how strong the result is, because
both sides share every blind spot. This is the first measurement against a field
that does not.

**Baseline, frozen 2026-08-14 00:04, on the build at `62d1043`:**

| | |
|---|---|
| account | [`Crimsy_Bot`](https://lichess.org/@/Crimsy_Bot) |
| Lichess rapid | **2198**, rd=121, still provisional |
| record | **16-6-2** over 24 games (23 rated rapid) |
| time control | 900+10, rated, vs bots |
| PGNs | `/home/dheirav/Code/lichess-bot/game_records/` |

Every move in those games is this engine's own: every book, cloud-analysis and
tablebase source in `lichess/config.yml` is `enabled: false`.

**What the number is worth.** `rd=121` puts the 95% interval at roughly
**1960-2440**. It is a point estimate on a rating that has not settled, and it
was earned against a lopsided field — mostly 1200-1800, with nothing at all
between 2072 and 2199, which is the band the rating itself claims.

**The one clean signal** is that the results separate almost perfectly by
opponent strength: every loss is to an opponent rated **≥2199**, every win is
against **≤2072**, and the single honest draw is 2145. Practical strength is
therefore around **2100-2200**, which is where this feature set — alpha-beta,
quiescence, TT, null move, LMR, aspiration, SEE ordering, hand-written PST
evaluation — is expected to land.

Three things to keep straight when this baseline is next compared against:

- **Do not translate self-play Elo into pool Elo.** The +246, +140 and +25.6
  from the gates are real statements about this engine relative to itself. None
  of them is a prediction about Lichess.
- **The 08-12 games are a different engine** (pre-`seeordering`) and a much
  stronger field. Split at the 11:25 rebuild on 08-13 before comparing anything.
- **Wins over 1300-rated bots cannot move the ceiling.** The test of any Phase 3
  or Phase 4 work is whether the engine starts taking points off the 2200-2500
  tier, not whether the rating drifts up.

Two of the eight non-wins were self-inflicted and are written up in `BUGS.md`:
a drawn win against a 1404 (−48) and a time forfeit caused by restarting the bot
mid-game (−120).

**The engine has already moved past this baseline.** `BUGS.md` 1, 2, 3, 4 and 5
and `PLAN.md` 3.3 all landed on 2026-08-14, so games from those builds are not
comparable to the 24 above without splitting them. The baseline stays frozen as
written rather than being edited forward; that is what makes it usable as a
comparison at all.

### Second measurement — 2026-08-15 00:49

**The rating is no longer provisional.** 44 rated games, `prov=false`.

| | baseline (08-14 00:04) | now (08-15 00:49) |
|---|---|---|
| rating | 2198 | **2080** |
| rd | 121 | **79** |
| 95% band | 1960-2440 | **1922-2238** |
| record | 16-6-2 | **30-13-2** |

The drop is the estimate converging, not the engine weakening. A provisional
rating built almost entirely on wins over 1200-1800 opponents was always going
to fall as it met stronger ones, and it is up 36 points across the last eight
games.

**Strength by opponent band**, over the 43 decided games recorded locally:

| opponent | games | W-D-L | score |
|---|---|---|---|
| under 1500 | 13 | 12-1-0 | 96% |
| 1500-1900 | 13 | 12-0-1 | 92% |
| 1900-2100 | 6 | 4-0-2 | 67% |
| 2100-2300 | 8 | 2-1-5 | 31% |
| 2300+ | 3 | 0-0-3 | 0% |

The crossover sits at **2050-2100**, which is where the rating settled — the two
agree, which the first measurement could not claim.

**The ceiling moved.** On 08-14 the pattern was absolute: every win ≤2072, every
loss ≥2199, nothing in between ever played. That gap is now filled and the
boundary has shifted — highest opponent beaten **2156**, lowest lost to 1739.

**Watch the 2100-2300 band, not the rating.** It sits at 31% over 8 games. If
the +23.0 Elo from check extensions is real against a diverse field rather than
against this engine's own blind spots, that band is where it shows up first. The
rating will move for reasons that have nothing to do with the engine.

### Third measurement — 2026-08-16 10:30, and the answer to the question

**This is the measurement the whole project was waiting on**, and it came back
positive. The question was whether self-play Elo means anything here: 6.2
produced +121.2 and then +155.0 against the engine itself, and every gate ever
run in this project is worth exactly what that answer says it is.

Lichess `/api/user/Crimsy_Bot`, live, and the archive at 98 games:

| | 08-15 00:49 | now |
|---|---|---|
| rating | 2080 | **2200**, rd=**52**, prog +45 |
| 95% band | 1922-2238 | **2096-2304** |
| record | 30-13-2 | **72-8-14** over 94 rated |

**Split at `2026.08.15-13:27:00` UTC — commit `4a81fe8`, the hanging-piece
deletion, at 17:27 local.** Results either side of it:

| opponent | before, 61 games | after, 31 games |
|---|---|---|
| under 1500 | 97.1% (16-1-0) | 100% (4-0-0) |
| 1500-1900 | 91.7% (16-1-1) | 100% (16-0-0) |
| 1900-2100 | 70.8% (7-3-2) | 100% (6-0-0) |
| 2100-2300 | **31.8%** (2-3-6) | **100%** (5-0-0) |
| 2300+ | 0% (0-0-3) | *none played* |
| **all** | **73.8%** | **100% — 31-0-0** |

Thirty-one games, thirty-one wins, including kopyto_dev (2191), mate1-bot
(2176) and stickshark99 twice (2187, 2169), an opponent that had beaten it
twice before.

**Move quality says the same thing, which is the part that is not luck.**
`./tools/archive-profile.py --compare 2026.08.15-13:27:00`, Stockfish 16 at
depth 14:

| | before (2 730 moves) | after (1 358 moves) |
|---|---|---|
| accuracy | 94.9% | 95.4% |
| avg centipawn loss | 20.9 | **16.3** (−22%) |
| criticised moves | 172 (6.30%) | 68 (5.01%) |
| **blunders** | **10** | **0** |

**The blunder count is the finding.** Zero in 1 358 moves where the old rate
predicts five; P(0) = 0.7%. It is the one number here that does not lean on the
accuracy metric's soft parts, and unlike the score it cannot be produced by an
easy draw of opponents.

**The within-band cut is what defeats the "softer field" objection.** The
after-cut opponent mix *is* easier, and accuracy always falls against stronger
opponents, so the headline could have been mix rather than merit:

| opponent | accuracy | avg cp |
|---|---|---|
| under 1500 | 96.6 → 96.4 | 14.2 → 16.5 |
| 1500-1900 | 95.4 → 96.0 | 19.0 → 14.0 |
| 1900-2100 | 94.5 → 94.5 | 24.5 → 20.0 |
| 2100-2300 | **93.8 → 95.2** | **22.0 → 15.7** |

The gain concentrates in **2100-2300** — largest accuracy jump and largest
cp-loss drop both — and is flat or marginally worse against weak opposition.
That is the shape 6.2 predicts: a term that shouted about phantom threats costs
most against opponents able to punish a wasted tempo, and nothing against
opponents who cannot. `threats` also stopped dominating the error attributions,
leading 51 of 172 criticised moves before and 15 of 68 after.

**What this does not license.** The magnitude does not convert: +276 Elo of
self-play produced roughly +97 of rating. One change, measured once, is not a
general warrant for self-play Elo — it is one instance of the instrument
agreeing with the field. **No 2300+ opponent has been met since the cut** (0-0-3
before), so the ceiling has not been shown to move, only the tier below it. And
`BUGS.md` 6 still applies: play is deterministic, so the repeated opponents in
that 31 are correlated and the sample is worth less than its count.

---

## In flight

**The Lichess bot is running.** Restarted 2026-08-16 at 02:08 on the post-6.2
build, and it has been playing rated 900+10 since. **Do not `make` the engine
without checking first** — it relinks `./chessbot`, and the bot spawns a fresh
engine per game, so the next rated game would silently get an ungated binary.
`make review` and `make tests` do not touch it.

To stop it, wait for a gap between games:

```bash
pgrep -x chessbot     # a PID means a game is live — WAIT
# only once that prints nothing:
kill -INT "$(ps -eo pid,args --no-headers | grep 'lichess-bot\.py' \
             | grep -v 'bash -c' | grep -v grep | awk '{print $1}')"

cd /home/dheirav/Code/lichess-bot && nohup ./venv/bin/python lichess-bot.py \
  --config /home/dheirav/Code/ChessBot/lichess/config.yml -v > /tmp/bot.log 2>&1 &
```

`pgrep -x`, and `ps` with the wrappers filtered — **not** `pgrep -f`, which
matches the shell running the check and produced false positives three separate
times on 2026-08-15 (`BUGS.md` 9). `./lichess/run.sh` is the documented
launcher but needs `LICHESS_BOT_TOKEN` exported in the calling shell; it also
builds first, so it deploys whatever is in the working tree — do not use it on
top of unverified changes.

**SIGINT does not finish the game in progress** (`BUGS.md` 7). It cost eight
minutes of clock in a rated game on 2026-08-15 and nearly a second forfeit.

**6.2 is now externally validated** — see the third measurement above. The
uninterrupted night happened, 31 games came out of it, and both the results and
the move quality moved. That was the outstanding measurement; it is closed.

**The clock fix shipped: +78 Elo** (`BUGS.md` 11). The bug entry described one
leak — the allocation being too small — and there were two. The larger one was
that the engine spent only **75% of even that**: iterative deepening refused to
begin an iteration unless the whole predicted iteration fitted in the budget, so
it abandoned the tail of every move. Splitting the deadline into a target that
governs *starting* an iteration and a hard cap that governs *abandoning* one
took budget usage from 72% to 167% and mean depth from 8.8 to 10.0 plies.

**Gated on a clock, which is the point.** 200 games at `--tc 30+0.33`: **+78
Elo, 95% CI [+40, +117]**, zero time forfeits. This defect was invisible to
every gate this project has ever run, because `-N` pays both sides the same
nodes and an engine wasting *time* looks identical to one that is not. It needed
`--tc`. **The allocation formula itself is still unfixed and wants its own
gate** — see the entry.

**King safety is closed and negative** (`ROADMAP.md` 6.4). It came out of
reviewing the three losses to 2300+ opposition, which turned out to contain
**zero blunders between them** — the engine was outplayed and mated, not caught
out. The cause is real and still in the code: king safety counts no attackers
at all, so a queen, rook and knight around the king score what an empty board
does. Building the missing term measured **+1.3 [−7.9, +10.6]**; at 8× it
measured **−216.9**; combined with the other half it measured **−11.0
[−20.4, −1.6]**. Monotone, no peak above zero, and 5% slower even switched off.
It was deleted. The engine is byte-identical to before the experiment — `evalref`
and `bench` prove it — and the reasoning lives in `evaluation.cpp` beside the
absence, as the hanging-piece penalty's does.

**The instruments it left behind are the lasting part**, and `TODO.md` §2b
lists them: `tests/engine` (gate evaluation without relinking `./chessbot`),
per-side `setoption` forwarding in `tests/match`, `tests/evalref --opt`,
`tests/evaltrace`, `tests/gate-progress.sh`, `tests/gate-pause.sh`.

### In flight right now — a `--tc` gate, started 2026-08-19 18:00

**`softtime` is being re-gated at `--tc 120+1.33`, 200 games, and the run is
detached** (parent is `init`), so it survives any session ending. **The Lichess
bot is stopped and must stay stopped until it finishes.**

```bash
L=/tmp/claude-1000/-home-dheirav-Code-ChessBot/4d6a5f36-aa81-4f77-bcac-6642a2b30753/scratchpad/gate-softtime2.log
grep -c '^  pair ' "$L"          # games done, of 200
grep -c 'time forfeit' "$L"      # the number that matters most
sed -n '/^=== result/,$p' "$L"   # the result, once it is there
./tests/gate-progress.sh "$L"    # live bar; it also handles a single log
```

**Why the bot must not run alongside it, unlike every other gate here.** A
`--tc` match measures the engine's clock in wall time, so anything else on the
machine changes the effective time control *while the experiment runs* — and
unevenly, because the arm that starts more iterations is the one more exposed to
being cut short, which is the mechanism under test. A node budget has no such
problem, which is why the IID gate could have coexisted with the bot and this
one cannot.

**It has already been restarted once.** The first attempt was abandoned six
games in when the machine slept: a suspend is exactly what corrupts wall-clock
measurement, and `ps` etime under WSL2 *understates* it, so the only symptom was
the progress bar reporting an absurd elapsed figure. If the elapsed time ever
looks impossible again, suspect a sleep and discard the run.

**What it is deciding.** `softtime` was gated at +78 Elo at `--tc 30+0.33`,
shipped, and forfeited a rated game, because its hard bound was a *ratio*
(`budget * 3`) that permits a 2-second overshoot at that control and seventy at
900+10. The bound is now absolute (`budget + increment`) and verified at 900+10
clocks — 44.8 s spent against a 44.9 s bound where the old code took 73 s. This
gate asks whether it is still worth Elo with the overshoot bounded, at a control
that has the same 90:1 shape as the one the bot plays.

Logs from finished gates are kept; re-pool any with
`./tests/pool-shards.sh <dir>/`.

**The review archive is 41 games behind.** `~/reviews/records/` caches one
review per game and stops at 2026-08-15, so the index covers 80 of 121 — none of
the post-6.2 games it would be most useful for. Catching up costs Stockfish over
41 games, which is why it is waiting for the bot to be idle:

```bash
./tools/review-archive.sh          # only re-reviews what is missing
```

| directory | gate | pooled result |
|---|---|---|
| `shard-20260813-000634/` | `ttaging` | +11.5 [+3.2, +19.8] |
| `shard-20260813-015757/` | `seepruning` | +2.2 [−7.2, +11.6] |
| `shard-20260813-034736/` | `seeordering` | +25.6 [+16.1, +35.2] |
| `gate-seepruning-timed.log` | `seepruning`, **timed** `-t 100` | +4 [−7, +14] |
| `shard-20260814-122546/` | Phase 4 evaluation, two binaries | +6.1 [−3.9, +16.1] |
| `shard-20260814-153213/` | `deltapruning`, 200cp margin | **−50.0 [−60.3, −39.7]** |
| `shard-20260814-175417/` | `deltapruning`, 900cp margin | +7.1 [−2.9, +17.2] |
| `shard-20260814-203000/` | `checkext` | **+23.0 [+13.3, +32.7]** — accepted |
| `shard-20260815-112406/` | 6.2 SEE rebuild vs baseline | **+121.2 [+110.8, +131.9]** |
| `shard-20260815-121725/` | **null control**, baseline vs itself | **50.00%** — no side bias |
| `shard-20260815-131111/` | divisor 1 vs 2 | **−177.7 [−189.1, −166.7]** |
| `shard-20260815-142024/` | divisor 3 vs 2 | +66.8 [+57.0, +76.7] |
| `shard-20260815-153101/` | divisor 4 vs 2, scan | +98.1 [+80.9, +115.7] |
| `shard-20260815-155045/` | divisor 6 vs 2, scan | +105.2 [+87.4, +123.5] |
| `shard-20260815-160908/` | no penalty vs 2, scan | +152.0 [+133.1, +171.8] |
| `shard-20260815-163117/` | **no penalty vs 2, full** | **+155.0 [+144.3, +166.0]** — shipped |
| `shard-20260816-114039/` | **null control**, two binaries, same build | **50.00%**, `0-0-480-0-0` |
| `shard-20260816-115624/` | `kingdanger` on vs off | +1.3 [−7.9, +10.6] |
| `shard-20260816-131704/` | `kingcentre` off vs on | +2.2 [−6.8, +11.1] |
| `shard-20260816-140940/` | both king-safety changes | **−11.0 [−20.4, −1.6]** |
| `shard-20260816-150140/` | `kingdanger` at 8× magnitude | **−216.9 [−241.9, −193.8]** |

**Run a null control when a result is surprising.** The +121.2 was five times
anything previously measured here, so the same harness was pointed at the
baseline against itself from two paths: 1 120 games, exactly 50.00%, pentanomial
`0-0-560-0-0`. It costs twenty minutes and is the difference between a
measurement and a hope.

**Always pool before believing a shard.** Shard 1 of the `ttaging` gate on its
own read +13 with a CI spanning zero — "no difference demonstrated". Pooled over
all fourteen, the same experiment is +11.5 with the interval clear of zero. The
point estimate barely moved; the interval is what shrank. One shard is a
240-game match, and no 240-game match resolves a 10-Elo effect.

---

## Next, in order

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

1. **The time-management fix** (`BUGS.md` 11). The bot spends under half its
   clock; `parseGo` divides what is left by a hardcoded 30 and banks half the
   increment. The instrument to gate it now exists (`--tc`), and `tools/review`
   charts the clock, so a real game can show whether a fix worked and not only a
   gate. The fix is deliberately unwritten, because "spend more clock" is exactly
   the kind of plausible argument that has been wrong twice here.
2. **The general Texel tune** — the rest of 6.2, now over an evaluation whose
   largest term has stopped shouting, and now with a reason to believe a gate
   on it will mean something.
3. **Meet the 2300+ tier.** The band that defined the ceiling is 0-0-3 and has
   not been played since 6.2 landed, so nothing measured on 08-16 speaks to it.
   Everything else is a measurement of the tier below.

Still open in the existing plan: 3.4 (futility/razoring — **read 3.1's result
first**, the same bet swung 57 Elo on one constant), 3.5 (IID, the safe one),
3.6 (retune LMR, deliberately last), 5.4 (lazy evaluation). `deltapruning` is
one gate short of a verdict at +7.1 [−2.9, +17.2]; settling it needs twice the
games and **different seeds**, since `shard-gate.sh` derives them from a fixed
base and would otherwise replay the same games.


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
