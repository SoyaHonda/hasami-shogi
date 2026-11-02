#include "RaceTime.h"
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace {

float fmax2(float a, float b) { return a > b ? a : b; }
float fmin2(float a, float b) { return a < b ? a : b; }
float clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }
// t >= need, where an INF t always holds (check INF before comparing).
bool atLeast(float t, float need) { return raceInf(t) || t >= need; }

// Index of the alive human piece standing on (r,c) (the pending piece on its target), or -1.
int humanAt(const RaceState& st, int r, int c) {
  for (int i = 0; i < st.hCount; i++) {
    const RaceHuman& h = st.h[i];
    if (h.alive && !h.held && h.r == r && h.c == c) return i;
  }
  return -1;
}

// Whether a piece at (r,c) reaches the empty square (tr,tc) in one straight move on b.
bool reachesStraight(const Board& b, int r, int c, int tr, int tc) {
  if ((r == tr) == (c == tc) || b.at(tr, tc) != EMPTY) return false;
  const int dr = tr > r ? 1 : tr < r ? -1 : 0, dc = tc > c ? 1 : tc < c ? -1 : 0;
  for (int rr = r + dr, cc = c + dc; rr != tr || cc != tc; rr += dr, cc += dc)
    if (b.at(rr, cc) != EMPTY) return false;
  return true;
}

// Reach of human piece i on st.board (3.10): one move = destinations from its square; two moves =
// destinations from each of those with the piece moved there. The reserved square is never a
// target (nor a first stop).
void buildReach(RaceState& st, int i) {
  RaceHuman& h = st.h[i];
  h.reach1.clear();
  h.reach2.clear();
  if (!h.alive) return;
  Board b = st.board;
  const int n = b.n;
  int d[MAX_N * MAX_N];
  const int k = dests(b, h.r, h.c, d);
  if (!h.held) b.set(h.r, h.c, EMPTY);
  for (int j = 0; j < k; j++) {
    if (d[j] == st.reserved) continue;
    h.reach1.set(d[j]);
    b.cells[d[j]] = HUMAN;
    int d2[MAX_N * MAX_N];
    const int k2 = dests(b, d[j] / n, d[j] % n, d2);
    for (int m = 0; m < k2; m++)
      if (d2[m] != st.reserved) h.reach2.set(d2[m]);
    b.cells[d[j]] = EMPTY;
  }
}

// Updates alive from the board (a captured piece is gone).
void refreshAlive(RaceState& st) {
  for (int i = 0; i < st.hCount; i++) {
    RaceHuman& h = st.h[i];
    if (h.alive && !h.held && st.board.at(h.r, h.c) != HUMAN) {
      h.alive = false;
      h.reach1.clear();
      h.reach2.clear();
    }
  }
}

// When human piece i can be lifted (not for a held piece).
float liftAt(const RaceState& st, const RaceHuman& h) { return fmax2(h.ready, st.firstLift); }

// H3 and H4 of 3.5: the human piece vi on one end steps away; another piece g (not on the other
// end, square ob) comes into its square, or vi itself comes back after its cooldown.
float stepAwayTime(const RaceState& st, const RaceTimes& t, int vi, int ob) {
  const RaceHuman& v = st.h[vi];
  if (v.held || v.pending || !v.reach1.any()) return RACE_INF;
  const float tv = fmax2(liftAt(st, v) + t.humanMove, st.firstPlace);
  float best = tv + t.humanCooldown + t.humanMove;  // H4
  Board b = st.board;
  b.set(v.r, v.c, EMPTY);
  for (int g = 0; g < st.hCount; g++) {
    const RaceHuman& h = st.h[g];
    if (g == vi || g == ob || !h.alive || h.held) continue;
    if (!reachesStraight(b, h.r, h.c, v.r, v.c)) continue;
    const float tg = fmax2(fmax2(tv + t.humanInterval, tv + t.humanMove), liftAt(st, h) + t.humanMove);  // H3
    best = fmin2(best, tg);
  }
  return best;
}

// H2 of 3.5 in the order: first piece to square a, second to square b.
float twoPieces(const RaceState& st, const RaceTimes& t, int a, int b) {
  float best = RACE_INF;
  for (int i1 = 0; i1 < st.hCount; i1++) {
    const float e = raceHumanPlace(st, t, i1, a);
    if (raceInf(e)) continue;
    const float t1 = fmax2(e, st.firstPlace);
    for (int i2 = 0; i2 < st.hCount; i2++) {
      const RaceHuman& h = st.h[i2];
      // A held piece is placed first (nothing else can be lifted while it is up).
      if (i2 == i1 || !h.alive || h.held || !h.reach1.has(b)) continue;
      const float t2 = fmax2(fmax2(t1 + t.humanInterval, t1 + t.humanMove), liftAt(st, h) + t.humanMove);
      best = fmin2(best, t2);
    }
  }
  return best;
}

// Machine board: blocked squares count as occupied.
Board machineBoard(const RaceState& st) {
  Board b = st.board;
  for (int i = 0; i < b.n * b.n; i++)
    if (st.blocked[i] && b.cells[i] == EMPTY) b.cells[i] = HUMAN;
  return b;
}

float travelMs(const RaceTimes& t, int r, int c, float headR, float headC) {
  const float dr = static_cast<float>(r) - headR, dc = static_cast<float>(c) - headC;
  return std::sqrt(dr * dr + dc * dc) / t.freeSpeed * 1000.0f;
}

float dragMs(const RaceTimes& t, int r, int c, int tr, int tc) {
  return static_cast<float>(std::abs(tr - r) + std::abs(tc - c)) / t.dragSpeed * 1000.0f;
}

// Earliest arrival of the machine piece at (r,c) on target (tr,tc) of mb (3.8): one move, or two
// moves of the same piece with its cooldown between. Head free at hf at (hr,hc).
float machineFill(const RaceState& st, const RaceTimes& t, const Board& mb, int r, int c, int tr, int tc, float hf,
                  float hr, float hc, bool twoMoves) {
  const int n = mb.n;
  const float start = fmax2(st.mReady[r * n + c], hf) + travelMs(t, r, c, hr, hc) + t.engage;
  float best = RACE_INF;
  if (reachesStraight(mb, r, c, tr, tc)) best = start + dragMs(t, r, c, tr, tc);
  if (!twoMoves) return best;
  Board b = mb;
  int d[MAX_N * MAX_N];
  const int k = dests(b, r, c, d);
  b.set(r, c, EMPTY);
  for (int j = 0; j < k; j++) {
    const int xr = d[j] / n, xc = d[j] % n;
    if (xr == tr && xc == tc) continue;
    b.cells[d[j]] = MACHINE;
    if (reachesStraight(b, xr, xc, tr, tc)) {
      const float a1 = start + dragMs(t, r, c, xr, xc);
      const float a2 = a1 + t.machineCooldown + t.engage + dragMs(t, xr, xc, tr, tc);
      best = fmin2(best, a2);
    }
    b.cells[d[j]] = EMPTY;
  }
  return best;
}

// Escape moves of one machine piece (3.6): destination, what it leaves for the human, drag time.
struct Escape {
  int dr, dc;
  float thDest, thRest, drag;
};

constexpr int MAX_ESCAPES = MAX_N * 2 * (MAX_N - 1);

struct Guard {
  float save;  // S of the guarding piece
  float arrival;
  int pr, pc, dr, dc;
};

// Escapes of the machine pieces in the threatened lines, per square.
struct EscapeTable {
  int start[MAX_N * MAX_N];
  int count[MAX_N * MAX_N];
  Escape e[MAX_ESCAPES];
  int used;
};

void addEscapes(const RaceState& st, const RaceTimes& t, const Board& mb, EscapeTable& et, int r, int c) {
  const int n = mb.n, i = r * n + c;
  if (et.count[i] >= 0) return;
  et.start[i] = et.used;
  et.count[i] = 0;
  int d[MAX_N * MAX_N];
  const int k = dests(mb, r, c, d);
  for (int j = 0; j < k && et.used < MAX_ESCAPES; j++) {
    Escape& x = et.e[et.used];
    x.dr = d[j] / n;
    x.dc = d[j] % n;
    raceDestTimes(st, t, r, c, x.dr, x.dc, &x.thDest, &x.thRest);
    x.drag = dragMs(t, r, c, x.dr, x.dc);
    et.used++;
    et.count[i]++;
  }
}

// save(L) with the head free at hf at (hr,hc): the earliest S over the pieces of L with a safe
// destination; the guarding move is the one arriving first.
Guard saveLine(const RaceState& st, const RaceTimes& t, const EscapeTable& et, const RaceLine& L, float hf, float hr,
               float hc) {
  Guard g{RACE_INF, RACE_INF, -1, -1, -1, -1};
  const int n = st.board.n;
  for (int k = 0; k < L.len; k++) {
    const int r = L.r + L.dr * k, c = L.c + L.dc * k, i = r * n + c;
    const float S = raceStart(st, t, r, c, hf, hr, hc);
    for (int j = 0; j < et.count[i]; j++) {
      const Escape& x = et.e[et.start[i] + j];
      const float arrival = S + x.drag;
      const float need1 = arrival + t.machineCooldown + t.react + t.engage + t.margin;
      if (!atLeast(x.thDest, need1) || !atLeast(x.thRest, S + t.margin)) continue;
      if (S < g.save || (S == g.save && arrival < g.arrival)) g = Guard{S, arrival, r, c, x.dr, x.dc};
    }
  }
  return g;
}

bool guardable(const RaceTimes& t, const Guard& g, float th) { return !raceInf(g.save) && g.save + t.margin <= th; }

struct Search {
  const RaceState* st;
  const RaceTimes* t;
  const EscapeTable* et;
  const RaceLineEval* ev;
  const int* idx;  // threatened lines sorted by T_h
  int m;
  int path[RACE_EXHAUSTIVE];
  Guard guards[RACE_EXHAUSTIVE];
  int best;
  int bestPath[RACE_EXHAUSTIVE];
  Guard bestGuards[RACE_EXHAUSTIVE];
  int bestLen;
};

void searchOrders(Search& s, int depth, int pieces, float hf, float hr, float hc) {
  if (pieces > s.best) {
    s.best = pieces;
    s.bestLen = depth;
    for (int i = 0; i < depth; i++) {
      s.bestPath[i] = s.path[i];
      s.bestGuards[i] = s.guards[i];
    }
  }
  if (depth == s.m) return;
  for (int j = 0; j < s.m; j++) {
    bool used = false;
    for (int i = 0; i < depth; i++) used = used || s.path[i] == j;
    if (used) continue;
    const RaceLineEval& e = s.ev[s.idx[j]];
    const Guard g = saveLine(*s.st, *s.t, *s.et, e.line, hf, hr, hc);
    if (!guardable(*s.t, g, e.th)) continue;
    s.path[depth] = j;
    s.guards[depth] = g;
    searchOrders(s, depth + 1, pieces + e.line.len, g.arrival, static_cast<float>(g.dr), static_cast<float>(g.dc));
  }
}

}  // namespace

RaceTimes raceTimes(const Snapshot& s, const Params& p) {
  RaceTimes t;
  t.humanMove = static_cast<float>(p.humanMoveMs);
  t.humanLift = static_cast<float>(p.humanLiftMs);
  t.humanPlace = static_cast<float>(p.humanPlaceMs);
  t.humanReact = static_cast<float>(p.humanReactMs);
  t.humanCooldown = static_cast<float>(s.humanCooldownMs);
  t.humanInterval = static_cast<float>(s.humanIntervalMs);
  t.maxHold = static_cast<float>(s.maxHoldMs);
  t.machineCooldown = static_cast<float>(s.machineCooldownMs);
  t.engage = static_cast<float>(p.engageMs);
  t.react = static_cast<float>(p.reactMs);
  t.freeSpeed = p.freeSpeed;
  t.dragSpeed = p.dragSpeed;
  t.easy = p.level == Level::Easy;
  t.margin = p.level == Level::Easy ? 0.0f : p.level == Level::Normal ? 200.0f : 300.0f;
  return t;
}

void raceInit(RaceState& st, const Snapshot& s, const RaceTimes& t) {
  st.board = s.board;
  const int n = st.board.n;
  std::memset(st.blocked, 0, sizeof(st.blocked));
  std::memset(st.mReady, 0, sizeof(st.mReady));
  std::memset(st.mId, 0, sizeof(st.mId));
  st.reserved = -1;
  if (st.board.inside(s.reservedR, s.reservedC)) {
    st.reserved = s.reservedR * n + s.reservedC;
    st.blocked[st.reserved] = true;
  }
  int pendingId = -1;
  if (s.pending && st.board.inside(s.pendingR, s.pendingC)) {
    pendingId = s.pendingId >= 1 ? s.pendingId : (s.heldCount > 0 ? s.heldIds[0] : -1);
    const int i = s.pendingR * n + s.pendingC;
    st.blocked[i] = true;
    if (st.board.cells[i] == EMPTY) st.board.cells[i] = HUMAN;
  }
  const uint32_t due = s.lastPlaceAt + s.humanIntervalMs;
  st.gap = s.hasLastPlace && due > s.clockMs ? static_cast<float>(due - s.clockMs) : 0.0f;
  st.firstPlace = pendingId >= 1 ? st.gap + t.humanInterval : st.gap;
  st.firstLift = pendingId >= 1 ? st.gap : 0.0f;
  st.hCount = 0;
  for (int i = 0; i < s.count; i++) {
    const PieceView& q = s.pieces[i];
    if (!q.alive || !st.board.inside(q.r, q.c)) continue;
    const float rem = q.cooldownUntil > s.clockMs ? static_cast<float>(q.cooldownUntil - s.clockMs) : 0.0f;
    if (q.side == MACHINE) {
      if (q.held) continue;
      st.mReady[q.r * n + q.c] = rem;
      st.mId[q.r * n + q.c] = q.id;
      continue;
    }
    if (q.side != HUMAN || st.hCount >= RACE_MAX_HUMANS) continue;
    RaceHuman& h = st.h[st.hCount++];
    h.id = q.id;
    h.r = q.r;
    h.c = q.c;
    h.alive = true;
    h.held = false;
    h.pending = false;
    h.ready = rem;
    h.holdLeft = RACE_INF;
    if (q.held && q.id == pendingId) {
      // Its origin stays closed to the machine until it lands, like any held origin (3.1).
      st.blocked[q.r * n + q.c] = true;
      h.pending = true;
      h.r = s.pendingR;
      h.c = s.pendingC;
      h.ready = st.gap + t.humanCooldown;
    } else if (q.held) {
      st.blocked[q.r * n + q.c] = true;
      h.held = true;
      h.ready = 0.0f;
      if (t.maxHold > 0.0f) {
        const float up = s.clockMs > q.liftedAt ? static_cast<float>(s.clockMs - q.liftedAt) : 0.0f;
        h.holdLeft = t.maxHold - up;
      }
    }
  }
  st.headFree = 0.0f;
  st.headR = s.headR;
  st.headC = s.headC;
  refreshAlive(st);
  for (int i = 0; i < st.hCount; i++) buildReach(st, i);
}

void raceRebuild(RaceState& st, int r0, int c0, int r1, int c1) {
  refreshAlive(st);
  for (int i = 0; i < st.hCount; i++) {
    const RaceHuman& h = st.h[i];
    if (!h.alive) continue;
    const bool near0 = r0 >= 0 && (h.r == r0 || h.c == c0);
    const bool near1 = r1 >= 0 && (h.r == r1 || h.c == c1);
    if (near0 || near1) buildReach(st, i);
  }
}

int raceMoveMachine(RaceState& st, const RaceTimes& t, int fr, int fc, int tr, int tc, float arrival) {
  const int n = st.board.n, from = fr * n + fc, to = tr * n + tc;
  st.board.cells[from] = EMPTY;
  st.board.cells[to] = MACHINE;
  st.mReady[to] = arrival + t.machineCooldown;
  st.mId[to] = st.mId[from];
  st.mReady[from] = 0.0f;
  st.mId[from] = 0;
  // Captures on the real board: the pending target is still empty there.
  Board real = st.board;
  for (int i = 0; i < st.hCount; i++)
    if (st.h[i].alive && st.h[i].pending) real.set(st.h[i].r, st.h[i].c, EMPTY);
  int caps[MAX_N * MAX_N];
  const int k = capturesAt(real, MACHINE, tr, tc, caps);
  for (int i = 0; i < k; i++) st.board.cells[caps[i]] = EMPTY;
  st.headFree = arrival;
  st.headR = static_cast<float>(tr);
  st.headC = static_cast<float>(tc);
  raceRebuild(st, fr, fc, tr, tc);
  // A captured piece opens paths too (missing an opened path errs on the unsafe side, 3.6).
  for (int i = 0; i < k; i += 2) {
    const int r1 = i + 1 < k ? caps[i + 1] / n : -1, c1 = i + 1 < k ? caps[i + 1] % n : -1;
    raceRebuild(st, caps[i] / n, caps[i] % n, r1, c1);
  }
  return k;
}

RaceLine raceLineThrough(const Board& b, int8_t side, int r, int c, int dr, int dc) {
  RaceLine L{r, c, dr, dc, 0, side};
  if (!b.inside(r, c) || b.at(r, c) != side) return L;
  while (b.inside(L.r - dr, L.c - dc) && b.at(L.r - dr, L.c - dc) == side) {
    L.r -= dr;
    L.c -= dc;
  }
  while (b.inside(L.r + dr * L.len, L.c + dc * L.len) && b.at(L.r + dr * L.len, L.c + dc * L.len) == side) L.len++;
  return L;
}

int raceLines(const Board& b, int8_t side, RaceLine out[]) {
  int k = 0;
  for (int dir = 0; dir < 2; dir++) {
    const int dr = dir == 0 ? 0 : 1, dc = dir == 0 ? 1 : 0;
    for (int r = 0; r < b.n; r++) {
      for (int c = 0; c < b.n; c++) {
        if (b.at(r, c) != side) continue;
        if (b.inside(r - dr, c - dc) && b.at(r - dr, c - dc) == side) continue;
        if (k < RACE_MAX_LINES) out[k++] = raceLineThrough(b, side, r, c, dr, dc);
      }
    }
  }
  return k;
}

float raceHumanPlace(const RaceState& st, const RaceTimes& t, int hi, int sq) {
  const RaceHuman& h = st.h[hi];
  if (!h.alive) return RACE_INF;
  const bool one = h.reach1.has(sq);
  if (!one && !h.reach2.has(sq)) return RACE_INF;
  if (h.held) {
    // Steps that would outlast the hold limit do not count.
    if (t.humanPlace > h.holdLeft) return RACE_INF;
    if (one) return t.humanPlace;
    return fmax2(t.humanPlace, st.firstPlace) + t.humanCooldown + t.humanMove;
  }
  const float lift = liftAt(st, h);
  if (one) return lift + t.humanMove;
  return fmax2(lift + t.humanMove, st.firstPlace) + t.humanCooldown + t.humanMove;
}

float raceHumanTime(const RaceState& st, const RaceTimes& t, const RaceLine& L) {
  const Board& b = st.board;
  const int n = b.n;
  const int ar = L.r - L.dr, ac = L.c - L.dc;
  const int br = L.r + L.dr * L.len, bc = L.c + L.dc * L.len;
  if (L.len <= 0 || !b.inside(ar, ac) || !b.inside(br, bc)) return RACE_INF;
  const int8_t ca = b.at(ar, ac), cb = b.at(br, bc);
  if (ca == MACHINE || cb == MACHINE) return RACE_INF;
  const int ha = ca == HUMAN ? humanAt(st, ar, ac) : -1;
  const int hb = cb == HUMAN ? humanAt(st, br, bc) : -1;
  if (ca == HUMAN && cb == HUMAN) {
    // A pending placement on one end closes the line when it lands.
    if ((ha >= 0 && st.h[ha].pending) || (hb >= 0 && st.h[hb].pending)) return st.gap;
    if (t.easy) return RACE_INF;
    float best = RACE_INF;
    if (ha >= 0) best = fmin2(best, stepAwayTime(st, t, ha, hb));
    if (hb >= 0) best = fmin2(best, stepAwayTime(st, t, hb, ha));
    return best;
  }
  if (ca == HUMAN || cb == HUMAN) {
    // H1: one placement on the empty end, by any piece but the one on the other end.
    const int on = ca == HUMAN ? ha : hb;
    const int empty = ca == HUMAN ? br * n + bc : ar * n + ac;
    float best = RACE_INF;
    for (int i = 0; i < st.hCount; i++) {
      if (i == on || (t.easy && !st.h[i].held)) continue;
      const float e = raceHumanPlace(st, t, i, empty);
      if (!raceInf(e)) best = fmin2(best, fmax2(e, st.firstPlace));
    }
    return best;
  }
  if (t.easy) return RACE_INF;
  const int a = ar * n + ac, z = br * n + bc;
  return fmin2(twoPieces(st, t, a, z), twoPieces(st, t, z, a));
}

float raceStart(const RaceState& st, const RaceTimes& t, int r, int c, float headFree, float headR, float headC) {
  const float base = fmax2(st.mReady[r * st.board.n + c], headFree);
  return base + t.react + travelMs(t, r, c, headR, headC) + t.engage;
}

float raceArrival(const RaceTimes& t, int r, int c, int tr, int tc, float start) {
  return start + dragMs(t, r, c, tr, tc);
}

void raceDestTimes(const RaceState& st, const RaceTimes& t, int pr, int pc, int dr, int dc, float* thDest,
                   float* thRest) {
  RaceState s2 = st;
  raceMoveMachine(s2, t, pr, pc, dr, dc, 0.0f);
  const Board& b2 = s2.board;
  float dest = RACE_INF, rest = RACE_INF;
  for (int dir = 0; dir < 2; dir++) {
    const int ldr = dir == 0 ? 0 : 1, ldc = dir == 0 ? 1 : 0;
    dest = fmin2(dest, raceHumanTime(s2, t, raceLineThrough(b2, MACHINE, dr, dc, ldr, ldc)));
    // What is left of the piece's old line in this direction, on either side of its square.
    for (int side = -1; side <= 1; side += 2) {
      const int nr = pr + ldr * side, nc = pc + ldc * side;
      if (!b2.inside(nr, nc) || st.board.at(nr, nc) != MACHINE || b2.at(nr, nc) != MACHINE) continue;
      rest = fmin2(rest, raceHumanTime(s2, t, raceLineThrough(b2, MACHINE, nr, nc, ldr, ldc)));
    }
  }
  *thDest = dest;
  *thRest = rest;
}

bool raceSafeDest(const RaceState& st, const RaceTimes& t, int pr, int pc, int dr, int dc, float start) {
  float thDest, thRest;
  raceDestTimes(st, t, pr, pc, dr, dc, &thDest, &thRest);
  const float arrival = raceArrival(t, pr, pc, dr, dc, start);
  const float need1 = arrival + t.machineCooldown + t.react + t.engage + t.margin;
  return atLeast(thDest, need1) && atLeast(thRest, start + t.margin);
}

int raceAssess(const RaceState& st, const RaceTimes& t, RaceLineEval out[]) {
  RaceLine lines[RACE_MAX_LINES];
  const int k = raceLines(st.board, MACHINE, lines);
  int idx[RACE_MAX_LINES];
  int m = 0;
  for (int i = 0; i < k; i++) {
    RaceLineEval& e = out[i];
    e.line = lines[i];
    e.th = raceHumanTime(st, t, lines[i]);
    e.save = RACE_INF;
    e.danger = !raceInf(e.th) && e.th < RACE_DANGER_MS;
    e.guarded = false;
    e.order = -1;
    e.guardR = e.guardC = e.toR = e.toC = -1;
    e.risk = 0.0f;
    if (!e.danger) continue;
    // Threatened lines sorted by T_h, then by index (insertion keeps the order of equals).
    int j = m++;
    while (j > 0 && out[idx[j - 1]].th > e.th) {
      idx[j] = idx[j - 1];
      j--;
    }
    idx[j] = i;
  }
  if (m == 0) return k;

  static_assert(sizeof(EscapeTable) < 4096, "EscapeTable stays small enough for the ESP32 stack");
  EscapeTable et;
  et.used = 0;
  for (int i = 0; i < MAX_N * MAX_N; i++) et.count[i] = -1;
  const Board mb = machineBoard(st);
  for (int j = 0; j < m; j++) {
    const RaceLine& L = out[idx[j]].line;
    for (int q = 0; q < L.len; q++) addEscapes(st, t, mb, et, L.r + L.dr * q, L.c + L.dc * q);
  }

  float hf = st.headFree, hr = st.headR, hc = st.headC;
  auto take = [&](int line, const Guard& g, int order) {
    RaceLineEval& e = out[line];
    e.guarded = true;
    e.order = order;
    e.save = g.save;
    e.guardR = g.pr;
    e.guardC = g.pc;
    e.toR = g.dr;
    e.toC = g.dc;
    hf = g.arrival;
    hr = static_cast<float>(g.dr);
    hc = static_cast<float>(g.dc);
  };
  if (m <= RACE_EXHAUSTIVE) {
    // Every subset and order; the most pieces guarded wins, the first found (earliest T_h first)
    // on a tie.
    Search s;
    s.st = &st;
    s.t = &t;
    s.et = &et;
    s.ev = out;
    s.idx = idx;
    s.m = m;
    s.best = 0;
    s.bestLen = 0;
    searchOrders(s, 0, 0, hf, hr, hc);
    for (int i = 0; i < s.bestLen; i++) take(idx[s.bestPath[i]], s.bestGuards[i], i);
  } else {
    int order = 0;
    for (int j = 0; j < m; j++) {
      const Guard g = saveLine(st, t, et, out[idx[j]].line, hf, hr, hc);
      if (guardable(t, g, out[idx[j]].th)) take(idx[j], g, order++);
    }
  }
  // Lines left unguarded: save with the head after the guards, and their risk (3.7).
  for (int j = 0; j < m; j++) {
    RaceLineEval& e = out[idx[j]];
    if (e.guarded) continue;
    const Guard g = saveLine(st, t, et, e.line, hf, hr, hc);
    e.save = g.save;
    if (raceInf(g.save)) {
      e.risk = clamp01((2500.0f - e.th) / 1000.0f);
    } else {
      e.risk = clamp01(0.5f + (g.save + t.margin - e.th) / 2000.0f);
    }
  }
  return k;
}

void racePieceRisk(const Board& b, const RaceLineEval ev[], int count, float riskAt[]) {
  for (int i = 0; i < b.n * b.n; i++) riskAt[i] = 0.0f;
  for (int i = 0; i < count; i++) {
    const RaceLine& L = ev[i].line;
    for (int k = 0; k < L.len; k++) {
      const int sq = (L.r + L.dr * k) * b.n + (L.c + L.dc * k);
      riskAt[sq] = fmax2(riskAt[sq], ev[i].risk);
    }
  }
}

float raceMachineTime(const RaceState& st, const RaceTimes& t, const RaceLine& L) {
  const Board& b = st.board;
  const int n = b.n;
  const int ar = L.r - L.dr, ac = L.c - L.dc;
  const int br = L.r + L.dr * L.len, bc = L.c + L.dc * L.len;
  if (L.len <= 0 || !b.inside(ar, ac) || !b.inside(br, bc)) return RACE_INF;
  const Board mb = machineBoard(st);
  const int8_t ca = mb.at(ar, ac), cb = mb.at(br, bc);
  // An end the machine cannot enter (a human piece there, held origin, pending target, reserved).
  if (ca == HUMAN || cb == HUMAN) return RACE_INF;
  if (ca == MACHINE && cb == MACHINE) return RACE_INF;
  float best = RACE_INF;
  if (ca == MACHINE || cb == MACHINE) {
    const int xr = ca == MACHINE ? ar : br, xc = ca == MACHINE ? ac : bc;
    const int yr = ca == MACHINE ? br : ar, yc = ca == MACHINE ? bc : ac;
    for (int i = 0; i < n * n; i++) {
      if (mb.cells[i] != MACHINE || i == xr * n + xc || st.mId[i] == 0) continue;
      best = fmin2(best, machineFill(st, t, mb, i / n, i % n, yr, yc, st.headFree, st.headR, st.headC, true));
    }
    return best;
  }
  for (int order = 0; order < 2; order++) {
    const int fr = order == 0 ? ar : br, fc = order == 0 ? ac : bc;
    const int sr = order == 0 ? br : ar, sc = order == 0 ? bc : ac;
    for (int i = 0; i < n * n; i++) {
      if (mb.cells[i] != MACHINE || st.mId[i] == 0) continue;
      const float t1 = machineFill(st, t, mb, i / n, i % n, fr, fc, st.headFree, st.headR, st.headC, false);
      if (raceInf(t1)) continue;
      Board b2 = mb;
      b2.cells[i] = EMPTY;
      b2.set(fr, fc, MACHINE);
      for (int j = 0; j < n * n; j++) {
        if (j == i || b2.cells[j] != MACHINE || st.mId[j] == 0) continue;
        const float t2 = machineFill(st, t, b2, j / n, j % n, sr, sc, t1, static_cast<float>(fr),
                                     static_cast<float>(fc), false);
        best = fmin2(best, t2);
      }
    }
  }
  return best;
}

float raceEscape(const RaceState& st, const RaceTimes& t, const RaceLine& L) {
  float best = RACE_INF;
  for (int k = 0; k < L.len; k++) {
    const int hi = humanAt(st, L.r + L.dr * k, L.c + L.dc * k);
    if (hi < 0) continue;
    best = fmin2(best, st.h[hi].ready + t.humanReact + t.humanLift);
  }
  return best;
}

float raceGain(const RaceState& st, const RaceTimes& t, const RaceLine& L) {
  const float tm = raceMachineTime(st, t, L);
  if (raceInf(tm)) return 0.0f;
  const float esc = raceEscape(st, t, L);
  if (raceInf(esc)) return static_cast<float>(L.len);
  return static_cast<float>(L.len) * clamp01(0.5f + (esc - tm) / 2000.0f);
}
