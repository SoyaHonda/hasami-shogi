#pragma once
#include <cstdint>
#include "Types.h"

// at: for Place, the time the piece was first seen on the square (the placement time passed to
// Game::onHumanPlace as placedAt); for Lift and MachineTouch, the update time.
struct WatchEvent {
  enum Kind { Lift, Place, MachineTouch } kind;
  int r, c;
  uint32_t at;
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

  // Rebuilds the reported marks from the boards, when the game resumes after a board-mismatch
  // pause (plan 2026-10-31 3.2). Events reported while the game was not playing were ignored by
  // it, so every current Lift/Place difference is reported again: a Lift at the next update, a
  // Place PLACE_MS after the next update. MachineTouch differences stay reported (no repeat).
  void resync(const int8_t sensed[], const int8_t expected[]);

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
