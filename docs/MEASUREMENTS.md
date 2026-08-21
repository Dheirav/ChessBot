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

