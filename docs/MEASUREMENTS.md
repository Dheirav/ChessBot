# Measured strength — every external reading, in order

**Append-only, like `GATES.md`.** Gates measure this engine against itself;
this file is the other kind of evidence — what happened against opponents that
are not this engine. It was carved out of `HANDOFF.md` on 2026-08-22, when that
document had grown to 718 lines of state mixed with archive and four of its
claims turned out to be stale.

`HANDOFF.md` states the current reading and links here for the history.

## The readings

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

## Accuracy by opponent — 2026-08-22, 200 games

The most useful reading this archive has produced, and the one that reframes
what "getting stronger" means here.

| opponent | games | score | accuracy | avg cp loss |
|---|---|---|---|---|
| under 1500 | 36 | **99%** | 96.3% | 14.0 |
| 1500–1900 | 87 | 89% | 95.5% | 17.3 |
| 1900–2100 | 34 | 81% | 94.8% | 20.0 |
| 2100–2300 | 37 | 49% | 94.6% | 20.3 |
| 2300+ | 6 | **0%** | 94.3% | 27.4 |

**Two percentage points of accuracy separate beating everyone from beating no
one.** The engine does not fall apart against stronger opposition — it plays
very slightly worse, and chess converts a very slight difference into a total
one. Whole-archive accuracy is 95.1% over 8 726 moves, and quoting that single
figure hides the only structure in the table.

Regenerate with `tools/archive-profile.py`. `REVIEW.md` has the method and the
caveats; this is the reading.

---

## Fourth measurement — 2026-08-23, and the cost of a wider pool

Live from `/api/user/Crimsy_Bot`, with the archive at 230 records. **All times
in this section are UTC**, which is what the PGN headers carry.

| | 08-16 10:30 | 08-21 08:30 | 08-22 | now (08-22 20:25 UTC) |
|---|---|---|---|---|
| rating | 2200 | 2190 | 2160 | **2130**, rd=**45**, prog **−29** |
| rated rapid | 94 | 173 | 190 | **218** |
| account record | 72-8-14 | — | — | **163-17-45** over 225 rated |

**The 60 points since 08-21 are the opponent cap, not the engine.**
`opponent_max_rating` went to 2500 on 2026-08-21, and matchmaking immediately
started finding the band that had never been reachable before. Nine of the
fourteen games this account has ever played against 2300+ were played on 08-21
and 08-22. The per-band rates did not move; the mix did.

**Strength by opponent, all 219 decided archive games:**

| opponent | games | W-D-L | score |
|---|---|---|---|
| under 1500 | 37 | 36-1-0 | 99% |
| 1500-1900 | 90 | 83-3-4 | 94% |
| 1900-2100 | 40 | 28-7-5 | 79% |
| 2100-2300 | 38 | 15-6-17 | 47% |
| **2300+** | **14** | **1-0-13** | **7%** |

The crossover is **2100-2150**, unchanged since the 08-15 reading put it at
2050-2100. A rating of 2130 is now simply where that crossover is, which is
what a settled rating is supposed to be.

**Six of the 45 losses were never played.** Wins and draws reconcile exactly
between the archive and the account — 163 and 17 in both — so all of the
difference is in the loss column: 39 losses over the board, 45 on the account.
The archive holds 11 records with no result (aborts, restarts, one game still
running); Lichess scored six of them as losses and did not count the other
five. **Restarts and aborts are 13% of every loss this account has.**
`BUGS.md` 7 and 12 are the causes, and this is their price in rating.

Time forfeits proper are rare and are not a `softtime` regression: two forfeit
*losses* in the whole archive at this reading, 08-17 07:29 and 08-21 16:11.
Since `softtime` shipped the engine has also won three games on the opponent's
flag.

**Corrected 2026-08-23.** This paragraph originally read the 08-21 forfeit as
"the signature of a restart, not of the clock", on the grounds that an
unterminated and an abandoned game bracket it. That was coincidence reasoning.
Its log carries **84 connection errors**; so does 08-17's, with 93. Both are
the host's network dropping mid-game, which is `BUGS.md` 15 and accounts for
every forfeit loss this account has.

### The first field data on razoring and reverse futility — inconclusive, keep counting

`./chessbot` was relinked 2026-08-22 09:14 UTC with `razoring` and
`revfutility` on (commit `a8c5a5b`, +39.1 and +18.4 in `GATES.md`). lichess-bot
spawns a fresh engine per game, so every game from 09:20 UTC onward is that
build. **Ten games: 3-1-6.**

That is not the 35% it looks like — half of those games were against 2100+.
Priced against the per-band rates above, the field was worth **≈5.9 points** and
it scored **3.5**, which is about **two standard deviations low on ten games**.
Borderline, one-sided, and confounded by the same cap change that produced the
harder field in the first place.

**Do not act on this.** It is recorded because it is the first window in which
the new pruning has met opponents that are not this engine, and because ten
games is exactly the size at which a real regression and pure noise look
identical. The next fifty games settle it; `tools/archive-profile.py --compare
2026.08.22-09:14:00` is the sharper instrument when there are enough of them,
since blunder counts do not need the opponent mix to hold still.

---

## Fifth measurement — 2026-08-23 20:06, and the razoring window closes

**2132 rapid (rd ±45, prog −12) over 242 rated games**, account record
177-18-54 over 249. The fall stopped: 2130 → 2132 across a day, after 2190 →
2160 → 2130 over the three before it. The rating has found its level.

**`razoring` + `revfutility` in the field, at 34 games — no case to answer.**
Same binary throughout (`./chessbot` untouched since 08-22 13:14 local).

| | at n=10 | at n=34 |
|---|---|---|
| record | 3-1-6 | **17-2-15** |
| actual vs band-expected | 3.5 vs 5.9 | **18.0 vs 21.1** |
| deviation | −1.96σ | **−1.45σ** |

An excursion regressing toward zero as the sample grows is what noise looks
like. The earlier entry was right to record it and right not to act on it.

| band | games | W-D-L | actual | expected |
|---|---|---|---|---|
| under 1500 | 3 | 3-0-0 | 3.0 | 3.0 |
| 1500-1900 | 8 | 8-0-0 | 8.0 | 7.5 |
| 1900-2100 | 6 | 4-1-1 | 4.5 | 4.9 |
| **2100-2300** | 10 | 2-0-8 | **2.0** | **5.1** |
| 2300+ | 7 | 0-1-6 | 0.5 | 0.6 |

**All of the residual is in one band, and a quarter of it is not chess.** Two
of those eight losses are time forfeits — `7kgNwYF5` (2159) and `GlZN6Jnv`
(2225) — both caused by the host's network, `BUGS.md` 15. Score them at band
expectation and the aggregate deviation is **−0.97σ**.

That is the reading to carry: **the pruning is fine, and the machine is what is
losing games in the band that sets the rating.**

---

## Sixth measurement — 2026-08-24, and the fifth one was taken on a broken machine

**2135 rapid (rd ±45, prog +5) over 252 rated games**, account record
184-18-57 over 259. Recovering off the 2130 floor rather than still falling.

**Read `BUGS.md` 16 before anything below.** The engine ran at roughly a third
of its normal speed from 08-22 19:00 to 08-23 16:00 — median 257 knps on 08-23
against a normal 700-880, recovering exactly at the WSL restart. **The fifth
reading's razoring window sits inside that.** Its −1.45σ was read as an
excursion regressing toward noise; it is better read as a measurement of a
crippled machine, taken alongside two network forfeits. The pruning is neither
implicated nor exonerated — **there is no valid field reading of `razoring` and
`revfutility` yet, and the next one has to be taken on a healthy box.**

### Strength by opponent, all 253 decided games

| opponent | games | W-D-L | score |
|---|---|---|---|
| under 1500 | 39 | 38-1-0 | 99% |
| 1500-1900 | 101 | 94-3-4 | 95% |
| 1900-2100 | 45 | 33-7-5 | 81% |
| 2100-2300 | 49 | 18-6-25 | **43%** |
| 2300+ | 19 | 1-1-17 | **8%** |

A cliff at ~2100, which is where the rating sits. Unchanged in shape since the
08-15 reading; only the sample has grown.

### The clock is healthy, and this is the evidence

Across **181 games with clock traces, our clock never fell below 30 seconds**
except in the two network forfeits of `BUGS.md` 15 (0.0s and 11.9s). 95 of the
181 never went below five minutes. `softtime` is doing its job and the time
manager needs no work — which is worth stating plainly, because a forfeit was
misattributed to it on 08-23.

### Two structural facts about this archive

**No colour bias.** White 130 games at 76.9%, Black 123 at 75.6%, against
average opposition of 1864 and 1845. Nothing to fix.

**The sample is smaller than its count** (`BUGS.md` 6). 115 distinct opponents,
but **16 of them account for 80 of the 253 games — 32%** — and play is
deterministic: croco_bot 8-0-0, debzero 6-0-0, croco_little_bot 6-0-0. Those
are largely one game replayed. Every percentage in this file inherits it.

### What the machine fix has and has not shown

Since the restart: 17 games, 12-0-5. But **2100-2300 is 1-0-5 over six games**,
so the fix has not been shown to move the band that decides the rating. Six
games cannot show it either way. Count more before concluding anything.

**No Stockfish move-quality profile accompanies this reading.** One was started
and killed: it was launched at `--jobs 8` with no `nice` while the bot was
down, and the bot was restarted underneath it, leaving eight un-niced analysis
jobs competing with live rated games for eleven hours. `TODO.md` §1 specifies
`--jobs 4 --nice 19` for exactly this. No damage was measurable — 6-0-3 over
the window, nps 630-1065 throughout — but the run produced nothing and the
profile is still owed.

---

## Seventh measurement — 2026-08-24 20:11, both machine fixes verified

**2147 rapid (rd ±45, prog +3) over 269 rated games**, account 196-20-60 over
276. Up 17 from the 2130 floor in a day.

### The two environmental fixes worked, and this is the evidence

**Network** (`BUGS.md` 15) — connection failures per day:

| day | failures |
|---|---|
| 08-20 | 59 |
| 08-21 | 98 |
| 08-22 | 22 |
| 08-23 | 61 |
| **08-24** | **1** |

`networkingMode=mirrored` went in late on 08-23. A full day since has produced
**one** failure against 61 the day before. **Zero time forfeits since.**

**Speed** (`BUGS.md` 16) — median knps in live games: 257 on 08-23, **795 on
08-24** over 3 415 samples, hourly range 629-1088 across twenty-one hours. No
recurrence. **The cause is still unknown** and that has not changed: a restart
cured it, which is a workaround. If it returns, 16 is the first thing to read.

### `razoring` + `revfutility` are clear — the real field reading

The fifth reading's window was void (`BUGS.md` 16). This is the replacement:
same build, healthy machine, priced against the same pre-razoring band rates.

| cut | games | W-D-L | actual | expected | deviation | forfeits |
|---|---|---|---|---|---|---|
| 08-23 11:30 UTC | 34 | 24-2-8 | 25.0 | 25.7 | **−0.32σ** | 1 |
| 08-23 14:00 UTC | 30 | 22-2-6 | 23.0 | 22.8 | **+0.11σ** | 0 |

Two cuts because the log timezone changed at the restart (+04 → IST), leaving
the boundary uncertain by about ninety minutes; the looser cut still contains
one pre-restart network forfeit. **The verdict does not depend on the choice:
both land on expectation.** The −1.96σ at ten games and −1.45σ at thirty-four
were the machine, not the pruning.

Worth keeping: **the same games, on a broken machine, produced a 2σ scare and a
plausible story about a 500cp margin being too aggressive.** Nothing about that
story was true, and nothing in the results alone could have shown it. It took
`nps`.

### What has not moved

| opponent | all-time games | score |
|---|---|---|
| under 1500 | 43 | 99% |
| 1500-1900 | 112 | 95% |
| 1900-2100 | 52 | 82% |
| 2100-2300 | 60 | 41% |
| 2300+ | 20 | 10% |

The cliff at ~2100 is exactly where it was at the 08-15 reading. **Two clean
machine fixes and a +57 Elo pruning pair have not moved it**, which is the
argument for `BUGS.md` 13 being the real ceiling: the evaluation cannot see
compensation, and opponents above 2100 are the ones able to offer it.

---

## Move-quality profile — 2026-08-24, 282 games

The reading `TODO.md` §1 has demanded after every ship, run on a healthy
machine with the bot down. Stockfish 14 at depth 14, split at the machine fix.

**Whole archive to 08-23 14:00 UTC** — 248 games, 11 313 of the bot's moves,
accuracy **94.8%**, avg cp loss 19.9:

| opponent | games | score | accuracy | avg cp |
|---|---|---|---|---|
| under 1500 | 38 | 99% | 96.2% | 14.4 |
| 1500-1900 | 100 | 90% | 95.4% | 17.3 |
| 1900-2100 | 43 | 78% | 94.7% | 20.3 |
| 2100-2300 | 48 | 42% | **94.0%** | 22.8 |
| 2300+ | 19 | 8% | 94.3% | 25.3 |

**Since the machine fix** — 31 games, 1 865 moves, accuracy 95.1%, cp 18.5. But
2100-2300 reads **94.2% / 22.6 cp / 39%**, which is the same band on the same
build before it. **The machine fix restored speed, not quality where it
matters.** That is the correct expectation — `BUGS.md` 16 cost a day of games,
not a property of the engine — and it is worth stating because the temptation
after a fix is to read the next good number as its consequence.

### The shape, which has not changed since 2026-08-15

**2.2 accuracy points separate 99% scoring from 8% scoring.** 96.2 → 94.0. The
deficit is not catastrophes: 28 blunders in 11 313 moves, 0.25%. It is a small
uniform quality gap that chess compounds into a total result. Every plan that
proposes to fix one class of position is arguing against this table.

### Term attribution, and the 6.1 re-run

`ROADMAP.md` 6.3 is closed on this data — blindness 4.3%, `threats` exonerated,
details there rather than repeated here. The number that matters:

| term | mentioned | median | p90 | **leads** |
|---|---|---|---|---|
| material | 382 | 162 | 535 | **351 of 720** |
| threats | 454 | 59 | 155 | 168 |
| piece placement | 352 | 40 | 80 | 101 |
| mobility | 223 | 34 | 74 | 38 |

**`material` leads half of every criticised move at a p90 of 535 centipawns**,
in every band, in both halves of the split. Depth refutes a bad material grab
regardless of what the evaluation understands, so this is the argument for
Phase 7 search work over any further evaluation term.

Caveat carried from `BUGS.md` 6: play is deterministic and 32% of the archive is
against sixteen repeated opponents, so these samples are worth less than their
counts. And accuracy is a function of the analysing engine and depth — these
numbers compare this engine to itself and to nothing else.

---

## Contention calibration — 2026-09-01, and what `nps-health` never had a number for

`BUGS.md` 16 is the most expensive measurement error in this project: on
2026-08-23 the engine ran at roughly a third of its speed for twenty-one hours,
and thirty-four rated games were recorded, analysed and written up as evidence
that `razoring` had regressed. Re-measured on healthy hardware the same build
came back at +0.11 sigma. `tools/nps-health.py` was built so that could not
recur silently.

What the tool never had was a **calibration point** — a reading taken while the
machine was known to be busy, with the load described. Every day in its table
so far is either healthy or the 08-23 disaster. This is the middle case.

### The load

Seven CPU-bound workers from an unrelated project (`BrassBot`, CFR training),
six at `nice 5` and one at `nice 19`, on an 8-core WSL. Load average **7.23**.
The engine runs at `nice 0` and is single-threaded (`Threads` min 1 max 1), so
it contends for one core against seven niced ones — and CFS weights are
exponential, so `nice 5` carries roughly 2.5x less weight than the engine and
`nice 19` about 68x less.

### The reading

Measured directly, `go depth 12` from the start position, while all seven ran:

| | knps |
|---|---|
| engine under load, depth 10 / 11 / 12 | 525 / 528 / 485 |
| baseline, median of daily medians in real games | **593** |
| `nps-health` degraded threshold (0.6x) | 356 |
| that day's own game median | 595, reported `ok` |

**82-89% of baseline.** Well clear of the flag, and materially different from
the 08-23 failure, which was near a third.

### What this calibrates

**The flag threshold is not the interesting boundary.** A machine can be 15%
slow and pass, which is correct for the flag's purpose — it exists to catch the
catastrophic case that invalidated a whole day of games. But 11-18% fewer nodes
is a fraction of a ply and worth a few Elo, so a *passing* reading is not the
same as a clean one.

**The distinction is between playing and measuring, not between fast and slow.**
Rated games tolerate this: the loss is small, it applies to every game equally,
and the rating absorbs it. A gate does not. `shard-gate.sh 14 60` wants fourteen
engine processes against seven already-busy cores, and the pooled result would
be load-dependent in exactly the way 16 describes. So the standing rule is now
stated as a number rather than a caution: **the bot may run on a machine at this
load; a gate may not.**

Also worth carrying: the day's game median read 595 while a direct probe read
485-528. Not a contradiction — most of that day's games were played before the
poker job scaled up. **A daily median is a lagging indicator**, so a same-day
reading cannot clear a machine that got busy an hour ago. Probe directly before
trusting a gate.
