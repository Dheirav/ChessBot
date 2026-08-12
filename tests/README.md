# Tests

Two different jobs live here, and confusing them is how a chess engine ends up
with a green suite and a weaker engine.

- **Regression tests** answer *did I break something?* They are exact, fast, and
  run in CI on every push and pull request.
- **The match harness** answers *did I make it stronger?* No unit test can
  answer that, and no amount of green CI substitutes for it.

```bash
make tests        # build everything here
make test-perft   # ...or any single target; see the table below
```

## Regression tests

| target | what it guards | how it fails |
|---|---|---|
| `test-perft` | move generation, against published perft counts | exact — one wrong or missing move changes a leaf count |
| `test-bitboard` | 107,648 exhaustive magic lookups, 765,696 `isSquareAttacked` comparisons against the mailbox engine over 5,982 positions, pin detection, and perft through the bitboard generator | exact |
| `test-see` | static exchange evaluation, against hand-computed positions | exact |
| `test-evalref` | every evaluation term over 23,603 positions | diff against a stored reference |
| `test-bench` | the search's node-count signature at depth 6 | diff against a stored signature |
| `test-gamestate` | a finished game refuses moves, and terminal state is not sticky across a new game | exact |
| `test-timecontrol` | a search given a budget returns inside it | exact |
| `test-uci` | the UCI protocol, driven as an external tool would | exact |
| `test-guiinput` | selection, click-to-move, dragging, promotion-dialog hitboxes — headless | exact |
| `test-pgn` | SAN export, disambiguation, numbering, whole documents | exact |

A few of these are worth explaining, because their value is not obvious:

**Magic bitboards fail silently.** A wrong magic produces a plausible attack set
for most blocker configurations and a wrong one for a few, which shows up as a
lost game weeks later. Nothing short of exhaustive verification — every square,
every blocker subset — is worth anything, so that is what `test-bitboard` does.

**`test-uci` catches a class of bug invisible to everything else here.** A
protocol mistake lets the engine play perfectly through its own GUI while being
undrivable by any external tool. It needs `./chessbot`, since UCI is a mode of
the main binary rather than a separate one.

**`test-pgn` exists because SAN is only unambiguous if the writer does the
disambiguation work.** A viewer handed `Nf3` where it needed `Nbd2` replays a
different game than the one that was played.

**`test-guiinput` pins the promotion dialog's hitboxes to the geometry it is
drawn with.** The two were computed separately once and drifted apart the moment
the side panel widened the window.

### The two reference-diff tests

`test-evalref` and `test-bench` fail on *any* change to what they cover. That is
the point: they turn "I did not mean to change the evaluation" and "this refactor
preserves search behaviour" into claims that get checked rather than assumed.

When the change was intended, review what they report and regenerate:

```bash
make evalref-regen    # after reviewing the reported term changes
make bench-regen      # after reviewing the new node counts
```

Regenerating without reading the diff throws away the only thing these tests do.
In particular, any change claiming to preserve search behaviour must reproduce
the bench signature *exactly* — not approximately.

## The match harness

`tests/match.cpp` plays two search configurations against each other. It is the
only thing here that measures strength.

```bash
make test-match                                   # short smoke match
./tests/match -n 100 -N 100000 --optA lmr=on --optB lmr=off
./tests/match -n 100 -t 1000 --sprt
```

Key options — the full list is in the header of `match.cpp`:

| flag | meaning |
|---|---|
| `-n <pairs>` | game pairs; each pair plays one opening from both sides |
| `-N <nodes>` | per-move node budget for both sides |
| `-t <ms>` | per-move time budget for both sides |
| `-d <depth>` | fixed depth for both sides |
| `-s <seed>` | opening-line seed |
| `--optA/--optB` | per-side heuristics, e.g. `--optA seepruning=on` |
| `--sprt` | stop as soon as the result is conclusive |

### Why matches are node-limited, not time-limited

A millisecond is worth whatever the machine happens to have spare. Two
configurations given the same milliseconds are only comparable if they get the
same share of the CPU for the whole match — which stops being true the moment
anything else runs, and is untrue in a different way on every machine. The gate
that prompted this rule spent its first hours competing with a job pinning
fifteen cores and its last hours on an idle box: one command, two different time
controls, one meaningless pooled result.

A node budget is spent identically wherever it runs. The same seed replays the
same games move for move, and shards can run in parallel without each one
changing the others' effective time control.

It costs one thing worth stating plainly: **a change whose value is speed per
node rather than quality per node is invisible to a node-limited match.** A
faster evaluation makes no difference at fixed nodes. Gate those on the clock
(`-t`) instead.

### Why fixed-depth matches mislead

Fixed depth asks "what do these cost in accuracy at equal depth?" — not "are
they worth it?", which is a question about equal time. The search heuristics are
1.31x faster at depth 4 and 31x faster at depth 9, so a shallow fixed-depth
match charges them their full accuracy cost while giving them almost none of
their benefit. A −30 Elo figure in `BACKLOG.md` was produced exactly this way,
and is uninterpretable for the additional reason that it never recorded its
depth.

### Games are played in pairs

Each pair plays the same opening line twice with the colours swapped. White has
a real advantage, so unpaired games measure the luck of the colour draw
alongside the change under test. Pairing cancels it.

The pair, not the game, is therefore the unit of observation. Both games of a
pair share an opening, so their results are correlated, and scoring them
separately would count that shared noise twice. The SPRT scores each pair once
across its five possible outcomes — lost both, through won both — which is the
pentanomial model. Both games of a pair are always played; stopping between them
would leave the colour bias uncancelled in the final sample.

### Reading the result

A fixed-size match reports a 95% confidence interval. **If that interval spans
zero, nothing was demonstrated in either direction** — at realistic draw rates
that takes many hundreds of games. `--sprt` instead stops the moment the
evidence is conclusive either way, which usually costs a fraction of the games
and fails a bad change fast.

## Running gates

A *gate* is a match a feature must win before it ships. `tests/match` is
single-threaded, so it uses one core of however many the machine has; the
scripts here use the rest.

```bash
./tests/run-gates.sh [pairs-per-shard] [shards]     # all outstanding gates, unattended
./tests/shard-gate.sh 14 60 -N 100000 --optA seepruning=on --optB seepruning=off
./tests/pool-shards.sh shard-20260813-000634/       # score an interrupted run by hand
```

`run-gates.sh` runs gates sequentially rather than in parallel — each one
saturates the machine on its own, so running them together only makes each
finish later.

Two rules that sharding depends on:

- **Shard only node-limited gates.** Under a time budget the shards compete for
  the CPU, so each plays a weaker engine than it would alone, and the pooled
  result describes a time control nobody chose.
- **Never pass `--sprt` to a shard.** A stopping rule applied per shard and then
  pooled is not a valid test — each shard stops on its own favourable noise.
  Fixed N per shard, pooled afterwards, is valid.

Gate logs (`gate-*.log`, `shard-*/`) are gitignored: they are tee output, often
still being written.

## What CI does and does not cover

CI runs every regression test above, plus a two-pair match as a smoke test —
that is a check that the engine plays a legal game end to end, **not** a
strength measurement. Real matches take hours and are run deliberately, not on
every push.

And note what self-play cannot tell you at all: it only ever measures a change
against the previous version of itself, so it cannot detect that the whole
engine has a blind spot. That is what the Lichess rating in
[../lichess/README.md](../lichess/README.md) is for — an independent measurement.
