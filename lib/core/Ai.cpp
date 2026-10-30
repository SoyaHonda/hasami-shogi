#include "Ai.h"
#include <cmath>
#include <cstdlib>

// Port of think() from the original simulator (sim/hasami-sim.html). Everything is float and
// evaluated in the same order as the original so that every build picks the same move.

namespace {

struct Threat {
  int r, c;
  float w;
  bool held;
};

struct Ctx {
  int n;
  float aggr;
  Threat threats[2 * MAX_N];
  int threatCount;
};

// Most pieces the human piece at (r,c) can capture with one move on bb (bb is restored).
int bestCapture(Board& bb, int8_t side, int r, int c) {
  int d[MAX_N * MAX_N];
  int caps[MAX_N * MAX_N];
  const int k = dests(bb, r, c, d);
  int best = 0;
  for (int i = 0; i < k; i++) {
    bb.cells[d[i]] = side;
    const int n = capturesAt(bb, side, d[i] / bb.n, d[i] % bb.n, caps);
    bb.cells[d[i]] = EMPTY;
    if (n > best) best = n;
  }
  return best;
}

float danger(const Ctx& x, Board bb) {
  float mx = 0.0f, sum = 0.0f;
  for (int t = 0; t < x.threatCount; t++) {
    const Threat& h = x.threats[t];
    const int i = h.r * x.n + h.c;
    // A non-held threat whose piece is gone (captured by the move) no longer counts.
    if (!h.held && bb.cells[i] != HUMAN) continue;
    if (!h.held) bb.cells[i] = EMPTY;
    const int best = bestCapture(bb, HUMAN, h.r, h.c);
    if (!h.held) bb.cells[i] = HUMAN;
    const float v = static_cast<float>(best) * h.w;
    if (v > mx) mx = v;
    sum += v;
  }
  return mx + 0.25f * sum;
}

float pressure(const Ctx& x, Board bb) {
  int best = 0;
  for (int i = 0; i < x.n * x.n; i++) {
    if (bb.cells[i] != MACHINE) continue;
    bb.cells[i] = EMPTY;
    const int b = bestCapture(bb, MACHINE, i / x.n, i % x.n);
    if (b > best) best = b;
    bb.cells[i] = MACHINE;
  }
  return static_cast<float>(best);
}

float shape(const Ctx& x, const Board& bb) {
  int adj = 0;
  float adv = 0.0f;
  const float denom = static_cast<float>(x.n - 1);
  for (int i = 0; i < x.n * x.n; i++) {
    if (bb.cells[i] != MACHINE) continue;
    const int r = i / x.n, c = i % x.n;
    adv += static_cast<float>(r) / denom;
    for (int d = 0; d < 4; d++) {
      const int rr = r + DIR_R[d], cc = c + DIR_C[d];
      if (bb.inside(rr, cc) && bb.at(rr, cc) == HUMAN) adj++;
    }
  }
  return static_cast<float>(adj) * 8.0f + adv * 30.0f;
}

float evalBoard(const Ctx& x, const Board& bb) {
  return -danger(x, bb) * 80.0f * (1.0f - 0.5f * x.aggr) + pressure(x, bb) * (20.0f + 60.0f * x.aggr) +
         x.aggr * shape(x, bb);
}

}  // namespace

AiMove think(const Snapshot& s, const Params& p) {
  Ctx x;
  x.n = s.board.n;
  x.aggr = p.aggression;
  x.threatCount = 0;

  float wReady = 0.0f, wCool = 0.0f;
  switch (p.level) {
    case Level::Easy:
      break;
    case Level::Normal:
      wReady = 0.35f;
      break;
    case Level::Hard:
      wReady = 1.0f;
      wCool = 0.7f;
      break;
  }

  // Human threats, in id order.
  for (int i = 0; i < s.count; i++) {
    const PieceView& q = s.pieces[i];
    if (!q.alive || q.side != HUMAN) continue;
    if (q.held) {
      if (p.readLift) x.threats[x.threatCount++] = Threat{q.r, q.c, 1.6f, true};
      continue;
    }
    const uint32_t rem = q.cooldownUntil > s.clockMs ? q.cooldownUntil - s.clockMs : 0;
    if (rem == 0 && wReady > 0.0f) {
      x.threats[x.threatCount++] = Threat{q.r, q.c, wReady, false};
    } else if (rem > 0 && rem <= 1500 && wCool > 0.0f) {
      x.threats[x.threatCount++] = Threat{q.r, q.c, wCool, false};
    }
  }

  const Board& b = s.board;
  AiMove best{true, -1, -1, -1, evalBoard(x, b)};
  const float engageS = static_cast<float>(p.engageMs) / 1000.0f;

  for (int i = 0; i < s.count; i++) {
    const PieceView& q = s.pieces[i];
    if (!q.alive || q.held || q.side != MACHINE || s.clockMs < q.cooldownUntil) continue;
    int d[MAX_N * MAX_N];
    const int k = dests(b, q.r, q.c, d);
    for (int j = 0; j < k; j++) {
      const int tr = d[j] / x.n, tc = d[j] % x.n;
      Board b2 = b;
      b2.set(q.r, q.c, EMPTY);
      b2.set(tr, tc, MACHINE);
      int caps[MAX_N * MAX_N];
      const int nc = capturesAt(b2, MACHINE, tr, tc, caps);
      for (int m = 0; m < nc; m++) b2.cells[caps[m]] = EMPTY;
      const int dist = std::abs(tr - q.r) + std::abs(tc - q.c);
      const float dr = static_cast<float>(q.r) - s.headR, dc = static_cast<float>(q.c) - s.headC;
      const float tm = std::sqrt(dr * dr + dc * dc) / p.freeSpeed + engageS +
                       static_cast<float>(dist) / p.dragSpeed;
      const float sc = static_cast<float>(nc) * 100.0f + evalBoard(x, b2) - tm * 4.0f - 2.0f;
      if (sc > best.score + 0.01f) best = AiMove{false, q.id, tr, tc, sc};
    }
  }
  return best;
}
