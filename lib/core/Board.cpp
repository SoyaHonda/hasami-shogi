#include "Board.h"

int dests(const Board& b, int r, int c, int out[]) {
  int k = 0;
  for (int d = 0; d < 4; d++) {
    int rr = r + DIR_R[d], cc = c + DIR_C[d];
    while (b.inside(rr, cc) && b.at(rr, cc) == EMPTY) {
      out[k++] = rr * b.n + cc;
      rr += DIR_R[d];
      cc += DIR_C[d];
    }
  }
  return k;
}
