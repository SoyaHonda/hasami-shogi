#include "HallClassify.h"

Cell classify(int raw, Cell prev, const HallThresholds& t) {
  int d = raw - t.center;
  int mag = d < 0 ? -d : d;
  Cell side = ((d > 0) == t.southHigh) ? MACHINE : HUMAN;
  if (mag >= t.enter) return side;
  if (prev != EMPTY && prev == side && mag >= t.exit) return prev;
  return EMPTY;
}
