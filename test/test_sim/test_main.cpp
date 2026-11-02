#include <unity.h>
#include <cstdio>
#include <cstring>
#include "HumanBot.h"
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
  // The machine reads and moves on its own. It does not walk into the open where two human
  // pieces could close on it, so offer it something: a human piece next to its back row.
  TEST_ASSERT_EQUAL_INT((int)LiftResult::Ok, (int)d.lift(6, 1));
  TEST_ASSERT_EQUAL_INT((int)PlaceResult::Ok, (int)d.place(1, 1));
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
  return UNITY_END();
}
