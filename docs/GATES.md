# Gates — every match this engine has been measured by

**Append-only.** A row is added when a gate finishes and is never edited
afterwards, because the value of this file is that it records what was measured
at the time rather than what is currently believed.

**This is the only place these results live in full.** Elsewhere — `HANDOFF.md`,
`TODO.md`, `ROADMAP.md` — a verdict is stated once, next to the thing it
decides, and links here for the number. That rule exists because on 2026-08-22
an audit found one verdict restated in ten files and four of twelve queue rows
pointing at work already finished: a number with ten homes has ten chances to
go stale, and a stale number is indistinguishable from a true one to the next
reader.

**The living verdict for a toggle is on the toggle**, in `search.hpp` or
`evaluation.cpp`. That is the one record that cannot drift from the code,
and on 2026-08-22 it was right when three documents were wrong.

Re-pool any of these with `./tests/pool-shards.sh <dir>/`.

## Rules these numbers depend on

- **Gate on nodes, not milliseconds** (`-N 100000` is the standing budget). A
  timed match is not reproducible from its seed and depends on machine load.
  Use `-t` or `--tc` only when the change's value is speed or clock management.
- **Never `--sprt` under sharding** — each shard stops on its own favourable
  noise.
- **A and B differ in exactly one thing**, or the result is not about that
  thing.
- **Pool before believing.** One 240-game shard cannot resolve a 10-Elo effect;
  the `ttaging` gate read +13 with an interval spanning zero on shard 1 and
  +11.5 clear of zero over all fourteen.
- **Do not pool across builds.** The 2026-08-14 `deltapruning` run and the
  2026-08-21 one measure different engines and were deliberately kept apart.
- **Run a null control when a result is surprising.** The +121.2 was five times
  anything previously measured here, so the harness was pointed at the baseline
  from two paths: 1 120 games, exactly 50.00%.

- **Do not let the machine sleep during a `--tc` gate.** The first `softtime`
  attempt was abandoned six games in for that reason. Under WSL2 the process
  clock does not advance while Windows sleeps, so `ps` etime *understates* the
  gap while file timestamps do not, and the only symptom was a progress bar
  reporting an impossible elapsed figure. Wall-clock measurement is precisely
  what a suspend corrupts, and a timed gate is the one built on it. The
  accepted run went 17 hours unbroken.

## Results

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
| `shard-20260821-112153/` | `deltapruning` re-measured on the current build | **+0.9 [−5.8, +7.7]** — closed, stays off |
| `shard-20260821-220901/` | king exposure 100% + king danger 300%, two binaries | **−33.1 [−43.2, −23.0]** — rejected |
