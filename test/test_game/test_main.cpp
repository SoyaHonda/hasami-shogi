#include <unity.h>
#include "Game.h"

// Piece ids: column c has the machine piece (row 0) id 2c+1 and the human piece (row n-1) id 2c+2.
static int mId(int c) { return 2 * c + 1; }
static int hId(int c) { return 2 * c + 2; }

// Start a game and run the 3 s countdown so that it is in Play at time 3000 (clock 0).
static void startPlay(Game& g) {
  g.start(0);
  g.tick(3000);
}

// Move a piece anywhere (test setup only; onMachineStep does not check adjacency).
static void teleport(Game& g, int id, int r, int c) { g.onMachineStep(id, r, c); }

static const PieceView& piece(const Game& g, int id) { return g.pieces()[id - 1]; }

void setUp() {}
void tearDown() {}

void test_initial_position() {
  Game g;
  TEST_ASSERT_EQUAL_INT((int)Phase::Ready, (int)g.phase());
  TEST_ASSERT_EQUAL_INT(14, g.pieceCount());
  for (int c = 0; c < 7; c++) {
    const PieceView& m = piece(g, mId(c));
    const PieceView& h = piece(g, hId(c));
    TEST_ASSERT_EQUAL_INT(mId(c), m.id);
    TEST_ASSERT_EQUAL_INT(MACHINE, m.side);
    TEST_ASSERT_EQUAL_INT(0, m.r);
    TEST_ASSERT_EQUAL_INT(c, m.c);
    TEST_ASSERT_EQUAL_INT(hId(c), h.id);
    TEST_ASSERT_EQUAL_INT(HUMAN, h.side);
    TEST_ASSERT_EQUAL_INT(6, h.r);
    TEST_ASSERT_EQUAL_INT(c, h.c);
    TEST_ASSERT_TRUE(m.alive && h.alive);
    TEST_ASSERT_FALSE(m.held || h.held);
  }
  Board b = g.boardWithoutHeld();
  TEST_ASSERT_EQUAL_INT(7, b.n);
  TEST_ASSERT_EQUAL_INT(MACHINE, b.at(0, 3));
  TEST_ASSERT_EQUAL_INT(HUMAN, b.at(6, 3));
  TEST_ASSERT_EQUAL_INT(EMPTY, b.at(3, 3));
}

void test_start_countdown_to_play() {
  Game g;
  g.start(1000);
  TEST_ASSERT_EQUAL_INT((int)Phase::Countdown, (int)g.phase());
  g.tick(3999);
  TEST_ASSERT_EQUAL_INT((int)Phase::Countdown, (int)g.phase());
  g.tick(4000);
  TEST_ASSERT_EQUAL_INT((int)Phase::Play, (int)g.phase());
  TEST_ASSERT_EQUAL_UINT32(0, g.clockMs());
  g.tick(4250);
  TEST_ASSERT_EQUAL_UINT32(250, g.clockMs());
}

void test_lift_in_cooldown_is_violation() {
  Game g;
  startPlay(g);
  TEST_ASSERT_EQUAL_INT((int)LiftResult::Ok, (int)g.onHumanLift(6, 0, 3000));
  TEST_ASSERT_EQUAL_INT((int)PlaceResult::Ok, (int)g.onHumanPlace(5, 0, 3000));
  TEST_ASSERT_EQUAL_UINT32(3000, piece(g, hId(0)).cooldownUntil);
  g.tick(4000);
  TEST_ASSERT_EQUAL_INT((int)LiftResult::InCooldown, (int)g.onHumanLift(5, 0, 4000));
  Violation v = g.lastViolation();
  TEST_ASSERT_TRUE(v.valid);
  TEST_ASSERT_EQUAL_INT(5, v.r);
  TEST_ASSERT_EQUAL_INT(0, v.c);
  TEST_ASSERT_EQUAL_UINT32(4000, v.at);
  TEST_ASSERT_FALSE(piece(g, hId(0)).held);
  g.tick(6000);
  TEST_ASSERT_EQUAL_INT((int)LiftResult::Ok, (int)g.onHumanLift(5, 0, 6000));
}

void test_lift_not_playing_and_not_human() {
  Game g;
  TEST_ASSERT_EQUAL_INT((int)LiftResult::NotPlaying, (int)g.onHumanLift(6, 0, 0));
  TEST_ASSERT_EQUAL_INT((int)PlaceResult::NotPlaying, (int)g.onHumanPlace(5, 0, 0));
  startPlay(g);
  TEST_ASSERT_EQUAL_INT((int)LiftResult::NotHumanPiece, (int)g.onHumanLift(0, 0, 3000));
  TEST_ASSERT_EQUAL_INT((int)LiftResult::NotHumanPiece, (int)g.onHumanLift(3, 3, 3000));
  TEST_ASSERT_EQUAL_INT((int)PlaceResult::NoHeldPiece, (int)g.onHumanPlace(5, 0, 3000));
}

void test_pause_stops_clock() {
  Game g;
  startPlay(g);
  g.tick(3500);
  TEST_ASSERT_EQUAL_UINT32(500, g.clockMs());
  g.togglePause(3500);
  TEST_ASSERT_EQUAL_INT((int)Phase::Paused, (int)g.phase());
  g.tick(10000);
  TEST_ASSERT_EQUAL_UINT32(500, g.clockMs());
  g.togglePause(10000);
  TEST_ASSERT_EQUAL_INT((int)Phase::Countdown, (int)g.phase());
  g.tick(10999);
  TEST_ASSERT_EQUAL_INT((int)Phase::Countdown, (int)g.phase());
  TEST_ASSERT_EQUAL_UINT32(500, g.clockMs());
  g.tick(11000);
  TEST_ASSERT_EQUAL_INT((int)Phase::Play, (int)g.phase());
  g.tick(11200);
  TEST_ASSERT_EQUAL_UINT32(700, g.clockMs());
}

void test_capture_removing_countdown_play() {
  Game g;
  startPlay(g);
  // H(3,0) M(3,1) M(3,2) M(3,3) _ ; human moves (6,4) -> (3,4) and captures three.
  teleport(g, hId(0), 3, 0);
  teleport(g, mId(0), 3, 1);
  teleport(g, mId(1), 3, 2);
  teleport(g, mId(2), 3, 3);
  TEST_ASSERT_EQUAL_INT((int)LiftResult::Ok, (int)g.onHumanLift(6, 4, 3000));
  TEST_ASSERT_EQUAL_INT((int)PlaceResult::Ok, (int)g.onHumanPlace(3, 4, 3000));
  TEST_ASSERT_EQUAL_INT((int)Phase::Removing, (int)g.phase());
  TEST_ASSERT_EQUAL_INT(3, g.captured(HUMAN));
  TEST_ASSERT_EQUAL_INT(0, g.captured(MACHINE));
  TEST_ASSERT_FALSE(piece(g, mId(0)).alive);
  TEST_ASSERT_EQUAL_INT(EMPTY, g.boardWithoutHeld().at(3, 2));

  Removal rm[2 * MAX_N];
  int k = g.removals(rm);
  TEST_ASSERT_EQUAL_INT(3, k);
  TEST_ASSERT_EQUAL_INT(mId(0), rm[0].pieceId);
  TEST_ASSERT_EQUAL_INT(3, rm[0].from.r);
  TEST_ASSERT_EQUAL_INT(1, rm[0].from.c);
  TEST_ASSERT_EQUAL_INT(-1, rm[0].toRow);
  TEST_ASSERT_EQUAL_INT(1, rm[0].slotC);
  TEST_ASSERT_EQUAL_INT(mId(2), rm[2].pieceId);
  TEST_ASSERT_EQUAL_INT(3, rm[2].slotC);

  g.tick(3500);  // the clock does not run while removing
  TEST_ASSERT_EQUAL_UINT32(0, g.clockMs());
  g.onRemoved(mId(0), -1, 1);
  TEST_ASSERT_TRUE(g.reserveUsed(-1)[1]);
  g.onRemoved(mId(1), -1, 2);
  TEST_ASSERT_EQUAL_INT((int)Phase::Removing, (int)g.phase());
  TEST_ASSERT_EQUAL_INT(1, g.removals(rm));
  g.onRemoved(mId(2), -1, 3);
  TEST_ASSERT_EQUAL_INT((int)Phase::Countdown, (int)g.phase());
  TEST_ASSERT_EQUAL_INT(0, g.removals(rm));
  g.tick(4000);
  g.tick(4999);
  TEST_ASSERT_EQUAL_INT((int)Phase::Countdown, (int)g.phase());
  g.tick(5000);
  TEST_ASSERT_EQUAL_INT((int)Phase::Play, (int)g.phase());

  // Fourth capture ends the game without removals: M(4,4) between H(5,4) and H(3,4).
  teleport(g, mId(3), 4, 4);
  teleport(g, hId(5), 6, 4);
  TEST_ASSERT_EQUAL_INT((int)LiftResult::Ok, (int)g.onHumanLift(6, 4, 5000));
  TEST_ASSERT_EQUAL_INT((int)PlaceResult::Ok, (int)g.onHumanPlace(5, 4, 5000));
  TEST_ASSERT_EQUAL_INT(4, g.captured(HUMAN));
  TEST_ASSERT_EQUAL_INT((int)Phase::Over, (int)g.phase());
  TEST_ASSERT_EQUAL_INT((int)Winner::Human, (int)g.winner());
  TEST_ASSERT_EQUAL_INT(0, g.removals(rm));

  // start from Over resets.
  g.start(6000);
  TEST_ASSERT_EQUAL_INT((int)Phase::Countdown, (int)g.phase());
  TEST_ASSERT_EQUAL_INT(0, g.captured(HUMAN));
  TEST_ASSERT_TRUE(piece(g, mId(0)).alive);
  TEST_ASSERT_FALSE(g.reserveUsed(-1)[1]);
}

void test_diff_win() {
  Rules rules;
  rules.diffOn = true;
  rules.diff = 1;
  Game g(rules);
  startPlay(g);
  teleport(g, hId(0), 3, 2);
  teleport(g, mId(0), 3, 3);
  g.onHumanLift(6, 4, 3000);
  TEST_ASSERT_EQUAL_INT((int)PlaceResult::Ok, (int)g.onHumanPlace(3, 4, 3000));
  TEST_ASSERT_EQUAL_INT(1, g.captured(HUMAN));
  TEST_ASSERT_EQUAL_INT((int)Phase::Over, (int)g.phase());
  TEST_ASSERT_EQUAL_INT((int)Winner::Human, (int)g.winner());
}

void test_cancel_has_no_cooldown() {
  Game g;
  startPlay(g);
  TEST_ASSERT_EQUAL_INT((int)LiftResult::Ok, (int)g.onHumanLift(6, 0, 3000));
  TEST_ASSERT_EQUAL_INT(1, g.heldCount());
  TEST_ASSERT_EQUAL_INT((int)PlaceResult::Cancelled, (int)g.onHumanPlace(6, 0, 3100));
  TEST_ASSERT_EQUAL_INT(0, g.heldCount());
  TEST_ASSERT_EQUAL_UINT32(0, piece(g, hId(0)).cooldownUntil);
  TEST_ASSERT_EQUAL_INT((int)LiftResult::Ok, (int)g.onHumanLift(6, 0, 3200));
}

void test_diagonal_place_is_illegal() {
  Game g;
  startPlay(g);
  g.onHumanLift(6, 0, 3000);
  TEST_ASSERT_EQUAL_INT((int)PlaceResult::Illegal, (int)g.onHumanPlace(5, 1, 3100));
  const PieceView& p = piece(g, hId(0));
  TEST_ASSERT_TRUE(p.held);
  TEST_ASSERT_EQUAL_INT(6, p.r);
  TEST_ASSERT_EQUAL_INT(0, p.c);
  TEST_ASSERT_EQUAL_UINT32(0, p.cooldownUntil);
  TEST_ASSERT_EQUAL_INT(1, g.heldCount());
  Violation v = g.lastViolation();
  TEST_ASSERT_TRUE(v.valid);
  TEST_ASSERT_EQUAL_INT(5, v.r);
  TEST_ASSERT_EQUAL_INT(1, v.c);
  TEST_ASSERT_EQUAL_UINT32(3100, v.at);
}

void test_place_on_reserved_cell_is_illegal() {
  Game g;
  startPlay(g);
  TEST_ASSERT_TRUE(g.machineCanEnter(mId(0), 1, 0));
  Sq rc = g.reservedCell();
  TEST_ASSERT_EQUAL_INT(1, rc.r);
  TEST_ASSERT_EQUAL_INT(0, rc.c);
  TEST_ASSERT_FALSE(g.machineCanEnter(mId(1), 1, 0));
  TEST_ASSERT_EQUAL_INT(MACHINE, g.boardWithReserve().at(1, 0));
  TEST_ASSERT_EQUAL_INT(EMPTY, g.boardWithoutHeld().at(1, 0));
  g.onHumanLift(6, 0, 3000);
  TEST_ASSERT_EQUAL_INT((int)PlaceResult::Illegal, (int)g.onHumanPlace(1, 0, 3000));
  TEST_ASSERT_TRUE(piece(g, hId(0)).held);
  g.onMachineStep(mId(0), 1, 0);
  TEST_ASSERT_TRUE(g.reservedCell().r < 0);
  TEST_ASSERT_EQUAL_INT(1, piece(g, mId(0)).r);
}

void test_two_held_nearer_origin_wins() {
  Game g;
  startPlay(g);
  teleport(g, hId(2), 3, 5);
  teleport(g, hId(3), 3, 6);
  // Row 6: H H . . H H H. Hold (6,1) and (6,4), place at (6,3): both legal, (6,4) is nearer.
  TEST_ASSERT_EQUAL_INT((int)LiftResult::Ok, (int)g.onHumanLift(6, 1, 3000));
  TEST_ASSERT_EQUAL_INT((int)LiftResult::Ok, (int)g.onHumanLift(6, 4, 3000));
  TEST_ASSERT_EQUAL_INT(2, g.heldCount());
  TEST_ASSERT_EQUAL_INT((int)PlaceResult::Ok, (int)g.onHumanPlace(6, 3, 3000));
  TEST_ASSERT_EQUAL_INT(3, piece(g, hId(4)).c);
  TEST_ASSERT_FALSE(piece(g, hId(4)).held);
  TEST_ASSERT_TRUE(piece(g, hId(1)).held);
  TEST_ASSERT_EQUAL_INT(1, g.heldCount());
  TEST_ASSERT_EQUAL_INT(hId(1), g.heldIds()[0]);
}

void test_two_held_tie_goes_to_smaller_id() {
  Game g;
  startPlay(g);
  teleport(g, hId(2), 3, 5);
  g.onHumanLift(6, 3, 3000);
  g.onHumanLift(6, 1, 3000);
  TEST_ASSERT_EQUAL_INT((int)PlaceResult::Ok, (int)g.onHumanPlace(6, 2, 3000));
  TEST_ASSERT_EQUAL_INT(2, piece(g, hId(1)).c);
  TEST_ASSERT_TRUE(piece(g, hId(3)).held);
}

void test_third_lift_is_too_many() {
  Game g;
  startPlay(g);
  g.onHumanLift(6, 0, 3000);
  g.onHumanLift(6, 1, 3000);
  TEST_ASSERT_EQUAL_INT((int)LiftResult::TooMany, (int)g.onHumanLift(6, 2, 3100));
  TEST_ASSERT_EQUAL_INT(2, g.heldCount());
  TEST_ASSERT_FALSE(piece(g, hId(2)).held);
  Violation v = g.lastViolation();
  TEST_ASSERT_TRUE(v.valid);
  TEST_ASSERT_EQUAL_INT(6, v.r);
  TEST_ASSERT_EQUAL_INT(2, v.c);
  TEST_ASSERT_EQUAL_UINT32(3100, v.at);
}

void test_escape_when_origin_filled_and_no_moves() {
  Game g;
  startPlay(g);
  g.onHumanLift(6, 0, 3000);
  // A machine piece enters the lifted piece's origin; (5,0) is blocked too.
  TEST_ASSERT_TRUE(g.machineCanEnter(mId(0), 6, 0));
  g.onMachineStep(mId(0), 6, 0);
  teleport(g, mId(1), 5, 0);
  TEST_ASSERT_EQUAL_INT((int)PlaceResult::Illegal, (int)g.onHumanPlace(6, 0, 3000));
  TEST_ASSERT_EQUAL_INT((int)PlaceResult::Ok, (int)g.onHumanPlace(3, 3, 3000));
  const PieceView& p = piece(g, hId(0));
  TEST_ASSERT_FALSE(p.held);
  TEST_ASSERT_EQUAL_INT(3, p.r);
  TEST_ASSERT_EQUAL_INT(3, p.c);
  TEST_ASSERT_EQUAL_UINT32(3000, p.cooldownUntil);
}

void test_no_escape_while_legal_moves_exist() {
  Game g;
  startPlay(g);
  g.onHumanLift(6, 0, 3000);
  g.machineCanEnter(mId(0), 6, 0);
  g.onMachineStep(mId(0), 6, 0);
  // (5,0)..(0,0) are still reachable, so an arbitrary square is illegal.
  TEST_ASSERT_EQUAL_INT((int)PlaceResult::Illegal, (int)g.onHumanPlace(3, 3, 3000));
  TEST_ASSERT_EQUAL_INT((int)PlaceResult::Ok, (int)g.onHumanPlace(4, 0, 3000));
}

void test_machine_blocked_then_arrives_and_captures() {
  Game g;
  startPlay(g);
  teleport(g, hId(5), 2, 4);
  teleport(g, mId(5), 2, 5);
  // Machine piece in column 3 heads down towards (4,3).
  TEST_ASSERT_TRUE(g.machineCanEnter(mId(3), 1, 3));
  g.onMachineStep(mId(3), 1, 3);
  TEST_ASSERT_TRUE(g.machineCanEnter(mId(3), 2, 3));
  g.onMachineStep(mId(3), 2, 3);
  // The human drops a piece in its way.
  g.onHumanLift(6, 3, 3000);
  TEST_ASSERT_EQUAL_INT((int)PlaceResult::Ok, (int)g.onHumanPlace(3, 3, 3000));
  TEST_ASSERT_EQUAL_INT((int)Phase::Play, (int)g.phase());
  TEST_ASSERT_FALSE(g.machineCanEnter(mId(3), 3, 3));
  g.tick(3400);
  CaptureResult cr = g.onMachineArrive(mId(3), 3400);
  TEST_ASSERT_EQUAL_INT(1, cr.count);
  TEST_ASSERT_FALSE(cr.over);
  TEST_ASSERT_EQUAL_UINT32(400 + 1000, piece(g, mId(3)).cooldownUntil);
  TEST_ASSERT_FALSE(piece(g, hId(5)).alive);
  TEST_ASSERT_EQUAL_INT(1, g.captured(MACHINE));
  TEST_ASSERT_EQUAL_INT((int)Phase::Removing, (int)g.phase());
  Removal rm[2 * MAX_N];
  TEST_ASSERT_EQUAL_INT(1, g.removals(rm));
  TEST_ASSERT_EQUAL_INT(hId(5), rm[0].pieceId);
  TEST_ASSERT_EQUAL_INT(2, rm[0].from.r);
  TEST_ASSERT_EQUAL_INT(4, rm[0].from.c);
  // (2,4) is nearer the machine's edge, so the human piece goes to row -1.
  TEST_ASSERT_EQUAL_INT(-1, rm[0].toRow);
  TEST_ASSERT_EQUAL_INT(4, rm[0].slotC);
}

void test_removal_goes_to_nearer_reserve_row() {
  Game g;
  startPlay(g);
  // M(5,3) between H(5,2) and H moving (6,4) -> (5,4): row 5 is nearer the human's edge.
  teleport(g, mId(0), 5, 3);
  teleport(g, hId(2), 5, 2);
  g.onHumanLift(6, 4, 3000);
  TEST_ASSERT_EQUAL_INT((int)PlaceResult::Ok, (int)g.onHumanPlace(5, 4, 3000));
  TEST_ASSERT_EQUAL_INT((int)Phase::Removing, (int)g.phase());
  Removal rm[2 * MAX_N];
  TEST_ASSERT_EQUAL_INT(1, g.removals(rm));
  TEST_ASSERT_EQUAL_INT(7, rm[0].toRow);
  TEST_ASSERT_EQUAL_INT(3, rm[0].slotC);
  g.onRemoved(mId(0), 7, 3);
  TEST_ASSERT_TRUE(g.reserveUsed(7)[3]);
  TEST_ASSERT_FALSE(g.reserveUsed(-1)[3]);
  TEST_ASSERT_EQUAL_INT((int)Phase::Countdown, (int)g.phase());
}

void test_dragged_machine_piece_is_not_captured() {
  Game g;
  startPlay(g);
  // Machine piece (0,3) is dragging to (1,3) when the human closes H(0,2) _ H(0,4) around the
  // square it is leaving: a piece in motion is off the board for captures, like a lifted one.
  TEST_ASSERT_TRUE(g.machineCanEnter(mId(3), 1, 3));
  teleport(g, mId(2), 3, 6);
  teleport(g, mId(4), 4, 6);
  teleport(g, hId(0), 0, 2);
  g.onHumanLift(6, 4, 3000);
  TEST_ASSERT_EQUAL_INT((int)PlaceResult::Ok, (int)g.onHumanPlace(0, 4, 3000));
  TEST_ASSERT_EQUAL_INT((int)Phase::Play, (int)g.phase());
  TEST_ASSERT_TRUE(piece(g, mId(3)).alive);
  TEST_ASSERT_EQUAL_INT(0, g.captured(HUMAN));
  // Once it has stopped (no reservation), the same shape captures.
  g.onMachineStep(mId(3), 1, 3);
  g.onMachineArrive(mId(3), 3000);
  teleport(g, mId(3), 0, 3);
  teleport(g, hId(4), 6, 4);
  g.tick(6100);
  g.onHumanLift(0, 4, 6100);
  g.onHumanPlace(0, 4, 6100);  // cancel
  teleport(g, hId(4), 1, 4);
  g.tick(9200);
  g.onHumanLift(1, 4, 9200);
  TEST_ASSERT_EQUAL_INT((int)PlaceResult::Ok, (int)g.onHumanPlace(0, 4, 9200));
  TEST_ASSERT_FALSE(piece(g, mId(3)).alive);
  TEST_ASSERT_EQUAL_INT((int)Phase::Removing, (int)g.phase());
}

void test_removal_stuck_and_manual_removal() {
  Game g;
  startPlay(g);
  teleport(g, hId(0), 3, 2);
  teleport(g, mId(0), 3, 3);
  g.onHumanLift(6, 4, 3000);
  g.onHumanPlace(3, 4, 3000);
  TEST_ASSERT_EQUAL_INT((int)Phase::Removing, (int)g.phase());
  g.onRemovalStuck(mId(0), 3100);
  TEST_ASSERT_EQUAL_INT((int)Phase::Paused, (int)g.phase());
  TEST_ASSERT_EQUAL_INT(mId(0), g.stuckId());
  g.togglePause(3200);
  TEST_ASSERT_EQUAL_INT((int)Phase::Paused, (int)g.phase());
  g.onManualRemoval(mId(0));
  TEST_ASSERT_EQUAL_INT(-1, g.stuckId());
  TEST_ASSERT_EQUAL_INT((int)Phase::Countdown, (int)g.phase());
  g.tick(5000);
  g.tick(6000);
  TEST_ASSERT_EQUAL_INT((int)Phase::Play, (int)g.phase());
}

void test_report_violation() {
  Game g;
  TEST_ASSERT_FALSE(g.lastViolation().valid);
  g.reportViolation(2, 3, 1234);
  Violation v = g.lastViolation();
  TEST_ASSERT_TRUE(v.valid);
  TEST_ASSERT_EQUAL_INT(2, v.r);
  TEST_ASSERT_EQUAL_INT(3, v.c);
  TEST_ASSERT_EQUAL_UINT32(1234, v.at);
}

void test_snapshot_excludes_held() {
  Game g;
  startPlay(g);
  g.tick(3300);
  g.onHumanLift(6, 0, 3300);
  Snapshot s = g.snapshot(0.5f, 3.0f);
  TEST_ASSERT_EQUAL_INT(EMPTY, s.board.at(6, 0));
  TEST_ASSERT_EQUAL_INT(HUMAN, s.board.at(6, 1));
  TEST_ASSERT_EQUAL_INT(1, s.heldCount);
  TEST_ASSERT_EQUAL_INT(hId(0), s.heldIds[0]);
  TEST_ASSERT_EQUAL_INT(14, s.count);
  TEST_ASSERT_TRUE(s.pieces[hId(0) - 1].held);
  TEST_ASSERT_EQUAL_UINT32(300, s.clockMs);
  TEST_ASSERT_EQUAL_FLOAT(0.5f, s.headR);
  TEST_ASSERT_EQUAL_FLOAT(3.0f, s.headC);
}

void test_nine_by_nine_ids() {
  Rules rules;
  rules.n = 9;
  Game g(rules);
  TEST_ASSERT_EQUAL_INT(18, g.pieceCount());
  TEST_ASSERT_EQUAL_INT(8, piece(g, 18).r);
  TEST_ASSERT_EQUAL_INT(8, piece(g, 18).c);
  TEST_ASSERT_EQUAL_INT(0, piece(g, 17).r);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_initial_position);
  RUN_TEST(test_start_countdown_to_play);
  RUN_TEST(test_lift_in_cooldown_is_violation);
  RUN_TEST(test_lift_not_playing_and_not_human);
  RUN_TEST(test_pause_stops_clock);
  RUN_TEST(test_capture_removing_countdown_play);
  RUN_TEST(test_diff_win);
  RUN_TEST(test_cancel_has_no_cooldown);
  RUN_TEST(test_diagonal_place_is_illegal);
  RUN_TEST(test_place_on_reserved_cell_is_illegal);
  RUN_TEST(test_two_held_nearer_origin_wins);
  RUN_TEST(test_two_held_tie_goes_to_smaller_id);
  RUN_TEST(test_third_lift_is_too_many);
  RUN_TEST(test_escape_when_origin_filled_and_no_moves);
  RUN_TEST(test_no_escape_while_legal_moves_exist);
  RUN_TEST(test_machine_blocked_then_arrives_and_captures);
  RUN_TEST(test_dragged_machine_piece_is_not_captured);
  RUN_TEST(test_removal_goes_to_nearer_reserve_row);
  RUN_TEST(test_removal_stuck_and_manual_removal);
  RUN_TEST(test_report_violation);
  RUN_TEST(test_snapshot_excludes_held);
  RUN_TEST(test_nine_by_nine_ids);
  return UNITY_END();
}
