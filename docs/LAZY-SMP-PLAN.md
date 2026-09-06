# Lazy SMP — scope, phases and the gate problem — 2026-09-06

Eight cores sit idle on every move this engine makes. `ROADMAP.md` Phase 7 puts
Lazy SMP at +200-280 assuming ~16 threads, so **expect roughly half at 8**, and
less again on a laptop that thermally throttles. That prior is from general
engine practice and has to earn its number here, like everything else.

It is next because it is the only remaining item with a solid prior and **no
premise risk**: no corpus, no training, no novel failure mode. `NNUE-DECISION.md`
already recommended it first, and its addendum of 2026-09-06 strengthened that —
the NNUE premise got weaker, not stronger, when the hand-crafted evaluation
turned out to explain 94.5% of the labels a self-play corpus was built on.

---

## What was surveyed, and what it found

**The good news first: `g_evalCache` is already SMP-safe.** It stores
`lock = hash XOR score` and re-derives on read, so a torn read is detected
rather than believed. That was built because the GUI thread can call
`evaluatePosition()` during a search, and it happens to be exactly the scheme a
lock-free table needs. One fewer thing to build.

**The transposition table is not.** It has no lockless scheme at all; safety
today comes entirely from `ChessBotEngine::ttMutex` serialising whole searches.
A mutex on the hottest data structure in the program is not an option at 8
threads.

**`TTEntry` is 40 bytes**, which cannot be written atomically — and that, not
capacity, is why `tt-16byte` is a prerequisite.

---

## Phase 0 — ship `tt-16byte`

The branch exists, is verified, and is **41 commits behind main**. Its own commit
message parks it and says why it should be unparked here:

> *"Kept because a lock-free entry needs a packable one: this is Phase 0 of Lazy
> SMP if that is ever built, and it should ship there, on that argument, not on
> a strength claim it does not have."*

Read that carefully, because it is the trap this phase must avoid. That branch
measured **no strength gain**: quadrupling the table from 256MB moves 0.09% of
nodes, and old and new produce bit-identical trees at depth 12 on three
positions. The table is already past where capacity buys anything.

**So it ships on atomicity, not on speed, and the commit must say so.** 16 bytes
is one 128-bit atomic write and divides 64, so entries stop straddling cache
lines — at 40 bytes three in eight did. Both matter for concurrency and neither
is a strength claim.

**Cost, paid knowingly:** it moves the bench signature and makes every
historical node count incomparable. That is a real loss to a project whose
`GATES.md` is built on comparable node counts, and it is the reason the branch
was parked in the first place. It is worth paying only as part of this.

Work: rebase 41 commits, re-verify the move packing round-trip, `bench-regen`.

## Phase 1 — lockless TT

Hyatt's XOR trick, the same one `g_evalCache` already uses: store
`key XOR data` alongside `data`, and on probe recompute the XOR. A torn entry
fails its own checksum and reads as a miss. No locks, no atomics beyond
relaxed 64-bit loads and stores, and the failure mode is a wasted node rather
than a wrong move.

Generation-based ageing and `shouldReplace` stay as they are; racing writers
may clobber each other and that is acceptable — the entry that survives is
still a legitimate entry.

## Phase 2 — per-thread search state, the real work

This is the phase with the risk in it. The search carries a lot of global
mutable state, and `move_ordering.cpp` says so in its own comment: *"Not
synchronized: safe only because every search in the process runs under
ChessBotEngine::ttMutex."*

| state | today | needs |
|---|---|---|
| `g_moveOrderer` — killers, history, contHist, captHist | one global | **per thread** (~2.4MB each, 19MB at 8) |
| `g_searchNodes` | one global | per thread, summed for reporting |
| `g_corrHist` | one global | per thread (off by default, still allocated) |
| `g_nextTimeCheck` | one global | per thread — it is a per-thread amortiser |
| `g_outOfTime` | plain `bool` | **atomic**, written by whichever thread notices |
| `g_gameHistory`, `g_searchOptions`, `g_rootSeed`, deadlines | globals | shared **read-only**, written once before threads start |
| `pathHashes` | already a parameter | nothing — already per-thread |

The honest way to do this is a `SearchContext` struct passed down, rather than
`thread_local`: `thread_local` on a hot path costs an indirection per access on
some ABIs, and the profile that inlined `Piece::type()` for exactly that reason
(1.87 billion calls, ~21% of runtime) is a warning about paying per-access costs
in this search.

## Phase 3 — the thread pool

All threads run iterative deepening on the root against a shared TT; thread 0
owns the result. Divergence comes from TT races, and is usually helped along by
staggering start depths per thread. `Threads` is currently advertised
`min 1 max 1` and accepted-and-ignored in `uci.cpp` — it becomes real here.

---

## The gate problem, which is the hard part

**Lazy SMP cannot be gated at equal nodes.** The entire point is more nodes per
unit time, so a node budget divides out exactly what is being bought. This
needs `--tc`, and `shard-gate.sh` refuses to shard a timed gate for good
reason — shards would compete for the CPU and each would play a weaker engine
than it would alone.

So: **two processes, `Threads=1` against `Threads=8`, at a real clock, serial,
on an otherwise idle machine.** `tests/match --tc` already requires
`--engineA`/`--engineB` and refuses to combine `--tc` with `-N`.

**The cost is smaller than `HANDOFF.md`'s "~17 hours" suggests, because the
effect is large.** Resolving +100 Elo to a ±30 interval needs roughly 600 games,
not the 3 360 a 10-Elo question needs. At 10+0.1 and ~25s per game that is
**4-5 hours**, serial. If the true effect is nearer +40 it needs four times
that, so the honest plan is: run 600, and let the interval decide whether more
is warranted.

**The machine must be genuinely idle.** A timed gate is load-sensitive by
construction — that is `BUGS.md` 16 and the calibration in `MEASUREMENTS.md` —
and the 8-thread side is the one that suffers, which biases the result against
the change being tested. Bot down, no poker jobs, nothing else.

---

## Risks specific to this codebase

**A packing bug in the TT move would not error, it would silently degrade move
ordering.** The branch verified this by round-tripping 384 moves across nine
positions until all five flag values fired. Re-run that after the rebase.

**`generateLegalMoves` mutates the board it is given** and restores it. That is
fine per-thread — each thread owns its board — but it means no board may ever be
shared between threads, not even read-only.

**Determinism goes away.** Every gate and bench number this project has depends
on a search that reproduces from its seed. A multi-threaded search does not: TT
races make it non-deterministic by design. `tests/bench` and `tests/match -N`
must therefore keep running at `Threads=1`, and that constraint should be
enforced in code rather than remembered.

**The 2026-08-15 profile is the guide for where not to add cost.** `Piece::type()`
at 1.87 billion calls, `Move`'s default constructor at 116 million. An
indirection added on those paths costs more than a thread buys.
