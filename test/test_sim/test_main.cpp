#include <unity.h>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "HumanBot.h"
#include "RaceTime.h"
#include "SimDriver.h"

void setUp() {}
void tearDown() {}

// Game's time limit (Rules::timeLimitMs, 10 min) ends every game; this safety net on simulated
// time only stops a game stuck outside Play (clock frozen).
static const uint32_t MAX_SIM_MS = 3u * Rules().timeLimitMs;

struct Outcome {
  Phase phase;
  Winner winner;
  uint32_t clockMs;
  int capHuman, capMachine, saves, machineMoves;
};

static BotParams botOf(BotStyle style) {
  BotParams b;
  b.style = style;
  return b;
}

static Outcome autoplay(uint32_t seed, const Params& params = Params(), const BotParams& bot = BotParams()) {
  SimDriver d(Rules(), params, seed);
  d.enableBot(bot);
  d.start();
  uint32_t simMs = 0;
  State s = d.state();
  while (s.phase != Phase::Over && simMs < MAX_SIM_MS) {
    d.advance(10);
    simMs += 10;
    s = d.state();
  }
  return Outcome{s.phase, s.winner, s.clockMs, s.capturedHuman, s.capturedMachine, s.saves, s.machineMoves};
}

void test_same_seed_same_game() {
  for (uint32_t seed = 3; seed <= 5; seed++) {
    const Outcome a = autoplay(seed);
    const Outcome b = autoplay(seed);
    TEST_ASSERT_EQUAL_INT((int)a.phase, (int)b.phase);
    TEST_ASSERT_EQUAL_INT((int)a.winner, (int)b.winner);
    TEST_ASSERT_EQUAL_UINT32(a.clockMs, b.clockMs);
    TEST_ASSERT_EQUAL_INT(a.capHuman, b.capHuman);
    TEST_ASSERT_EQUAL_INT(a.capMachine, b.capMachine);
    TEST_ASSERT_EQUAL_INT(a.saves, b.saves);
    TEST_ASSERT_EQUAL_INT(a.machineMoves, b.machineMoves);
  }
}

void test_hundred_bot_games_finish() {
  int decided = 0, draws = 0, moves = 0;
  for (uint32_t seed = 1; seed <= 100; seed++) {
    const Outcome o = autoplay(seed);
    char msg[48];
    snprintf(msg, sizeof(msg), "seed %u", (unsigned)seed);
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)Phase::Over, (int)o.phase, msg);
    TEST_ASSERT_TRUE_MESSAGE(o.winner != Winner::None, msg);
    if (o.winner == Winner::Draw) {
      draws++;
      // A draw only comes from the time limit with equal captures.
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(Rules().timeLimitMs, o.clockMs, msg);
      TEST_ASSERT_EQUAL_INT_MESSAGE(o.capHuman, o.capMachine, msg);
    } else {
      decided++;
    }
    moves += o.machineMoves;
  }
  TEST_ASSERT_EQUAL_INT(100, decided + draws);
  TEST_ASSERT_TRUE(decided > 0);
  TEST_ASSERT_TRUE(moves > 0);
}

static const BotStyle STYLES[4] = {BotStyle::Random, BotStyle::Trap, BotStyle::Race, BotStyle::Race3};

// Every bot keeps to the human rules: one piece in hand, never put back by the hold limit (it
// holds for the jittered move time < maxHoldMs and waits while a placement is pending), and
// placements that land at least humanIntervalMs apart (Game holds early ones as Pending).
void test_bot_keeps_to_human_rules() {
  for (BotStyle style : STYLES) {
    for (uint32_t seed = 1; seed <= 6; seed++) {
      SimDriver d(Rules(), Params(), seed);
      d.enableBot(botOf(style));
      d.start();
      bool hadLast = false;
      uint32_t last = 0;
      int placements = 0;
      for (uint32_t t = 0; t < 120000 && d.state().phase != Phase::Over; t += 10) {
        d.advance(10);
        TEST_ASSERT_TRUE(d.game().heldCount() <= 1);
        const Snapshot s = d.game().snapshot(0.0f, 0.0f);
        if (s.hasLastPlace && (!hadLast || s.lastPlaceAt != last)) {
          if (hadLast) TEST_ASSERT_TRUE(s.lastPlaceAt - last >= Rules().humanIntervalMs);
          hadLast = true;
          last = s.lastPlaceAt;
          placements++;
        }
      }
      TEST_ASSERT_EQUAL_INT(0, d.game().holdTimeouts());
      TEST_ASSERT_TRUE(placements > 3);
    }
  }
}

// The move time is moveMs +- 40 %: every hold lies in [300, 700] ms and they are not all equal.
void test_move_time_jitter() {
  SimDriver d(Rules(), Params(), 7);
  d.enableBot(BotParams());
  d.start();
  uint32_t liftedAt = 0, lo = 0xFFFFFFFFu, hi = 0;
  bool held = false;
  int holds = 0;
  for (uint32_t t = 0; t < 120000 && d.state().phase != Phase::Over; t += 10) {
    d.advance(10);
    const bool now = d.game().heldCount() > 0;
    if (now && !held) liftedAt = d.game().clockMs();
    if (!now && held && d.game().phase() == Phase::Play) {
      const uint32_t h = d.game().clockMs() - liftedAt;
      if (h < lo) lo = h;
      if (h > hi) hi = h;
      holds++;
    }
    held = now;
  }
  TEST_ASSERT_TRUE(holds > 10);
  // A tick of 10 ms on either side; a pending placement lands later than it was put down.
  TEST_ASSERT_TRUE(lo >= 300 - 10);
  TEST_ASSERT_TRUE(hi > lo);
}

void test_same_seed_same_game_every_style() {
  for (BotStyle style : STYLES) {
    for (uint32_t seed = 11; seed <= 12; seed++) {
      const Outcome a = autoplay(seed, Params(), botOf(style));
      const Outcome b = autoplay(seed, Params(), botOf(style));
      TEST_ASSERT_EQUAL_INT((int)a.winner, (int)b.winner);
      TEST_ASSERT_EQUAL_UINT32(a.clockMs, b.clockMs);
      TEST_ASSERT_EQUAL_INT(a.capHuman, b.capHuman);
      TEST_ASSERT_EQUAL_INT(a.capMachine, b.capMachine);
      TEST_ASSERT_EQUAL_INT(a.machineMoves, b.machineMoves);
      TEST_ASSERT_EQUAL_INT((int)Phase::Over, (int)a.phase);
    }
  }
}

// ---- The bots alone against a machine that stands still (Game only, no controller).

// Seven 'M' and seven 'H' in rows separated by '\n'; machine ids 1, 3, ... and human ids 2, 4, ...
// in row-major order. The game is in Play at clock 0 with every cooldown over.
struct Bench {
  Game g;
  HumanBot bot;
  uint32_t now;
  struct Placement {
    int id, fr, fc, tr, tc, capturedAfter;
  } log[64];
  int count;
  int lastR[2 * MAX_N + 1], lastC[2 * MAX_N + 1];

  Bench(const char* layout, BotStyle style, uint32_t seed) : bot(botOf(style), seed), now(3000), count(0) {
    g.start(0);
    g.tick(3000);
    int m = 0, h = 0, r = 0, c = 0;
    for (const char* p = layout; *p; ++p) {
      if (*p == '\n') {
        r++;
        c = 0;
        continue;
      }
      if (*p == 'M') g.onMachineStep(2 * (m++) + 1, r, c);
      if (*p == 'H') g.onMachineStep(2 * (h++) + 2, r, c);
      c++;
    }
    TEST_ASSERT_EQUAL_INT(7, m);
    TEST_ASSERT_EQUAL_INT(7, h);
    for (int i = 0; i < g.pieceCount(); i++) {
      lastR[g.pieces()[i].id] = g.pieces()[i].r;
      lastC[g.pieces()[i].id] = g.pieces()[i].c;
    }
  }

  void notePlacements() {
    for (int i = 0; i < g.pieceCount(); i++) {
      const PieceView& p = g.pieces()[i];
      if (p.side != HUMAN || !p.alive || p.held) continue;
      if (p.r == lastR[p.id] && p.c == lastC[p.id]) continue;
      if (count < 64) log[count++] = Placement{p.id, lastR[p.id], lastC[p.id], p.r, p.c, g.captured(HUMAN)};
      lastR[p.id] = p.r;
      lastC[p.id] = p.c;
    }
  }

  // Runs until the human captures or `ms` of game time pass. Returns whether it captured.
  bool run(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 10) {
      now += 10;
      g.tick(now);
      notePlacements();
      if (g.captured(HUMAN) > 0) return true;
      const BotAction a = bot.decide(g, 0.0f, 3.0f);
      if (a.kind == BotAction::Lift) {
        bot.onLifted(g, g.onHumanLift(a.r, a.c, now) == LiftResult::Ok, a.r, a.c);
      } else if (a.kind == BotAction::Place) {
        bot.onPlaced(g, g.onHumanPlace(a.r, a.c, now));
      }
      notePlacements();
      if (g.captured(HUMAN) > 0) return true;
      TEST_ASSERT_TRUE(g.heldCount() <= 1);
    }
    return false;
  }
};

static bool isSq(int r, int c, int er, int ec) { return r == er && c == ec; }

// Trap A: the machine came out to (5,3); the pieces at (6,2) and (6,4) step up to (5,2) and (5,4)
// in turn and close the row.
static const char* TRAP_A =
    "MMM.MMM\n"
    ".......\n"
    ".......\n"
    ".......\n"
    ".......\n"
    "...M...\n"
    "HHHHHHH";

void test_trap_a() {
  for (uint32_t seed = 1; seed <= 5; seed++) {
    Bench b(TRAP_A, BotStyle::Trap, seed);
    TEST_ASSERT_TRUE(b.run(10000));
    TEST_ASSERT_EQUAL_INT(2, b.count);
    const Bench::Placement &p0 = b.log[0], &p1 = b.log[1];
    TEST_ASSERT_TRUE(isSq(p0.tr, p0.tc, 5, 2) || isSq(p0.tr, p0.tc, 5, 4));
    TEST_ASSERT_TRUE(isSq(p1.tr, p1.tc, 5, 2) || isSq(p1.tr, p1.tc, 5, 4));
    TEST_ASSERT_TRUE(p0.id != p1.id && p0.tc != p1.tc);
    TEST_ASSERT_EQUAL_INT(0, p0.capturedAfter);
    TEST_ASSERT_EQUAL_INT(1, p1.capturedAfter);
  }
}

// Trap B: the machine sits between (5,2) and (5,4) and nobody else can step into either square:
// one of them steps away and comes back after its cooldown.
void test_trap_b() {
  for (uint32_t seed = 1; seed <= 5; seed++) {
    Bench b(
        "MMM.MMM\n"
        "......H\n"
        ".......\n"
        ".......\n"
        ".......\n"
        "..HMH..\n"
        "HH...HH",
        BotStyle::Trap, seed);
    TEST_ASSERT_TRUE(b.run(10000));
    TEST_ASSERT_EQUAL_INT(2, b.count);
    const Bench::Placement &p0 = b.log[0], &p1 = b.log[1];
    TEST_ASSERT_TRUE(isSq(p0.fr, p0.fc, 5, 2) || isSq(p0.fr, p0.fc, 5, 4));
    TEST_ASSERT_EQUAL_INT(p0.id, p1.id);
    TEST_ASSERT_EQUAL_INT(p0.fr, p1.tr);
    TEST_ASSERT_EQUAL_INT(p0.fc, p1.tc);
    TEST_ASSERT_EQUAL_INT(1, p1.capturedAfter);
  }
}

// Trap C: (5,2) steps away and (6,2) steps into its square.
void test_trap_c() {
  for (uint32_t seed = 1; seed <= 5; seed++) {
    Bench b(
        "MMM.MMM\n"
        ".......\n"
        ".......\n"
        ".......\n"
        ".......\n"
        "..HMH..\n"
        "HHH..HH",
        BotStyle::Trap, seed);
    TEST_ASSERT_TRUE(b.run(10000));
    TEST_ASSERT_EQUAL_INT(2, b.count);
    const Bench::Placement &p0 = b.log[0], &p1 = b.log[1];
    TEST_ASSERT_TRUE(isSq(p0.fr, p0.fc, 5, 2));
    TEST_ASSERT_TRUE(isSq(p1.fr, p1.fc, 6, 2));
    TEST_ASSERT_TRUE(isSq(p1.tr, p1.tc, 5, 2));
    TEST_ASSERT_EQUAL_INT(1, p1.capturedAfter);
  }
}

// Race picks the move after which the machine's spare time is the least: on trap A's board one
// of the two pieces under the row's ends moves up (the other end then closes in one move at the
// placement interval, before the head can get there).
void test_race_picks_least_spare() {
  for (uint32_t seed = 1; seed <= 5; seed++) {
    Bench b(TRAP_A, BotStyle::Race, seed);
    const BotMove m = b.bot.choose(b.g, 0.0f, 3.0f);
    TEST_ASSERT_EQUAL_INT((int)BotMove::Move, (int)m.kind);
    const PieceView& p = b.g.pieces()[m.id - 1];
    TEST_ASSERT_TRUE((isSq(p.r, p.c, 6, 2) && isSq(m.toR, m.toC, 5, 2)) ||
                     (isSq(p.r, p.c, 6, 4) && isSq(m.toR, m.toC, 5, 4)));
    const float chosen = b.bot.raceAfter(b.g, 0.0f, 3.0f, m.id, m.toR, m.toC).spare;
    // No legal move leaves less.
    const Board bd = b.g.boardWithReserve();
    for (int i = 0; i < b.g.pieceCount(); i++) {
      const PieceView& q = b.g.pieces()[i];
      if (q.side != HUMAN) continue;
      int d[MAX_N * MAX_N];
      const int k = dests(bd, q.r, q.c, d);
      for (int j = 0; j < k; j++)
        TEST_ASSERT_TRUE(chosen <= b.bot.raceAfter(b.g, 0.0f, 3.0f, q.id, d[j] / 7, d[j] % 7).spare);
    }
    // And with the machine standing still it captures.
    TEST_ASSERT_TRUE(b.run(10000));
  }
}

// Race first looks after a piece the machine is about to capture: the human piece just put on
// (5,5) cools down for 3 s, and the machine piece at (1,4) closes (5,4) before then.
void test_race_guards_a_threatened_piece() {
  Bench b(
      "MMM.M.M\n"
      "....M..\n"
      ".......\n"
      ".......\n"
      ".....H.\n"
      "......M\n"
      "HHHHH.H",
      BotStyle::Race, 1);
  TEST_ASSERT_EQUAL_INT((int)LiftResult::Ok, (int)b.g.onHumanLift(4, 5, b.now));
  TEST_ASSERT_EQUAL_INT((int)PlaceResult::Ok, (int)b.g.onHumanPlace(5, 5, b.now));
  const BotMove m = b.bot.choose(b.g, 1.0f, 4.0f);
  TEST_ASSERT_EQUAL_INT((int)BotMove::Move, (int)m.kind);
  const float chosen = b.bot.raceAfter(b.g, 1.0f, 4.0f, m.id, m.toR, m.toC).ownGain;
  // A move elsewhere ((6,0) -> (5,0), id 4) leaves the piece in danger.
  TEST_ASSERT_TRUE(b.bot.raceAfter(b.g, 1.0f, 4.0f, 4, 5, 0).ownGain >= 0.5f);
  const Board bd = b.g.boardWithReserve();
  for (int i = 0; i < b.g.pieceCount(); i++) {
    const PieceView& q = b.g.pieces()[i];
    if (q.side != HUMAN || q.cooldownUntil > b.g.clockMs()) continue;
    int d[MAX_N * MAX_N];
    const int k = dests(bd, q.r, q.c, d);
    for (int j = 0; j < k; j++)
      TEST_ASSERT_TRUE(chosen <= b.bot.raceAfter(b.g, 1.0f, 4.0f, q.id, d[j] / 7, d[j] % 7).ownGain + 0.05f);
  }
  TEST_ASSERT_TRUE(chosen < 0.5f);
}

// Race3: the capture takes three placements (no piece reaches a second end of the machine piece
// at (3,3) in one move), and with the machine standing still it makes them.
void test_race3_three_moves() {
  const char* layout =
      "MMM.MMM\n"
      ".......\n"
      ".......\n"
      "...M...\n"
      ".......\n"
      "H.....H\n"
      "HHH..HH";
  for (uint32_t seed = 1; seed <= 3; seed++) {
    Bench b(layout, BotStyle::Race3, seed);
    const bool cap = b.run(30000);
    TEST_ASSERT_TRUE(cap);
    TEST_ASSERT_TRUE(b.count >= 3);
    TEST_ASSERT_EQUAL_INT(1, b.log[b.count - 1].capturedAfter);
    // Three placements: it waits for a piece in cooldown that will capture instead of moving
    // something else (which would push the capture back by the placement interval).
    TEST_ASSERT_EQUAL_INT(3, b.count);
  }
}

// ---- Traps A-D in real time against the machine (plan 2026-10-31 section 1 and 3.13).

// A human that plays the traps as fast as the rules allow (one piece in hand, placements 1 s
// apart, holding each piece humanMoveMs): a capture in one move, else trap A (two pieces close
// both empty ends of a machine line), trap C (one end steps away and another piece steps in), trap
// B (one end steps away and comes back after its cooldown), trap D (a piece still cooling down
// that reaches the open end of a half-closed line moves the moment it can). Nothing else: the
// machine loses a piece only to a trap.
struct Trapper {
  struct Step {
    int fr, fc, tr, tc;
    uint32_t notBefore;  // game clock
  };
  Step q[2];
  int qn = 0;
  bool holding = false;
  uint32_t liftClock = 0;
  int fromR = -1, fromC = -1, toR = -1, toC = -1;
  int traps = 0;  // trap sequences started

  static bool ready(const Game& g, const PieceView& p) { return p.cooldownUntil <= g.clockMs(); }

  static const PieceView* humanAt(const Game& g, int r, int c) {
    for (int i = 0; i < g.pieceCount(); i++) {
      const PieceView& p = g.pieces()[i];
      if (p.alive && !p.held && p.side == HUMAN && p.r == r && p.c == c) return &p;
    }
    return nullptr;
  }

  // Whether (r,c) reaches the empty square (tr,tc) in one straight move on b.
  static bool reaches(const Board& b, int r, int c, int tr, int tc) {
    if ((r == tr) == (c == tc) || !b.inside(tr, tc) || b.at(tr, tc) != EMPTY) return false;
    const int dr = tr > r ? 1 : tr < r ? -1 : 0, dc = tc > c ? 1 : tc < c ? -1 : 0;
    for (int rr = r + dr, cc = c + dc; rr != tr || cc != tc; rr += dr, cc += dc)
      if (b.at(rr, cc) != EMPTY) return false;
    return true;
  }

  void queue2(const Step& a, const Step& b) {
    q[0] = a;
    q[1] = b;
    qn = 2;
    traps++;
  }

  // Picks the next sequence (nothing held, nothing pending, queue empty).
  void plan(const Game& g) {
    Board b = g.boardWithReserve();
    const int n = b.n;
    const uint32_t now = g.clockMs();
    // A capture in one move.
    for (int i = 0; i < g.pieceCount(); i++) {
      const PieceView& h = g.pieces()[i];
      if (!h.alive || h.held || h.side != HUMAN || !ready(g, h)) continue;
      int d[MAX_N * MAX_N];
      const int k = dests(b, h.r, h.c, d);
      b.set(h.r, h.c, EMPTY);
      for (int j = 0; j < k; j++) {
        int caps[MAX_N * MAX_N];
        b.cells[d[j]] = HUMAN;
        const int nc = capturesAt(b, HUMAN, d[j] / n, d[j] % n, caps);
        b.cells[d[j]] = EMPTY;
        if (nc > 0) {
          q[0] = Step{h.r, h.c, d[j] / n, d[j] % n, 0};
          qn = 1;
          return;
        }
      }
      b.set(h.r, h.c, HUMAN);
    }
    RaceLine lines[RACE_MAX_LINES];
    const int k = raceLines(b, MACHINE, lines);
    for (int li = 0; li < k; li++) {
      const RaceLine& L = lines[li];
      const int e[2][2] = {{L.r - L.dr, L.c - L.dc}, {L.r + L.dr * L.len, L.c + L.dc * L.len}};
      if (!b.inside(e[0][0], e[0][1]) || !b.inside(e[1][0], e[1][1])) continue;
      const int8_t ca = b.at(e[0][0], e[0][1]), cb = b.at(e[1][0], e[1][1]);
      if (ca == EMPTY && cb == EMPTY) {
        // Trap A: two different ready pieces, one per end.
        for (int first = 0; first < 2; first++) {
          const int* A = e[first];
          const int* B = e[1 - first];
          for (int i = 0; i < g.pieceCount(); i++) {
            const PieceView& h1 = g.pieces()[i];
            if (!h1.alive || h1.held || h1.side != HUMAN || !ready(g, h1) || !reaches(b, h1.r, h1.c, A[0], A[1])) continue;
            for (int j = 0; j < g.pieceCount(); j++) {
              const PieceView& h2 = g.pieces()[j];
              if (j == i || !h2.alive || h2.held || h2.side != HUMAN) continue;
              if (h2.cooldownUntil > now + 1000 || !reaches(b, h2.r, h2.c, B[0], B[1])) continue;
              queue2(Step{h1.r, h1.c, A[0], A[1], 0}, Step{h2.r, h2.c, B[0], B[1], h2.cooldownUntil});
              return;
            }
          }
        }
      } else if (ca == HUMAN && cb == HUMAN) {
        for (int side = 0; side < 2; side++) {
          const PieceView* v = humanAt(g, e[side][0], e[side][1]);
          if (!v || !ready(g, *v)) continue;
          const int vr = v->r, vc = v->c;
          int d[MAX_N * MAX_N];
          const int kd = dests(b, vr, vc, d);
          // Trap C: another ready piece steps into v's square after v steps aside.
          for (int j = 0; j < kd; j++) {
            const int sr = d[j] / n, sc = d[j] % n;
            Board b2 = b;
            b2.set(vr, vc, EMPTY);
            b2.set(sr, sc, HUMAN);
            for (int i = 0; i < g.pieceCount(); i++) {
              const PieceView& w = g.pieces()[i];
              if (&w == v || !w.alive || w.held || w.side != HUMAN || !ready(g, w)) continue;
              if (!reaches(b2, w.r, w.c, vr, vc)) continue;
              queue2(Step{vr, vc, sr, sc, 0}, Step{w.r, w.c, vr, vc, 0});
              return;
            }
          }
          // Trap B: v steps aside and comes back after its cooldown.
          if (kd > 0) {
            const int sr = d[0] / n, sc = d[0] % n;
            queue2(Step{vr, vc, sr, sc, 0}, Step{sr, sc, vr, vc, now + 500 + Rules().humanCooldownMs});
            return;
          }
        }
      } else if (ca == HUMAN || cb == HUMAN) {
        // Trap D: a piece still cooling down reaches the open end; it goes the moment it can.
        const int* E = ca == HUMAN ? e[1] : e[0];
        if (b.at(E[0], E[1]) != EMPTY) continue;
        for (int i = 0; i < g.pieceCount(); i++) {
          const PieceView& h = g.pieces()[i];
          if (!h.alive || h.held || h.side != HUMAN || ready(g, h)) continue;
          if (!reaches(b, h.r, h.c, E[0], E[1])) continue;
          q[0] = Step{h.r, h.c, E[0], E[1], h.cooldownUntil};
          qn = 1;
          traps++;
          return;
        }
      }
    }
  }

  void act(SimDriver& d) {
    const Game& g = d.game();
    if (g.phase() != Phase::Play) return;
    const uint32_t now = g.clockMs();
    if (holding) {
      if (now - liftClock < Params().humanMoveMs) return;
      const PlaceResult res = d.place(toR, toC);
      if (res != PlaceResult::Ok && res != PlaceResult::Pending) {
        d.place(fromR, fromC);  // the square is gone: put it back
        qn = 0;
      }
      holding = false;
      return;
    }
    if (g.heldCount() > 0) return;  // a pending placement has not landed yet
    if (qn == 0) plan(g);
    if (qn == 0) return;
    const Step st = q[0];
    if (now < st.notBefore) return;
    const PieceView* p = humanAt(g, st.fr, st.fc);
    if (!p) {
      qn = 0;
      return;
    }
    if (!ready(g, *p)) return;
    if (d.lift(st.fr, st.fc) != LiftResult::Ok) {
      qn = 0;
      return;
    }
    holding = true;
    liftClock = now;
    fromR = st.fr;
    fromC = st.fc;
    toR = st.tr;
    toC = st.tc;
    q[0] = q[1];
    qn--;
  }
};

// Mirror (left-right) and shift of a seven-row layout.
static void mirrorLayout(const char* in, char* out) {
  int start = 0;
  for (int i = 0;; i++) {
    if (in[i] == '\n' || in[i] == '\0') {
      for (int j = start; j < i; j++) out[j] = in[start + (i - 1 - j)];
      out[i] = in[i];
      if (in[i] == '\0') return;
      start = i + 1;
    }
  }
}

// The machine plays `level` from `layout` (7 'M' and 7 'H'; ids per Game's order: machine pieces
// odd, human pieces even, both in row-major order) against the Trapper for `ms` of game time.
// Returns the machine pieces the human captured; *traps = trap sequences the human started.
// `pre`: a first human move (from row, col, to row, col; pre[0] < 0 for none). *predicted (when
// given) = pieces lost that the read saw coming (State::lostPredicted).
static int playTraps(const char* layout, const int pre[4], Level level, uint32_t ms, int* traps,
                     int* predicted = nullptr) {
  Params p;
  p.level = level;
  SimDriver d(Rules(), p, 1);
  d.start();
  for (int i = 0; i < 300; i++) d.advance(10);  // countdown
  TEST_ASSERT_EQUAL_INT((int)Phase::Play, (int)d.state().phase);
  int m = 0, h = 0, r = 0, c = 0;
  for (const char* q = layout; *q; ++q) {
    if (*q == '\n') {
      r++;
      c = 0;
      continue;
    }
    if (*q == 'M') d.teleport(2 * (m++) + 1, r, c);
    if (*q == 'H') d.teleport(2 * (h++) + 2, r, c);
    c++;
  }
  TEST_ASSERT_EQUAL_INT(7, m);
  TEST_ASSERT_EQUAL_INT(7, h);
  Trapper t;
  if (pre[0] >= 0) {
    t.q[0] = Trapper::Step{pre[0], pre[1], pre[2], pre[3], 0};
    t.qn = 1;
  }
  const uint32_t end = d.game().clockMs() + ms;
  while (d.state().phase != Phase::Over && d.game().clockMs() < end) {
    d.advance(10);
    t.act(d);
  }
  *traps = t.traps;
  if (predicted) *predicted = d.state().lostPredicted;
  return d.state().capturedHuman;
}

struct TrapCase {
  const char* name;
  const char* layout;
  int pre[4];  // a first human move, or {-1, ...}
};

// Each layout, its mirror image and the shifted copies given, at normal and hard. With `sprung`,
// the human must also have started a trap in every game (the machine piece is already exposed, so
// the test checks that it gets out in time, not only that it does not walk in). `firstMirror` = 1
// checks the mirror image only.
static void checkTraps(const TrapCase* cases, int count, bool sprung, int firstMirror = 0) {
  char msg[96];
  for (int i = 0; i < count; i++) {
    for (int mirror = firstMirror; mirror < 2; mirror++) {
      char buf[64];
      if (mirror) {
        mirrorLayout(cases[i].layout, buf);
      } else {
        std::snprintf(buf, sizeof(buf), "%s", cases[i].layout);
      }
      int pre[4];
      for (int j = 0; j < 4; j++) pre[j] = cases[i].pre[j];
      if (mirror && pre[0] >= 0) {
        pre[1] = 6 - pre[1];
        pre[3] = 6 - pre[3];
      }
      for (Level lv : {Level::Normal, Level::Hard}) {
        int traps = 0;
        const int lost = playTraps(buf, pre, lv, 20000, &traps);
        std::snprintf(msg, sizeof(msg), "%s%s at %s: lost %d (%d traps)", cases[i].name, mirror ? " mirrored" : "",
                      lv == Level::Hard ? "hard" : "normal", lost, traps);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, lost, msg);
        if (sprung) TEST_ASSERT_TRUE_MESSAGE(traps > 0, msg);
      }
    }
  }
}

// Trap A: from the opening, a machine piece that comes out into the open is closed in by two
// human pieces one after the other.
void test_trap_a_in_time() {
  const TrapCase cases[] = {
      {"A", "MMMMMMM\n.......\n.......\n.......\n.......\n.......\nHHHHHHH", {-1, -1, -1, -1}},
      {"A shifted up", "MMMMMMM\n.......\n.......\n.......\n.......\nHHHHHHH\n.......", {-1, -1, -1, -1}},
      {"A shifted right", ".MMMMMM\nM......\n.......\n.......\n.......\nH......\n.HHHHHH", {-1, -1, -1, -1}},
  };
  checkTraps(cases, 3, false);
  // A machine piece already in the open: both ends of its row can be reached at once.
  const TrapCase exposed[] = {
      {"A exposed", "MMM.MMM\n...M...\n.......\n.......\n.......\n.......\nHHHHHHH", {-1, -1, -1, -1}},
      {"A exposed shifted", "MMM.MMM\n..M....\n.......\n.......\n.......\n.......\nHHHHHHH", {-1, -1, -1, -1}},
      {"A exposed shifted down", "MMM.MMM\n.......\n...M...\n.......\n.......\n.......\nHHHHHHH", {-1, -1, -1, -1}},
  };
  checkTraps(exposed, 3, true);
}

// Trap B: two human pieces with an empty square between them; a machine piece that goes in is
// caught when one of them steps aside and comes back after its cooldown.
void test_trap_b_in_time() {
  const TrapCase cases[] = {
      {"B", "MMMMMMM\n......H\n.......\n.......\n.......\n..H.H..\nHH...HH", {-1, -1, -1, -1}},
      {"B shifted", "MMMMMMM\n......H\n.......\n.......\n.......\n.H.H...\nH...HHH", {-1, -1, -1, -1}},
      {"B shifted up", "MMMMMMM\n......H\n.......\n.......\n..H.H..\n.......\nHH...HH", {-1, -1, -1, -1}},
  };
  checkTraps(cases, 3, false);
  // A machine piece already between the two human pieces.
  const TrapCase exposed[] = {
      {"B exposed", "MMM.MMM\n..HMH..\n.......\n.......\n.......\n......H\nHH...HH", {-1, -1, -1, -1}},
      {"B exposed shifted", "MMM.MMM\n.HMH...\n.......\n.......\n.......\n......H\nH...HHH", {-1, -1, -1, -1}},
      {"B exposed shifted down", "MMM.MMM\n.......\n..HMH..\n.......\n.......\n......H\nHH...HH", {-1, -1, -1, -1}},
  };
  checkTraps(exposed, 3, true);
}

// Trap C: as B, with a third piece that steps into the square the first one left.
void test_trap_c_in_time() {
  const TrapCase cases[] = {
      {"C", "MMMMMMM\n.......\n.......\n.......\n.......\n..H.H..\nHHH..HH", {-1, -1, -1, -1}},
      {"C shifted", "MMMMMMM\n.......\n.......\n.......\n.......\n...H.H.\nHHHH..H", {-1, -1, -1, -1}},
      {"C shifted up", "MMMMMMM\n.......\n.......\n.......\n..H.H..\n..H....\nHH...HH", {-1, -1, -1, -1}},
  };
  checkTraps(cases, 3, false);
  // A machine piece already between the two human pieces, a third one ready to step in.
  const TrapCase exposed[] = {
      {"C exposed", "MMM.MMM\n..HMH..\n.......\n.......\n.......\n.......\nHHH..HH", {-1, -1, -1, -1}},
      {"C exposed shifted down", "MMM.MMM\n.......\n..HMH..\n.......\n.......\n.......\nHHH..HH", {-1, -1, -1, -1}},
  };
  checkTraps(exposed, 2, true);
  // "C exposed shifted": its mirror image is escaped in time like the others.
  const TrapCase shifted[] = {
      {"C exposed shifted", "MMM.MMM\n...HMH.\n.......\n.......\n.......\n.......\nHHHH..H", {-1, -1, -1, -1}},
  };
  checkTraps(shifted, 1, true, 1);
  // The unmirrored copy is a forced loss under the time model: (1,3) steps aside and (6,3) comes
  // up the open column into its square, with (1,5) and the pieces on row 6 left to answer an
  // escape. Forcing each first move of the machine in turn (every column escape, every move
  // elsewhere) loses the piece against a human playing as fast as the model allows; the column
  // escapes only survive the Trapper because it never changes the plan it queued. So the test
  // checks the read instead: every piece lost was seen coming (risk >= 0.5 in the latest read at
  // least the level's margin before the capture, State::lostPredicted).
  for (Level lv : {Level::Normal, Level::Hard}) {
    char msg[96];
    int traps = 0, predicted = 0;
    const int lost = playTraps(shifted[0].layout, shifted[0].pre, lv, 20000, &traps, &predicted);
    std::snprintf(msg, sizeof(msg), "C exposed shifted (forced loss) at %s: lost %d, predicted %d (%d traps)",
                  lv == Level::Hard ? "hard" : "normal", lost, predicted, traps);
    TEST_ASSERT_TRUE_MESSAGE(traps > 0, msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(lost, predicted, msg);
  }
}

// Trap D: a machine piece beside a human piece; a human piece moves to where it reaches the open
// side and goes there the moment its cooldown is over (the old read did not count a piece in
// cooldown until 1.5 s were left, or at all at normal).
void test_trap_d_in_time() {
  const TrapCase cases[] = {
      {"D", "MMM.MMM\n.......\n..HM...\n.......\n.......\nH......\nHHHH..H", {6, 6, 2, 6}},
      {"D shifted", "MMM.MMM\n.......\n...HM..\n.......\n.......\nH......\nHHHH..H", {6, 6, 2, 6}},
      {"D shifted down", "MMM.MMM\n.......\n.......\n..HM...\n.......\nH......\nHHHH..H", {6, 6, 3, 6}},
  };
  checkTraps(cases, 3, true);
}

void test_state_shape_and_manual_play() {
  SimDriver d(Rules(), Params(), 1);
  State s = d.state();
  TEST_ASSERT_EQUAL_INT((int)Phase::Ready, (int)s.phase);
  TEST_ASSERT_EQUAL_INT(7, s.n);
  TEST_ASSERT_EQUAL_INT(14, s.pieceCount);
  TEST_ASSERT_EQUAL_INT(MACHINE, s.cells[0 * 7 + 3]);
  TEST_ASSERT_EQUAL_INT(HUMAN, s.cells[6 * 7 + 3]);
  TEST_ASSERT_EQUAL_INT(-1, s.stuckId);
  d.start();
  for (int i = 0; i < 300; i++) d.advance(10);  // 3 s countdown
  s = d.state();
  TEST_ASSERT_EQUAL_INT((int)Phase::Play, (int)s.phase);
  // Lift the human piece in column 0: it leaves the board and its destinations light up blue.
  TEST_ASSERT_EQUAL_INT((int)LiftResult::Ok, (int)d.lift(6, 0));
  s = d.state();
  TEST_ASSERT_EQUAL_INT(EMPTY, s.cells[6 * 7 + 0]);
  TEST_ASSERT_TRUE(s.pieces[1].held);
  TEST_ASSERT_EQUAL_INT(255, s.leds[5 * 7 + 0].b);
  TEST_ASSERT_EQUAL_INT((int)PlaceResult::Illegal, (int)d.place(5, 1));
  TEST_ASSERT_EQUAL_INT((int)PlaceResult::Ok, (int)d.place(4, 0));
  s = d.state();
  const PieceState& p = s.pieces[1];
  TEST_ASSERT_EQUAL_INT(2, p.id);
  TEST_ASSERT_EQUAL_INT(4, p.r);
  TEST_ASSERT_EQUAL_INT(0, p.c);
  TEST_ASSERT_FALSE(p.held);
  TEST_ASSERT_EQUAL_UINT32(3000, p.cooldownLeft);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, p.x);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 4.0f, p.y);
  TEST_ASSERT_EQUAL_INT((int)LiftResult::InCooldown, (int)d.lift(4, 0));
  // A placement within 1 s of the previous one is held until the interval has passed.
  TEST_ASSERT_EQUAL_INT((int)LiftResult::Ok, (int)d.lift(6, 6));
  TEST_ASSERT_EQUAL_INT((int)PlaceResult::Pending, (int)d.place(5, 6));
  s = d.state();
  TEST_ASSERT_EQUAL_INT(14, s.pendingId);
  TEST_ASSERT_EQUAL_INT(5, s.pendingR);
  TEST_ASSERT_EQUAL_INT(6, s.pendingC);
  TEST_ASSERT_EQUAL_INT(EMPTY, s.cells[5 * 7 + 6]);
  for (int i = 0; i < 100; i++) d.advance(10);
  s = d.state();
  TEST_ASSERT_EQUAL_INT(-1, s.pendingId);
  TEST_ASSERT_EQUAL_INT(HUMAN, s.cells[5 * 7 + 6]);
  for (int i = 0; i < 100; i++) d.advance(10);
  // The machine reads and moves on its own. It stays put while nothing is gained or threatened,
  // so offer it a capture: with its piece moved to (1,3), a human piece on (1,2) is caught by
  // (0,1) -> (1,1).
  d.teleport(13, 1, 3);
  TEST_ASSERT_EQUAL_INT((int)LiftResult::Ok, (int)d.lift(6, 2));
  TEST_ASSERT_EQUAL_INT((int)PlaceResult::Ok, (int)d.place(1, 2));
  for (int i = 0; i < 1000; i++) d.advance(10);
  s = d.state();
  TEST_ASSERT_TRUE(s.machineMoves > 0);
}

void test_events_are_recorded() {
  // Find a seeded game with captures and check the event log mirrors the counters.
  for (uint32_t seed = 1; seed <= 20; seed++) {
    SimDriver d(Rules(), Params(), seed);
    d.enableBot(BotParams());
    d.start();
    State s = d.state();
    for (uint32_t t = 0; t < MAX_SIM_MS && s.phase != Phase::Over; t += 10) {
      d.advance(10);
      s = d.state();
    }
    const int caps = s.capturedHuman + s.capturedMachine;
    if (caps == 0) continue;
    int logged = 0, saves = 0;
    uint32_t lastSeq = 0;
    for (int i = 0; i < s.eventCount; i++) {
      const SimEvent& e = s.events[i];
      if (i > 0) TEST_ASSERT_TRUE(e.seq < lastSeq);  // newest first
      lastSeq = e.seq;
      if (e.kind == EventKind::Capture) logged += e.count;
      if (e.kind == EventKind::Save) saves++;
    }
    TEST_ASSERT_TRUE(s.eventCount <= MAX_EVENTS);
    if (s.events[0].seq <= (uint32_t)MAX_EVENTS) {
      // Nothing dropped yet: the log holds every capture and save.
      TEST_ASSERT_EQUAL_INT(caps, logged);
      TEST_ASSERT_EQUAL_INT(s.saves, saves);
    }
    return;
  }
  TEST_FAIL_MESSAGE("no seeded game had a capture");
}

// ---- Rule candidates (plan 2026-11-06 3.1) ----

static Rules ruleSet(bool edge, bool wall, uint32_t untouchedMs) {
  Rules r;
  r.edgeCapture = edge;
  r.wallLimit = wall;
  r.untouchedMs = untouchedMs;
  return r;
}

// RW: after the countdown the read and the bot each make one move (the opening), then Play.
void test_opening_one_move_each_then_play() {
  for (BotStyle style : {BotStyle::Random, BotStyle::Trap, BotStyle::Race, BotStyle::Race3}) {
    SimDriver d(ruleSet(true, true, 0), Params(), 7);
    d.enableBot(botOf(style));
    d.start();
    const State s0 = d.state();
    State s = s0;
    bool sawOpening = false;
    for (int i = 0; i < 2000 && s.phase != Phase::Play; i++) {
      d.advance(10);
      s = d.state();
      if (s.phase != Phase::Opening) continue;
      sawOpening = true;
      TEST_ASSERT_EQUAL_UINT32(0, s.clockMs);
    }
    TEST_ASSERT_TRUE(sawOpening);
    TEST_ASSERT_EQUAL_INT((int)Phase::Play, (int)s.phase);
    TEST_ASSERT_TRUE(s.openingHuman);
    TEST_ASSERT_TRUE(s.openingMachine);
    int movedH = 0, movedM = 0;
    for (int i = 0; i < s.pieceCount; i++) {
      const PieceState& a = s0.pieces[i];
      const PieceState& b = s.pieces[i];
      if (a.r == b.r && a.c == b.c) continue;
      if (b.side == HUMAN) {
        movedH++;
      } else {
        movedM++;
      }
    }
    TEST_ASSERT_EQUAL_INT(1, movedH);
    TEST_ASSERT_EQUAL_INT(1, movedM);
    TEST_ASSERT_EQUAL_INT(1, s.machineMoves);
  }
  // R0 has no opening: the flags read as done from the start.
  SimDriver r0(Rules(), Params(), 7);
  TEST_ASSERT_TRUE(r0.state().openingHuman && r0.state().openingMachine);
}

static bool humanWall(const State& s) {
  for (int r = 0; r < s.n; r++) {
    int k = 0;
    for (int c = 0; c < s.n; c++) k += s.cells[r * s.n + c] == HUMAN;
    if (k == s.n) return true;
  }
  return false;
}

// Bot games under RE + RW (and RU) end, and the bot never builds a wall after the opening.
void test_rule_bot_games_finish() {
  for (uint32_t seed = 1; seed <= 6; seed++) {
    const uint32_t untouched = seed > 4 ? 20000u : 0u;
    SimDriver d(ruleSet(true, true, untouched), Params(), seed);
    d.enableBot(botOf(seed % 2 == 0 ? BotStyle::Race : BotStyle::Random));
    d.start();
    State s = d.state();
    bool opened = false;
    for (uint32_t t = 0; t < MAX_SIM_MS && s.phase != Phase::Over; t += 10) {
      d.advance(10);
      s = d.state();
      if (s.phase == Phase::Play) opened = true;
      if (opened) TEST_ASSERT_FALSE(humanWall(s));
    }
    TEST_ASSERT_EQUAL_INT((int)Phase::Over, (int)s.phase);
    TEST_ASSERT_TRUE(opened);
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_state_shape_and_manual_play);
  RUN_TEST(test_same_seed_same_game);
  RUN_TEST(test_events_are_recorded);
  RUN_TEST(test_hundred_bot_games_finish);
  RUN_TEST(test_bot_keeps_to_human_rules);
  RUN_TEST(test_move_time_jitter);
  RUN_TEST(test_same_seed_same_game_every_style);
  RUN_TEST(test_trap_a);
  RUN_TEST(test_trap_b);
  RUN_TEST(test_trap_c);
  RUN_TEST(test_race_picks_least_spare);
  RUN_TEST(test_race_guards_a_threatened_piece);
  RUN_TEST(test_race3_three_moves);
  RUN_TEST(test_trap_a_in_time);
  RUN_TEST(test_trap_b_in_time);
  RUN_TEST(test_trap_c_in_time);
  RUN_TEST(test_trap_d_in_time);
  RUN_TEST(test_opening_one_move_each_then_play);
  RUN_TEST(test_rule_bot_games_finish);
  return UNITY_END();
}
