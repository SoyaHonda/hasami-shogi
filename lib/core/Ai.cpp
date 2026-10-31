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

// Whether the piece at (r,c) reaches the empty square (tr,tc) in one move on bb.
bool reaches(const Board& bb, int r, int c, int tr, int tc) {
  if ((r == tr) == (c == tc) || bb.at(tr, tc) != EMPTY) return false;
  const int dr = tr > r ? 1 : tr < r ? -1 : 0, dc = tc > c ? 1 : tc < c ? -1 : 0;
  for (int rr = r + dr, cc = c + dc; rr != tr || cc != tc; rr += dr, cc += dc)
    if (bb.at(rr, cc) != EMPTY) return false;
  return true;
}

// Strongest threat other than `skip` that reaches (tr,tc) in one move on bb (0 if none).
float reachWeight(const Ctx& x, const Board& bb, int tr, int tc, int skip) {
  float w = 0.0f;
  for (int t = 0; t < x.threatCount; t++) {
    const Threat& h = x.threats[t];
    if (t == skip || (!h.held && bb.cells[h.r * x.n + h.c] != HUMAN)) continue;
    if (h.w > w && reaches(bb, h.r, h.c, tr, tc)) w = h.w;
  }
  return w;
}

// Captures the human can make with two quick moves of two different pieces, which one-move
// danger misses: both ends of a machine line empty and each reachable by its own piece, or a
// machine line already sitting between two human pieces (one steps away, another steps in).
// The human's cooldowns are per piece, so two pieces can move back to back faster than the
// machine can react with its single head.
float danger2(const Ctx& x, Board bb) {
  float mx = 0.0f, sum = 0.0f;
  for (int i = 0; i < x.n * x.n; i++) {
    if (bb.cells[i] != MACHINE) continue;
    const int r = i / x.n, c = i % x.n;
    for (int d = 0; d < 4; d += 2) {  // down (rows) and right (columns)
      const int dr = DIR_R[d], dc = DIR_C[d];
      if (bb.inside(r - dr, c - dc) && bb.at(r - dr, c - dc) == MACHINE) continue;  // not the line's start
      int len = 1;
      while (bb.inside(r + dr * len, c + dc * len) && bb.at(r + dr * len, c + dc * len) == MACHINE) len++;
      const int ar = r - dr, ac = c - dc, br = r + dr * len, bc = c + dc * len;
      if (!bb.inside(ar, ac) || !bb.inside(br, bc)) continue;  // a line reaching the edge is safe
      const int8_t a = bb.at(ar, ac), b = bb.at(br, bc);
      float w = 0.0f;
      if (a == EMPTY && b == EMPTY) {
        for (int t = 0; t < x.threatCount; t++) {
          const Threat& h = x.threats[t];
          if (!h.held && bb.cells[h.r * x.n + h.c] != HUMAN) continue;
          if (!reaches(bb, h.r, h.c, ar, ac)) continue;
          const float wb = reachWeight(x, bb, br, bc, t);
          const float v = h.w < wb ? h.w : wb;
          if (v > w) w = v;
        }
      } else if (a == HUMAN && b == HUMAN) {
        for (int e = 0; e < 2; e++) {
          const int sr = e == 0 ? ar : br, sc = e == 0 ? ac : bc;
          // The piece that steps away must be free to move now.
          int self = -1;
          for (int t = 0; t < x.threatCount; t++)
            if (!x.threats[t].held && x.threats[t].r == sr && x.threats[t].c == sc) self = t;
          if (self < 0) continue;
          bb.set(sr, sc, EMPTY);
          const float wi = reachWeight(x, bb, sr, sc, self);
          bb.set(sr, sc, HUMAN);
          const float v = x.threats[self].w < wi ? x.threats[self].w : wi;
          if (v > w) w = v;
        }
      }
      const float v = static_cast<float>(len) * w * 0.8f;
      if (v > mx) mx = v;
      sum += v;
    }
  }
  return mx + 0.25f * sum;
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
  const float two = danger2(x, bb);
  return (two > mx ? two : mx) + 0.25f * sum;
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
    } else if (rem > 0 && wCool > 0.0f) {
      // The single head may be busy for a while, so a piece still cooling down counts too,
      // less the longer it has to wait.
      x.threats[x.threatCount++] = Threat{q.r, q.c, rem <= 1500 ? wCool : 0.5f * wCool, false};
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
