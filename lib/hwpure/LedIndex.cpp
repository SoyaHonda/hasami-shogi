#include "LedIndex.h"

int ledIndex(int r, int c, int n) {
  return r * n + ((r % 2 == 0) ? c : (n - 1 - c));
}
