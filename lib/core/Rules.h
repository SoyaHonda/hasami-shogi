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
};
