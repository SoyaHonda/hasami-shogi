#pragma once
#include <cstdint>

struct Rules {
  int n = 7;
  int target = 4;
  bool diffOn = false;
  int diff = 2;
  uint32_t humanCooldownMs = 3000, machineCooldownMs = 1000, countdownMs = 3000, resumeCountdownMs = 1000;
  // Human pieces held at once (1 or 2; plan 2026-10-31 3.1 sets 1).
  int maxHeld = 1;
  // Least game-clock time between two human placements; an earlier one is held as Pending.
  uint32_t humanIntervalMs = 1000;
  // Longest a lifted piece may stay up (game clock); then it goes back to its square. 0 = no limit.
  uint32_t maxHoldMs = 1000;
  // Game length (game clock); then the side with more captures wins, equal is a draw. 0 = none.
  uint32_t timeLimitMs = 600000;

  // Rule candidates of plan 2026-11-06 3.1. All off is R0, the rules above. They apply to both
  // sides alike; the capture judgement is captures() of Capture.h.
  // RE: the official edge capture. A group of one side's pieces (4-connected) touching the
  // board's edge is captured when the other side arrives next to it and it has no empty
  // neighbour left.
  bool edgeCapture = false;
  // RW: no placement or machine move may fill a row with one side's pieces from edge to edge
  // (a wall), and the start countdown leads to Phase::Opening: each side moves one piece to
  // another square, then Play (the game clock stays stopped until then).
  bool wallLimit = false;
  // RU: a piece not moved to another square for this long (game clock) is captured when one
  // enemy piece arrives next to it. 0 = off; candidates 20000 and 40000.
  uint32_t untouchedMs = 0;
};

// The official rules (user decision 2026-11-12, plan 2026-11-06 3.1): RE + RW + RU40. The firmware,
// the web simulator (officialRules() of the WASM module) and the learning environment play them.
// Rules() itself stays R0, the rules the golden tables and most tests are written for.
inline Rules officialRules() {
  Rules r;
  r.edgeCapture = true;
  r.wallLimit = true;
  r.untouchedMs = 40000;
  return r;
}
