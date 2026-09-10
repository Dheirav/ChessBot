#pragma once
//
// Phase B3 of the bitboard replacement (docs/BITBOARD-REPLACEMENT.md).
//
// Static exchange evaluation on a Position. Same question and same answer as
// see.cpp: play the capture, let both sides recapture with their cheapest
// available attacker, and report what the mover ends up ahead by in centipawns.
//
// The mailbox version finds the next attacker by walking rays outward from the
// target square and treating spent pieces as empty, which is how it discovers
// x-rays. Here attackersTo() already takes a caller-supplied occupancy, so the
// same discovery is one call with the spent pieces cleared: remove the bishop
// that just captured and the queen behind it appears in the very next lookup.
// That is the reason the bitboard module was written in the first place.
//
#include "bb_position.hpp"

int bbSee(const Position& pos, const BitboardMove& move);
