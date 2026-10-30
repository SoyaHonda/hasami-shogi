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

int capturesAt(const Board& b, int8_t side, int r, int c, int out[]) {
  const int8_t enemy = side == HUMAN ? MACHINE : HUMAN;
  int k = 0;
  for (int d = 0; d < 4; d++) {
    int run[MAX_N];
    int len = 0;
    int rr = r + DIR_R[d], cc = c + DIR_C[d];
    while (b.inside(rr, cc)) {
      int8_t v = b.at(rr, cc);
      if (v == enemy) {
        run[len++] = rr * b.n + cc;
      } else {
        if (v == side && len > 0)
          for (int i = 0; i < len; i++) out[k++] = run[i];
        break;
      }
      rr += DIR_R[d];
      cc += DIR_C[d];
    }
  }
  return k;
}
