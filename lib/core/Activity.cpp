#include "Activity.h"
#include "Capture.h"

namespace {

uint32_t lineKey(const RaceLine& L) {
  return static_cast<uint32_t>(L.r) | static_cast<uint32_t>(L.c) << 4 | static_cast<uint32_t>(L.dr) << 8 |
         static_cast<uint32_t>(L.dc) << 9 | static_cast<uint32_t>(L.len) << 10;
}

bool sameSet(const SquareSet& a, const SquareSet& b) {
  return a.w[0] == b.w[0] && a.w[1] == b.w[1] && a.w[2] == b.w[2];
}

// Square key independent of the board size (for the moves).
int sq16(int r, int c) { return r * 16 + c; }

}  // namespace

void ActivityMeter::reset() {
  s_ = ActivityStats{};
  s_.firstMoveId = s_.firstMoveR = s_.firstMoveC = -1;
  s_.firstCaptureMs = ACTIVITY_NONE;
  started_ = false;
  lastClock_ = 0;
  nextThreatAt_ = 0;
  leadSign_ = 0;
  lineCount_ = 0;
  groupCount_ = 0;
  for (int i = 0; i <= 2 * MAX_N; i++) lastFrom_[i] = lastTo_[i] = -1;
}

void ActivityMeter::machineMove(int id, int fr, int fc, int tr, int tc) {
  if (id < 1 || id > 2 * MAX_N) return;
  s_.moves++;
  const int from = sq16(fr, fc), to = sq16(tr, tc);
  if (lastFrom_[id] == to && lastTo_[id] == from) s_.backForth++;
  if (s_.firstMoveId < 0) {
    s_.firstMoveId = id;
    s_.firstMoveR = tr;
    s_.firstMoveC = tc;
  }
  lastFrom_[id] = from;
  lastTo_[id] = to;
}

void ActivityMeter::observe(const Game& g, float headR, float headC, const Params& machine) {
  const uint32_t clock = g.clockMs();
  if (!started_) {
    started_ = true;
    lastClock_ = clock;
    nextThreatAt_ = clock;
  }
  // The game clock only runs in Play: the time since the previous call is Play time.
  const uint32_t dt = clock > lastClock_ ? clock - lastClock_ : 0;
  lastClock_ = clock;
  if (dt > 0) {
    int alive = 0, outside = 0, rows = 0;
    for (int i = 0; i < g.pieceCount(); i++) {
      const PieceView& p = g.pieces()[i];
      if (!p.alive || p.side != MACHINE) continue;
      alive++;
      rows += p.r;
      if (p.r != 0) outside++;
    }
    if (alive > 0) {
      s_.outsideAcc += static_cast<uint64_t>(dt) * outside * ACTIVITY_SCALE / alive;
      s_.rowAcc += static_cast<uint64_t>(dt) * rows * ACTIVITY_SCALE / alive;
    }
    s_.playMs += dt;
  }
  const int machineCaps = g.captured(MACHINE), humanCaps = g.captured(HUMAN);
  if (s_.firstCaptureMs == ACTIVITY_NONE && machineCaps + humanCaps > 0) s_.firstCaptureMs = clock;
  const int diff = machineCaps - humanCaps;
  const int sign = diff > 0 ? 1 : diff < 0 ? -1 : 0;
  if (sign != 0) {
    if (leadSign_ != 0 && sign != leadSign_) s_.leadChanges++;
    leadSign_ = sign;
  }
  if (g.phase() == Phase::Play && clock >= nextThreatAt_) {
    findThreats(g, headR, headC, machine);
    nextThreatAt_ = clock + ACTIVITY_THREAT_MS;
  }
}

void ActivityMeter::findThreats(const Game& g, float headR, float headC, const Params& machine) {
  const Snapshot snap = g.snapshot(headR, headC);
  const RaceTimes t = raceTimes(snap, machine);
  RaceState st;
  raceInit(st, snap, t);
  const int n = st.board.n;

  // Lines: forget the counted ones that are gone, count the new ones in prospect.
  RaceLine lines[RACE_MAX_LINES];
  const int k = raceLines(st.board, HUMAN, lines);
  uint32_t keys[RACE_MAX_LINES];
  for (int i = 0; i < k; i++) keys[i] = lineKey(lines[i]);
  int kept = 0;
  for (int j = 0; j < lineCount_; j++) {
    bool still = false;
    for (int i = 0; i < k && !still; i++) still = keys[i] == lines_[j];
    if (still) lines_[kept++] = lines_[j];
  }
  lineCount_ = kept;
  for (int i = 0; i < k; i++) {
    bool counted = false;
    for (int j = 0; j < lineCount_ && !counted; j++) counted = lines_[j] == keys[i];
    if (counted || raceGain(st, t, lines[i]) < 0.5f) continue;
    s_.threats++;
    s_.lineThreats++;
    if (lineCount_ < RACE_MAX_LINES) lines_[lineCount_++] = keys[i];
  }

  // RE: human edge groups with one empty neighbour left that a machine piece reaches.
  SquareSet now[2 * MAX_N];
  bool open[2 * MAX_N];
  int groups = 0;
  if (snap.edgeCapture) {
    Board mb = st.board;  // where the machine can go: blocked squares are taken
    for (int i = 0; i < n * n; i++)
      if (st.blocked[i] && mb.cells[i] == EMPTY) mb.cells[i] = HUMAN;
    SquareSet seen;
    seen.clear();
    for (int sq = 0; sq < n * n; sq++) {
      if (snap.board.cells[sq] != HUMAN || seen.has(sq)) continue;
      EdgeGroup eg;
      if (!edgeGroupAt(snap.board, sq / n, sq % n, eg)) {
        seen.set(sq);
        continue;
      }
      for (int w = 0; w < 3; w++) seen.w[w] |= eg.cells.w[w];
      if (groups >= 2 * MAX_N) continue;
      now[groups] = eg.cells;
      open[groups] = false;
      if (eg.libCount == 1) {
        const int lib = eg.libs[0];
        for (int m = 0; m < n * n && !open[groups]; m++) {
          if (mb.cells[m] != MACHINE || st.mId[m] == 0) continue;
          int d[MAX_N * MAX_N];
          const int nd = dests(mb, m / n, m % n, d);
          for (int j = 0; j < nd && !open[groups]; j++)
            open[groups] = d[j] == lib && !makesWall(mb, t.rules, MACHINE, m / n, m % n, lib / n, lib % n);
        }
      }
      groups++;
    }
  }
  kept = 0;
  for (int j = 0; j < groupCount_; j++) {
    bool still = false;
    for (int i = 0; i < groups && !still; i++) still = sameSet(now[i], groups_[j]);
    if (still) groups_[kept++] = groups_[j];
  }
  groupCount_ = kept;
  for (int i = 0; i < groups; i++) {
    bool counted = false;
    for (int j = 0; j < groupCount_ && !counted; j++) counted = sameSet(groups_[j], now[i]);
    if (counted || !open[i]) continue;
    s_.threats++;
    s_.groupThreats++;
    if (groupCount_ < 2 * MAX_N) groups_[groupCount_++] = now[i];
  }
}
