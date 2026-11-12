#include "Path.h"
#include <cmath>
#include <cstdlib>

int pickReserveSlot(const bool used[], int n, int preferC) {
  int best = -1, bestD = 0;
  for (int c = 0; c < n; c++) {
    if (used[c]) continue;
    int d = std::abs(c - preferC);
    if (best < 0 || d < bestD) {
      best = c;
      bestD = d;
    }
  }
  return best;
}

bool removalPath(const Board& b, Sq from, int toRow, const bool reserveUsed[], const bool blocked[],
                 Sq out[], int& len, int& slotC) {
  const int n = b.n;
  // Board squares get indices 0..n*n-1; reserve squares get n*n + column.
  int parent[MAX_N * MAX_N + MAX_N];
  bool seen[MAX_N * MAX_N + MAX_N] = {false};
  int queue[MAX_N * MAX_N];
  int head = 0, tail = 0;

  const int start = from.r * n + from.c;
  seen[start] = true;
  parent[start] = -1;
  queue[tail++] = start;

  while (head < tail) {
    // Process one BFS layer, collecting reserve columns reached from it.
    int layerEnd = tail;
    bool reached[MAX_N];
    bool any = false;
    for (int c = 0; c < n; c++) reached[c] = false;
    for (; head < layerEnd; head++) {
      int cur = queue[head];
      int r = cur / n, c = cur % n;
      for (int d = 0; d < 4; d++) {
        int rr = r + DIR_R[d], cc = c + DIR_C[d];
        if (rr == toRow && cc >= 0 && cc < n) {
          int idx = n * n + cc;
          if (!reserveUsed[cc] && !seen[idx]) {
            seen[idx] = true;
            parent[idx] = cur;
            reached[cc] = true;
            any = true;
          }
          continue;
        }
        if (!b.inside(rr, cc)) continue;
        int idx = rr * n + cc;
        if (seen[idx] || blocked[idx] || b.at(rr, cc) != EMPTY) continue;
        seen[idx] = true;
        parent[idx] = cur;
        queue[tail++] = idx;
      }
    }
    if (any) {
      bool notReached[MAX_N];
      for (int c = 0; c < n; c++) notReached[c] = !reached[c];
      slotC = pickReserveSlot(notReached, n, from.c);
      // Reconstruct backwards, then reverse.
      int tmp[REMOVAL_MAX_POINTS + 1];
      int k = 0;
      for (int v = n * n + slotC; v != -1; v = parent[v]) tmp[k++] = v;
      len = k;
      for (int i = 0; i < k; i++) {
        int v = tmp[k - 1 - i];
        if (v >= n * n) {
          out[i] = Sq{static_cast<int8_t>(toRow), static_cast<int8_t>(v - n * n)};
        } else {
          out[i] = Sq{static_cast<int8_t>(v / n), static_cast<int8_t>(v % n)};
        }
      }
      return true;
    }
  }
  len = 0;
  slotC = -1;
  return false;
}

// Corner coordinate (x.5) of target square `t` nearest to corner coordinate `from`.
static float nearestCornerOf(int t, float from) {
  float lo = static_cast<float>(t) - 0.5f, hi = static_cast<float>(t) + 0.5f;
  return std::fabs(from - lo) <= std::fabs(from - hi) ? lo : hi;
}

int travelPath(float fromR, float fromC, int toR, int toC, float outR[], float outC[]) {
  const float tR = static_cast<float>(toR), tC = static_cast<float>(toC);
  int k = 0;
  outR[k] = fromR;
  outC[k] = fromC;
  k++;
  if (fromR == tR && fromC == tC) return k;

  auto push = [&](float r, float c) {
    if (outR[k - 1] == r && outC[k - 1] == c) return;
    outR[k] = r;
    outC[k] = c;
    k++;
  };

  const float r0 = std::floor(fromR) + 0.5f, c0 = std::floor(fromC) + 0.5f;
  const float r1 = nearestCornerOf(toR, r0), c1 = nearestCornerOf(toC, c0);
  push(r0, c0);
  push(r1, c0);
  push(r1, c1);
  push(tR, tC);
  return k;
}

float polylineLength(const float r[], const float c[], int count) {
  float total = 0.0f;
  for (int i = 1; i < count; i++) {
    float dr = r[i] - r[i - 1], dc = c[i] - c[i - 1];
    total += std::sqrt(dr * dr + dc * dc);
  }
  return total;
}
