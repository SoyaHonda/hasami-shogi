#include "HumanBot.h"
#include "RaceTime.h"

namespace {

// Every move of every human piece (at most MAX_N pieces of a side, 2*(MAX_N-1) destinations).
constexpr int MAX_MOVES = MAX_N * 2 * (MAX_N - 1);

struct Move {
  int id;
  int fr, fc, tr, tc;
};

uint32_t cooldownLeft(const PieceView& p, uint32_t clock) {
  return p.cooldownUntil > clock ? p.cooldownUntil - clock : 0;
}

bool humanOnBoard(const PieceView& p) { return p.alive && !p.held && p.side == HUMAN; }

// The game's capture rules and its untouched pieces (RU), for capturesBy.
struct CapRules {
  const Rules* rules;
  SquareSet stale;
};

CapRules capRules(const Game& g) { return CapRules{&g.rules(), g.stale()}; }

// Destinations of the human piece on (r,c) on b that are legal under the game's rules (RW: not
// the ones that make a wall), in dests() order.
int legalDests(const Game& g, const Board& b, int r, int c, int out[]) {
  int d[MAX_N * MAX_N];
  const int k = dests(b, r, c, d);
  int m = 0;
  for (int j = 0; j < k; j++)
    if (!makesWall(b, g.rules(), HUMAN, r, c, d[j] / b.n, d[j] % b.n)) out[m++] = d[j];
  return m;
}

// Whether the piece at (r,c) reaches the empty square (tr,tc) in one move on b.
bool reaches(const Board& b, int r, int c, int tr, int tc) {
  if (!b.inside(tr, tc) || (r == tr) == (c == tc) || b.at(tr, tc) != EMPTY) return false;
  const int dr = tr > r ? 1 : tr < r ? -1 : 0, dc = tc > c ? 1 : tc < c ? -1 : 0;
  for (int rr = r + dr, cc = c + dc; rr != tr || cc != tc; rr += dr, cc += dc)
    if (b.at(rr, cc) != EMPTY) return false;
  return true;
}

// Machine pieces the human captures by moving (fr,fc) (-1: a held piece) to (tr,tc) on cb.
int capturesBy(const CapRules& cr, Board cb, int fr, int fc, int tr, int tc) {
  if (fr >= 0) cb.set(fr, fc, EMPTY);
  cb.set(tr, tc, HUMAN);
  int caps[MAX_N * MAX_N];
  return captures(cb, *cr.rules, HUMAN, tr, tc, caps, &cr.stale);
}

// Moves of the cooled-down human pieces on b (the board with the reserved square).
int readyMoves(const Game& g, const Board& b, Move out[]) {
  const uint32_t clock = g.clockMs();
  int k = 0;
  for (int i = 0; i < g.pieceCount(); i++) {
    const PieceView& p = g.pieces()[i];
    if (!humanOnBoard(p) || cooldownLeft(p, clock) > 0) continue;
    int d[MAX_N * MAX_N];
    const int nd = legalDests(g, b, p.r, p.c, d);
    for (int j = 0; j < nd && k < MAX_MOVES; j++) out[k++] = Move{p.id, p.r, p.c, d[j] / b.n, d[j] % b.n};
  }
  return k;
}

void apply(Board& b, int fr, int fc, int tr, int tc) {
  b.set(fr, fc, EMPTY);
  b.set(tr, tc, HUMAN);
}

bool lineHas(const RaceLine& L, int r, int c) {
  for (int k = 0; k < L.len; k++)
    if (L.r + L.dr * k == r && L.c + L.dc * k == c) return true;
  return false;
}

// End squares of the machine lines on cb whose both ends are on the board (the squares a
// capture lands on), then (RE) the empty neighbours of the machine's edge groups with at most
// RACE_GROUP_MAX_FILL of them. Returns the count.
int lineEnds(const Board& cb, const Rules& rules, int out[]) {
  RaceLine lines[RACE_MAX_LINES];
  const int nl = raceLines(cb, MACHINE, lines);
  bool seen[MAX_N * MAX_N] = {};
  int k = 0;
  for (int i = 0; i < nl; i++) {
    const RaceLine& L = lines[i];
    const int ar = L.r - L.dr, ac = L.c - L.dc, br = L.r + L.dr * L.len, bc = L.c + L.dc * L.len;
    if (!cb.inside(ar, ac) || !cb.inside(br, bc)) continue;
    const int a = ar * cb.n + ac, e = br * cb.n + bc;
    if (!seen[a]) out[k++] = a, seen[a] = true;
    if (!seen[e]) out[k++] = e, seen[e] = true;
  }
  if (rules.edgeCapture) {
    const int n = cb.n;
    SquareSet done;
    done.clear();
    for (int sq = 0; sq < n * n; sq++) {
      if (cb.cells[sq] != MACHINE || done.has(sq)) continue;
      EdgeGroup g;
      if (!edgeGroupAt(cb, sq / n, sq % n, g)) {
        done.set(sq);
        continue;
      }
      for (int w = 0; w < 3; w++) done.w[w] |= g.cells.w[w];
      if (g.libCount > RACE_GROUP_MAX_FILL) continue;
      for (int i = 0; i < g.libCount; i++)
        if (!seen[g.libs[i]]) out[k++] = g.libs[i], seen[g.libs[i]] = true;
    }
  }
  return k;
}

// Whether one more human move on (b, cb) captures: some human piece reaches an end square and
// closes the line there. Cooldowns are not looked at (the move comes later).
bool captureNext(const CapRules& cr, const Board& b, const Board& cb, const int ends[], int ne) {
  const int n = b.n;
  for (int e = 0; e < ne; e++) {
    const int tr = ends[e] / n, tc = ends[e] % n;
    if (b.at(tr, tc) != EMPTY) continue;
    for (int r = 0; r < n; r++)
      for (int c = 0; c < n; c++) {
        if (b.at(r, c) != HUMAN || !reaches(b, r, c, tr, tc)) continue;
        if (makesWall(b, *cr.rules, HUMAN, r, c, tr, tc)) continue;
        if (capturesBy(cr, cb, r, c, tr, tc) > 0) return true;
      }
  }
  return false;
}

// Whether a human piece still in cooldown has a capturing move on (b, cb).
bool captureAfterCooldown(const Game& g, const CapRules& cr, const Board& b, const Board& cb) {
  const uint32_t clock = g.clockMs();
  for (int i = 0; i < g.pieceCount(); i++) {
    const PieceView& p = g.pieces()[i];
    const uint32_t cd = cooldownLeft(p, clock);
    if (!humanOnBoard(p) || cd == 0 || cd > g.rules().humanCooldownMs) continue;
    int d[MAX_N * MAX_N];
    const int nd = legalDests(g, b, p.r, p.c, d);
    for (int j = 0; j < nd; j++)
      if (capturesBy(cr, cb, p.r, p.c, d[j] / b.n, d[j] % b.n) > 0) return true;
  }
  return false;
}

// Sort key of a race move: smaller is better, compared in order.
struct RaceKey {
  int k[4];
  bool operator<(const RaceKey& o) const {
    for (int i = 0; i < 4; i++)
      if (k[i] != o.k[i]) return k[i] < o.k[i];
    return false;
  }
  bool operator==(const RaceKey& o) const { return !(*this < o) && !(o < *this); }
};

int roundTenths(float v) { return static_cast<int>(v * 10.0f + 0.5f); }

// blind (B1's threat miss): only the machine's spare time counts.
RaceKey raceKey(const BotRaceEval& e, bool threatened, bool blind = false) {
  float s = e.spare;
  if (s > 100000.0f) s = 100000.0f;
  if (s < -100000.0f) s = -100000.0f;
  // 50 ms buckets so that moves that leave the same line the closest count as equal.
  const float q = s / 50.0f;
  int bucket = static_cast<int>(q);
  if (static_cast<float>(bucket) > q) bucket--;  // floor
  if (blind) return RaceKey{{0, 0, bucket, 0}};
  const int own = roundTenths(e.ownGain);
  return RaceKey{{threatened ? own : 0, e.unsafe ? 1 : 0, bucket, own}};
}

}  // namespace

HumanBot::HumanBot(const BotParams& p, uint32_t seed) : params_(p), rng_(seed) { reset(); }

void HumanBot::reset() {
  holding_ = false;
  heldId_ = -1;
  planR_ = planC_ = -1;
  blind_ = false;
  armed_ = false;
  nextAt_ = 0;
  failures_ = 0;
  now_ = 0;
  openArmed_ = false;
  openNextAt_ = 0;
}

uint32_t HumanBot::moveTime(const Game& game) {
  const float j = params_.moveJitter < 0.0f ? 0.0f : params_.moveJitter > 1.0f ? 1.0f : params_.moveJitter;
  const float f = 1.0f - j + 2.0f * j * rng_.unit();
  uint32_t hold = static_cast<uint32_t>(static_cast<float>(params_.moveMs) * f + 0.5f);
  // Stay well inside the hold limit.
  const uint32_t limit = game.rules().maxHoldMs;
  if (limit > 0 && hold >= limit) hold = limit / 2;
  return hold;
}

uint32_t HumanBot::waitTime() {
  if (params_.thinkMaxMs <= params_.reactMs) return params_.reactMs;
  const int span = static_cast<int>(params_.thinkMaxMs - params_.reactMs);
  return params_.reactMs + static_cast<uint32_t>(rng_.below(span + 1));
}

BotRaceEval HumanBot::raceAfter(const Game& game, float headR, float headC, int id, int toR, int toC) const {
  Snapshot s = game.snapshot(headR, headC);
  PieceView& p = s.pieces[id - 1];
  if (!p.held) s.board.set(p.r, p.c, EMPTY);
  int kept = 0;
  for (int i = 0; i < s.heldCount; i++)
    if (s.heldIds[i] != id) s.heldIds[kept++] = s.heldIds[i];
  for (int i = kept; i < 2; i++) s.heldIds[i] = -1;
  s.heldCount = kept;
  if (s.pendingId == id || kept == 0) {
    s.pending = false;
    s.pendingId = -1;
  }
  p.held = false;
  p.r = toR;
  p.c = toC;
  p.cooldownUntil = s.clockMs + s.humanCooldownMs;
  p.movedAt = s.clockMs;
  s.board.set(toR, toC, HUMAN);
  s.hasLastPlace = true;
  s.lastPlaceAt = s.clockMs;
  int caps[MAX_N * MAX_N];
  const SquareSet stale = game.stale();
  const int nc = captures(s.board, game.rules(), HUMAN, toR, toC, caps, &stale);
  for (int i = 0; i < nc; i++) {
    for (int j = 0; j < s.count; j++) {
      PieceView& q = s.pieces[j];
      if (q.alive && !q.held && q.side == MACHINE && q.r * s.board.n + q.c == caps[i]) q.alive = false;
    }
    s.board.cells[caps[i]] = EMPTY;
  }

  const RaceTimes t = raceTimes(s, machine_);
  RaceState st;
  raceInit(st, s, t);
  RaceLineEval ev[RACE_MAX_LINES];
  const int nl = raceAssess(st, t, ev);
  BotRaceEval out{1.0e9f, 0.0f, false};
  for (int i = 0; i < nl; i++) {
    const RaceLineEval& e = ev[i];
    if (raceInf(e.th)) continue;
    float v;
    if (!e.danger) {
      v = e.th;
    } else if (raceInf(e.save)) {
      v = e.th - 10000.0f;
    } else {
      v = e.th - e.save - t.margin;
    }
    if (v < out.spare) out.spare = v;
  }
  RaceLine hl[RACE_MAX_LINES];
  const int nh = raceLines(st.board, HUMAN, hl);
  for (int i = 0; i < nh; i++) {
    const float g = raceGain(st, t, hl[i]);
    out.ownGain += g;
    if (g >= 0.5f * static_cast<float>(hl[i].len) && lineHas(hl[i], toR, toC)) out.unsafe = true;
  }
  return out;
}

BotMove HumanBot::choose(const Game& game, float headR, float headC) {
  switch (params_.style) {
    case BotStyle::Random:
      return BotMove{BotMove::Random, -1, -1, -1};
    case BotStyle::Trap:
      return chooseTrap(game);
    case BotStyle::Race:
      return chooseRace(game, headR, headC, false);
    case BotStyle::Race3:
      return chooseRace(game, headR, headC, true);
  }
  return BotMove{BotMove::None, -1, -1, -1};
}

BotMove HumanBot::chooseTrap(const Game& game) {
  const BotMove none{BotMove::None, -1, -1, -1};
  const Board b = game.boardWithReserve();
  const Board cb = game.boardWithoutHeld();
  const CapRules cr = capRules(game);
  const uint32_t clock = game.clockMs();
  const Rules& rules = game.rules();
  const PieceView* pieces = game.pieces();
  Move mv[MAX_MOVES];
  const int k = readyMoves(game, b, mv);
  if (k == 0) return none;
  int cand[MAX_MOVES];
  int nc = 0;
  auto pick = [&]() {
    const Move& m = mv[cand[rng_.below(nc)]];
    return BotMove{BotMove::Move, m.id, m.tr, m.tc};
  };

  // 1. A capture now (the most pieces).
  int best = 0;
  for (int i = 0; i < k; i++) {
    const int c = capturesBy(cr, cb, mv[i].fr, mv[i].fc, mv[i].tr, mv[i].tc);
    if (c > best) best = c, nc = 0;
    if (c > 0 && c == best) cand[nc++] = i;
  }
  if (nc > 0) return pick();

  // 2. A piece still in cooldown will capture (trap B's return): wait for it.
  if (captureAfterCooldown(game, cr, b, cb)) return BotMove{BotMove::Wait, -1, -1, -1};

  RaceLine lines[RACE_MAX_LINES];
  const int nl = raceLines(cb, MACHINE, lines);
  // A second piece that is ready by the time the first placement's interval has passed.
  const uint32_t soon = rules.humanIntervalMs + params_.moveMs;

  // 3. Trap A: both ends empty; one piece goes to one end now, another reaches the other end.
  for (int li = 0; li < nl; li++) {
    const RaceLine& L = lines[li];
    const int ends[2][2] = {{L.r - L.dr, L.c - L.dc}, {L.r + L.dr * L.len, L.c + L.dc * L.len}};
    if (!b.inside(ends[0][0], ends[0][1]) || !b.inside(ends[1][0], ends[1][1])) continue;
    if (b.at(ends[0][0], ends[0][1]) != EMPTY || b.at(ends[1][0], ends[1][1]) != EMPTY) continue;
    for (int i = 0; i < k; i++) {
      for (int e = 0; e < 2; e++) {
        if (mv[i].tr != ends[e][0] || mv[i].tc != ends[e][1]) continue;
        Board b1 = b;
        apply(b1, mv[i].fr, mv[i].fc, mv[i].tr, mv[i].tc);
        bool second = false;
        for (int q = 0; q < game.pieceCount() && !second; q++) {
          const PieceView& p = pieces[q];
          if (!humanOnBoard(p) || p.id == mv[i].id || cooldownLeft(p, clock) > soon) continue;
          second = reaches(b1, p.r, p.c, ends[1 - e][0], ends[1 - e][1]);
        }
        if (second) cand[nc++] = i;
      }
    }
  }
  if (nc > 0) return pick();

  // 4./5. Both ends human: one end piece v steps away; another piece g steps into its square
  // (trap C), or else v comes back after its cooldown (trap B).
  int candB[MAX_MOVES];
  int nb = 0;
  for (int li = 0; li < nl; li++) {
    const RaceLine& L = lines[li];
    const int ends[2][2] = {{L.r - L.dr, L.c - L.dc}, {L.r + L.dr * L.len, L.c + L.dc * L.len}};
    if (!b.inside(ends[0][0], ends[0][1]) || !b.inside(ends[1][0], ends[1][1])) continue;
    if (b.at(ends[0][0], ends[0][1]) != HUMAN || b.at(ends[1][0], ends[1][1]) != HUMAN) continue;
    for (int e = 0; e < 2; e++) {
      const int er = ends[e][0], ec = ends[e][1], orr = ends[1 - e][0], oc = ends[1 - e][1];
      for (int i = 0; i < k; i++) {
        if (mv[i].fr != er || mv[i].fc != ec) continue;
        Board b1 = b;
        apply(b1, er, ec, mv[i].tr, mv[i].tc);
        bool g = false;
        for (int q = 0; q < game.pieceCount() && !g; q++) {
          const PieceView& p = pieces[q];
          if (!humanOnBoard(p) || p.id == mv[i].id || (p.r == orr && p.c == oc)) continue;
          if (cooldownLeft(p, clock) > soon) continue;
          g = reaches(b1, p.r, p.c, er, ec);
        }
        if (g) {
          cand[nc++] = i;
        } else if (reaches(b1, mv[i].tr, mv[i].tc, er, ec)) {
          candB[nb++] = i;
        }
      }
    }
  }
  if (nc > 0) return pick();
  if (nb > 0) {
    const Move& m = mv[candB[rng_.below(nb)]];
    return BotMove{BotMove::Move, m.id, m.tr, m.tc};
  }

  // 6. Trap B's set-up: make an empty square with human pieces on both sides (in a row or a
  // column) that a machine piece can enter.
  for (int i = 0; i < k; i++) {
    Board b1 = b;
    apply(b1, mv[i].fr, mv[i].fc, mv[i].tr, mv[i].tc);
    bool ok = false;
    for (int d = 0; d < 4 && !ok; d++) {
      const int sr = mv[i].tr + DIR_R[d], sc = mv[i].tc + DIR_C[d];
      const int orr = sr + DIR_R[d], oc = sc + DIR_C[d];
      if (!b1.inside(orr, oc) || b1.at(sr, sc) != EMPTY || b1.at(orr, oc) != HUMAN) continue;
      if (game.machineBlocked(sr, sc)) continue;
      for (int q = 0; q < game.pieceCount() && !ok; q++) {
        const PieceView& p = pieces[q];
        if (!p.alive || p.held || p.side != MACHINE) continue;
        ok = reaches(b1, p.r, p.c, sr, sc);
      }
    }
    if (ok) cand[nc++] = i;
  }
  if (nc > 0) return pick();
  return BotMove{BotMove::Random, -1, -1, -1};
}

BotMove HumanBot::chooseRace(const Game& game, float headR, float headC, bool search) {
  const BotMove none{BotMove::None, -1, -1, -1};
  const Board b = game.boardWithReserve();
  const Board cb = game.boardWithoutHeld();
  const CapRules cr = capRules(game);
  Move mv[MAX_MOVES];
  const int k = readyMoves(game, b, mv);
  if (k == 0) return none;
  int cand[MAX_MOVES];
  int nc = 0;

  // A capture now (the most pieces).
  int best = 0;
  for (int i = 0; i < k; i++) {
    const int c = capturesBy(cr, cb, mv[i].fr, mv[i].fc, mv[i].tr, mv[i].tc);
    if (c > best) best = c, nc = 0;
    if (c > 0 && c == best) cand[nc++] = i;
  }
  if (nc > 0) {
    const Move& m = mv[cand[rng_.below(nc)]];
    return BotMove{BotMove::Move, m.id, m.tr, m.tc};
  }

  if (search && captureAfterCooldown(game, cr, b, cb)) return BotMove{BotMove::Wait, -1, -1, -1};
  if (search) {
    // Race3: the first moves of the shortest capture with two or three placements while the
    // machine stands still (one piece moving more than once, or several pieces in turn).
    int ends[MAX_N * MAX_N];
    const int ne = lineEnds(cb, game.rules(), ends);
    if (ne > 0) {
      for (int i = 0; i < k; i++) {
        Board b1 = b, c1 = cb;
        apply(b1, mv[i].fr, mv[i].fc, mv[i].tr, mv[i].tc);
        apply(c1, mv[i].fr, mv[i].fc, mv[i].tr, mv[i].tc);
        if (captureNext(cr, b1, c1, ends, ne)) cand[nc++] = i;
      }
      if (nc == 0) {
        const int n = b.n;
        for (int i = 0; i < k; i++) {
          Board b1 = b, c1 = cb;
          apply(b1, mv[i].fr, mv[i].fc, mv[i].tr, mv[i].tc);
          apply(c1, mv[i].fr, mv[i].fc, mv[i].tr, mv[i].tc);
          bool found = false;
          for (int sq = 0; sq < n * n && !found; sq++) {
            if (b1.cells[sq] != HUMAN) continue;
            int d[MAX_N * MAX_N];
            const int nd = legalDests(game, b1, sq / n, sq % n, d);
            for (int j = 0; j < nd && !found; j++) {
              Board b2 = b1, c2 = c1;
              apply(b2, sq / n, sq % n, d[j] / n, d[j] % n);
              apply(c2, sq / n, sq % n, d[j] / n, d[j] % n);
              found = captureNext(cr, b2, c2, ends, ne);
            }
          }
          if (found) cand[nc++] = i;
        }
      }
    }
  }
  if (nc == 0) {
    for (int i = 0; i < k; i++) cand[i] = i;
    nc = k;
  }

  // Is a human piece about to be captured now? Then getting away comes first.
  bool threatened = false;
  {
    const Snapshot s = game.snapshot(headR, headC);
    const RaceTimes t = raceTimes(s, machine_);
    RaceState st;
    raceInit(st, s, t);
    RaceLine hl[RACE_MAX_LINES];
    const int nh = raceLines(st.board, HUMAN, hl);
    for (int i = 0; i < nh && !threatened; i++)
      threatened = raceGain(st, t, hl[i]) >= 0.5f * static_cast<float>(hl[i].len);
  }
  // B1: now and then the machine's threats go unnoticed.
  blind_ = params_.threatMiss > 0.0f && rng_.unit() < params_.threatMiss;
  if (blind_) threatened = false;

  int bestIdx[MAX_MOVES];
  int nbest = 0;
  RaceKey bestKey{};
  for (int j = 0; j < nc; j++) {
    const Move& m = mv[cand[j]];
    const RaceKey key = raceKey(raceAfter(game, headR, headC, m.id, m.tr, m.tc), threatened, blind_);
    if (nbest == 0 || key < bestKey) {
      bestKey = key;
      nbest = 0;
    }
    if (key == bestKey) bestIdx[nbest++] = cand[j];
  }
  const Move& m = mv[bestIdx[rng_.below(nbest)]];
  return BotMove{BotMove::Move, m.id, m.tr, m.tc};
}

BotAction HumanBot::decide(const Game& game, float headR, float headC, uint32_t now) {
  const BotAction none{BotAction::None, -1, -1};
  now_ = now;
  if (game.phase() == Phase::Opening) return decideOpening(game, headR, headC, now);
  if (game.phase() != Phase::Play) return none;
  const uint32_t clock = game.clockMs();
  if (holding_) {
    // The piece may have left the hand without a placement of ours: a pending placement landed,
    // the hold limit put it back, or something else released it (e.g. a new game).
    bool stillHeld = false;
    for (int i = 0; i < game.heldCount(); i++)
      if (game.heldIds()[i] == heldId_) stillHeld = true;
    if (!stillHeld) {
      holding_ = false;
      armed_ = true;
      nextAt_ = clock + waitTime();
      return none;
    }
    // Pending: wait until it lands or is dropped.
    if (game.pendingId() == heldId_) return none;
  } else if (game.heldCount() > 0) {
    return none;  // one piece in hand at a time
  } else if (!armed_) {
    armed_ = true;
    nextAt_ = clock + waitTime();
  }
  if (clock < nextAt_) return none;
  if (holding_) return putDown(game, headR, headC);

  const BotMove m = choose(game, headR, headC);
  if (m.kind != BotMove::Move && m.kind != BotMove::Random) {
    nextAt_ = clock + 100;  // look again shortly
    return none;
  }
  return liftFor(game, m);
}

BotAction HumanBot::liftFor(const Game& game, const BotMove& m) {
  if (m.kind == BotMove::Move) {
    planR_ = m.toR;
    planC_ = m.toC;
    const PieceView& p = game.pieces()[m.id - 1];
    return BotAction{BotAction::Lift, p.r, p.c};
  }
  // Random: a movable piece (cooled down with at least one legal destination).
  planR_ = planC_ = -1;
  const uint32_t clock = game.clockMs();
  const Board b = game.boardWithReserve();
  const PieceView* pieces = game.pieces();
  int ids[2 * MAX_N];
  int k = 0;
  for (int i = 0; i < game.pieceCount(); i++) {
    const PieceView& p = pieces[i];
    if (!humanOnBoard(p) || clock < p.cooldownUntil) continue;
    int d[MAX_N * MAX_N];
    if (legalDests(game, b, p.r, p.c, d) == 0) continue;
    ids[k++] = p.id;
  }
  if (k == 0) return BotAction{BotAction::None, -1, -1};
  const PieceView& p = pieces[ids[rng_.below(k)] - 1];
  return BotAction{BotAction::Lift, p.r, p.c};
}

BotAction HumanBot::decideOpening(const Game& game, float headR, float headC, uint32_t now) {
  const BotAction none{BotAction::None, -1, -1};
  if (holding_) {
    bool stillHeld = false;
    for (int i = 0; i < game.heldCount(); i++)
      if (game.heldIds()[i] == heldId_) stillHeld = true;
    if (!stillHeld) {
      // The hold limit put it back: choose again shortly.
      holding_ = false;
      openNextAt_ = now + waitTime();
      return none;
    }
    if (static_cast<int32_t>(now - openNextAt_) < 0) return none;
    return putDown(game, headR, headC);
  }
  if (game.openingDone(HUMAN) || game.heldCount() > 0) return none;
  if (!openArmed_) {
    openArmed_ = true;
    openNextAt_ = now + waitTime();
  }
  if (static_cast<int32_t>(now - openNextAt_) < 0) return none;
  const BotMove m = choose(game, headR, headC);
  if (m.kind != BotMove::Move && m.kind != BotMove::Random) {
    openNextAt_ = now + 100;
    return none;
  }
  return liftFor(game, m);
}

BotAction HumanBot::putDown(const Game& game, float headR, float headC) {
  const BotAction none{BotAction::None, -1, -1};
  const Board b = game.boardWithReserve();
  const int n = b.n;
  const PieceView& p = game.pieces()[heldId_ - 1];
  int d[MAX_N * MAX_N];
  const int k = legalDests(game, b, p.r, p.c, d);
  if (k == 0) {
    // Put it back when the origin is free; otherwise any empty square (escape rule).
    if (b.at(p.r, p.c) == EMPTY) return BotAction{BotAction::Place, p.r, p.c};
    int empty[MAX_N * MAX_N];
    int ne = 0;
    for (int i = 0; i < n * n; i++)
      if (b.cells[i] == EMPTY && !makesWall(b, game.rules(), HUMAN, p.r, p.c, i / n, i % n)) empty[ne++] = i;
    if (ne == 0) return none;
    const int e = empty[rng_.below(ne)];
    return BotAction{BotAction::Place, e / n, e % n};
  }
  // Capture check on the real board (the reserved square is not a machine piece yet).
  const Board bb = game.boardWithoutHeld();
  const CapRules cr = capRules(game);
  int capt[MAX_N * MAX_N];
  int nc = 0, best = 0;
  for (int i = 0; i < k; i++) {
    const int c = capturesBy(cr, bb, -1, -1, d[i] / n, d[i] % n);
    if (params_.style != BotStyle::Random && c > best) best = c, nc = 0;
    if (c > 0 && (params_.style == BotStyle::Random || c == best)) capt[nc++] = d[i];
  }
  if (params_.style == BotStyle::Random) {
    const int to = nc > 0 && failures_ == 0 && rng_.unit() < params_.greed ? capt[rng_.below(nc)] : d[rng_.below(k)];
    return BotAction{BotAction::Place, to / n, to % n};
  }
  if (failures_ == 0) {
    if (nc > 0) {
      const int to = capt[rng_.below(nc)];
      return BotAction{BotAction::Place, to / n, to % n};
    }
    for (int i = 0; i < k; i++)
      if (planR_ >= 0 && d[i] == planR_ * n + planC_) return BotAction{BotAction::Place, planR_, planC_};
    if (params_.style == BotStyle::Race || params_.style == BotStyle::Race3) {
      // The plan no longer works: the best destination of this piece by the race.
      int bestSq[MAX_N * MAX_N];
      int nb = 0;
      RaceKey bestKey{};
      for (int i = 0; i < k; i++) {
        const RaceKey key = raceKey(raceAfter(game, headR, headC, heldId_, d[i] / n, d[i] % n), false, blind_);
        if (nb == 0 || key < bestKey) bestKey = key, nb = 0;
        if (key == bestKey) bestSq[nb++] = d[i];
      }
      const int to = bestSq[rng_.below(nb)];
      return BotAction{BotAction::Place, to / n, to % n};
    }
  }
  const int to = d[rng_.below(k)];
  return BotAction{BotAction::Place, to / n, to % n};
}

void HumanBot::onLifted(const Game& game, bool ok, int r, int c) {
  if (!ok) return;
  (void)r;
  (void)c;
  // The lifted piece is the last one in the held list.
  heldId_ = game.heldIds()[game.heldCount() - 1];
  holding_ = true;
  failures_ = 0;
  const uint32_t hold = moveTime(game);
  nextAt_ = game.clockMs() + hold;
  openNextAt_ = now_ + hold;
}

void HumanBot::onPlaced(const Game& game, PlaceResult res) {
  if (res == PlaceResult::Pending) return;  // still held; decide() waits for it to land
  if (res != PlaceResult::Ok && res != PlaceResult::Cancelled) {
    failures_++;  // retry next tick with the fallback
    return;
  }
  holding_ = false;
  heldId_ = -1;
  planR_ = planC_ = -1;
  failures_ = 0;
  armed_ = true;
  const uint32_t wait = waitTime();
  nextAt_ = game.clockMs() + wait;
  openNextAt_ = now_ + wait;
}
