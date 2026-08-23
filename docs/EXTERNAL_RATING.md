# External rating: getting ChessBot onto CCRL

## Why

Every strength number in this repo is self-referential. SPRT gates measure a change
against the previous version of itself, which says a heuristic helped but says nothing
about where the engine actually sits. Lichess fixed half of that: `Crimsy_Bot` holds a
non-provisional **2130 rapid (rd ±45) over 218 rated games** as of 2026-08-23.
`HANDOFF.md` carries the current reading and `MEASUREMENTS.md` every past one —
do not restate either here, since this document is read months after it is written.

CCRL is the other half, and it is the one that matters for a rating people can compare.
It is the standard engine-vs-engine list, run on fixed hardware, and being on it puts a
number next to every other engine tested under identical conditions.

## What CCRL requires

- Testing runs on a benchmark Intel i7-4770k, with Stockfish used to calibrate time
  controls, generic opening books capped at 12 moves, and **ponder off**.
- **150 games minimum** before a version is ranked. Only the best version that clears
  150 games gets a rank.
- Submission goes through the CCRL forum. They only accept people known to the computer
  chess community or vouched for by someone who is, so this is not an upload form. Expect
  this step to take the longest.

Start here: https://computerchess.org.uk/ and https://www.chessprogramming.org/CCRL

## What has to happen in this repo first

1. **A tagged release with binaries.** CCRL testers need something they can download and
   run, not a Makefile. Cut a versioned release (`chessbot-1.0`) and attach binaries.
2. **A Windows build.** Most CCRL testing is on Windows. Right now this builds on Linux
   against SFML. The engine core does not need SFML — only the GUI does — so produce a
   **GUI-free UCI-only binary** and cross-compile that with mingw-w64. This is the single
   biggest blocker and it is worth doing regardless, since it halves what a tester has to
   install.
3. **Verify behaviour at CCRL time controls.** The list runs 40/15 (40 moves in 15
   minutes) and a blitz list at 2+1. `lichess/README.md` already notes the time manager
   divides remaining clock by an assumed 30 moves with no pondering, and that bullet is
   where it flags rather than where it plays badly. Confirm 2+1 does not flag before
   submitting to the blitz list.
4. ~~**Check the advertised UCI option set.**~~ **Done 2026-08-23.** `Threads`
   (spin, min and max both 1 — honest about being single-threaded), `Ponder`
   (check, false) and `Move Overhead` (spin, default 100 ms) are now advertised.
   The first two are accepted and ignored, because announcing an option and then
   rejecting it is worse than never announcing it: the GUI has no way to learn
   its configuration did not take. **`Move Overhead` is real**, not a no-op — it
   comes off the clock before the budget is computed. `SyzygyPath` is still
   absent and should stay absent while there are no tablebases behind it.
5. **Decide the shipped defaults.** `SeeOrdering` won its gate at +25.6 Elo and defaults
   on; `SeePruning` is still commented out in the Lichess config. Whatever is submitted
   should be the configuration that actually measured strongest, and it should be frozen
   for the whole 150-game run.

## Done when

A CCRL list shows a rating for a named ChessBot version over 150+ games. At that point
the resume line changes from a Lichess rating to a CCRL rating, which is the stronger
claim.

## Watch out for

- Do not submit while the evaluation rewrite in `ROADMAP.md` is in flight. CCRL ranks the
  best version with 150+ games, so submitting a version you are about to obsolete wastes
  the slot and their testers' time.
- The Lichess rating moves (2198-provisional, then 2065, 2162, 2190, now 2130 after
  the opponent cap went to 2500). Any figure quoted outside the repo should be a floor,
  not today's reading.
