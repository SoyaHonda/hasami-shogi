#include "Capture.h"

namespace {

bool onEdge(int n, int r, int c) { return r == 0 || c == 0 || r == n - 1 || c == n - 1; }

void add(int out[], int& k, SquareSet& seen, int sq) {
  if (seen.has(sq)) return;
  seen.set(sq);
  out[k++] = sq;
}

}  // namespace

bool edgeGroupAt(const Board& b, int r, int c, EdgeGroup& out) {
  if (!b.inside(r, c) || b.at(r, c) == EMPTY) return false;
  const int n = b.n;
  const int8_t side = b.at(r, c);
  out.side = side;
  out.cells.clear();
  out.size = 0;
  out.first = r * n + c;
  bool edge = false;
  // Flood fill with an explicit stack (a group holds at most n*n squares).
  uint8_t stack[MAX_N * MAX_N];
  int top = 0;
  stack[top++] = static_cast<uint8_t>(r * n + c);
  out.cells.set(r * n + c);
  while (top > 0) {
    const int sq = stack[--top];
    out.size++;
    if (sq < out.first) out.first = sq;
    const int sr = sq / n, sc = sq % n;
    edge = edge || onEdge(n, sr, sc);
    for (int d = 0; d < 4; d++) {
      const int rr = sr + DIR_R[d], cc = sc + DIR_C[d];
      if (!b.inside(rr, cc)) continue;
      const int i = rr * n + cc;
      if (b.cells[i] != side || out.cells.has(i)) continue;
      out.cells.set(i);
      stack[top++] = static_cast<uint8_t>(i);
    }
  }
  if (!edge) return false;
  // Empty neighbours in square order.
  out.libCount = 0;
  SquareSet libs;
  libs.clear();
  for (int sq = 0; sq < n * n; sq++) {
    if (!out.cells.has(sq)) continue;
    const int sr = sq / n, sc = sq % n;
    for (int d = 0; d < 4; d++) {
      const int rr = sr + DIR_R[d], cc = sc + DIR_C[d];
      if (b.inside(rr, cc) && b.at(rr, cc) == EMPTY) libs.set(rr * n + cc);
    }
  }
  for (int sq = 0; sq < n * n; sq++) {
    if (!libs.has(sq)) continue;
    if (out.libCount < EDGE_GROUP_LIBS) out.libs[out.libCount] = sq;
    out.libCount++;
  }
  return true;
}

int captures(const Board& b, const Rules& rules, int8_t side, int r, int c, int out[], const SquareSet* stale) {
  const int8_t enemy = side == HUMAN ? MACHINE : HUMAN;
  const int n = b.n;
  int k = 0;
  SquareSet seen;
  seen.clear();
  // Custodian (R0).
  for (int d = 0; d < 4; d++) {
    int run[MAX_N];
    int len = 0;
    int rr = r + DIR_R[d], cc = c + DIR_C[d];
    while (b.inside(rr, cc)) {
      const int8_t v = b.at(rr, cc);
      if (v == enemy) {
        run[len++] = rr * n + cc;
      } else {
        if (v == side && len > 0)
          for (int i = 0; i < len; i++) add(out, k, seen, run[i]);
        break;
      }
      rr += DIR_R[d];
      cc += DIR_C[d];
    }
  }
  // RE: the enemy groups the arrival has newly come next to.
  if (rules.edgeCapture) {
    SquareSet checked;
    checked.clear();
    for (int d = 0; d < 4; d++) {
      const int rr = r + DIR_R[d], cc = c + DIR_C[d];
      if (!b.inside(rr, cc) || b.at(rr, cc) != enemy || checked.has(rr * n + cc)) continue;
      EdgeGroup g;
      if (!edgeGroupAt(b, rr, cc, g)) {
        checked.set(rr * n + cc);
        continue;
      }
      for (int i = 0; i < 3; i++) checked.w[i] |= g.cells.w[i];
      if (g.libCount > 0) continue;
      for (int sq = 0; sq < n * n; sq++)
        if (g.cells.has(sq)) add(out, k, seen, sq);
    }
  }
  // RU: untouched enemy pieces next to the arrival.
  if (rules.untouchedMs > 0 && stale) {
    for (int d = 0; d < 4; d++) {
      const int rr = r + DIR_R[d], cc = c + DIR_C[d];
      if (b.inside(rr, cc) && b.at(rr, cc) == enemy && stale->has(rr * n + cc)) add(out, k, seen, rr * n + cc);
    }
  }
  return k;
}

int capturesAt(const Board& b, int8_t side, int r, int c, int out[]) { return captures(b, Rules(), side, r, c, out); }

bool completesWall(const Board& b, int8_t side, int fr, [[maybe_unused]] int fc, int tr, int tc) {
  // A piece from the row itself leaves its square empty; one from another row (or off the
  // board) does not touch row tr.
  if (!b.inside(tr, tc) || fr == tr) return false;
  for (int c = 0; c < b.n; c++)
    if (c != tc && b.at(tr, c) != side) return false;
  return true;
}

bool makesWall(const Board& b, const Rules& rules, int8_t side, int fr, int fc, int tr, int tc) {
  return rules.wallLimit && completesWall(b, side, fr, fc, tr, tc);
}
