#include <unity.h>
#include <cmath>
#include <cstdio>
#include "Game.h"
#include "LedPlan.h"
#include "MachineController.h"
#include "Path.h"
#include "VirtualGantry.h"

// Piece ids: column c has the machine piece (row 0) id 2c+1 and the human piece (row n-1) id 2c+2.
static int mId(int c) { return 2 * c + 1; }
static int hId(int c) { return 2 * c + 2; }
static const PieceView& piece(const Game& g, int id) { return g.pieces()[id - 1]; }
static void teleport(Game& g, int id, int r, int c) { g.onMachineStep(id, r, c); }

// Game in Play at caller time 3000 with game clock 0.
static void startPlay(Game& g) {
  g.start(0);
  g.tick(3000);
}

// One step of the driver loop.
static void step(Game& g, MachineController& mc, uint32_t now) {
  g.tick(now);
  mc.update(now);
}

static AiMove move(int id, int r, int c) { return AiMove{false, id, r, c, 0.0f}; }

void setUp() {}
void tearDown() {}

void test_stage_durations_follow_params() {
  Game g;
  startPlay(g);
  Params p;
  p.freeSpeed = 4.0f;
  p.dragSpeed = 2.0f;
  p.engageMs = 300;
  VirtualGantry vg(p, 0.0f, 3.0f);
  MachineController mc(g, vg, p);
  uint32_t now = 3000;
  step(g, mc, now);

  // Expected travel time from the head (0,3) to the piece at (0,0).
  float pr[TRAVEL_MAX_POINTS], pc[TRAVEL_MAX_POINTS];
  int k = travelPath(0.0f, 3.0f, 0, 0, pr, pc);
  const float travelMs = polylineLength(pr, pc, k) / p.freeSpeed * 1000.0f;

  mc.takeSnapshot(now);
  mc.onThinkResult(move(mId(0), 3, 0), now);
  TEST_ASSERT_TRUE(mc.job().active);
  TEST_ASSERT_FALSE(mc.job().removing);
  TEST_ASSERT_EQUAL_INT((int)Stage::Travel, (int)mc.job().stage);
  TEST_ASSERT_EQUAL_INT((int)MagnetMode::Off, (int)vg.magnetMode());
  const uint32_t t0 = now;

  uint32_t engageAt = 0, dragAt = 0, endAt = 0;
  int stepsDone = 0;
  uint32_t stepAt[3] = {0, 0, 0};
  int lastR = 0;
  while (now < t0 + 10000) {
    now += 1;
    step(g, mc, now);
    JobView j = mc.job();
    if (!engageAt && j.active && j.stage == Stage::Engage) {
      engageAt = now;
      TEST_ASSERT_EQUAL_INT((int)MagnetMode::PullMachine, (int)vg.magnetMode());
      TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, vg.headR());
      TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, vg.headC());
    }
    if (!dragAt && j.active && j.stage == Stage::Drag) dragAt = now;
    if (piece(g, mId(0)).r != lastR) {
      lastR = piece(g, mId(0)).r;
      if (stepsDone < 3) stepAt[stepsDone] = now;
      stepsDone++;
    }
    if (!j.active) {
      endAt = now;
      break;
    }
  }
  TEST_ASSERT_TRUE(endAt > 0);
  TEST_ASSERT_UINT32_WITHIN(1, (uint32_t)std::lround(t0 + travelMs), engageAt);
  TEST_ASSERT_UINT32_WITHIN(1, engageAt + p.engageMs, dragAt);
  TEST_ASSERT_EQUAL_INT(3, stepsDone);
  TEST_ASSERT_UINT32_WITHIN(1, dragAt + 500, stepAt[0]);
  TEST_ASSERT_UINT32_WITHIN(1, dragAt + 1000, stepAt[1]);
  TEST_ASSERT_UINT32_WITHIN(1, dragAt + 1500, stepAt[2]);
  TEST_ASSERT_EQUAL_INT(3, piece(g, mId(0)).r);
  TEST_ASSERT_EQUAL_INT((int)MagnetMode::Off, (int)vg.magnetMode());
  // Arrival set the cooldown on the game clock.
  TEST_ASSERT_UINT32_WITHIN(1, g.clockMs() + 1000, piece(g, mId(0)).cooldownUntil);
}

void test_removal_magnet_modes() {
  // Human captures a machine piece: carried off with PullMachine to row -1.
  {
    Game g;
    startPlay(g);
    Params p;
    VirtualGantry vg(p, 0.0f, 3.0f);
    MachineController mc(g, vg, p);
    teleport(g, mId(3), 3, 3);
    teleport(g, hId(2), 3, 2);
    uint32_t now = 3000;
    step(g, mc, now);
    g.onHumanLift(6, 4, now);
    TEST_ASSERT_EQUAL_INT((int)PlaceResult::Ok, (int)g.onHumanPlace(3, 4, now));
    TEST_ASSERT_EQUAL_INT((int)Phase::Removing, (int)g.phase());
    bool sawEngage = false;
    while (g.phase() == Phase::Removing && now < 20000) {
      now += 5;
      step(g, mc, now);
      JobView j = mc.job();
      if (j.active && j.removing && j.stage == Stage::Engage) {
        sawEngage = true;
        TEST_ASSERT_EQUAL_INT(mId(3), j.pieceId);
        TEST_ASSERT_EQUAL_INT(-1, j.toR);
        TEST_ASSERT_EQUAL_INT((int)MagnetMode::PullMachine, (int)vg.magnetMode());
      }
    }
    TEST_ASSERT_TRUE(sawEngage);
    TEST_ASSERT_EQUAL_INT((int)Phase::Countdown, (int)g.phase());
    TEST_ASSERT_TRUE(g.reserveUsed(MACHINE)[3]);
    TEST_ASSERT_EQUAL_INT((int)MagnetMode::Off, (int)vg.magnetMode());
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -1.0f, vg.headR());
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 3.0f, vg.headC());
  }
  // Machine captures a human piece: carried off with PullHuman to row n.
  {
    Game g;
    startPlay(g);
    Params p;
    VirtualGantry vg(p, 0.0f, 3.0f);
    MachineController mc(g, vg, p);
    teleport(g, hId(3), 3, 3);
    teleport(g, mId(2), 3, 2);
    uint32_t now = 3000;
    step(g, mc, now);
    mc.takeSnapshot(now);
    mc.onThinkResult(move(mId(4), 3, 4), now);
    TEST_ASSERT_TRUE(mc.job().active);
    while (g.phase() == Phase::Play && now < 20000) {
      now += 5;
      step(g, mc, now);
    }
    TEST_ASSERT_EQUAL_INT((int)Phase::Removing, (int)g.phase());
    TEST_ASSERT_EQUAL_INT(1, g.captured(MACHINE));
    bool sawEngage = false;
    while (g.phase() == Phase::Removing && now < 40000) {
      now += 5;
      step(g, mc, now);
      JobView j = mc.job();
      if (j.active && j.removing && j.stage == Stage::Engage) {
        sawEngage = true;
        TEST_ASSERT_EQUAL_INT(hId(3), j.pieceId);
        TEST_ASSERT_EQUAL_INT(7, j.toR);
        TEST_ASSERT_EQUAL_INT((int)MagnetMode::PullHuman, (int)vg.magnetMode());
      }
    }
    TEST_ASSERT_TRUE(sawEngage);
    TEST_ASSERT_EQUAL_INT((int)Phase::Countdown, (int)g.phase());
    TEST_ASSERT_TRUE(g.reserveUsed(HUMAN)[3]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 7.0f, vg.headR());

    // Back to Play: the first read comes reactMs (on the game clock) after resuming.
    while (g.phase() != Phase::Play && now < 60000) {
      now += 5;
      step(g, mc, now);
    }
    const uint32_t resumeClock = g.clockMs();
    TEST_ASSERT_FALSE(mc.wantsThink(now));
    while (g.clockMs() < resumeClock + p.reactMs - 5) {
      now += 5;
      step(g, mc, now);
      TEST_ASSERT_FALSE(mc.wantsThink(now));
    }
    now += 5;
    step(g, mc, now);
    TEST_ASSERT_TRUE(mc.wantsThink(now));
  }
}

void test_wants_think_timing() {
  Game g;
  startPlay(g);
  Params p;  // reactMs 250 -> interval 250
  VirtualGantry vg(p, 0.0f, 3.0f);
  MachineController mc(g, vg, p);
  TEST_ASSERT_FALSE(mc.wantsThink(3000));  // clock 0, last read 0
  step(g, mc, 3000);
  TEST_ASSERT_FALSE(mc.wantsThink(3000));
  step(g, mc, 3249);
  TEST_ASSERT_FALSE(mc.wantsThink(3249));
  step(g, mc, 3250);
  TEST_ASSERT_TRUE(mc.wantsThink(3250));

  // Waiting for the result: false.
  mc.takeSnapshot(3250);
  step(g, mc, 3600);
  TEST_ASSERT_FALSE(mc.wantsThink(3600));
  mc.onThinkResult(AiMove{true, -1, 0, 0, 0.0f}, 3600);
  TEST_ASSERT_TRUE(mc.wantsThink(3600));  // 350 ms since the last read

  // During a job: false.
  mc.takeSnapshot(3600);
  mc.onThinkResult(move(mId(0), 2, 0), 3600);
  TEST_ASSERT_TRUE(mc.job().active);
  uint32_t now = 3600;
  while (mc.job().active && now < 20000) {
    now += 10;
    step(g, mc, now);
    if (mc.job().active) TEST_ASSERT_FALSE(mc.wantsThink(now));
  }
  TEST_ASSERT_FALSE(mc.job().active);
  TEST_ASSERT_TRUE(mc.wantsThink(now));

  // Not in Play: false.
  g.togglePause(now);
  step(g, mc, now + 1000);
  TEST_ASSERT_FALSE(mc.wantsThink(now + 1000));
}

// Lift brings the read forward to reactMs after the lift (when readLift is on).
static void liftCase(bool readLift) {
  Game g;
  startPlay(g);
  Params p;
  p.reactMs = 50;  // interval max(150, 50) = 150
  p.readLift = readLift;
  VirtualGantry vg(p, 0.0f, 3.0f);
  MachineController mc(g, vg, p);
  step(g, mc, 3000);
  mc.takeSnapshot(3000);  // read at clock 0
  mc.onThinkResult(AiMove{true, -1, 0, 0, 0.0f}, 3000);
  step(g, mc, 3010);
  TEST_ASSERT_EQUAL_INT((int)LiftResult::Ok, (int)g.onHumanLift(6, 3, 3010));
  mc.onHumanLift(3010);
  step(g, mc, 3059);
  TEST_ASSERT_FALSE(mc.wantsThink(3059));
  step(g, mc, 3060);
  TEST_ASSERT_EQUAL(readLift, mc.wantsThink(3060));
  step(g, mc, 3149);
  TEST_ASSERT_EQUAL(readLift, mc.wantsThink(3149));
  step(g, mc, 3150);
  TEST_ASSERT_TRUE(mc.wantsThink(3150));
  // A read consumes the lift trigger.
  mc.takeSnapshot(3150);
  mc.onThinkResult(AiMove{true, -1, 0, 0, 0.0f}, 3150);
  step(g, mc, 3250);
  TEST_ASSERT_FALSE(mc.wantsThink(3250));
}

void test_lift_brings_read_forward() { liftCase(true); }
void test_lift_ignored_without_read_lift() { liftCase(false); }

void test_stale_result_is_discarded() {
  Game g;
  startPlay(g);
  Params p;
  VirtualGantry vg(p, 0.0f, 3.0f);
  MachineController mc(g, vg, p);
  step(g, mc, 3000);
  step(g, mc, 3250);
  TEST_ASSERT_TRUE(mc.wantsThink(3250));
  mc.takeSnapshot(3250);
  // While the machine thinks, the human blocks column 0 at (2,0).
  g.onHumanLift(6, 0, 3250);
  TEST_ASSERT_EQUAL_INT((int)PlaceResult::Ok, (int)g.onHumanPlace(2, 0, 3250));
  mc.onThinkResult(move(mId(0), 3, 0), 3250);
  TEST_ASSERT_FALSE(mc.job().active);
  TEST_ASSERT_TRUE(g.reservedCell().r < 0);
  // A result that is still legal starts a job.
  mc.takeSnapshot(3250);
  mc.onThinkResult(move(mId(1), 3, 1), 3250);
  TEST_ASSERT_TRUE(mc.job().active);
}

void test_capture_mid_drag_aborts_job_and_removes() {
  Game g;
  startPlay(g);
  Params p;
  VirtualGantry vg(p, 0.0f, 0.0f);
  MachineController mc(g, vg, p);
  // Another machine piece will be captured: M(3,3) between H(3,2) and H arriving at (3,4).
  teleport(g, mId(3), 3, 3);
  teleport(g, hId(2), 3, 2);
  uint32_t now = 3000;
  step(g, mc, now);
  mc.takeSnapshot(now);
  mc.onThinkResult(move(mId(0), 5, 0), now);
  // Drag until the piece has completed (1,0) and is pulling into (2,0).
  while (!(piece(g, mId(0)).r == 1 && mc.job().stage == Stage::Drag) && now < 20000) {
    now += 5;
    step(g, mc, now);
  }
  TEST_ASSERT_EQUAL_INT(2, g.reservedCell().r);
  now += 100;  // part way through the square
  step(g, mc, now);
  g.onHumanLift(6, 4, now);
  TEST_ASSERT_EQUAL_INT((int)PlaceResult::Ok, (int)g.onHumanPlace(3, 4, now));
  TEST_ASSERT_EQUAL_INT((int)Phase::Removing, (int)g.phase());
  const uint32_t clockAtCapture = g.clockMs();
  // The square in progress is finished, then the job is aborted.
  while (mc.job().active && !mc.job().removing && now < 20000) {
    now += 5;
    step(g, mc, now);
  }
  TEST_ASSERT_EQUAL_INT(2, piece(g, mId(0)).r);
  TEST_ASSERT_EQUAL_INT(0, piece(g, mId(0)).c);
  TEST_ASSERT_TRUE(g.reservedCell().r < 0);
  TEST_ASSERT_EQUAL_UINT32(clockAtCapture + 1000, piece(g, mId(0)).cooldownUntil);
  // Then the captured piece is carried off.
  while (g.phase() == Phase::Removing && now < 40000) {
    now += 5;
    step(g, mc, now);
  }
  TEST_ASSERT_EQUAL_INT((int)Phase::Countdown, (int)g.phase());
  TEST_ASSERT_TRUE(g.reserveUsed(MACHINE)[3]);
}

void test_dragged_piece_captured_is_removed_from_finished_square() {
  Game g;
  startPlay(g);
  Params p;
  VirtualGantry vg(p, 0.0f, 1.0f);
  MachineController mc(g, vg, p);
  teleport(g, hId(0), 2, 0);
  uint32_t now = 3000;
  step(g, mc, now);
  mc.takeSnapshot(now);
  mc.onThinkResult(move(mId(1), 5, 1), now);
  while (!(piece(g, mId(1)).r == 2 && mc.job().stage == Stage::Drag) && now < 20000) {
    now += 5;
    step(g, mc, now);
  }
  TEST_ASSERT_EQUAL_INT(3, g.reservedCell().r);
  // The human captures the dragged piece at (2,1) against H(2,0).
  g.onHumanLift(6, 2, now);
  TEST_ASSERT_EQUAL_INT((int)PlaceResult::Ok, (int)g.onHumanPlace(2, 2, now));
  TEST_ASSERT_EQUAL_INT((int)Phase::Removing, (int)g.phase());
  TEST_ASSERT_FALSE(piece(g, mId(1)).alive);
  while (mc.job().active && !mc.job().removing && now < 20000) {
    now += 5;
    step(g, mc, now);
  }
  // The piece finished the square (3,1) and is carried off from there.
  Removal rm[2 * MAX_N];
  TEST_ASSERT_EQUAL_INT(1, g.removals(rm));
  TEST_ASSERT_EQUAL_INT(mId(1), rm[0].pieceId);
  TEST_ASSERT_EQUAL_INT(3, rm[0].from.r);
  TEST_ASSERT_EQUAL_INT(1, rm[0].from.c);
  bool sawEngageAtSquare = false;
  while (g.phase() == Phase::Removing && now < 40000) {
    now += 5;
    step(g, mc, now);
    JobView j = mc.job();
    if (j.active && j.removing && j.stage == Stage::Engage) {
      sawEngageAtSquare = std::fabs(vg.headR() - 3.0f) < 1e-4f && std::fabs(vg.headC() - 1.0f) < 1e-4f;
    }
  }
  TEST_ASSERT_TRUE(sawEngageAtSquare);
  TEST_ASSERT_EQUAL_INT((int)Phase::Countdown, (int)g.phase());
  TEST_ASSERT_TRUE(g.reserveUsed(MACHINE)[1]);
}

void test_no_route_pauses_then_manual_removal() {
  Game g;
  startPlay(g);
  Params p;
  VirtualGantry vg(p, 0.0f, 3.0f);
  MachineController mc(g, vg, p);
  // H(3,3) will be captured between M(3,2) and M arriving at (3,4); H(2,3) and H(4,3) wall it in.
  teleport(g, mId(2), 3, 2);
  teleport(g, hId(2), 3, 3);
  teleport(g, hId(3), 2, 3);
  teleport(g, hId(5), 4, 3);
  uint32_t now = 3000;
  step(g, mc, now);
  mc.takeSnapshot(now);
  mc.onThinkResult(move(mId(4), 3, 4), now);
  TEST_ASSERT_TRUE(mc.job().active);
  while (g.phase() == Phase::Play && now < 20000) {
    now += 5;
    step(g, mc, now);
  }
  TEST_ASSERT_EQUAL_INT(1, g.captured(MACHINE));
  while (g.phase() == Phase::Removing && now < 20000) {
    now += 5;
    step(g, mc, now);
  }
  TEST_ASSERT_EQUAL_INT((int)Phase::Paused, (int)g.phase());
  TEST_ASSERT_EQUAL_INT(hId(2), g.stuckId());
  TEST_ASSERT_FALSE(mc.job().active);
  // Nothing happens while paused.
  now += 1000;
  step(g, mc, now);
  TEST_ASSERT_EQUAL_INT((int)Phase::Paused, (int)g.phase());
  g.onManualRemoval(hId(2));
  TEST_ASSERT_EQUAL_INT((int)Phase::Countdown, (int)g.phase());
  now += 5;
  step(g, mc, now);
  now += 1000;
  step(g, mc, now);
  TEST_ASSERT_EQUAL_INT((int)Phase::Play, (int)g.phase());
}

void test_virtual_gantry_drag_to_reserve_rows() {
  Params p;
  p.dragSpeed = 2.0f;
  VirtualGantry vg(p, 0.0f, 2.0f);
  vg.update(0);
  vg.dragTo(-1, 2);
  TEST_ASSERT_FALSE(vg.idle());
  vg.update(250);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -0.5f, vg.headR());
  vg.update(500);
  TEST_ASSERT_TRUE(vg.idle());
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -1.0f, vg.headR());
  vg.magnet(MagnetMode::RepelWeak);
  TEST_ASSERT_EQUAL_INT((int)MagnetMode::RepelWeak, (int)vg.magnetMode());
}

// ---- ledPlan ----

static bool sameRgb(Rgb a, Rgb b) { return a.r == b.r && a.g == b.g && a.b == b.b; }
#define ASSERT_RGB(expected, actual)                                                           \
  do {                                                                                         \
    Rgb e_ = (expected), a_ = (actual);                                                        \
    char msg_[64];                                                                             \
    snprintf(msg_, sizeof(msg_), "got (%d,%d,%d)", a_.r, a_.g, a_.b);                          \
    TEST_ASSERT_TRUE_MESSAGE(sameRgb(e_, a_), msg_);                                           \
  } while (0)

void test_led_held_destinations_are_blue() {
  Game g;
  startPlay(g);
  Params p;
  VirtualGantry vg(p, 0.0f, 3.0f);
  MachineController mc(g, vg, p);
  TEST_ASSERT_EQUAL_INT((int)LiftResult::Ok, (int)g.onHumanLift(6, 3, 3000));
  Rgb out[49];
  ledPlan(g, mc, 3000, out);
  for (int r = 0; r < 7; r++) {
    for (int c = 0; c < 7; c++) {
      const bool blue = c == 3 && r >= 1 && r <= 5;  // up to the machine piece at (0,3)
      ASSERT_RGB(blue ? LED_BLUE : LED_OFF, out[r * 7 + c]);
    }
  }
}

void test_led_cooldown_red_proportional() {
  Game g;
  startPlay(g);
  Params p;
  VirtualGantry vg(p, 0.0f, 3.0f);
  MachineController mc(g, vg, p);
  g.onHumanLift(6, 0, 3000);
  TEST_ASSERT_EQUAL_INT((int)PlaceResult::Ok, (int)g.onHumanPlace(3, 0, 3000));  // cooldown 3000 ms
  Rgb out[49];
  ledPlan(g, mc, 3000, out);
  ASSERT_RGB((Rgb{255, 0, 0}), out[3 * 7 + 0]);
  g.tick(4500);  // 1500 ms left
  ledPlan(g, mc, 4500, out);
  ASSERT_RGB((Rgb{127, 0, 0}), out[3 * 7 + 0]);
  g.tick(5250);  // 750 ms left
  ledPlan(g, mc, 5250, out);
  ASSERT_RGB((Rgb{63, 0, 0}), out[3 * 7 + 0]);
  g.tick(6000);
  ledPlan(g, mc, 6000, out);
  ASSERT_RGB(LED_OFF, out[3 * 7 + 0]);
}

void test_led_job_destination_is_purple() {
  Game g;
  startPlay(g);
  Params p;
  VirtualGantry vg(p, 0.0f, 3.0f);
  MachineController mc(g, vg, p);
  step(g, mc, 3000);
  mc.takeSnapshot(3000);
  mc.onThinkResult(move(mId(0), 3, 0), 3000);
  TEST_ASSERT_TRUE(mc.job().active);
  Rgb out[49];
  ledPlan(g, mc, 3000, out);
  for (int i = 0; i < 49; i++) ASSERT_RGB(i == 3 * 7 + 0 ? LED_PURPLE : LED_OFF, out[i]);
}

void test_led_read_lift_threat_is_yellow() {
  for (int readLift = 0; readLift < 2; readLift++) {
    Game g;
    startPlay(g);
    Params p;
    p.readLift = readLift != 0;
    VirtualGantry vg(p, 0.0f, 3.0f);
    MachineController mc(g, vg, p);
    teleport(g, mId(3), 3, 3);
    teleport(g, hId(4), 3, 4);
    // H from (6,2) to (3,2) would capture M(3,3) against H(3,4).
    TEST_ASSERT_EQUAL_INT((int)LiftResult::Ok, (int)g.onHumanLift(6, 2, 3000));
    Rgb out[49];
    ledPlan(g, mc, 3000, out);
    ASSERT_RGB(readLift ? LED_YELLOW : LED_BLUE, out[3 * 7 + 2]);
    ASSERT_RGB(LED_BLUE, out[4 * 7 + 2]);
    ASSERT_RGB(LED_BLUE, out[2 * 7 + 2]);
  }
}

void test_led_removal_blinks_white() {
  Game g;
  startPlay(g);
  Params p;
  VirtualGantry vg(p, 0.0f, 3.0f);
  MachineController mc(g, vg, p);
  teleport(g, mId(3), 3, 3);
  teleport(g, hId(4), 3, 4);
  g.onHumanLift(6, 2, 3000);
  TEST_ASSERT_EQUAL_INT((int)PlaceResult::Ok, (int)g.onHumanPlace(3, 2, 3000));
  TEST_ASSERT_EQUAL_INT((int)Phase::Removing, (int)g.phase());
  Rgb out[49];
  ledPlan(g, mc, 3000, out);  // 3000/500 = 6: lit
  ASSERT_RGB(LED_WHITE, out[3 * 7 + 3]);
  ledPlan(g, mc, 3500, out);
  ASSERT_RGB(LED_OFF, out[3 * 7 + 3]);
  ledPlan(g, mc, 4000, out);
  ASSERT_RGB(LED_WHITE, out[3 * 7 + 3]);
}

void test_led_violation_blinks() {
  Game g;
  startPlay(g);
  Params p;
  VirtualGantry vg(p, 0.0f, 3.0f);
  MachineController mc(g, vg, p);
  g.onHumanLift(6, 1, 3000);
  TEST_ASSERT_EQUAL_INT((int)PlaceResult::Illegal, (int)g.onHumanPlace(5, 2, 3010));  // diagonal
  Rgb out[49];
  ledPlan(g, mc, 3010, out);
  ASSERT_RGB(LED_RED, out[5 * 7 + 2]);
  ledPlan(g, mc, 3110, out);
  ASSERT_RGB(LED_OFF, out[5 * 7 + 2]);
  ledPlan(g, mc, 3210, out);
  ASSERT_RGB(LED_RED, out[5 * 7 + 2]);
  ledPlan(g, mc, 3960, out);  // 950 ms: dark half
  ASSERT_RGB(LED_OFF, out[5 * 7 + 2]);
  ledPlan(g, mc, 4010, out);  // 1 s over: plain square again
  ASSERT_RGB(LED_OFF, out[5 * 7 + 2]);
  ledPlan(g, mc, 4060, out);
  ASSERT_RGB(LED_OFF, out[5 * 7 + 2]);

  // The violation wins over blue; its dark half is black.
  g.reportViolation(4, 1, 5000);
  ledPlan(g, mc, 5000, out);
  ASSERT_RGB(LED_RED, out[4 * 7 + 1]);
  ledPlan(g, mc, 5100, out);
  ASSERT_RGB(LED_OFF, out[4 * 7 + 1]);
  ledPlan(g, mc, 6000, out);
  ASSERT_RGB(LED_BLUE, out[4 * 7 + 1]);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_stage_durations_follow_params);
  RUN_TEST(test_removal_magnet_modes);
  RUN_TEST(test_wants_think_timing);
  RUN_TEST(test_lift_brings_read_forward);
  RUN_TEST(test_lift_ignored_without_read_lift);
  RUN_TEST(test_stale_result_is_discarded);
  RUN_TEST(test_capture_mid_drag_aborts_job_and_removes);
  RUN_TEST(test_dragged_piece_captured_is_removed_from_finished_square);
  RUN_TEST(test_no_route_pauses_then_manual_removal);
  RUN_TEST(test_virtual_gantry_drag_to_reserve_rows);
  RUN_TEST(test_led_held_destinations_are_blue);
  RUN_TEST(test_led_cooldown_red_proportional);
  RUN_TEST(test_led_job_destination_is_purple);
  RUN_TEST(test_led_read_lift_threat_is_yellow);
  RUN_TEST(test_led_removal_blinks_white);
  RUN_TEST(test_led_violation_blinks);
  return UNITY_END();
}
