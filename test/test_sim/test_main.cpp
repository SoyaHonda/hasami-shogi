#include <unity.h>
#include <cstdio>
#include "SimDriver.h"

void setUp() {}
void tearDown() {}

static const uint32_t MAX_CLOCK_MS = 10u * 60u * 1000u;  // game clock limit (draw)
// Safety net on simulated time so a game stuck outside Play (clock frozen) still ends the loop.
static const uint32_t MAX_SIM_MS = 3u * MAX_CLOCK_MS;

struct Outcome {
  Phase phase;
  Winner winner;
  uint32_t clockMs;
  int capHuman, capMachine, saves, machineMoves;
};

static Outcome autoplay(uint32_t seed, const Params& params = Params()) {
  SimDriver d(Rules(), params, seed);
  d.enableBot(BotParams());
  d.start();
  uint32_t simMs = 0;
  State s = d.state();
  while (s.phase != Phase::Over && s.clockMs < MAX_CLOCK_MS && simMs < MAX_SIM_MS) {
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
  int over = 0, timeouts = 0, moves = 0;
  for (uint32_t seed = 1; seed <= 100; seed++) {
    const Outcome o = autoplay(seed);
    char msg[48];
    snprintf(msg, sizeof(msg), "seed %u", (unsigned)seed);
    TEST_ASSERT_TRUE_MESSAGE(o.phase == Phase::Over || o.clockMs >= MAX_CLOCK_MS, msg);
    TEST_ASSERT_TRUE_MESSAGE(o.phase != Phase::Paused && o.phase != Phase::Removing, msg);
    if (o.phase == Phase::Over) {
      over++;
      TEST_ASSERT_TRUE_MESSAGE(o.winner != Winner::None, msg);
    } else {
      timeouts++;
    }
    moves += o.machineMoves;
  }
  TEST_ASSERT_EQUAL_INT(100, over + timeouts);
  TEST_ASSERT_TRUE(over > 0);
  TEST_ASSERT_TRUE(moves > 0);
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
    for (uint32_t t = 0; t < MAX_SIM_MS && s.phase != Phase::Over && s.clockMs < MAX_CLOCK_MS; t += 10) {
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
  return UNITY_END();
}
