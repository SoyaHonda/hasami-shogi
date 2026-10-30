#pragma once
#include "Types.h"

// Hall-sensor thresholds for one square, in raw ADC counts.
// Defaults are provisional (12-bit ADC midpoint); measure them on the
// one-square prototype and replace.
struct HallThresholds {
  int center = 2048;     // reading with no magnet
  int enter = 300;       // |raw - center| at or above this: a piece is present
  int exit = 150;        // a detected piece is kept until |raw - center| drops below this
  bool southHigh = true; // true: S (machine) reads above center, N (human) below
};

// Classify one reading as EMPTY, HUMAN (N pole) or MACHINE (S pole), with
// hysteresis: a piece already detected in prev is kept while the reading
// stays on the same side of center and at least `exit` away from it.
Cell classify(int raw, Cell prev, const HallThresholds& t);
