# Handoff — 2026-08-13

Current state, what is in flight, and what to pick up. This is the file to read
first; it is meant to be rewritten as state changes, unlike `BACKLOG.md`, which
is a frozen archive of the 2026-08-10 profiling session.

| document | what it is | current? |
|---|---|---|
| `HANDOFF.md` | this file — where things stand | **yes, keep it that way** |
| `README.md` | features, UCI, build, controls | yes |
| `src/README.md` | architecture | yes |
| `tests/README.md` | test suite and gate methodology | yes |
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
| null-move pruning, LMR, aspiration windows | **on** | accepted by gate 1.5 |
| TT aging | **on** | gate running now — see below |
| `seeordering` | off | awaiting gate |
| `seepruning` | off | awaiting gate |

TT aging defaults on because it is a repair, not a feature: the table was
depth-preferred and ageless, so positions from moves already played could only
be displaced by something deeper still, and the live search got a shrinking
share of the table. The others default off until a gate accepts them.

The bench signature is **2,056,371 nodes** at depth 6. Any change claiming to
preserve search behaviour must reproduce it exactly.

---

## In flight

**The `ttaging` gate is running.** Started 2026-08-13 00:06, 14 shards × 120
pairs (3,360 games), roughly 110 minutes.

```
./tests/shard-gate.sh 14 120 -N 100000 --optA ttaging=on --optB ttaging=off
```

Logs are in `shard-20260813-000634/`. When it finishes:

```bash
./tests/pool-shards.sh shard-20260813-000634/
```

Then record the pooled result in `PLAN.md` 3.7 and in the table above. If it
comes back negative, the flag is the way to revert — `ttaging` is a toggle
precisely so the repair can be measured rather than assumed.

Note the gate is node-budgeted, so it is immune to whatever else was running on
the machine while it ran. That is the whole reason for `-N`.

---

## Next, in order

1. **Pool and record the `ttaging` result.** Ten minutes of work; do it before
   starting anything else, or the logs get confusing.
2. **The two SEE gates** (`PLAN.md` 3.2). Run one at a time, each is hours:
   ```bash
   ./tests/shard-gate.sh 14 60 -N 100000 --optA seepruning=on  --optB seepruning=off
   ./tests/shard-gate.sh 14 60 -N 100000 --optA seeordering=on --optB seeordering=off
   ```
   Both have been attempted before and produced nothing usable — the first was
   timed rather than node-budgeted and ran half its length against a job pinning
   fifteen cores. Expect a modest effect (+40–60 Elo plausible) and therefore
   many games; a slow LLR drift is a real result, not a failure.
3. **Phase 3 remainder:** bound quiescence and delta pruning (3.1), check
   extensions (3.3), futility/razoring (3.4), IID (3.5), retune LMR (3.6).
   3.1 and 3.3 are the cheap ones and neither is implemented yet.
4. **Phase 4** (evaluation correctness) has a known concrete bug waiting: king
   safety is **−4** in the mirror-symmetric starting position, where every term
   must be 0. See `PLAN.md` 4.2.

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
- **`test-evalref` and `test-bench` fail on any change they cover, by design.**
  Review the diff, then `make evalref-regen` / `make bench-regen`. Regenerating
  without reading the diff throws away the only thing they do.
- **`LICENSE` is empty.** No licence has been chosen; default copyright applies.
