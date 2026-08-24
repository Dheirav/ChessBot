# TODO — the work queue

Every open piece of work, with what blocks it and roughly what it costs. Kept
current; the last entry here was checked on **2026-08-16**, after §1 closed.

**This file deliberately does not explain anything.** The reasoning lives where
it was earned — `ROADMAP.md` for why the priorities are what they are, `BUGS.md`
for defects and their evidence, `REVIEW.md` for the review tool. Every item
below is a pointer plus a size, and duplicating the argument here is how two
copies of the same knowledge start to drift. Read `HANDOFF.md` first for state.

Sizes are honest rather than encouraging: **S** is under an hour, **M** is an
afternoon, **L** is a day or a night of machine time.

---

## Before touching anything

- **`pgrep -x chessbot`** — a PID means a rated game is live. `pgrep -f` matches
  the shell running the check and has produced false positives repeatedly
  (`BUGS.md` 9).
- **`./lichess/bot-stop.sh` does the stopping correctly, so do not do it by
  hand.** Run it with no arguments for a menu; the flags (`--games N`,
  `--minutes N`, `--at HH:MM`, `--now`, `--status`, `--quiet`) are for
  scripting. `--games 1` makes the game on the board now the last one — it
  signals *during* that game, which only works because the config sets
  `quit_after_all_games_finish: true`. The five rules in its header are each
  something that has been got wrong here at least once (`BUGS.md` 7 and 9).
- **Do not `make` the engine while the bot runs.** It relinks `./chessbot`, and
  the bot spawns a fresh engine per game, so the next rated game silently gets
  an ungated binary. `make review` and `make tests` do not touch it.
- **A bot started without `quit_after_all_games_finish` can only be stopped in
  a gap between games** (`BUGS.md` 7): a bare SIGINT does *not* finish the game
  in progress. The option is read at startup, so `bot-stop.sh --status` reports
  which kind of stop the *running* process supports, and editing `config.yml`
  does not change that until the next restart.
- **Gate discipline:** `-N 100000`, never `--sprt` under sharding, A and B differ
  in exactly one thing, pool before believing. `tests/README.md` has the rest.

---

## 1. ~~The measurement everything else is waiting on~~ — **done 2026-08-16**

**Does self-play Elo mean anything here? Yes, this time.** 31 games on the
post-6.2 build: **31-0-0**, avg centipawn loss 20.9 → 16.3, **10 blunders → 0**,
and the 2100-2300 band 31.8% → 100%. `HANDOFF.md`'s third measurement is the
write-up; do not re-derive it here.

Kept as an entry because the *habit* is the item, not this run. Re-run it after
anything that ships:

```bash
./tools/archive-profile.py --compare <stamp> --jobs 4 --nice 19 \
    --work ~/reviews/profile-cache
```

The stamp is Lichess **UTC**. `--jobs 4 --nice 19` is what makes it safe to run
while the bot is playing — its games are on a real clock and starving them
corrupts the evidence being collected. `--work` must be a persistent directory:
the default is a fresh `mktemp` that a reboot discards, and re-analysing the
whole archive costs ~7 minutes where re-analysing only the new games costs
seconds.

**Still write down what it says even when it says nothing.** A null result is
the more valuable one and the easiest to quietly not record.

**What it did not answer:** no 2300+ opponent has been met since 6.2 landed
(0-0-3 before it), so the ceiling itself is still unmeasured.

---

## 1b. The machine, which currently outranks the engine — 2026-08-24

**Find out why nps collapsed** (`BUGS.md` 16). The engine ran at ~a third
speed for twenty-one hours across 08-22/08-23 — median 257 knps on 08-23
against a normal 700-880 — and recovered exactly at a `wsl --shutdown`. That
restart is a workaround; the cause is unknown. **Size: M. Blocked by nothing.**

Worth doing before any item in §2, on arithmetic rather than taste: at
doubling-is-70-Elo a 3x slowdown is ~100 Elo sustained, and `razoring`, the
largest search win ever gated here, is +39.1. A recurrence costs more than the
whole queue below is worth.

Two things fall out of it and are cheap:

- **Watch nps in live games.** Nothing in the suite does. One grep over the
  bot's logs found this; `BUGS.md` 16 carries it. Wire it into whatever gets
  looked at routinely.
- **Re-run the `razoring` + `revfutility` field test.** The 34-game window in
  `MEASUREMENTS.md`'s fifth reading is void — it was taken inside the slow
  window. The sixth reading says so; the measurement is still owed.

Also owed: the **Stockfish move-quality profile** for the current archive. The
08-24 attempt was launched at `--jobs 8` with no `nice` while the bot was
playing and had to be killed. §1's invocation, `--jobs 4 --nice 19`, is not
advice.

---

## 2. Engine

| item | size | blocked by | note |
|---|---|---|---|
| **Time management, part 1: spend the budget** (`BUGS.md` 11) | — | **done 2026-08-16** | `softtime` on by default. **+78 Elo [+40, +117]**, 200 games at `--tc 30+0.33`, zero forfeits. |
| **Time management, part 2: size the budget** (`BUGS.md` 11) | — | **gated 2026-08-17, stays off** | `timealloc` measured **+14 Elo [−22, +50]** over 200 games. Spans zero. Kept as a toggle like `seepruning`. Resolving +14 needs ~4× the games and `--tc` cannot be sharded, so ~20 h — do not reopen without that budget. Key finding: after `softtime`, every allocation formula converges to ~97% of the clock, so this can only redistribute, not add. |
| **Re-measure the clock on Lichess** | — | **done 2026-08-21** | 19 rated games at 900+10: **zero time forfeits, lowest clock in any game 41 s**, and that only in two 90-plus-move games against 2100+ opposition. Everything else finished with two minutes or more in hand. The 535-seconds-unused pattern is gone at the control that matters. |
| **King safety** (`ROADMAP.md` 6.4) | — | **closed, negative — reopened once and closed again** | Four gates, 10 080 games: +1.3, +2.2, −11.0, −216.9. The gauntlet this row demanded was built on 2026-08-21 (`tests/gauntlet.sh`) and the term was rebuilt with it: **−33.1 Elo [−43.2, −23.0]** over 3 360 games, `shard-20260821-220901/`. Self-play saw the harm immediately, so the "self-play may be unable to see it" caveat did **not** hold here. `BUGS.md` 13 has the failed hypotheses and the position that defeated both. |
| **6.2 remainder: the general tune** | L | nothing | Texel-style, over an evaluation whose largest term has stopped shouting. Position set exists: the game archive plus `evalref`'s 23 603 positions. |
| **Re-run 6.1** | S | 6.2 landing | A corrected `threats` may expose blind spots it was masking. `ROADMAP.md` 6.3. |
| **`deltapruning`** | — | **closed 2026-08-21, stays off** | Re-measured on the current build: **+0.9 Elo [−5.8, +7.7]** over 3 360 games, `shard-20260821-112153/`. The 2026-08-14 +7.1 was a pre-6.2 engine and was deliberately **not** pooled with it. `SEED_BASE` now exists on `shard-gate.sh` for the general case. |
| **PLAN 3.5 — IID** | — | **gated 2026-08-18, stays off** | **−0.1 Elo [−4.9, +4.7]** over 3 360 games — the tightest null in the file. Iterative deepening already fills the table at the depths IID fires at. Toggle and reasoning on `search.hpp`. |
| **PLAN 3.4 — futility / razoring** | — | **both accepted 2026-08-22** | `razoring` **+39.1 [+28.4, +49.9]**, then `revfutility` **+18.4 [+7.8, +29.1]** measured *on top of* it. Both on by default; bench 1,086,693 → **793,823** (−27.0%) with no best move changed on any of the nine positions. The margins were sized off the evaluation's measured error (median 125cp, 90th 407cp) rather than textbook 100–150 — 3.1 lost 50 Elo making the same bet inside the evaluation's own noise. |
| **Sweep the razoring margin** | M | nothing | 500cp was a first guess off the error distribution and returned +39.1. 3.1's finding is that this bet swings **57 Elo on the constant alone**, so 350 and 700 against 500 may be worth more than the next feature. |
| **PLAN 3.6 — retune LMR** | L | 6.2 | Deliberately last, against a search that has stopped changing shape. |
| **PLAN 5.4 — lazy evaluation** | M | nothing | Evaluation is ~33% of search time, but speed-per-node has returned ~zero Elo three times. Do it for the wall clock, not for a gate. |
| **`BUGS.md` 6 — deterministic play** | M | nothing | Results against a repeated opponent are correlated, so the archive is worth less than its game count and every accuracy figure inherits that. A seeded random tiebreak among near-equal root moves; weigh against gate reproducibility before committing. |

### The road to 3000 — `ROADMAP.md` Phase 7

2130 today, so 870 Elo away. The Elo column is a **prior from general engine
practice, not measured here**; the middle column is measured.

| item | measured state today | prior | size |
|---|---|---|---|
| **Lazy SMP** | single-threaded, on a 16-core machine | +200–280 | L |
| **NNUE evaluation** | hand-crafted, material-dominated; ~290cp of addressable static error | +200–400 | XL |
| **Search pruning suite** — futility, razoring, LMP, singular, probcut | **none of them exist** | +150–300 | M each |
| **Ponder** | absent; `config.yml` says the engine has no support | +30–50 | M |
| **Opening book + Syzygy 3-4-5** | neither exists | +20–50 | S |
| **Raw speed** | ~800 knps; bitboards measured at only 1.02× | +50–100 | L |

Order by Elo per effort: pruning suite → Lazy SMP → NNUE → the afternoon jobs.
PLAN 3.4 is the first pruning item and is already in the table above.

---

## 2b. Instruments added 2026-08-16

Built for `ROADMAP.md` 6.4 and kept because they outlived it.

| thing | what it is for |
|---|---|
| `tests/engine` | the UCI engine with no GUI. **Gate evaluation changes with this, never `./chessbot`** — building that relinks the binary the bot spawns per game, and 24 processes named `chessbot` make `pgrep -x chessbot` lie about whether a rated game is live (`BUGS.md` 9). |
| per-side `setoption` forwarding in `tests/match` | two-binary mode could drive two *builds* but could not tell a running engine which configuration to be, so an eval toggle needed two hand-maintained binaries. It now sends every option explicitly and aborts if one is not acknowledged. |
| `tests/evalref --opt <name>=<on\|off>` | skips the reference comparison (which describes the defaults) and still checks the two invariants that hold under any options — chiefly mirror symmetry, which cannot be regenerated into agreement. |
| `tests/evaltrace <game.pgn> [plies]` | replays a game and prints **this** engine's evaluation term by term. `tools/review` says what a stronger engine thinks; this is the only way to catch a term saying *nothing*. |
| `tests/evalerror` + `tools/eval-corpus.py` | scores the evaluation against Stockfish over positions it misjudged in real games, in a second. **It anti-predicted a gate on 2026-08-21** — 37cp better on the corpus, −33 Elo in games — so it finds error and kills hypotheses cheaply, and never decides anything. |
| `tests/gauntlet.sh` | plays a fixed external opponent instead of ourselves. For changes whose value depends on the opponent's behaviour, and for absolute-strength checks self-play cannot give. Shard it (`shard-gate.sh` with `--na/--nb`) or it will not resolve anything. |
| `tests/gate-progress.sh [dir] [--once]` | live bar over a running gate, read from the shard logs. A gate is hours that print nothing until they are over, which is how one that died in its first minute goes unnoticed. |
| `tests/gate-pause.sh stop\|start\|status` | freeze and thaw a gate to get the machine back. Safe **only** because the budget is nodes and `waitFor` has no deadline; both would stop being true for `-t` or `--tc`. |

**Do not gate while the bot plays rated games.** The 2026-08-16 loss was played
under a 12-shard gate and its average centipawn loss was 34.5 against a
post-6.2 norm of 15.7 for that opponent band. The gate is immune to load — node
budgets are — but the bot is not, and rated games cannot be re-run.

---

## 3. Review tool

| item | size | note |
|---|---|---|
| ~~**Archive index view**~~ | — | **done 2026-08-17**. Sortable table with date, opponent, rating, result, opening, accuracy, cp loss, blunders and clock used/left, plus the band table. Built from the cached records plus PGN tags via `--pgn-dir`, so it costs no analysis. |
| ~~**Opening names**~~ | — | **done 2026-08-17** with the index — the `ECO` and `Opening` tags are read straight from the export. |
| **Phantom-loss floor** | M | ~3% of criticised moves are moves the engine itself would have played, from successive searches disagreeing across a shared transposition table (`REVIEW.md`). Clearing the table between positions would cost analysis time; measure before choosing. |
| **`Miss` label** | S | Needs mate scores preserved rather than clamped to ±1000. |
| **`Brilliant` label** | M | Needs "material sacrificed and still best". |
| **`Great` label** | M | Needs MultiPV; `UciEngine` does not request it. |
| **`review-archive.sh` default path** | S | Hardcodes the Lichess archive; `ARCHIVE=` overrides. Fine for the bot, awkward for anyone reviewing their own games. |

**Explicitly not doing** (`REVIEW.md`): reimplementing Stockfish's analysis, a
tablebase or cloud-eval integration, an opening book for the `Book` label.

---

## 4. Housekeeping

- **`CLAUDE.md` is gitignored** (`.gitignore:4`), so its bench signature lives
  only on this machine. A fresh clone gets whatever was last committed. Current
  signature: **793,823** nodes at depth 6, verified 2026-08-23. It carried the
  pre-razoring 1,086,693 for a day after that shipped — a stale signature here
  reads to the next session as a broken search, which is the expensive direction
  for this particular number to drift in.
- **`PLAN.md` status lines are current, its body is dated.** `BACKLOG.md` is a
  frozen 2026-08-10 archive — §2.1 is wrong, §4 and §7 still good.
- **`~/reviews/` is generated output** and outside the repo. `records/` there is
  the per-game cache; it re-reviews itself when the record format changes.
