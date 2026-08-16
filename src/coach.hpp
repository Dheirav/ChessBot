#pragma once

// Play-along mode: the engine advises you in a game it is not playing.
//
// The engine had two ways to be used and neither fits a game happening
// somewhere else. The GUI plays *against* you; UCI expects another program to
// drive it. If the board is on a table, or in a browser tab, or across a room,
// there was nothing.
//
// This takes the moves as they are played, in the notation a person writes
// them in, and answers with what it would play. Returns a process exit code.
//
//   humanIsWhite  which side the advice is for
//   thinkMs       wall-clock per suggestion
int coachLoop(bool humanIsWhite, long thinkMs);
