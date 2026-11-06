#include "Ai.h"
#include "RaceTime.h"

// Move choice of plan 2026-10-31 3.9-3.11: the race of RaceTime (how soon the human can close a
// machine line against how soon the machine can start pulling a piece out of it) decides which
// candidates are safe; among the safe ones the attack (gain) and shape points decide; without a
// safe one, the candidate losing the least is chosen. Pure function of the snapshot, float only
// (+ - * / and sqrt), so that every build picks the same move (builds use -ffp-contract=off).
//
// The rule candidates of plan 2026-11-06 3.1 come with the snapshot: captures are captures() of
// Capture.h (RE, RU), RE's edge groups are threats of their own (group units of RaceTime), RW
// leaves wall-making moves out of the candidates, and in the opening (mustMove) "stay" does not
// contend.

namespace {

// Tuning knobs of 3.13 that live here (the margin and the threatened-line horizon are in
// RaceTime: raceTimes() and RACE_DANGER_MS).
constexpr float FORK_PENALTY = 60.0f;  // Hard: a top safe candidate a human threat move breaks
constexpr int FORK_TOP = 3;            // safe candidates checked for the fork penalty
constexpr int FORK_THREATS = 12;       // human threat moves checked per candidate
// Shape weights (3.9): advance (sum of r/(n-1)), human pieces beside a machine piece, machine
// pieces on the board's edge, vertical pairs of machine pieces.
constexpr float SHAPE_ADVANCE = 30.0f, SHAPE_ADJACENT = 8.0f, SHAPE_EDGE = 4.0f, SHAPE_PAIR = 4.0f;
constexpr float TIE_EPS = 0.01f;

float fmax2(float a, float b) { return a > b ? a : b; }
float fmin2(float a, float b) { return a < b ? a : b; }
float clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }
bool atLeast(float t, float need) { return raceInf(t) || t >= need; }

// 32-bit FNV-1a.
struct Fnv {
  uint32_t h = 2166136261u;
  void byte(uint32_t b) {
    h ^= b & 0xFFu;
    h *= 16777619u;
  }
  void u32(uint32_t v) {
    for (int i = 0; i < 4; i++) byte(v >> (8 * i));
  }
};

// Tie-break hash of 3.9: board, pieces and clock (cooldowns and clock in 250 ms units).
uint32_t snapshotHash(const Snapshot& s) {
  Fnv f;
  const int n = s.board.n;
  f.byte(static_cast<uint32_t>(n));
  for (int i = 0; i < n * n; i++) f.byte(static_cast<uint32_t>(s.board.cells[i]));
  for (int i = 0; i < s.count; i++) {
    const PieceView& q = s.pieces[i];
    f.u32(static_cast<uint32_t>(q.id));
    f.byte(static_cast<uint32_t>(q.side));
    f.byte(static_cast<uint32_t>(q.r));
    f.byte(static_cast<uint32_t>(q.c));
    f.byte((q.alive ? 1u : 0u) | (q.held ? 2u : 0u));
    const uint32_t rem = q.cooldownUntil > s.clockMs ? q.cooldownUntil - s.clockMs : 0;
    f.u32((rem + 249u) / 250u);
  }
  f.u32(s.clockMs / 250u);
  return f.h;
}

uint32_t candidateHash(uint32_t base, int index) {
  Fnv f;
  f.h = base;
  f.u32(static_cast<uint32_t>(index));
  return f.h;
}

// Whether a (score, hash) beats b: higher by more than TIE_EPS, or within it with a smaller hash.
bool beats(float sa, uint32_t ha, float sb, uint32_t hb) {
  if (sa > sb + TIE_EPS) return true;
  if (sb > sa + TIE_EPS) return false;
  return ha < hb;
}

// The board the machine moves on: blocked squares (held origins, the pending target, the
// reserved square) count as occupied.
Board machineBoard(const RaceState& st) {
  Board b = st.board;
  for (int i = 0; i < b.n * b.n; i++)
    if (st.blocked[i] && b.cells[i] == EMPTY) b.cells[i] = HUMAN;
  return b;
}

float shapePoints(const Board& b) {
  const int n = b.n;
  const float denom = static_cast<float>(n - 1);
  float adv = 0.0f;
  int adj = 0, edge = 0, pairs = 0;
  for (int i = 0; i < n * n; i++) {
    if (b.cells[i] != MACHINE) continue;
    const int r = i / n, c = i % n;
    adv += static_cast<float>(r) / denom;
    for (int d = 0; d < 4; d++) {
      const int rr = r + DIR_R[d], cc = c + DIR_C[d];
      if (b.inside(rr, cc) && b.at(rr, cc) == HUMAN) adj++;
    }
    if (r == 0 || r == n - 1 || c == 0 || c == n - 1) edge++;
    if (r + 1 < n && b.at(r + 1, c) == MACHINE) pairs++;
  }
  return adv * SHAPE_ADVANCE + static_cast<float>(adj) * SHAPE_ADJACENT + static_cast<float>(edge) * SHAPE_EDGE +
         static_cast<float>(pairs) * SHAPE_PAIR;
}

// Sum of the gain (3.8) over the human lines of st. With riskAt (the nothing-safe score), a
// line's gain is scaled by 1 - the largest risk of the machine pieces on its ends: a capture
// that needs a machine piece the human is about to take does not count.
float gainSum(const RaceState& st, const RaceTimes& t, const float* riskAt = nullptr) {
  RaceLine lines[RACE_MAX_LINES];
  const Board& b = st.board;
  const int k = raceLines(b, HUMAN, lines);
  float sum = 0.0f;
  for (int i = 0; i < k; i++) {
    const RaceLine& L = lines[i];
    float g = raceGain(st, t, L);
    if (riskAt && g > 0.0f) {
      const int ends[2][2] = {{L.r - L.dr, L.c - L.dc}, {L.r + L.dr * L.len, L.c + L.dc * L.len}};
      float r = 0.0f;
      for (const auto& e : ends)
        if (b.inside(e[0], e[1]) && b.at(e[0], e[1]) == MACHINE) r = fmax2(r, riskAt[e[0] * b.n + e[1]]);
      g *= 1.0f - r;
    }
    sum += g;
  }
  return sum;
}

// (c) of 3.9: every threatened line is guarded in the order 3.7 picks.
bool allGuarded(const RaceLineEval ev[], int k) {
  for (int i = 0; i < k; i++)
    if (ev[i].danger && !ev[i].guarded) return false;
  return true;
}

// Least T_h of the two lines (and, under RE, the group) through the machine piece at (r,c) on st.
float lineTimeAt(const RaceState& st, const RaceTimes& t, int r, int c) {
  float th = RACE_INF;
  for (int dir = 0; dir < 2; dir++)
    th = fmin2(th, raceHumanTime(st, t, raceLineThrough(st.board, MACHINE, r, c, dir, 1 - dir)));
  if (t.rules.edgeCapture) th = fmin2(th, raceHumanTime(st, t, raceGroupThrough(st.board, t, MACHINE, r, c)));
  return th;
}

// raceDestTimes on a state already moved: before = st, after = st with (pr,pc) -> (dr,dc). The
// rest of the old lines is read as it is; then after takes the human's reaction to the move
// (reactAt, planned) and keeps it, and the destination's lines are read with it.
void destTimes(const RaceState& before, RaceState& after, const RaceTimes& t, int pr, int pc, int dr, int dc,
               float reactAt, const RaceMask& planned, float* thDest, float* thRest) {
  const Board& b2 = after.board;
  float dest = RACE_INF, rest = RACE_INF;
  for (int dir = 0; dir < 2; dir++) {
    const int ldr = dir == 0 ? 0 : 1, ldc = dir == 0 ? 1 : 0;
    for (int side = -1; side <= 1; side += 2) {
      const int nr = pr + ldr * side, nc = pc + ldc * side;
      if (!b2.inside(nr, nc) || before.board.at(nr, nc) != MACHINE || b2.at(nr, nc) != MACHINE) continue;
      rest = fmin2(rest, raceHumanTime(after, t, raceLineThrough(b2, MACHINE, nr, nc, ldr, ldc)));
      if (t.rules.edgeCapture)
        rest = fmin2(rest, raceHumanTime(after, t, raceGroupThrough(b2, t, MACHINE, nr, nc)));
    }
  }
  after.reactAt = reactAt;
  after.planned = planned;
  for (int dir = 0; dir < 2; dir++) {
    const int ldr = dir == 0 ? 0 : 1, ldc = dir == 0 ? 1 : 0;
    dest = fmin2(dest, raceHumanTime(after, t, raceLineThrough(b2, MACHINE, dr, dc, ldr, ldc)));
  }
  if (t.rules.edgeCapture) dest = fmin2(dest, raceHumanTime(after, t, raceGroupThrough(b2, t, MACHINE, dr, dc)));
  *thDest = dest;
  *thRest = rest;
}

// A machine line on the board as it is and its T_h (only lines the human can close), small to
// keep the ESP32 think stack low. A group unit (RE) has group >= 0: its squares are those with
// that number in Ctx::groupOf, its ends its empty neighbours.
struct OpenLine {
  int8_t r, c, dr, dc, len;
  int8_t group;  // -1 for a line
  float th;
};

struct Ctx {
  const Snapshot* s;
  RaceTimes t;
  float aggr;
  bool fork;
  uint32_t hash;
  OpenLine open[RACE_MAX_LINES];
  int openCount;
  RaceMask planned;  // ends of the threatened lines on the board as it is
  const Board* board;  // the board as it is (RaceState::board)
  int8_t groupOf[MAX_N * MAX_N];  // open group unit of each square, -1 for none
};

bool onLine(const Ctx& x, const OpenLine& L, int r, int c) {
  if (L.group >= 0) return x.board->inside(r, c) && x.groupOf[r * x.board->n + c] == L.group;
  for (int k = 0; k < L.len; k++)
    if (L.r + L.dr * k == r && L.c + L.dc * k == c) return true;
  return false;
}

bool isEnd(const Ctx& x, const OpenLine& L, int r, int c) {
  if (L.group >= 0) {
    const Board& b = *x.board;
    if (!b.inside(r, c) || b.at(r, c) != EMPTY) return false;
    for (int d = 0; d < 4; d++) {
      const int rr = r + DIR_R[d], cc = c + DIR_C[d];
      if (b.inside(rr, cc) && x.groupOf[rr * b.n + cc] == L.group) return true;
    }
    return false;
  }
  return (r == L.r - L.dr && c == L.c - L.dc) || (r == L.r + L.dr * L.len && c == L.c + L.dc * L.len);
}

// The squares of an open line or group unit on the board as it is. Returns the count.
int openSquares(const Ctx& x, const OpenLine& L, uint8_t out[]) {
  const int n = x.board->n;
  if (L.group < 0) {
    for (int k = 0; k < L.len; k++) out[k] = static_cast<uint8_t>((L.r + L.dr * k) * n + (L.c + L.dc * k));
    return L.len;
  }
  int k = 0;
  for (int sq = 0; sq < n * n; sq++)
    if (x.groupOf[sq] == L.group) out[k++] = static_cast<uint8_t>(sq);
  return k;
}

// One candidate: the moved piece (index into s.pieces, -1 for "stay") and its destination.
struct Cand {
  int index;  // candidate number (0 = stay)
  int piece;
  int toR, toC;
};

// The move part of a candidate (3.9): start of pulling, arrival D, (a) and (b), and st2 = the
// state after it (captures removed, head busy until D). For "stay", st2 = st.
struct MoveInfo {
  float start, arrival;
  float reserveAt;  // when the piece takes its destination square (the drag reserves it)
  int caps;
  float thOrig;  // least T_h of the moved piece's lines before it starts
  bool pathOpen;  // no human piece can block the drag path before the drag gets there
  bool safeAB;    // (a), (b) and the path
};

// When a move starts to count for a line on the board as it is: when the piece takes the line's
// end, or else when it arrives.
float takesEffect(const Ctx& x, const OpenLine& o, const Cand& c, const MoveInfo& mi) {
  return isEnd(x, o, c.toR, c.toC) ? mi.reserveAt : mi.arrival;
}

// Whether the drag (fr,fc) -> (tr,tc), starting at `start` and entering one square per
// 1/dragSpeed, gets through: every square of its path that is an end of a threatened line on the
// board as it is (where the human is heading) is entered at least margin before a human piece
// can be put on it in one move (the placement interval does not matter: a pending placement
// claims the square at once). A piece put there stops the drag: the machine piece stays where it
// is, with its cooldown.
bool pathOpen(const Ctx& x, const RaceState& st, int fr, int fc, int tr, int tc, float start) {
  const RaceTimes& t = x.t;
  const int n = st.board.n;
  const int dr = tr > fr ? 1 : tr < fr ? -1 : 0, dc = tc > fc ? 1 : tc < fc ? -1 : 0;
  int i = 0;
  for (int r = fr + dr, c = fc + dc;; r += dr, c += dc, i++) {
    bool end = false;
    for (int k = 0; k < x.openCount && !end; k++) end = x.open[k].th < RACE_DANGER_MS && isEnd(x, x.open[k], r, c);
    if (end) {
      const int sq = r * n + c;
      const float enter = start + static_cast<float>(i) / t.dragSpeed * 1000.0f;
      for (int hi = 0; hi < st.hCount; hi++) {
        if (!st.h[hi].alive || st.h[hi].pending || !st.h[hi].reach1.has(sq)) continue;
        const float e = raceHumanPlace(st, t, hi, sq);
        if (!raceInf(e) && e < enter + t.margin) return false;  // put there (pending or not)
      }
    }
    if (r == tr && c == tc) return true;
  }
}

MoveInfo applyCandidate(const Ctx& x, const RaceState& st, const Cand& c, RaceState& st2) {
  MoveInfo mi{0.0f, 0.0f, 0.0f, 0, RACE_INF, true, true};
  st2 = st;
  if (c.piece < 0) return mi;
  const RaceTimes& t = x.t;
  const PieceView& q = x.s->pieces[c.piece];
  mi.start = raceTravel(t, q.r, q.c, st.headR, st.headC) + t.engage;
  mi.arrival = raceArrival(t, q.r, q.c, c.toR, c.toC, mi.start);
  mi.thOrig = lineTimeAt(st, t, q.r, q.c);
  const bool safeA = atLeast(mi.thOrig, mi.start + t.margin);
  mi.caps = raceMoveMachine(st2, t, q.r, q.c, c.toR, c.toC, mi.arrival);
  float thDest, thRest;
  destTimes(st, st2, t, q.r, q.c, c.toR, c.toC, mi.start + t.humanReact, x.planned, &thDest, &thRest);
  const bool safeB = atLeast(thDest, mi.arrival + t.machineCooldown + t.react + t.engage + t.margin) &&
                     atLeast(thRest, mi.start + t.margin);
  // The other lines on the board as it is stay as they are until the move takes effect: the
  // arrival, or for a line whose end the piece fills, the moment it takes that square (the drag
  // reserves each square as it enters it). (c) looks at the board after the move only.
  bool safeOthers = true;
  const int dist = (c.toR > q.r ? c.toR - q.r : q.r - c.toR) + (c.toC > q.c ? c.toC - q.c : q.c - c.toC);
  mi.reserveAt = mi.start + static_cast<float>(dist - 1) / t.dragSpeed * 1000.0f;
  for (int i = 0; i < x.openCount && safeOthers; i++) {
    const OpenLine& o = x.open[i];
    if (onLine(x, o, q.r, q.c)) continue;  // (a) and (b)
    if (o.th < takesEffect(x, o, c, mi) + t.margin) safeOthers = false;
  }
  // The piece must also get through: a human piece put on a square of the drag path before the
  // drag enters it stops the piece where it is (with its cooldown), and nothing above holds.
  mi.pathOpen = pathOpen(x, st, q.r, q.c, c.toR, c.toC, mi.start);
  mi.safeAB = safeA && safeB && safeOthers && mi.pathOpen;
  return mi;
}

float timePenalty(const Cand& c, const MoveInfo& mi, float extra) {
  return c.piece < 0 ? 0.0f : mi.arrival / 1000.0f * 4.0f + extra;
}

float attackPoints(const Ctx& x, const RaceState& st2, const MoveInfo& mi, const float* riskAt = nullptr) {
  return static_cast<float>(mi.caps) * 100.0f + gainSum(st2, x.t, riskAt) * (40.0f + 40.0f * x.aggr);
}

// Score of a safe candidate (3.9), without the fork penalty.
float safeScore(const Ctx& x, const Cand& c, const MoveInfo& mi, const RaceState& st2) {
  return attackPoints(x, st2, mi) + x.aggr * shapePoints(st2.board) - timePenalty(c, mi, 2.0f);
}

// The part of a candidate's captures (in pieces) that may not happen: a captured run whose far
// end (the machine piece it is caught against) is in a line on the board as it is that the
// human closes before the capturing piece arrives counts as lost with the risk of 3.7 (the
// arrival as save).
float capturesLost(const Ctx& x, const RaceState& st, const Cand& c, const MoveInfo& mi) {
  if (c.piece < 0 || mi.caps == 0) return 0.0f;
  const RaceTimes& t = x.t;
  const PieceView& q = x.s->pieces[c.piece];
  Board b = st.board;
  for (int i = 0; i < st.hCount; i++)
    if (st.h[i].alive && st.h[i].pending) b.set(st.h[i].r, st.h[i].c, EMPTY);  // not landed yet
  b.set(q.r, q.c, EMPTY);
  b.set(c.toR, c.toC, MACHINE);
  float lost = 0.0f;
  for (int d = 0; d < 4; d++) {
    int len = 0, r = c.toR + DIR_R[d], cc = c.toC + DIR_C[d];
    while (b.inside(r, cc) && b.at(r, cc) == HUMAN) {
      len++;
      r += DIR_R[d];
      cc += DIR_C[d];
    }
    if (len == 0 || !b.inside(r, cc) || b.at(r, cc) != MACHINE) continue;
    float ro = 0.0f;
    for (int i = 0; i < x.openCount; i++) {
      const OpenLine& o = x.open[i];
      if (!onLine(x, o, r, cc) || onLine(x, o, q.r, q.c) || o.th >= mi.arrival + t.margin) continue;
      ro = fmax2(ro, clamp01(0.5f + (mi.arrival + t.margin - o.th) / 2000.0f));
    }
    lost += static_cast<float>(len) * ro;
  }
  return lost;
}

// Score when no candidate is safe (3.9): the risk of each machine piece (the larger of its
// lines') counts against it. A moved piece that cannot start before its old line closes ((a)
// fails) carries the risk of that line too, and so do the pieces of a line on the board as it is
// that closes before the move takes effect (the risk of 3.7 with the move's effect as save).
// "Stay" measures its saves as of the next read (lead = the read interval). The gain of a human
// line counts only as much as the machine pieces on its ends are not at risk.
float unsafeScore(const Ctx& x, const RaceState& st, const Cand& c, const MoveInfo& mi, const RaceState& st2,
                  RaceLineEval ev[]) {
  const RaceTimes& t = x.t;
  const int k = raceAssess(st2, t, ev, c.piece < 0 ? t.readInterval : 0.0f);
  float riskAt[MAX_N * MAX_N];
  racePieceRisk(st2.board, ev, k, riskAt);
  const int n = st2.board.n;
  if (c.piece >= 0 && (!atLeast(mi.thOrig, mi.start + t.margin) || !mi.pathOpen)) {
    // (a) fails: the risk of the old line with the start as save; the path can be blocked: the
    // piece stays in its old line, stuck there (save INF).
    float ra = 0.0f;
    if (!atLeast(mi.thOrig, mi.start + t.margin)) ra = clamp01(0.5f + (mi.start + t.margin - mi.thOrig) / 2000.0f);
    if (!mi.pathOpen) ra = fmax2(ra, clamp01((2500.0f - mi.thOrig) / 1000.0f));
    float& cell = riskAt[c.toR * n + c.toC];
    if (st2.board.cells[c.toR * n + c.toC] == MACHINE) cell = fmax2(cell, ra);
  }
  if (c.piece >= 0) {
    const PieceView& q = x.s->pieces[c.piece];
    for (int i = 0; i < x.openCount; i++) {
      const OpenLine& o = x.open[i];
      if (onLine(x, o, q.r, q.c)) continue;
      const float effect = takesEffect(x, o, c, mi);
      if (o.th >= effect + t.margin) continue;
      const float ro = clamp01(0.5f + (effect + t.margin - o.th) / 2000.0f);
      uint8_t sqs[MAX_N * MAX_N];
      const int count = openSquares(x, o, sqs);
      for (int k2 = 0; k2 < count; k2++) {
        const int sq = sqs[k2];
        if (st2.board.cells[sq] == MACHINE) riskAt[sq] = fmax2(riskAt[sq], ro);
      }
    }
  }
  float risk = 0.0f;
  for (int i = 0; i < n * n; i++)
    if (st2.board.cells[i] == MACHINE) risk += riskAt[i];
  return attackPoints(x, st2, mi, riskAt) - capturesLost(x, st, c, mi) * 100.0f - risk * 100.0f -
         timePenalty(c, mi, 0.0f);
}

// A human threat move of 3.9: piece index hi to square sq at time e.
struct Threat {
  int hi, sq;
  float e;
};

// Up to FORK_THREATS threat moves on st2, earliest e first (then piece order, then dests order).
// Not inlined (nor forkBroken): their frames then add to the think stack only during the fork
// check, not under every candidate's assessment.
__attribute__((noinline)) int threatMoves(const RaceState& st2, const RaceTimes& t, Threat out[]) {
  const Board& b = st2.board;
  const int n = b.n;
  RaceMask ends, onEnd, block;
  ends.clear();
  onEnd.clear();
  block.clear();
  {
    RaceLine lines[RACE_MAX_LINES];
    const int k = raceLines(b, MACHINE, lines);
    const Board mb = machineBoard(st2);
    for (int i = 0; i < k; i++) {
      const RaceLine& L = lines[i];
      const int ar = L.r - L.dr, ac = L.c - L.dc, br = L.r + L.dr * L.len, bc = L.c + L.dc * L.len;
      if (!b.inside(ar, ac) || !b.inside(br, bc)) continue;  // a line on the edge cannot be closed
      const int ends2[2] = {ar * n + ac, br * n + bc};
      for (int e : ends2) {
        if (b.cells[e] == EMPTY && !st2.blocked[e]) ends.set(e);  // (i)
        if (b.cells[e] == HUMAN) onEnd.set(e);                     // (ii)
      }
      const float th = raceHumanTime(st2, t, L);
      if (raceInf(th) || th >= RACE_DANGER_MS) continue;
      for (int q = 0; q < L.len; q++) {  // (iii)
        int d[MAX_N * MAX_N];
        const int kd = dests(mb, L.r + L.dr * q, L.c + L.dc * q, d);
        for (int j = 0; j < kd; j++) block.set(d[j]);
      }
    }
    if (t.rules.edgeCapture) {
      // RE: the edge groups the human can enclose. (i) their empty neighbours, (ii) with none
      // left, the human pieces next to them (step away and back), (iii) as for the lines.
      RaceMask seen;
      seen.clear();
      for (int sq = 0; sq < n * n; sq++) {
        if (b.cells[sq] != MACHINE || seen.has(sq)) continue;
        EdgeGroup g;
        if (!edgeGroupAt(b, sq / n, sq % n, g)) {
          seen.set(sq);
          continue;
        }
        for (int w = 0; w < 3; w++) seen.w[w] |= g.cells.w[w];
        if (g.libCount > RACE_GROUP_MAX_FILL) continue;
        for (int i = 0; i < g.libCount; i++)
          if (!st2.blocked[g.libs[i]]) ends.set(g.libs[i]);
        for (int q = 0; q < n * n; q++) {
          if (!g.cells.has(q)) continue;
          for (int d = 0; d < 4; d++) {
            const int rr = q / n + DIR_R[d], cc = q % n + DIR_C[d];
            if (g.libCount == 0 && b.inside(rr, cc) && b.at(rr, cc) == HUMAN) onEnd.set(rr * n + cc);
          }
        }
        const float th = raceHumanTime(st2, t, RaceLine{g.first / n, g.first % n, 0, 0, g.size, MACHINE});
        if (raceInf(th) || th >= RACE_DANGER_MS) continue;
        for (int q = 0; q < n * n; q++) {
          if (!g.cells.has(q)) continue;
          int d[MAX_N * MAX_N];
          const int kd = dests(mb, q / n, q % n, d);
          for (int j = 0; j < kd; j++) block.set(d[j]);
        }
      }
    }
  }
  // While a piece is held (not pending) nothing else can be lifted: only it moves.
  int heldIdx = -1;
  for (int i = 0; i < st2.hCount; i++)
    if (st2.h[i].alive && st2.h[i].held) heldIdx = i;
  int count = 0;
  for (int hi = 0; hi < st2.hCount; hi++) {
    const RaceHuman& h = st2.h[hi];
    if (!h.alive || h.pending || (heldIdx >= 0 && hi != heldIdx)) continue;
    const bool hOnEnd = !h.held && onEnd.has(h.r * n + h.c);
    int d[MAX_N * MAX_N];
    const int kd = dests(b, h.r, h.c, d);
    for (int j = 0; j < kd; j++) {
      const int sq = d[j];
      if (sq == st2.reserved || !h.reach1.has(sq)) continue;
      if (!ends.has(sq) && !hOnEnd && !block.has(sq)) continue;
      const float e0 = raceHumanPlace(st2, t, hi, sq);
      if (raceInf(e0)) continue;
      const float e = fmax2(e0, st2.firstPlace);
      // Insert after the threats with e <= this one; drop the last beyond FORK_THREATS.
      int pos = count;
      while (pos > 0 && out[pos - 1].e > e) pos--;
      if (pos >= FORK_THREATS) continue;
      const int last = count < FORK_THREATS ? count : FORK_THREATS - 1;
      for (int m = last; m > pos; m--) out[m] = out[m - 1];
      out[pos] = Threat{hi, sq, e};
      if (count < FORK_THREATS) count++;
    }
  }
  return count;
}

// Whether a threat move breaks candidate c: on the board after c and the threat move (the human
// clock advanced by e), some threatened line cannot be guarded (3.9). A threat move that captures
// at once is a line already found threatened and guarded after c, so it does not count. st3 is
// scratch (the state after c is rebuilt in it for each threat move, to keep one state off the
// stack).
__attribute__((noinline)) bool forkBroken(const Ctx& x, const RaceState& st, const Cand& c, RaceState& st3,
                                          RaceLineEval ev[]) {
  const RaceTimes& t = x.t;
  applyCandidate(x, st, c, st3);
  Threat th[FORK_THREATS];
  const int k = threatMoves(st3, t, th);
  const int n = st3.board.n;
  for (int i = 0; i < k; i++) {
    if (i > 0) applyCandidate(x, st, c, st3);
    RaceHuman& h = st3.h[th[i].hi];
    const int fr = h.r, fc = h.c, tr = th[i].sq / n, tc = th[i].sq % n;
    const float e = th[i].e;
    if (h.held) {
      st3.blocked[fr * n + fc] = false;
    } else {
      st3.board.set(fr, fc, EMPTY);
    }
    st3.board.set(tr, tc, HUMAN);
    st3.stale.reset(fr * n + fc);
    st3.stale.reset(tr * n + tc);
    int caps[MAX_N * MAX_N];
    if (captures(st3.board, t.rules, HUMAN, tr, tc, caps, &st3.stale) > 0) continue;
    h.r = tr;
    h.c = tc;
    h.held = false;
    h.pending = false;
    h.ready = t.humanCooldown;
    h.holdLeft = RACE_INF;
    for (int j = 0; j < st3.hCount; j++) {
      RaceHuman& o = st3.h[j];
      if (j == th[i].hi || !o.alive) continue;
      // A pending placement lands before the threat move (it is on st2's board already).
      o.pending = false;
      o.ready = fmax2(0.0f, o.ready - e);
      if (o.held && !raceInf(o.holdLeft)) o.holdLeft -= e;
    }
    st3.gap = t.humanInterval;
    st3.firstPlace = st3.gap;
    st3.firstLift = 0.0f;
    st3.headFree = fmax2(0.0f, st3.headFree - e);
    st3.reactAt = fmax2(0.0f, st3.reactAt - e);
    for (int q = 0; q < n * n; q++) st3.mReady[q] = fmax2(0.0f, st3.mReady[q] - e);
    raceRebuild(st3, fr, fc, tr, tc);
    const int m = raceAssess(st3, t, ev);
    if (!allGuarded(ev, m)) return true;
  }
  return false;
}

// Calls f(cand) for "stay" and every legal move of a cooled-down machine piece, in order (RW: not
// the moves that make a wall).
template <typename F>
void forEachCandidate(const Snapshot& s, const RaceState& st, F f) {
  f(Cand{0, -1, -1, -1});
  const Board mb = machineBoard(st);
  const int n = mb.n;
  int index = 1;
  for (int i = 0; i < s.count; i++) {
    const PieceView& q = s.pieces[i];
    if (!q.alive || q.held || q.side != MACHINE || s.clockMs < q.cooldownUntil) continue;
    if (!mb.inside(q.r, q.c) || mb.at(q.r, q.c) != MACHINE) continue;
    int d[MAX_N * MAX_N];
    const int k = dests(mb, q.r, q.c, d);
    for (int j = 0; j < k; j++) {
      if (s.wallLimit && completesWall(mb, MACHINE, q.r, q.c, d[j] / n, d[j] % n)) continue;
      f(Cand{index++, i, d[j] / n, d[j] % n});
    }
  }
}

struct Ranked {
  Cand c;
  float score;
  uint32_t hash;
  float penalty;
};

AiMove toMove(const Snapshot& s, const Cand& c, float score) {
  if (c.piece < 0) return AiMove{true, -1, -1, -1, score};
  return AiMove{false, s.pieces[c.piece].id, c.toR, c.toC, score};
}

AiCandidate toCandidate(const Snapshot& s, const Cand& c, bool safe) {
  AiCandidate a{};
  a.none = c.piece < 0;
  a.pieceId = c.piece < 0 ? -1 : s.pieces[c.piece].id;
  a.toR = c.toR;
  a.toC = c.toC;
  a.safe = safe;
  return a;
}

AiMove run(const Snapshot& s, const Params& p, AiDebug* dbg, AiCandidate* list, int cap, int* listCount) {
  Ctx x;
  x.s = &s;
  x.t = raceTimes(s, p);
  x.aggr = p.aggression;
  x.fork = p.level == Level::Hard;
  x.hash = snapshotHash(s);
  const RaceTimes& t = x.t;

  RaceState st;
  raceInit(st, s, t);
  if (!p.readLift) {
    // Held pieces are not read (their origins stay closed to the machine).
    for (int i = 0; i < st.hCount; i++) {
      if (!st.h[i].held) continue;
      st.h[i].alive = false;
      st.h[i].reach1.clear();
      st.h[i].reach2.clear();
    }
  }
  RaceState st2;
  RaceLineEval ev[RACE_MAX_LINES];
  const int n = st.board.n;

  // What the read sees on the board as it is ("stay"'s (c)): staying guards a line only if the
  // guard can still start at the next read, so every save counts the read interval on top.
  const int k0 = raceAssess(st, t, ev, t.readInterval);
  if (dbg) {
    racePieceRisk(st.board, ev, k0, dbg->pieceRisk);
    for (int i = n * n; i < MAX_N * MAX_N; i++) dbg->pieceRisk[i] = 0.0f;
    dbg->lineCount = k0;
    for (int i = 0; i < k0; i++) {
      const RaceLine& L = ev[i].line;
      dbg->lines[i] = AiLineRisk{L.r, L.c, L.dr, L.dc, L.len, ev[i].risk};
    }
    dbg->candidates = 0;
    dbg->safeCandidates = 0;
    dbg->forkChecked = 0;
    dbg->forkPenalized = 0;
  }
  // In the opening the machine must make its move: "stay" never contends.
  const bool staySafe = allGuarded(ev, k0) && !s.mustMove;
  racePlanned(st.board, ev, k0, &x.planned);
  x.board = &st.board;
  for (int i = 0; i < MAX_N * MAX_N; i++) x.groupOf[i] = -1;
  x.openCount = 0;
  int8_t groups = 0;
  for (int i = 0; i < k0; i++)
    if (!raceInf(ev[i].th)) {
      const RaceLine& L = ev[i].line;
      int8_t group = -1;
      if (raceIsGroup(L)) {
        group = groups++;
        uint8_t sqs[MAX_N * MAX_N];
        const int count = raceUnitSquares(st.board, L, sqs);
        for (int q = 0; q < count; q++) x.groupOf[sqs[q]] = group;
      }
      x.open[x.openCount++] = OpenLine{static_cast<int8_t>(L.r), static_cast<int8_t>(L.c), static_cast<int8_t>(L.dr),
                                       static_cast<int8_t>(L.dc), static_cast<int8_t>(L.len), group, ev[i].th};
    }

  // Pass 1: safety of every candidate, and the best FORK_TOP + 1 safe ones by score.
  Ranked top[FORK_TOP + 1];
  int topCount = 0, safeCount = 0, total = 0;
  forEachCandidate(s, st, [&](const Cand& c) {
    total++;
    bool safe;
    float score = 0.0f;
    if (c.piece < 0) {
      safe = staySafe;
      if (safe) score = safeScore(x, c, MoveInfo{0.0f, 0.0f, 0.0f, 0, RACE_INF, true, true}, st);
    } else {
      const MoveInfo mi = applyCandidate(x, st, c, st2);
      safe = mi.safeAB;
      if (safe) {
        const int k = raceAssess(st2, t, ev);
        safe = allGuarded(ev, k);
      }
      if (safe) score = safeScore(x, c, mi, st2);
    }
    if (list && c.index < cap) list[c.index] = toCandidate(s, c, safe);
    if (!safe) return;
    safeCount++;
    if (list && c.index < cap) list[c.index].score = score;
    const uint32_t h = candidateHash(x.hash, c.index);
    int pos = topCount;
    while (pos > 0 && beats(score, h, top[pos - 1].score, top[pos - 1].hash)) pos--;
    if (pos > FORK_TOP) return;
    const int last = topCount < FORK_TOP + 1 ? topCount : FORK_TOP;
    for (int m = last; m > pos; m--) top[m] = top[m - 1];
    top[pos] = Ranked{c, score, h, 0.0f};
    if (topCount < FORK_TOP + 1) topCount++;
  });
  if (dbg) {
    dbg->candidates = total;
    dbg->safeCandidates = safeCount;
  }
  if (listCount) *listCount = total < cap ? total : cap;

  if (safeCount > 0) {
    // Fork penalty (Hard): the top FORK_TOP safe candidates against the human's threat moves.
    if (x.fork) {
      const int checks = topCount < FORK_TOP ? topCount : FORK_TOP;
      for (int i = 0; i < checks; i++) {
        if (dbg) dbg->forkChecked++;
        if (forkBroken(x, st, top[i].c, st2, ev)) {
          top[i].penalty = FORK_PENALTY;
          if (dbg) dbg->forkPenalized++;
        }
      }
    }
    int best = 0;
    for (int i = 1; i < topCount; i++)
      if (beats(top[i].score - top[i].penalty, top[i].hash, top[best].score - top[best].penalty, top[best].hash))
        best = i;
    if (list) {
      // Every safe candidate contends; the penalty applies to the checked ones.
      for (int i = 0; i < *listCount; i++) {
        if (!list[i].safe) continue;
        list[i].contending = true;
        for (int j = 0; j < topCount; j++)
          if (top[j].c.index == i) {
            list[i].penalty = top[j].penalty;
            list[i].score = top[j].score - top[j].penalty;
          }
      }
    }
    return toMove(s, top[best].c, top[best].score - top[best].penalty);
  }

  // Pass 2: nothing is safe; the least expected loss with the attack kept.
  bool have = false;
  Cand bestC{0, -1, -1, -1};
  float bestS = 0.0f;
  uint32_t bestH = 0;
  forEachCandidate(s, st, [&](const Cand& c) {
    if (c.piece < 0 && s.mustMove) return;
    const MoveInfo mi = applyCandidate(x, st, c, st2);
    const float score = unsafeScore(x, st, c, mi, st2, ev);
    const uint32_t h = candidateHash(x.hash, c.index);
    if (list && c.index < cap) {
      list[c.index].contending = true;
      list[c.index].score = score;
    }
    if (!have || beats(score, h, bestS, bestH)) {
      have = true;
      bestC = c;
      bestS = score;
      bestH = h;
    }
  });
  return toMove(s, bestC, bestS);
}

}  // namespace

AiMove think(const Snapshot& s, const Params& p, AiDebug* dbg) { return run(s, p, dbg, nullptr, 0, nullptr); }

int thinkCandidates(const Snapshot& s, const Params& p, AiCandidate out[], int cap) {
  int count = 0;
  run(s, p, nullptr, out, cap, &count);
  return count;
}
