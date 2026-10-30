#include "BoardWatcher.h"

BoardWatcher::BoardWatcher(int n) : n_(n) { reset(); }

void BoardWatcher::reset() {
  for (int i = 0; i < MAX_N * MAX_N; i++) {
    liftReported_[i] = false;
    placePending_[i] = false;
    placeSince_[i] = 0;
    placeReported_[i] = false;
    touchReported_[i] = false;
  }
}

int BoardWatcher::update(const int8_t sensed[], const int8_t expected[],
                         const bool machineCells[], uint32_t now, WatchEvent out[]) {
  int k = 0;
  for (int r = 0; r < n_; r++) {
    for (int c = 0; c < n_; c++) {
      int i = r * n_ + c;
      int8_t s = sensed[i];
      int8_t e = expected[i];

      // Lift.
      if (e == HUMAN && s != HUMAN) {
        if (!liftReported_[i]) {
          out[k++] = WatchEvent{WatchEvent::Lift, r, c};
          liftReported_[i] = true;
        }
      } else {
        liftReported_[i] = false;
      }

      // Place.
      if (e == EMPTY && s == HUMAN) {
        if (!placePending_[i]) {
          placePending_[i] = true;
          placeSince_[i] = now;
        }
        if (!placeReported_[i] && now - placeSince_[i] >= PLACE_MS) {
          out[k++] = WatchEvent{WatchEvent::Place, r, c};
          placeReported_[i] = true;
        }
      } else {
        placePending_[i] = false;
        placeReported_[i] = false;
      }

      // MachineTouch.
      if (!machineCells[i] && ((s == MACHINE) != (e == MACHINE))) {
        if (!touchReported_[i]) {
          out[k++] = WatchEvent{WatchEvent::MachineTouch, r, c};
          touchReported_[i] = true;
        }
      } else {
        touchReported_[i] = false;
      }
    }
  }
  return k;
}

int BoardWatcher::diffInitial(const int8_t sensed[], const int8_t expected[], int out[]) const {
  int k = 0;
  for (int i = 0; i < n_ * n_; i++)
    if (sensed[i] != expected[i]) out[k++] = i;
  return k;
}
