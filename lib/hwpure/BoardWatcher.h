#pragma once
#include <cstdint>
#include "Types.h"

struct WatchEvent {
  enum Kind { Lift, Place, MachineTouch } kind;
  int r, c;
};

// A cell can yield at most two events per update (Lift + MachineTouch).
constexpr int MAX_WATCH_EVENTS = 2 * MAX_N * MAX_N;

// Turns changes of the classified sensor board into events.
// Boards are row-major arrays of Cell values, index r*n+c.
class BoardWatcher {
 public:
  static constexpr uint32_t PLACE_MS = 200;

  explicit BoardWatcher(int n = 7);
  void reset();
  int n() const { return n_; }

  // sensed: classified board. expected: the board Game believes in (held
  // pieces removed). machineCells: squares the machine is working on now.
  // Writes events in row-major order to out (capacity MAX_WATCH_EVENTS)
  // and returns their count.
  //  - Lift: a human piece in expected is no longer sensed (once).
  //  - Place: N sensed on an expected-empty square for PLACE_MS (once);
  //    a square left earlier yields nothing, so slid-over squares are ignored.
  //  - MachineTouch: outside machineCells, S presence differs from expected
  //    (once per square until it matches again).
  int update(const int8_t sensed[], const int8_t expected[], const bool machineCells[],
             uint32_t now, WatchEvent out[]);

  // Squares (r*n+c) where sensed differs from the initial layout `expected`.
  int diffInitial(const int8_t sensed[], const int8_t expected[], int out[]) const;

 private:
  int n_;
  bool liftReported_[MAX_N * MAX_N];
  bool placePending_[MAX_N * MAX_N];
  uint32_t placeSince_[MAX_N * MAX_N];
  bool placeReported_[MAX_N * MAX_N];
  bool touchReported_[MAX_N * MAX_N];
};
