#include "Features.h"
#include <cmath>
#include <cstring>
#include "Capture.h"

namespace {

int16_t clamp16(uint32_t v) { return static_cast<int16_t>(v > 32767u ? 32767u : v); }

// Remaining time in units of 100 ms, rounded up (0 only when nothing is left).
int16_t ceil100(uint32_t until, uint32_t now) { return until > now ? clamp16((until - now + 99u) / 100u) : 0; }

int nearestSquare(float v, int n) {
  int i = static_cast<int>(std::floor(v + 0.5f));
  if (i < 0) i = 0;
  if (i > n - 1) i = n - 1;
  return i;
}

}  // namespace

int actionIndex(int r, int c, int d, int dist) { return ((r * FEAT_N + c) * 4 + d) * ACTION_DISTS + (dist - 1); }

bool decodeAction(int a, int& r, int& c, int& d, int& dist) {
  if (a < 0 || a >= ACTION_STAY) return false;
  dist = a % ACTION_DISTS + 1;
  a /= ACTION_DISTS;
  d = a % 4;
  a /= 4;
  c = a % FEAT_N;
  r = a / FEAT_N;
  return true;
}

int actionOfMove(int r, int c, int tr, int tc) {
  int d, dist;
  if (tc == c && tr > r) {
    d = 0;
    dist = tr - r;
  } else if (tc == c && tr < r) {
    d = 1;
    dist = r - tr;
  } else if (tr == r && tc > c) {
    d = 2;
    dist = tc - c;
  } else if (tr == r && tc < c) {
    d = 3;
    dist = c - tc;
  } else {
    return -1;
  }
  if (r < 0 || r >= FEAT_N || c < 0 || c >= FEAT_N || dist > ACTION_DISTS) return -1;
  return actionIndex(r, c, d, dist);
}

namespace {

// Pieces in an RE edge group with exactly one empty neighbour left, on the board RE is judged on.
void markLastLiberty(const Game& g, int16_t* plane) {
  const int n = FEAT_N;
  const Board b = g.boardWithoutHeld();
  SquareSet seen;
  seen.clear();
  for (int sq = 0; sq < FEAT_SQUARES; sq++) {
    if (b.cells[sq] == EMPTY || seen.has(sq)) continue;
    EdgeGroup eg;
    if (!edgeGroupAt(b, sq / n, sq % n, eg)) continue;
    for (int q = 0; q < FEAT_SQUARES; q++) {
      if (!eg.cells.has(q)) continue;
      seen.set(q);
      if (eg.libCount == 1) plane[q] = 1;
    }
  }
}

}  // namespace

void buildFeatures(const Game& g, float headR, float headC, int speedScale100, int16_t planes[], int16_t scal[]) {
  std::memset(planes, 0, sizeof(int16_t) * FEAT_PLANES);
  const int n = FEAT_N;
  const Rules& rules = g.rules();
  const uint32_t clock = g.clockMs();
  int16_t* ch[FEAT_CHANNELS];
  for (int k = 0; k < FEAT_CHANNELS; k++) ch[k] = planes + k * FEAT_SQUARES;

  for (int i = 0; i < g.pieceCount(); i++) {
    const PieceView& p = g.pieces()[i];
    if (!p.alive) continue;
    const int sq = p.r * n + p.c;
    if (p.held) {
      ch[2][sq] = 1;
      ch[10][sq] = clamp16(clock > p.liftedAt ? (clock - p.liftedAt) / 100u : 0u);
      continue;
    }
    const bool machine = p.side == MACHINE;
    ch[machine ? 0 : 1][sq] = 1;
    ch[machine ? 5 : 6][sq] = ceil100(p.cooldownUntil, clock);
    if (rules.untouchedMs > 0) ch[machine ? 7 : 8][sq] = ceil100(p.movedAt + rules.untouchedMs, clock);
  }
  const Sq pend = g.pendingCell();
  if (pend.r >= 0) ch[3][pend.r * n + pend.c] = 1;
  if (g.hasLastPlace()) {
    const int16_t left = ceil100(g.lastPlaceMs() + rules.humanIntervalMs, clock);
    for (int sq = 0; sq < FEAT_SQUARES; sq++) ch[4][sq] = left;
  }
  ch[9][nearestSquare(headR, n) * n + nearestSquare(headC, n)] = 1;
  // The board RE is judged on: held origins and the pending target are empty squares.
  if (rules.edgeCapture) markLastLiberty(g, ch[11]);

  scal[0] = static_cast<int16_t>(g.captured(MACHINE));
  scal[1] = static_cast<int16_t>(g.captured(HUMAN));
  scal[2] = rules.timeLimitMs > 0 ? ceil100(rules.timeLimitMs, clock) : 0;
  scal[3] = g.phase() == Phase::Opening ? 1 : 0;
  scal[4] = g.openingDone(HUMAN) ? 1 : 0;
  scal[5] = static_cast<int16_t>(speedScale100);
}

int buildMask(const Game& g, uint8_t mask[]) {
  std::memset(mask, 0, ACTION_COUNT);
  int count = 0;
  const bool mustMove = g.phase() == Phase::Opening && !g.openingDone(MACHINE);
  if (!mustMove) {
    mask[ACTION_STAY] = 1;
    count++;
  }
  const Board b = g.boardForMachine();
  const int n = b.n;
  const uint32_t clock = g.clockMs();
  for (int i = 0; i < g.pieceCount(); i++) {
    const PieceView& q = g.pieces()[i];
    if (!q.alive || q.held || q.side != MACHINE || clock < q.cooldownUntil) continue;
    if (!b.inside(q.r, q.c) || b.at(q.r, q.c) != MACHINE) continue;
    int d[MAX_N * MAX_N];
    const int k = dests(b, q.r, q.c, d);
    for (int j = 0; j < k; j++) {
      const int tr = d[j] / n, tc = d[j] % n;
      if (g.rules().wallLimit && wallOnPath(b, MACHINE, q.r, q.c, tr, tc)) continue;
      const int a = actionOfMove(q.r, q.c, tr, tc);
      if (a < 0 || mask[a]) continue;
      mask[a] = 1;
      count++;
    }
  }
  return count;
}

AiMove moveOfAction(const Game& g, int action) {
  AiMove m{true, -1, -1, -1, 0.0f};
  int r, c, d, dist;
  if (!decodeAction(action, r, c, d, dist)) return m;
  for (int i = 0; i < g.pieceCount(); i++) {
    const PieceView& q = g.pieces()[i];
    if (q.alive && !q.held && q.side == MACHINE && q.r == r && q.c == c) {
      m.none = false;
      m.pieceId = q.id;
      m.toR = r + DIR_R[d] * dist;
      m.toC = c + DIR_C[d] * dist;
      return m;
    }
  }
  return m;
}

int actionOfAiMove(const Game& g, const AiMove& m) {
  if (m.none || m.pieceId < 1 || m.pieceId > g.pieceCount()) return ACTION_STAY;
  const PieceView& q = g.pieces()[m.pieceId - 1];
  const int a = actionOfMove(q.r, q.c, m.toR, m.toC);
  return a < 0 ? ACTION_STAY : a;
}

void buildHumanFeatures(const Game& g, float headR, float headC, const JobView& job, int speedScale100,
                        int16_t planes[], int16_t scal[]) {
  std::memset(planes, 0, sizeof(int16_t) * FEAT_PLANES);
  const int n = FEAT_N;
  const Rules& rules = g.rules();
  const uint32_t clock = g.clockMs();
  int16_t* ch[FEAT_CHANNELS];
  for (int k = 0; k < FEAT_CHANNELS; k++) ch[k] = planes + k * FEAT_SQUARES;

  for (int i = 0; i < g.pieceCount(); i++) {
    const PieceView& p = g.pieces()[i];
    if (!p.alive || p.held) continue;
    const int sq = p.r * n + p.c;
    const bool human = p.side == HUMAN;
    ch[human ? 0 : 1][sq] = 1;
    ch[human ? 6 : 5][sq] = ceil100(p.cooldownUntil, clock);
    if (rules.untouchedMs > 0) ch[human ? 8 : 7][sq] = ceil100(p.movedAt + rules.untouchedMs, clock);
  }
  if (job.active && !job.removing && job.pieceId >= 1 && job.pieceId <= g.pieceCount()) {
    const PieceView& q = g.pieces()[job.pieceId - 1];
    if (q.alive && q.r >= 0 && q.r < n && q.c >= 0 && q.c < n) ch[2][q.r * n + q.c] = 1;
    if (job.toR >= 0 && job.toR < n && job.toC >= 0 && job.toC < n) ch[10][job.toR * n + job.toC] = 1;
  }
  const Sq res = g.reservedCell();
  if (res.r >= 0 && res.r < n && res.c >= 0 && res.c < n) ch[3][res.r * n + res.c] = 1;
  if (g.hasLastPlace()) {
    const int16_t left = ceil100(g.lastPlaceMs() + rules.humanIntervalMs, clock);
    for (int sq = 0; sq < FEAT_SQUARES; sq++) ch[4][sq] = left;
  }
  ch[9][nearestSquare(headR, n) * n + nearestSquare(headC, n)] = 1;
  if (rules.edgeCapture) markLastLiberty(g, ch[11]);

  scal[0] = static_cast<int16_t>(g.captured(HUMAN));
  scal[1] = static_cast<int16_t>(g.captured(MACHINE));
  scal[2] = rules.timeLimitMs > 0 ? ceil100(rules.timeLimitMs, clock) : 0;
  scal[3] = g.phase() == Phase::Opening ? 1 : 0;
  scal[4] = g.openingDone(MACHINE) ? 1 : 0;
  scal[5] = static_cast<int16_t>(speedScale100);
}

int buildHumanMask(const Game& g, uint8_t mask[]) {
  std::memset(mask, 0, ACTION_COUNT);
  int count = 0;
  const bool mustMove = g.phase() == Phase::Opening && !g.openingDone(HUMAN);
  if (!mustMove) {
    mask[ACTION_STAY] = 1;
    count++;
  }
  const Board b = g.boardWithReserve();
  const int n = b.n;
  const uint32_t clock = g.clockMs();
  const bool opening = g.phase() == Phase::Opening;
  for (int i = 0; i < g.pieceCount(); i++) {
    const PieceView& q = g.pieces()[i];
    if (!q.alive || q.held || q.side != HUMAN) continue;
    // A cooldown does not hold a lift in Opening (Game).
    if (!opening && clock < q.cooldownUntil) continue;
    if (!b.inside(q.r, q.c) || b.at(q.r, q.c) != HUMAN) continue;
    int d[MAX_N * MAX_N];
    const int k = dests(b, q.r, q.c, d);
    for (int j = 0; j < k; j++) {
      const int tr = d[j] / n, tc = d[j] % n;
      if (makesWall(b, g.rules(), HUMAN, q.r, q.c, tr, tc)) continue;
      const int a = actionOfMove(q.r, q.c, tr, tc);
      if (a < 0 || mask[a]) continue;
      mask[a] = 1;
      count++;
    }
  }
  return count;
}

BotMove humanMoveOfAction(const Game& g, int action) {
  BotMove m{BotMove::Wait, -1, -1, -1};
  int r, c, d, dist;
  if (!decodeAction(action, r, c, d, dist)) return m;
  for (int i = 0; i < g.pieceCount(); i++) {
    const PieceView& q = g.pieces()[i];
    if (q.alive && !q.held && q.side == HUMAN && q.r == r && q.c == c) {
      m.kind = BotMove::Move;
      m.id = q.id;
      m.toR = r + DIR_R[d] * dist;
      m.toC = c + DIR_C[d] * dist;
      return m;
    }
  }
  return m;
}
