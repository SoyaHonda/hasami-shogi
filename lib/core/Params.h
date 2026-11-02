#pragma once
#include <cstdint>
#include "Types.h"

// Values to be updated from measurements. Distances are in squares, speeds in squares/second.
// Defaults match the S object of the original simulator (sim/hasami-sim.html).
struct Params {
  float freeSpeed = 4.0f, dragSpeed = 1.5f;
  uint32_t engageMs = 200, reactMs = 250;
  Level level = Level::Normal;
  float aggression = 0.5f;
  bool readLift = true;
  // Human timing used by the read (plan 2026-10-31 3.3), in ms: lift and put down, lift until the
  // square is free, put down a piece already lifted, notice a machine move.
  uint32_t humanMoveMs = 500, humanLiftMs = 150, humanPlaceMs = 350, humanReactMs = 300;
};
