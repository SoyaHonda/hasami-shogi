#pragma once
#include <stdint.h>
#include "HallClassify.h"
#include "Pins.h"
#include "Types.h"

// Scans the pins::HALL_N x pins::HALL_N Hall sensors through four CD74HC4067
// multiplexers (shared select lines, one ADC1 input each) at 100 Hz.
// Each reading goes through classify(); a square takes a new value only when
// two scans in a row agree. While the magnet is energized, squares within
// HOLD_RADIUS of the head keep their previous value (the head's own field
// would otherwise read as a piece).
class HallScanner {
 public:
  static constexpr int N = pins::HALL_N;
  static constexpr int CELLS = pins::HALL_CELLS;
  static constexpr uint32_t PERIOD_MS = 10;       // 100 Hz
  static constexpr uint32_t SETTLE_US = 20;       // after switching the mux channel
  static constexpr float HOLD_RADIUS = 0.75f;     // squares
  static constexpr int CAL_SAMPLES = 32;

  // Configures the select lines and the ADC (12-bit).
  void begin();
  // Runs one full scan when PERIOD_MS has passed since the last one.
  // Returns true when a scan ran.
  bool update(uint32_t now);
  // Runs one full scan now.
  void scan();
  void setHold(bool magnetOn, float headR, float headC);
  // Debounced classification, row-major (index r*N+c), values of Cell.
  const int8_t* cells() const { return cells_; }
  // Latest raw ADC reading of (r, c); -1 when out of range.
  int raw(int r, int c) const;
  // With no pieces on the board, measures every square's no-magnet reading
  // (CAL_SAMPLES averaged) and stores it as that square's
  // HallThresholds::center. Resets the classification to EMPTY. Blocks for a
  // few tens of milliseconds.
  void calibrate();
  const HallThresholds& thresholds(int r, int c) const { return thr_[r * N + c]; }

 private:
  void selectChannel(int ch);
  bool held(int idx) const;

  HallThresholds thr_[CELLS];
  int raw_[CELLS] = {};
  int8_t cells_[CELLS] = {};
  int8_t cand_[CELLS] = {};
  // [mux][channel] -> square index, or -1 when unused.
  int8_t cellAt_[4][16];
  uint32_t lastScan_ = 0;
  bool scanned_ = false;
  bool holdOn_ = false;
  float holdR_ = 0.0f, holdC_ = 0.0f;
};
