#pragma once
#include <cstdint>
#include "Types.h"

// XY stage with the electromagnet under the board. Coordinates are in squares (row, column);
// rows -1 and n are the reserve rows. Commands start a motion; update() advances it and idle()
// reports when it has finished.
class IGantry {
 public:
  virtual ~IGantry() = default;
  // Free (unloaded) move along the travelPath polyline to the centre of (r, c).
  virtual void travelTo(float r, float c) = 0;
  // Pull to the centre of the neighbouring square (r, c).
  virtual void dragTo(int r, int c) = 0;
  virtual void magnet(MagnetMode m) = 0;
  virtual bool idle() const = 0;
  virtual float headR() const = 0;
  virtual float headC() const = 0;
  virtual void update(uint32_t now) = 0;
};
