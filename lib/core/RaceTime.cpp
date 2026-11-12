#include "RaceTime.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include "Path.h"

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
// target (nor a first stop), nor (RW) a square where the piece would make a wall.
void buildReach(RaceState& st, int i) {
  RaceHuman& h = st.h[i];
  h.reach1.clear();
  h.reach2.clear();
  if (!h.alive) return;
  const Board& b = st.board;
  const int n = b.n, origin = h.r * n + h.c;
  for (int d = 0; d < 4; d++) {
    for (int rr = h.r + DIR_R[d], cc = h.c + DIR_C[d]; b.inside(rr, cc) && b.cells[rr * n + cc] == EMPTY;
         rr += DIR_R[d], cc += DIR_C[d]) {
      const int f = rr * n + cc;
      if (f == st.reserved || (st.wall && completesWall(b, HUMAN, h.r, h.c, rr, cc))) continue;
      h.reach1.set(f);
      // Second move from f: the piece's own square is empty by then.
      for (int e = 0; e < 4; e++) {
        for (int r2 = rr + DIR_R[e], c2 = cc + DIR_C[e]; b.inside(r2, c2); r2 += DIR_R[e], c2 += DIR_C[e]) {
          const int sq = r2 * n + c2;
          if (b.cells[sq] != EMPTY && sq != origin) break;
          if (sq != st.reserved && !(st.wall && completesWall(b, HUMAN, h.r, h.c, r2, c2))) h.reach2.set(sq);
        }
      }
    }
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

// Whether a placement on sq waits for the human's reaction to the machine's move (3.6).
bool reacts(const RaceState& st, int sq) { return st.reactAt > 0.0f && !st.planned.has(sq); }

// When human piece h can be lifted to be put on sq.
float liftFor(const RaceState& st, const RaceHuman& h, int sq) {
  const float l = liftAt(st, h);
  return reacts(st, sq) ? fmax2(l, st.reactAt) : l;
}

// H3 and H4 of 3.5: the human piece vi on one end steps away; another piece g (not one of the
// pieces in `exclude`, a bit per piece index: the one on the other end, or a group's other
// enclosers) comes into its square, or vi itself comes back after its cooldown.
float stepAwayTime(const RaceState& st, const RaceTimes& t, int vi, uint32_t exclude) {
  const RaceHuman& v = st.h[vi];
  if (v.held || v.pending || !v.reach1.any()) return RACE_INF;
  const int vsq = v.r * st.board.n + v.c;
  const float tv = fmax2(liftFor(st, v, vsq) + t.humanMove, st.firstPlace);
  float best = tv + t.humanCooldown + t.humanMove;  // H4
  Board b = st.board;
  b.set(v.r, v.c, EMPTY);
  for (int g = 0; g < st.hCount; g++) {
    const RaceHuman& h = st.h[g];
    if (g == vi || ((exclude >> g) & 1u) || !h.alive || h.held) continue;
    if (!reachesStraight(b, h.r, h.c, v.r, v.c)) continue;
    const float tg = fmax2(fmax2(tv + t.humanInterval, tv + t.humanMove), liftFor(st, h, vsq) + t.humanMove);  // H3
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
      const float t2 = fmax2(fmax2(t1 + t.humanInterval, t1 + t.humanMove), liftFor(st, h, b) + t.humanMove);
      best = fmin2(best, t2);
    }
  }
  return best;
}

// Whether (r,c) is next to a square of the group (and not in it).
bool nextToGroup(const EdgeGroup& g, int n, int r, int c) {
  if (g.cells.has(r * n + c)) return false;
  for (int d = 0; d < 4; d++) {
    const int rr = r + DIR_R[d], cc = c + DIR_C[d];
    if (rr >= 0 && rr < n && cc >= 0 && cc < n && g.cells.has(rr * n + cc)) return true;
  }
  return false;
}

// The empty neighbours of a group, filled one by one (RE): every order and every choice of
// different pieces, the earliest last placement wins.
struct Fill {
  const RaceState* st;
  const RaceTimes* t;
  int libs[RACE_GROUP_MAX_FILL];
  int k;
  float best;
};

void fillSearch(Fill& f, int depth, int usedLibs, uint32_t usedPieces, float prev) {
  if (depth == f.k) {
    if (prev < f.best) f.best = prev;
    return;
  }
  const RaceState& st = *f.st;
  const RaceTimes& t = *f.t;
  for (int li = 0; li < f.k; li++) {
    if ((usedLibs >> li) & 1) continue;
    const int sq = f.libs[li];
    for (int i = 0; i < st.hCount; i++) {
      const RaceHuman& h = st.h[i];
      if (((usedPieces >> i) & 1u) || !h.alive) continue;
      float ti;
      if (depth == 0) {
        // The first placement as H1 (two-move routes included).
        const float e = raceHumanPlace(st, t, i, sq);
        if (raceInf(e)) continue;
        ti = fmax2(e, st.firstPlace);
      } else {
        // Later ones as H2's second piece: one move, not a held piece (it is placed first).
        if (h.held || !h.reach1.has(sq)) continue;
        ti = fmax2(fmax2(prev + t.humanInterval, prev + t.humanMove), liftFor(st, h, sq) + t.humanMove);
      }
      if (!(ti < f.best)) continue;
      fillSearch(f, depth + 1, usedLibs | (1 << li), usedPieces | (1u << i), ti);
    }
  }
}

// T_h of a group unit (RE) on st.board; see raceHumanTime.
float groupTime(const RaceState& st, const RaceTimes& t, const RaceLine& G) {
  EdgeGroup g;
  if (G.len <= 0 || !t.rules.edgeCapture || !edgeGroupAt(st.board, G.r, G.c, g) || g.side != MACHINE)
    return RACE_INF;
  if (g.libCount > RACE_GROUP_MAX_FILL) return RACE_INF;
  const int n = st.board.n;
  // The human pieces next to the group enclose it; moving one opens its square, so they stay.
  // A pending piece is on its target (st.board) and lands at gap.
  uint32_t enclosers = 0;
  bool pendingNext = false;
  for (int i = 0; i < st.hCount; i++) {
    const RaceHuman& h = st.h[i];
    if (!h.alive || h.held || !nextToGroup(g, n, h.r, h.c)) continue;
    enclosers |= 1u << i;
    pendingNext = pendingNext || h.pending;
  }
  if (g.libCount == 0) {
    // Enclosed already (the machine entered the spot itself): the next human arrival next to it
    // captures. A pending placement next to it lands at gap; otherwise an encloser steps away and
    // a piece comes back into its square.
    if (pendingNext) return st.gap;
    if (t.easy) return RACE_INF;
    float best = RACE_INF;
    for (int i = 0; i < st.hCount; i++)
      if ((enclosers >> i) & 1u) best = fmin2(best, stepAwayTime(st, t, i, enclosers));
    return best;
  }
  if (t.easy) {
    // Easy reads only the held piece's single placement.
    if (g.libCount != 1) return RACE_INF;
    float best = RACE_INF;
    for (int i = 0; i < st.hCount; i++) {
      if (!st.h[i].held || ((enclosers >> i) & 1u)) continue;
      const float e = raceHumanPlace(st, t, i, g.libs[0]);
      if (!raceInf(e)) best = fmin2(best, fmax2(e, st.firstPlace));
    }
    return best;
  }
  Fill f;
  f.st = &st;
  f.t = &t;
  f.k = g.libCount;
  for (int i = 0; i < f.k; i++) f.libs[i] = g.libs[i];
  f.best = RACE_INF;
  fillSearch(f, 0, 0, enclosers, 0.0f);
  return f.best;
}

// Machine board: blocked squares count as occupied.
Board machineBoard(const RaceState& st) {
  Board b = st.board;
  for (int i = 0; i < b.n * b.n; i++)
    if (st.blocked[i] && b.cells[i] == EMPTY) b.cells[i] = HUMAN;
  return b;
}

// The head travels along travelPath (the gantries follow it), not straight.
float travelMs(const RaceTimes& t, int r, int c, float headR, float headC) {
  float pr[TRAVEL_MAX_POINTS], pc[TRAVEL_MAX_POINTS];
  const int k = travelPath(headR, headC, r, c, pr, pc);
  return polylineLength(pr, pc, k) / t.freeSpeed * 1000.0f;
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
  if (reachesStraight(mb, r, c, tr, tc) && !makesWall(mb, t.rules, MACHINE, r, c, tr, tc))
    best = start + dragMs(t, r, c, tr, tc);
  if (!twoMoves) return best;
  Board b = mb;
  int d[MAX_N * MAX_N];
  const int k = dests(b, r, c, d);
  b.set(r, c, EMPTY);
  for (int j = 0; j < k; j++) {
    const int xr = d[j] / n, xc = d[j] % n;
    if (xr == tr && xc == tc) continue;
    if (makesWall(mb, t.rules, MACHINE, r, c, xr, xc)) continue;
    b.cells[d[j]] = MACHINE;
    if (reachesStraight(b, xr, xc, tr, tc) && !makesWall(b, t.rules, MACHINE, xr, xc, tr, tc)) {
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
  int8_t dr, dc;
  bool known;  // thDest and thRest computed (they are computed only when a guard needs them)
  float drag, thDest, thRest;
};

constexpr int MAX_ESCAPES = MAX_N * 2 * (MAX_N - 1);

struct Guard {
  float save;  // S of the guarding piece
  float arrival;
  int pr, pc, dr, dc;
};

// Escapes of the machine pieces in the threatened lines, per square, each square's sorted by drag
// time (dests() order among equal drags).
struct EscapeTable {
  RaceMask planned;  // ends of the threatened lines (where the human is heading)
  int start[MAX_N * MAX_N];
  int count[MAX_N * MAX_N];
  Escape e[MAX_ESCAPES];
  int used;
};

void addEscapes(const RaceTimes& t, const Board& mb, EscapeTable& et, int r, int c) {
  const int n = mb.n, i = r * n + c;
  if (et.count[i] >= 0) return;
  et.start[i] = et.used;
  et.count[i] = 0;
  int d[MAX_N * MAX_N];
  const int k = dests(mb, r, c, d);
  for (int j = 0; j < k && et.used < MAX_ESCAPES; j++) {
    if (makesWall(mb, t.rules, MACHINE, r, c, d[j] / n, d[j] % n)) continue;  // RW
    Escape x;
    x.dr = static_cast<int8_t>(d[j] / n);
    x.dc = static_cast<int8_t>(d[j] % n);
    x.drag = dragMs(t, r, c, x.dr, x.dc);
    x.known = false;
    x.thDest = x.thRest = RACE_INF;
    int pos = et.used;
    while (pos > et.start[i] && et.e[pos - 1].drag > x.drag) {
      et.e[pos] = et.e[pos - 1];
      pos--;
    }
    et.e[pos] = x;
    et.used++;
    et.count[i]++;
  }
}

// save(L) with the head free at hf at (hr,hc): the earliest S over the pieces of L (a line's or a
// group unit's) with a safe destination; the guarding move is the one arriving first (the first
// piece of L and the first destination in dests() order among equals). A piece's escapes are
// tried by drag time, so the first safe one is its earliest arrival.
Guard saveLine(const RaceState& st, const RaceTimes& t, EscapeTable& et, const RaceLine& L, float hf, float hr,
               float hc) {
  Guard g{RACE_INF, RACE_INF, -1, -1, -1, -1};
  const int n = st.board.n;
  uint8_t sqs[MAX_N * MAX_N];
  const int count = raceUnitSquares(st.board, L, sqs);
  for (int k = 0; k < count; k++) {
    const int i = sqs[k], r = i / n, c = i % n;
    const float S = raceStart(st, t, r, c, hf, hr, hc);
    if (S > g.save) continue;
    for (int j = 0; j < et.count[i]; j++) {
      Escape& x = et.e[et.start[i] + j];
      const float arrival = S + x.drag;
      if (S == g.save && !(arrival < g.arrival)) break;
      if (!x.known) {
        // The human reacts no sooner than the earliest start this piece can have on st (S
        // varies with the head; the earliest keeps the cached times on the cautious side).
        raceDestTimes(st, t, r, c, x.dr, x.dc, &x.thDest, &x.thRest, raceReactAt(st, t, r, c), &et.planned);
        x.known = true;
      }
      const float need1 = arrival + t.machineCooldown + t.react + t.engage + t.margin;
      if (!atLeast(x.thDest, need1) || !atLeast(x.thRest, S + t.margin)) continue;
      g = Guard{S, arrival, r, c, x.dr, x.dc};
      break;
    }
  }
  return g;
}

bool guardable(const RaceTimes& t, const Guard& g, float th, float lead) {
  return !raceInf(g.save) && g.save + lead + t.margin <= th;
}

// Whether unit L on b holds the square sq.
bool unitHas(const Board& b, const RaceLine& L, int sq) {
  if (raceIsUntouched(L)) return L.r * b.n + L.c == sq;
  if (raceIsGroup(L)) {
    EdgeGroup g;
    return L.len > 0 && edgeGroupAt(b, L.r, L.c, g) && g.cells.has(sq);
  }
  for (int k = 0; k < L.len; k++)
    if ((L.r + L.dr * k) * b.n + (L.c + L.dc * k) == sq) return true;
  return false;
}

// Whether guard g, taken for unit `by`, guards unit L (threatened at th) with the same move: one
// of the two is an RU unit, L holds g's piece and the move starts in time for L. Moving the piece
// restarts its timer (RU) and takes it out of its lines and group, whose rest saveLine checked.
bool covers(const RaceState& st, const RaceTimes& t, const Guard& g, const RaceLine& by, const RaceLine& L, float th,
            float lead) {
  if (!raceIsUntouched(by) && !raceIsUntouched(L)) return false;
  return guardable(t, g, th, lead) && unitHas(st.board, L, g.pr * st.board.n + g.pc);
}

struct Search {
  const RaceState* st;
  const RaceTimes* t;
  EscapeTable* et;
  const RaceLineEval* ev;
  const int* idx;  // threatened lines sorted by T_h
  int m;
  float lead;
  int path[RACE_EXHAUSTIVE];
  Guard guards[RACE_EXHAUSTIVE];
  uint32_t cover[RACE_EXHAUSTIVE];  // the other units (bit j for idx[j]) each guard also guards
  int best;
  int bestPath[RACE_EXHAUSTIVE];
  Guard bestGuards[RACE_EXHAUSTIVE];
  uint32_t bestCover[RACE_EXHAUSTIVE];
  int bestLen;
};

// `used`: the units guarded so far (bit j for idx[j]), by a guard of their own or covered.
void searchOrders(Search& s, int depth, int pieces, uint32_t used, float hf, float hr, float hc) {
  if (pieces > s.best) {
    s.best = pieces;
    s.bestLen = depth;
    for (int i = 0; i < depth; i++) {
      s.bestPath[i] = s.path[i];
      s.bestGuards[i] = s.guards[i];
      s.bestCover[i] = s.cover[i];
    }
  }
  if (depth == s.m) return;
  for (int j = 0; j < s.m; j++) {
    if ((used >> j) & 1u) continue;
    const RaceLineEval& e = s.ev[s.idx[j]];
    const Guard g = saveLine(*s.st, *s.t, *s.et, e.line, hf, hr, hc);
    if (!guardable(*s.t, g, e.th, s.lead)) continue;
    uint32_t cov = 0;
    int add = e.line.len;
    for (int q = 0; q < s.m; q++) {
      if (q == j || ((used >> q) & 1u)) continue;
      const RaceLineEval& o = s.ev[s.idx[q]];
      if (!covers(*s.st, *s.t, g, e.line, o.line, o.th, s.lead)) continue;
      cov |= 1u << q;
      add += o.line.len;
    }
    s.path[depth] = j;
    s.guards[depth] = g;
    s.cover[depth] = cov;
    searchOrders(s, depth + 1, pieces + add, used | (1u << j) | cov, g.arrival, static_cast<float>(g.dr),
                 static_cast<float>(g.dc));
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
  t.readInterval = static_cast<float>(p.reactMs > 150 ? p.reactMs : 150);
  t.freeSpeed = p.freeSpeed;
  t.dragSpeed = p.dragSpeed;
  t.easy = p.level == Level::Easy;
  t.margin = p.level == Level::Easy ? 0.0f : p.level == Level::Normal ? 200.0f : 300.0f;
  t.rules = Rules();
  t.rules.n = s.board.n;
  t.rules.humanCooldownMs = s.humanCooldownMs;
  t.rules.machineCooldownMs = s.machineCooldownMs;
  t.rules.humanIntervalMs = s.humanIntervalMs;
  t.rules.maxHoldMs = s.maxHoldMs;
  t.rules.edgeCapture = s.edgeCapture;
  t.rules.wallLimit = s.wallLimit;
  t.rules.untouchedMs = s.untouchedMs;
  t.untouched = false;
  return t;
}

void raceInit(RaceState& st, const Snapshot& s, const RaceTimes& t) {
  st.board = s.board;
  const int n = st.board.n;
  st.wall = t.rules.wallLimit;
  st.stale.clear();
  if (t.rules.untouchedMs > 0) {
    for (int i = 0; i < s.count; i++) {
      const PieceView& q = s.pieces[i];
      if (q.alive && !q.held && st.board.inside(q.r, q.c) && s.clockMs >= q.movedAt + t.rules.untouchedMs)
        st.stale.set(q.r * n + q.c);
    }
  }
  std::memset(st.blocked, 0, sizeof(st.blocked));
  std::memset(st.mReady, 0, sizeof(st.mReady));
  std::memset(st.mId, 0, sizeof(st.mId));
  for (int i = 0; i <= 2 * MAX_N; i++) st.mStale[i] = RACE_INF;
  const bool ru = raceUntouchedRead(t);
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
    // RU read: when it goes untouched (the clock runs on).
    float stale = RACE_INF;
    if (ru) {
      const uint32_t due = q.movedAt + t.rules.untouchedMs;
      stale = due > s.clockMs ? static_cast<float>(due - s.clockMs) : 0.0f;
    }
    if (q.side == MACHINE) {
      if (q.held) continue;
      st.mReady[q.r * n + q.c] = rem;
      st.mId[q.r * n + q.c] = q.id;
      // The piece being dragged is off the board for the human; its timer restarts square by square.
      if (q.id > 0 && q.id <= 2 * MAX_N) st.mStale[q.id] = q.id == s.reservedId ? RACE_INF : stale;
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
    h.staleAt = stale;
    if (q.held && q.id == pendingId) {
      // Its origin stays closed to the machine until it lands, like any held origin (3.1).
      st.blocked[q.r * n + q.c] = true;
      h.pending = true;
      h.r = s.pendingR;
      h.c = s.pendingC;
      h.ready = st.gap + t.humanCooldown;
      h.staleAt = ru ? st.gap + static_cast<float>(t.rules.untouchedMs) : RACE_INF;
    } else if (q.held) {
      st.blocked[q.r * n + q.c] = true;
      h.held = true;
      h.ready = 0.0f;
      h.staleAt = RACE_INF;
      if (t.maxHold > 0.0f) {
        const float up = s.clockMs > q.liftedAt ? static_cast<float>(s.clockMs - q.liftedAt) : 0.0f;
        h.holdLeft = t.maxHold - up;
      }
    }
  }
  // One piece in hand (3.1): while a piece is held, no other piece is lifted before it is put
  // down, placed or put back (humanPlace), or sent back by the hold limit if that comes sooner.
  for (int i = 0; i < st.hCount; i++) {
    const RaceHuman& h = st.h[i];
    if (!h.held) continue;
    const float down = fmax2(0.0f, fmin2(t.humanPlace, h.holdLeft));
    st.firstLift = fmax2(st.firstLift, down);
  }
  st.reactAt = 0.0f;
  st.planned.clear();
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
  st.stale.reset(from);
  st.stale.reset(to);
  // RU read: the piece's timer restarts at the arrival (at every square it enters, the last one
  // counts).
  const bool ru = raceUntouchedRead(t);
  const int id = st.mId[to];
  if (id > 0 && id <= 2 * MAX_N) st.mStale[id] = ru ? arrival + static_cast<float>(t.rules.untouchedMs) : RACE_INF;
  // Captures on the real board: the pending target is still empty there.
  Board real = st.board;
  for (int i = 0; i < st.hCount; i++)
    if (st.h[i].alive && st.h[i].pending) real.set(st.h[i].r, st.h[i].c, EMPTY);
  // RU read: the human pieces untouched by the arrival (otherwise those of the snapshot).
  RaceMask stale = st.stale;
  if (ru) {
    for (int i = 0; i < st.hCount; i++) {
      const RaceHuman& h = st.h[i];
      if (h.alive && !h.held && !h.pending && !raceInf(h.staleAt) && h.staleAt <= arrival) stale.set(h.r * n + h.c);
    }
  }
  int caps[MAX_N * MAX_N];
  const int k = captures(real, t.rules, MACHINE, tr, tc, caps, &stale);
  for (int i = 0; i < k; i++) {
    st.board.cells[caps[i]] = EMPTY;
    st.stale.reset(caps[i]);
  }
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

RaceLine raceGroupThrough(const Board& b, const RaceTimes& t, int8_t side, int r, int c) {
  RaceLine L{r, c, 0, 0, 0, side};
  if (!t.rules.edgeCapture || !b.inside(r, c) || b.at(r, c) != side) return L;
  EdgeGroup g;
  if (!edgeGroupAt(b, r, c, g)) return L;
  L.r = g.first / b.n;
  L.c = g.first % b.n;
  L.len = g.size;
  return L;
}

RaceLine raceUntouchedThrough(const RaceState& st, const RaceTimes& t, int r, int c) {
  RaceLine L{r, c, 0, 0, 0, MACHINE, true};
  if (!raceUntouchedRead(t) || !st.board.inside(r, c) || st.board.at(r, c) != MACHINE) return L;
  if (raceMachineStale(st, r * st.board.n + c) < RACE_UNTOUCHED_DANGER_MS) L.len = 1;
  return L;
}

int raceUnitSquares(const Board& b, const RaceLine& L, uint8_t out[]) {
  if (raceIsUntouched(L)) {
    if (L.len <= 0) return 0;
    out[0] = static_cast<uint8_t>(L.r * b.n + L.c);
    return 1;
  }
  if (!raceIsGroup(L)) {
    for (int k = 0; k < L.len; k++) out[k] = static_cast<uint8_t>((L.r + L.dr * k) * b.n + (L.c + L.dc * k));
    return L.len > 0 ? L.len : 0;
  }
  EdgeGroup g;
  if (L.len <= 0 || !edgeGroupAt(b, L.r, L.c, g)) return 0;
  int k = 0;
  for (int sq = 0; sq < b.n * b.n; sq++)
    if (g.cells.has(sq)) out[k++] = static_cast<uint8_t>(sq);
  return k;
}

int raceUnitEnds(const Board& b, const RaceLine& L, uint8_t out[]) {
  int k = 0;
  if (raceIsUntouched(L)) {
    if (L.len <= 0) return 0;
    // Square order: up, left, right, down.
    const int nr[4] = {L.r - 1, L.r, L.r, L.r + 1}, nc[4] = {L.c, L.c - 1, L.c + 1, L.c};
    for (int d = 0; d < 4; d++)
      if (b.inside(nr[d], nc[d]) && b.at(nr[d], nc[d]) == EMPTY) out[k++] = static_cast<uint8_t>(nr[d] * b.n + nc[d]);
    return k;
  }
  if (!raceIsGroup(L)) {
    const int ar = L.r - L.dr, ac = L.c - L.dc, br = L.r + L.dr * L.len, bc = L.c + L.dc * L.len;
    if (b.inside(ar, ac)) out[k++] = static_cast<uint8_t>(ar * b.n + ac);
    if (b.inside(br, bc)) out[k++] = static_cast<uint8_t>(br * b.n + bc);
    return k;
  }
  EdgeGroup g;
  if (L.len <= 0 || !edgeGroupAt(b, L.r, L.c, g)) return 0;
  for (int sq = 0; sq < b.n * b.n; sq++)
    if (b.cells[sq] == EMPTY && nextToGroup(g, b.n, sq / b.n, sq % b.n)) out[k++] = static_cast<uint8_t>(sq);
  return k;
}

float raceHumanPlace(const RaceState& st, const RaceTimes& t, int hi, int sq) {
  const RaceHuman& h = st.h[hi];
  if (!h.alive) return RACE_INF;
  const bool one = h.reach1.has(sq);
  if (!one && !h.reach2.has(sq)) return RACE_INF;
  if (h.held) {
    // Steps that would outlast the hold limit do not count.
    const float place = reacts(st, sq) ? fmax2(t.humanPlace, st.reactAt) : t.humanPlace;
    if (place > h.holdLeft) return RACE_INF;
    if (one) return place;
    return fmax2(place, st.firstPlace) + t.humanCooldown + t.humanMove;
  }
  const float lift = liftFor(st, h, sq);
  float e = one ? lift + t.humanMove : fmax2(lift + t.humanMove, st.firstPlace) + t.humanCooldown + t.humanMove;
  // A held piece's origin frees only when that piece is placed elsewhere (putting it back fills
  // the square again): another piece lands there no sooner than that placement plus the
  // interval (one piece in hand, 3.1).
  for (int j = 0; j < st.hCount; j++) {
    const RaceHuman& o = st.h[j];
    if (j == hi || !o.alive || !o.held || o.r * st.board.n + o.c != sq) continue;
    e = fmax2(e, fmax2(t.humanPlace, st.firstPlace) + t.humanInterval);
  }
  return e;
}

float raceUntouchedTime(const RaceState& st, const RaceTimes& t, int r, int c) {
  if (!raceUntouchedRead(t) || !st.board.inside(r, c) || st.board.at(r, c) != MACHINE) return RACE_INF;
  const int n = st.board.n;
  const float stale = raceMachineStale(st, r * n + c);
  if (raceInf(stale)) return RACE_INF;
  float best = RACE_INF;
  for (int d = 0; d < 4; d++) {
    const int rr = r + DIR_R[d], cc = c + DIR_C[d];
    if (!st.board.inside(rr, cc)) continue;
    const int sq = rr * n + cc;
    const int8_t v = st.board.cells[sq];
    if (v == EMPTY) {
      // A placement there (H1); Easy reads only the held piece's.
      for (int i = 0; i < st.hCount; i++) {
        if (t.easy && !st.h[i].held) continue;
        const float e = raceHumanPlace(st, t, i, sq);
        if (!raceInf(e)) best = fmin2(best, fmax2(e, st.firstPlace));
      }
    } else if (v == HUMAN) {
      const int hi = humanAt(st, rr, cc);
      if (hi < 0) continue;
      if (st.h[hi].pending) {
        // It lands at gap: an arrival next to the piece, a capture once the piece is untouched.
        if (st.gap >= stale) best = fmin2(best, st.gap);
      } else if (!t.easy) {
        // It steps away and a piece comes into its square, or it comes back (H3, H4).
        best = fmin2(best, stepAwayTime(st, t, hi, 0u));
      }
    }
  }
  // The human can wait: the arrival comes once the piece is untouched.
  return raceInf(best) ? RACE_INF : fmax2(stale, best);
}

float raceHumanTime(const RaceState& st, const RaceTimes& t, const RaceLine& L) {
  if (raceIsUntouched(L)) return L.len > 0 ? raceUntouchedTime(st, t, L.r, L.c) : RACE_INF;
  if (raceIsGroup(L)) return groupTime(st, t, L);
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
    if (ha >= 0) best = fmin2(best, stepAwayTime(st, t, ha, hb >= 0 ? 1u << hb : 0u));
    if (hb >= 0) best = fmin2(best, stepAwayTime(st, t, hb, ha >= 0 ? 1u << ha : 0u));
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

float raceTravel(const RaceTimes& t, int r, int c, float headR, float headC) { return travelMs(t, r, c, headR, headC); }

float raceArrival(const RaceTimes& t, int r, int c, int tr, int tc, float start) {
  return start + dragMs(t, r, c, tr, tc);
}

void raceDestTimes(const RaceState& st, const RaceTimes& t, int pr, int pc, int dr, int dc, float* thDest,
                   float* thRest, float reactAt, const RaceMask* planned) {
  RaceState s2 = st;
  raceMoveMachine(s2, t, pr, pc, dr, dc, 0.0f);
  const Board& b2 = s2.board;
  float dest = RACE_INF, rest = RACE_INF;
  for (int dir = 0; dir < 2; dir++) {
    const int ldr = dir == 0 ? 0 : 1, ldc = dir == 0 ? 1 : 0;
    // What is left of the piece's old line in this direction, on either side of its square.
    for (int side = -1; side <= 1; side += 2) {
      const int nr = pr + ldr * side, nc = pc + ldc * side;
      if (!b2.inside(nr, nc) || st.board.at(nr, nc) != MACHINE || b2.at(nr, nc) != MACHINE) continue;
      rest = fmin2(rest, raceHumanTime(s2, t, raceLineThrough(b2, MACHINE, nr, nc, ldr, ldc)));
      // RE: the group it is left in (the old square is a new empty neighbour of it).
      if (t.rules.edgeCapture) rest = fmin2(rest, raceHumanTime(s2, t, raceGroupThrough(b2, t, MACHINE, nr, nc)));
      // RU read: the same for the piece itself.
      if (raceUntouchedRead(t)) rest = fmin2(rest, raceUntouchedTime(s2, t, nr, nc));
    }
  }
  s2.reactAt = reactAt;
  if (planned) {
    s2.planned = *planned;
  } else {
    s2.planned.clear();
  }
  for (int dir = 0; dir < 2; dir++) {
    const int ldr = dir == 0 ? 0 : 1, ldc = dir == 0 ? 1 : 0;
    dest = fmin2(dest, raceHumanTime(s2, t, raceLineThrough(b2, MACHINE, dr, dc, ldr, ldc)));
  }
  if (t.rules.edgeCapture) dest = fmin2(dest, raceHumanTime(s2, t, raceGroupThrough(b2, t, MACHINE, dr, dc)));
  *thDest = dest;
  *thRest = rest;
}

void racePlanned(const Board& b, const RaceLineEval ev[], int count, RaceMask* out) {
  out->clear();
  for (int i = 0; i < count; i++) {
    // Where the human is heading: the units threatened within RACE_DANGER_MS (lines and groups:
    // the threatened ones; RU units count from their longer horizon only once that near).
    if (raceInf(ev[i].th) || !(ev[i].th < RACE_DANGER_MS)) continue;
    uint8_t ends[MAX_N * MAX_N];
    const int k = raceUnitEnds(b, ev[i].line, ends);
    for (int j = 0; j < k; j++) out->set(ends[j]);
  }
}

float raceReactAt(const RaceState& st, const RaceTimes& t, int r, int c) {
  return fmax2(st.mReady[r * st.board.n + c], st.headFree) + t.react + t.engage + t.humanReact;
}

bool raceSafeDest(const RaceState& st, const RaceTimes& t, int pr, int pc, int dr, int dc, float start,
                  const RaceMask* planned) {
  float thDest, thRest;
  raceDestTimes(st, t, pr, pc, dr, dc, &thDest, &thRest, start + t.humanReact, planned);
  const float arrival = raceArrival(t, pr, pc, dr, dc, start);
  const float need1 = arrival + t.machineCooldown + t.react + t.engage + t.margin;
  return atLeast(thDest, need1) && atLeast(thRest, start + t.margin);
}

int raceAssess(const RaceState& st, const RaceTimes& t, RaceLineEval out[], float lead) {
  RaceLine lines[RACE_MAX_LINES];
  int k = raceLines(st.board, MACHINE, lines);
  if (t.rules.edgeCapture) {
    // RE: the machine's edge groups, each once, after the lines.
    const int n = st.board.n;
    RaceMask seen;
    seen.clear();
    for (int sq = 0; sq < n * n && k < RACE_MAX_LINES; sq++) {
      if (st.board.cells[sq] != MACHINE || seen.has(sq)) continue;
      EdgeGroup g;
      if (!edgeGroupAt(st.board, sq / n, sq % n, g)) {
        seen.set(sq);
        continue;
      }
      for (int w = 0; w < 3; w++) seen.w[w] |= g.cells.w[w];
      lines[k++] = RaceLine{g.first / n, g.first % n, 0, 0, g.size, MACHINE};
    }
  }
  if (raceUntouchedRead(t)) {
    // RU read: the pieces going untouched within RACE_UNTOUCHED_DANGER_MS, after the rest.
    const int n = st.board.n;
    for (int sq = 0; sq < n * n && k < RACE_MAX_LINES; sq++) {
      const RaceLine L = raceUntouchedThrough(st, t, sq / n, sq % n);
      if (L.len > 0) lines[k++] = L;
    }
  }
  int idx[RACE_MAX_LINES];
  int m = 0;
  for (int i = 0; i < k; i++) {
    RaceLineEval& e = out[i];
    e.line = lines[i];
    e.th = raceHumanTime(st, t, lines[i]);
    e.save = RACE_INF;
    const float horizon = raceIsUntouched(lines[i]) ? RACE_UNTOUCHED_DANGER_MS : RACE_DANGER_MS;
    e.danger = !raceInf(e.th) && e.th < horizon;
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
  racePlanned(st.board, out, k, &et.planned);
  const Board mb = machineBoard(st);
  const int bn = st.board.n;
  for (int j = 0; j < m; j++) {
    uint8_t sqs[MAX_N * MAX_N];
    const int count = raceUnitSquares(st.board, out[idx[j]].line, sqs);
    for (int q = 0; q < count; q++) addEscapes(t, mb, et, sqs[q] / bn, sqs[q] % bn);
  }

  float hf = st.headFree, hr = st.headR, hc = st.headC;
  // The head before each guard in the order, and the guard's piece.
  float hfAt[RACE_MAX_LINES], hrAt[RACE_MAX_LINES], hcAt[RACE_MAX_LINES];
  int gR[RACE_MAX_LINES], gC[RACE_MAX_LINES];
  int guards = 0;
  // A unit guarded by g in the order (its own guard, or covered by another unit's).
  auto mark = [&](int line, const Guard& g, int order) {
    RaceLineEval& e = out[line];
    e.guarded = true;
    e.order = order;
    e.save = g.save;
    e.guardR = g.pr;
    e.guardC = g.pc;
    e.toR = g.dr;
    e.toC = g.dc;
  };
  auto take = [&](int line, const Guard& g, int order) {
    hfAt[guards] = hf;
    hrAt[guards] = hr;
    hcAt[guards] = hc;
    gR[guards] = g.pr;
    gC[guards] = g.pc;
    guards++;
    mark(line, g, order);
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
    s.lead = lead;
    s.best = 0;
    s.bestLen = 0;
    searchOrders(s, 0, 0, 0u, hf, hr, hc);
    for (int i = 0; i < s.bestLen; i++) {
      take(idx[s.bestPath[i]], s.bestGuards[i], i);
      for (int q = 0; q < m; q++)
        if ((s.bestCover[i] >> q) & 1u) mark(idx[q], s.bestGuards[i], i);
    }
  } else {
    int order = 0;
    for (int j = 0; j < m; j++) {
      if (out[idx[j]].guarded) continue;  // covered by an earlier guard
      const Guard g = saveLine(st, t, et, out[idx[j]].line, hf, hr, hc);
      if (!guardable(t, g, out[idx[j]].th, lead)) continue;
      take(idx[j], g, order);
      for (int q = 0; q < m; q++) {
        RaceLineEval& o = out[idx[q]];
        if (q != j && !o.guarded && covers(st, t, g, out[idx[j]].line, o.line, o.th, lead)) mark(idx[q], g, order);
      }
      order++;
    }
  }
  // Lines left unguarded: save with the head after the guards, and their risk (3.7). A guard
  // that moves a piece of the line itself does not come first: the head would move that piece
  // for this line instead, so the save is taken with the head as it was before that guard.
  for (int j = 0; j < m; j++) {
    RaceLineEval& e = out[idx[j]];
    if (e.guarded) continue;
    float lf = hf, lr = hr, lc = hc;
    uint8_t sqs[MAX_N * MAX_N];
    const int count = raceUnitSquares(st.board, e.line, sqs);
    for (int o = 0; o < guards; o++) {
      bool mine = false;
      for (int q = 0; q < count && !mine; q++) mine = sqs[q] == gR[o] * bn + gC[o];
      if (!mine) continue;
      lf = hfAt[o];
      lr = hrAt[o];
      lc = hcAt[o];
      break;
    }
    const Guard g = saveLine(st, t, et, e.line, lf, lr, lc);
    e.save = g.save;
    if (raceInf(g.save)) {
      e.risk = clamp01((2500.0f - e.th) / 1000.0f);
      // RU: a unit beyond RACE_DANGER_MS whose piece has no safe move even with the head free now
      // is left to later reads.
      if (raceIsUntouched(e.line) && !(e.th < RACE_DANGER_MS) &&
          raceInf(saveLine(st, t, et, e.line, st.headFree, st.headR, st.headC).save))
        e.danger = false;
    } else {
      e.risk = clamp01(0.5f + (g.save + lead + t.margin - e.th) / 2000.0f);
    }
  }
  return k;
}

void racePieceRisk(const Board& b, const RaceLineEval ev[], int count, float riskAt[]) {
  for (int i = 0; i < b.n * b.n; i++) riskAt[i] = 0.0f;
  for (int i = 0; i < count; i++) {
    if (raceIsGroup(ev[i].line) && ev[i].risk == 0.0f) continue;
    uint8_t sqs[MAX_N * MAX_N];
    const int k = raceUnitSquares(b, ev[i].line, sqs);
    for (int j = 0; j < k; j++) riskAt[sqs[j]] = fmax2(riskAt[sqs[j]], ev[i].risk);
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
  float g = raceInf(esc) ? static_cast<float>(L.len) : static_cast<float>(L.len) * clamp01(0.5f + (esc - tm) / 2000.0f);
  if (raceUntouchedRead(t) && g > 0.0f) {
    // RU read: an end held by a machine piece the human takes by RU before T_m (+ margin).
    const int ends[2][2] = {{L.r - L.dr, L.c - L.dc}, {L.r + L.dr * L.len, L.c + L.dc * L.len}};
    for (const auto& e : ends) {
      const float tu = raceUntouchedTime(st, t, e[0], e[1]);
      if (!raceInf(tu) && tu < tm + t.margin) g *= 1.0f - clamp01(0.5f + (tm + t.margin - tu) / 2000.0f);
    }
  }
  return g;
}
