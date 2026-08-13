# Handoff — 2026-08-14

Current state, what is in flight, and what to pick up. This is the file to read
first; it is meant to be rewritten as state changes, unlike `BACKLOG.md`, which
is a frozen archive of the 2026-08-10 profiling session.

| document | what it is | current? |
|---|---|---|
| `HANDOFF.md` | this file — where things stand | **yes, keep it that way** |
| `README.md` | features, UCI, build, controls | yes |
| `src/README.md` | architecture | yes |
| `tests/README.md` | test suite and gate methodology | yes |
| `BUGS.md` | known defects, ordered by what fixing them is worth | yes |
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
| `seepruning` | off | equal-nodes gate neutral: +2.2 [−7.2, +11.6]; needs a **timed** gate |

All three gates ran 2026-08-13 at 14 shards × 120 pairs (3 360 games each,
`-N 100000`). TT aging defaults on because it is a repair, not a feature — the
table was depth-preferred and ageless, so positions from moves already played
could only be displaced by something deeper still, and the live search got a
shrinking share of the table. The gate has now confirmed that default rather
than merely assuming it.

**`seepruning` was not rejected; it was measured with the wrong instrument.** It
cuts the most nodes of anything in Phase 3 (−41.1% at bench 6) and scored
neutral at equal nodes, which is what a change that buys *speed* per node rather
than *quality* per node looks like when both sides are paid the same nodes. Its
open gate is a timed one. Do not read +2.2 as "SEE pruning does nothing".

The bench signature is **1,465,771 nodes** at depth 6. Any change claiming to
preserve search behaviour must reproduce it exactly. It was 2,056,371 until
`seeordering` was turned on (2026-08-13); older documents quoting that figure
are describing the baseline of their day, not a regression.

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

---

## In flight

**The Lichess bot is running** (`./lichess/run.sh`), playing rated 900+10 games
against bots continuously — `allow_matchmaking: true` means it challenges when
idle rather than waiting. It is the source of the baseline above, and it keeps
moving that baseline while it runs. Stop it **between** games, never during one
(`BUGS.md` 7), and stop it entirely before any timed gate.

**No gate is running.** Three completed overnight on 2026-08-13 and are pooled
and recorded above; their logs are kept:

| directory | gate | pooled result |
|---|---|---|
| `shard-20260813-000634/` | `ttaging` | +11.5 [+3.2, +19.8] |
| `shard-20260813-015757/` | `seepruning` | +2.2 [−7.2, +11.6] |
| `shard-20260813-034736/` | `seeordering` | +25.6 [+16.1, +35.2] |

Re-pool any of them with `./tests/pool-shards.sh <dir>/`.

**Always pool before believing a shard.** Shard 1 of the `ttaging` gate on its
own read +13 with a CI spanning zero — "no difference demonstrated". Pooled over
all fourteen, the same experiment is +11.5 with the interval clear of zero. The
point estimate barely moved; the interval is what shrank. One shard is a
240-game match, and no 240-game match resolves a 10-Elo effect.

---

## Next, in order

1. **Give the search the game's move history** (`BUGS.md` 1). The engine cannot
   see repetitions that happened in the actual game, only ones it creates inside
   its own tree, and it has already drawn a won position against a 1404 because
   of it. Smallest fix on this list with a proven cost attached.
2. **Evaluation correctness** (`BUGS.md` 2, 3, 5 — `PLAN.md` 4.1-4.3), in that
   order: `tempoBonus` truncates the total, so it has to land before the −4 king
   safety asymmetry can be read cleanly. Add the mirror-symmetry check to
   `tests/evalref.cpp` as part of the king-safety fix.
3. **A timed gate for `seepruning`** (`PLAN.md` 3.2), on a quiet machine:
   ```bash
   ./tests/shard-gate.sh 14 120 -t 3000 --optA seepruning=on --optB seepruning=off
   ```
   This is the documented exception to gating on nodes. Its baseline is now the
   engine as shipped, with `seeordering` on — which is also the harder test for
   it, since ordering already removes some of the tree pruning would have cut.
   **Nothing else may run on the machine while it does**, including the Lichess
   bot: a timed match silently measures whatever else was competing for the CPU.
4. **Phase 3 remainder:** bound quiescence and delta pruning (3.1 — also
   `BUGS.md` 4), check extensions (3.3), futility/razoring (3.4), IID (3.5),
   retune LMR (3.6). 3.1 and 3.3 are the cheap ones and neither is implemented.

`BUGS.md` is the list to read before picking any of these up; it carries the
evidence, the file and line, and what each defect has actually cost.

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
- **`LICENSE` is empty.** No licence has been chosen; default copyright applies.
