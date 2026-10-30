#pragma once
#include <cstdint>

struct Rules {
  int n = 7;
  int target = 4;
  bool diffOn = false;
  int diff = 2;
  uint32_t humanCooldownMs = 3000, machineCooldownMs = 1000, countdownMs = 3000, resumeCountdownMs = 1000;
};
