#include <unity.h>
#include <cstring>
#include "Capture.h"
#include "Game.h"

// The unified capture judgement (plan 2026-11-06 3.1-3.2): the custodian capture of R0, the
// official edge capture RE (the scenes of the Japan Shogi Association's diagram), RU, and the
// wall of RW.

// Rows separated by '\n': '.' empty, 'H' human, 'M' machine.
static Board makeBoard(const char* s) {
  Board b{};
  int n = 0;
  while (s[n] && s[n] != '\n') n++;
  b.n = n;
  std::memset(b.cells, 0, sizeof(b.cells));
  int r = 0, c = 0;
  for (const char* p = s; *p; ++p) {
    if (*p == '\n') {
      r++;
      c = 0;
      continue;
    }
    b.set(r, c, *p == 'H' ? HUMAN : *p == 'M' ? MACHINE : EMPTY);
    c++;
  }
  return b;
}

static Rules withEdge() {
  Rules r;
  r.edgeCapture = true;
  return r;
}

static bool contains(const int out[], int k, int sq) {
  for (int i = 0; i < k; i++)
    if (out[i] == sq) return true;
  return false;
}

// Piece ids of a new 7x7 game: column c has the machine piece (row 0) id 2c+1 and the human
// piece (row 6) id 2c+2.
static int mId(int c) { return 2 * c + 1; }
static int hId(int c) { return 2 * c + 2; }
static void startPlay(Game& g) {
  g.start(0);
  g.tick(3000);
}
static void teleport(Game& g, int id, int r, int c) { g.onMachineStep(id, r, c); }
static const PieceView& piece(const Game& g, int id) { return g.pieces()[id - 1]; }
// Drags the machine piece square by square to (tr,tc) in a straight line and arrives there.
static CaptureResult machineMove(Game& g, int id, int tr, int tc, uint32_t now) {
  const PieceView& p = piece(g, id);
  const int dr = tr > p.r ? 1 : tr < p.r ? -1 : 0, dc = tc > p.c ? 1 : tc < p.c ? -1 : 0;
  while (p.r != tr || p.c != tc) {
    const int nr = p.r + dr, nc = p.c + dc;
    TEST_ASSERT_TRUE(g.machineCanEnter(id, nr, nc));
    g.onMachineStep(id, nr, nc);
  }
  return g.onMachineArrive(id, now);
}

void setUp() {}
void tearDown() {}

// R0: captures() with the default rules is the custodian capture of Board.h, in the same order.
void test_r0_is_custodian_only() {
  Board b = makeBoard(
      "HMMH...\n"
      ".......\n"
      "...M...\n"
      "...H...\n"
      ".......\n"
      "MH.....\n"
      "HM.....");
  int a[MAX_N * MAX_N], o[MAX_N * MAX_N];
  const int k = captures(b, Rules(), HUMAN, 0, 3, a);
  TEST_ASSERT_EQUAL_INT(2, k);
  TEST_ASSERT_EQUAL_INT(capturesAt(b, HUMAN, 0, 3, o), k);
  for (int i = 0; i < k; i++) TEST_ASSERT_EQUAL_INT(o[i], a[i]);
  // The corner pieces (5,0)/(6,1) around H(6,0) do not capture it under R0.
  TEST_ASSERT_EQUAL_INT(0, captures(b, Rules(), MACHINE, 6, 1, a));
}

// Scene 1 of the diagram: three pieces along the top edge from the corner, taken by filling the
// square to their right and the three squares below.
void test_re_three_along_the_edge_from_the_corner() {
  Board b = makeBoard(
      "MMMH...\n"
      "HH.....\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......");
  b.set(1, 2, HUMAN);  // the last of the four squares
  int out[MAX_N * MAX_N];
  TEST_ASSERT_EQUAL_INT(0, captures(b, Rules(), HUMAN, 1, 2, out));
  const int k = captures(b, withEdge(), HUMAN, 1, 2, out);
  TEST_ASSERT_EQUAL_INT(3, k);
  TEST_ASSERT_TRUE(contains(out, k, 0));
  TEST_ASSERT_TRUE(contains(out, k, 1));
  TEST_ASSERT_TRUE(contains(out, k, 2));
  // The square to the right filled last captures the same.
  b = makeBoard(
      "MMM....\n"
      "HHH....\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......");
  b.set(0, 3, HUMAN);
  TEST_ASSERT_EQUAL_INT(3, captures(b, withEdge(), HUMAN, 0, 3, out));
}

// Scene 2: one piece on the left edge, taken by filling above, below and to the right.
void test_re_one_on_the_side_edge() {
  Board b = makeBoard(
      ".......\n"
      ".......\n"
      "H......\n"
      "MH.....\n"
      "H......\n"
      ".......\n"
      ".......");
  int out[MAX_N * MAX_N];
  TEST_ASSERT_EQUAL_INT(0, captures(b, Rules(), HUMAN, 3, 1, out));
  const int k = captures(b, withEdge(), HUMAN, 3, 1, out);
  TEST_ASSERT_EQUAL_INT(1, k);
  TEST_ASSERT_EQUAL_INT(3 * 7 + 0, out[0]);
}

// Scene 3: a corner piece, taken by filling above and to the right.
void test_re_corner_piece() {
  Board b = makeBoard(
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      "H......\n"
      "MH.....");
  int out[MAX_N * MAX_N];
  TEST_ASSERT_EQUAL_INT(0, captures(b, Rules(), HUMAN, 6, 1, out));
  const int k = captures(b, withEdge(), HUMAN, 6, 1, out);
  TEST_ASSERT_EQUAL_INT(1, k);
  TEST_ASSERT_EQUAL_INT(6 * 7 + 0, out[0]);
  // The same for the machine against a human corner piece.
  b = makeBoard(
      ".....MH\n"
      "......M\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......");
  TEST_ASSERT_EQUAL_INT(1, captures(b, withEdge(), MACHINE, 1, 6, out));
  TEST_ASSERT_EQUAL_INT(6, out[0]);
}

// With one gap left the group is not taken.
void test_re_one_gap_is_not_captured() {
  Board b = makeBoard(
      "MMMH...\n"
      "HH.....\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......");
  int out[MAX_N * MAX_N];
  TEST_ASSERT_EQUAL_INT(0, captures(b, withEdge(), HUMAN, 1, 1, out));
  TEST_ASSERT_EQUAL_INT(0, captures(b, withEdge(), HUMAN, 0, 3, out));
  // A group away from the edge is never taken by enclosure: only the custodian run (2,3),(3,3)
  // goes, not (3,4).
  b = makeBoard(
      ".......\n"
      "...H...\n"
      "..HMH..\n"
      "..HMMH.\n"
      "...HH..\n"
      ".......\n"
      ".......");
  TEST_ASSERT_EQUAL_INT(2, captures(b, withEdge(), HUMAN, 1, 3, out));
}

// The arriving side's own pieces are never judged: a piece in an enclosed spot is not taken by
// its own arrival.
void test_re_own_arrival_is_not_judged() {
  Board b = makeBoard(
      "MH.....\n"
      "H......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......");
  int out[MAX_N * MAX_N];
  TEST_ASSERT_EQUAL_INT(0, captures(b, withEdge(), MACHINE, 0, 0, out));
}

// Custodian and edge captures together: each piece once.
void test_re_and_custodian_without_duplicates() {
  Board b = makeBoard(
      "MMH....\n"
      "HM.....\n"
      ".H.....\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......");
  b.set(1, 2, HUMAN);
  int out[MAX_N * MAX_N];
  // (1,1) is caught between (1,0) and (1,2); the group (0,0),(0,1),(1,1) is enclosed too.
  const int k = captures(b, withEdge(), HUMAN, 1, 2, out);
  TEST_ASSERT_EQUAL_INT(3, k);
  TEST_ASSERT_EQUAL_INT(1 * 7 + 1, out[0]);  // the custodian capture comes first
  TEST_ASSERT_TRUE(contains(out, k, 0));
  TEST_ASSERT_TRUE(contains(out, k, 1));
}

void test_edge_group_geometry() {
  Board b = makeBoard(
      "MMM.H..\n"
      "HHM....\n"
      ".......\n"
      "...M...\n"
      ".......\n"
      ".......\n"
      ".......");
  EdgeGroup g;
  TEST_ASSERT_TRUE(edgeGroupAt(b, 1, 2, g));
  TEST_ASSERT_EQUAL_INT(MACHINE, g.side);
  TEST_ASSERT_EQUAL_INT(4, g.size);
  TEST_ASSERT_EQUAL_INT(0, g.first);
  TEST_ASSERT_TRUE(g.cells.has(0) && g.cells.has(1) && g.cells.has(2) && g.cells.has(9));
  // Empty neighbours: (0,3), (1,3), (2,2) in square order.
  TEST_ASSERT_EQUAL_INT(3, g.libCount);
  TEST_ASSERT_EQUAL_INT(3, g.libs[0]);
  TEST_ASSERT_EQUAL_INT(10, g.libs[1]);
  TEST_ASSERT_EQUAL_INT(16, g.libs[2]);
  TEST_ASSERT_FALSE(edgeGroupAt(b, 3, 3, g));  // not on the edge
  TEST_ASSERT_FALSE(edgeGroupAt(b, 2, 2, g));  // empty
}

// RW: a move that fills a row with one side's pieces from edge to edge.
void test_makes_wall() {
  Board b = makeBoard(
      ".......\n"
      ".......\n"
      ".......\n"
      "HHH.HHH\n"
      "...H...\n"
      ".......\n"
      "HHHHHH.");
  Rules rw;
  rw.wallLimit = true;
  TEST_ASSERT_TRUE(makesWall(b, rw, HUMAN, 4, 3, 3, 3));
  TEST_ASSERT_FALSE(makesWall(b, Rules(), HUMAN, 4, 3, 3, 3));  // off by default
  TEST_ASSERT_FALSE(makesWall(b, rw, MACHINE, 4, 3, 3, 3));     // the row is not the machine's
  // A piece of the row itself sliding along it leaves its square empty.
  TEST_ASSERT_FALSE(makesWall(b, rw, HUMAN, 6, 5, 6, 6));
  // A held piece (off the board) put into the gap.
  TEST_ASSERT_TRUE(makesWall(b, rw, HUMAN, -1, -1, 6, 6));
}

// RE in the game, the human capturing: the scene of the corner.
void test_game_re_human_captures_corner() {
  Rules rules = withEdge();
  Game g(rules);
  startPlay(g);
  // M at (6,0)'s spot: move the human pieces away and put a machine piece in the corner.
  teleport(g, hId(0), 3, 5);
  teleport(g, mId(0), 6, 0);
  teleport(g, hId(2), 5, 2);
  // H(6,1) is there; (5,2) slides to (5,0) and closes the corner.
  TEST_ASSERT_EQUAL_INT((int)LiftResult::Ok, (int)g.onHumanLift(5, 2, 3000));
  TEST_ASSERT_EQUAL_INT((int)PlaceResult::Ok, (int)g.onHumanPlace(5, 0, 3000));
  TEST_ASSERT_FALSE(piece(g, mId(0)).alive);
  TEST_ASSERT_EQUAL_INT(1, g.captured(HUMAN));
  TEST_ASSERT_EQUAL_INT((int)Phase::Removing, (int)g.phase());
}

// A human piece put back on its square (a cancel) where machine pieces surround it entered the
// enclosure itself: no capture, the cancel checks nothing.
void test_game_re_entering_by_cancel_is_not_captured() {
  Game g(withEdge());
  startPlay(g);
  teleport(g, hId(1), 3, 5);  // (6,1) empty
  TEST_ASSERT_EQUAL_INT((int)LiftResult::Ok, (int)g.onHumanLift(6, 0, 3000));
  // While (6,0) is held, the machine fills (5,0) and (6,1).
  teleport(g, mId(1), 5, 1);
  TEST_ASSERT_EQUAL_INT(0, machineMove(g, mId(0), 5, 0, 3100).count);
  TEST_ASSERT_EQUAL_INT(0, machineMove(g, mId(1), 6, 1, 3200).count);
  TEST_ASSERT_EQUAL_INT((int)PlaceResult::Cancelled, (int)g.onHumanPlace(6, 0, 3300));
  TEST_ASSERT_TRUE(piece(g, hId(0)).alive);
  TEST_ASSERT_EQUAL_INT((int)Phase::Play, (int)g.phase());
}

// The origin of a held piece is an empty square for the capture: the corner piece next to it
// keeps that square as a liberty. Without the lift the two pieces form one enclosed group.
void test_game_re_held_origin_is_empty() {
  for (int lifted = 0; lifted < 2; lifted++) {
    Game g(withEdge());
    startPlay(g);
    teleport(g, hId(2), 3, 4);
    teleport(g, mId(2), 6, 2);
    teleport(g, mId(1), 5, 1);
    if (lifted) TEST_ASSERT_EQUAL_INT((int)LiftResult::Ok, (int)g.onHumanLift(6, 1, 3000));
    const CaptureResult cr = machineMove(g, mId(0), 5, 0, 3100);
    if (lifted) {
      TEST_ASSERT_EQUAL_INT(0, cr.count);
      TEST_ASSERT_TRUE(piece(g, hId(0)).alive);
    } else {
      TEST_ASSERT_EQUAL_INT(2, cr.count);
      TEST_ASSERT_FALSE(piece(g, hId(0)).alive);
      TEST_ASSERT_FALSE(piece(g, hId(1)).alive);
    }
  }
}

// The target of a pending placement is an empty square for the capture too.
void test_game_re_pending_target_is_empty() {
  Game g(withEdge());
  startPlay(g);
  teleport(g, hId(1), 3, 1);
  teleport(g, hId(2), 2, 6);
  teleport(g, mId(2), 6, 2);
  teleport(g, mId(0), 5, 0);
  teleport(g, mId(4), 5, 4);
  // A first placement starts the interval; (3,1) -> (6,1) right after is pending.
  TEST_ASSERT_EQUAL_INT((int)LiftResult::Ok, (int)g.onHumanLift(6, 6, 3000));
  TEST_ASSERT_EQUAL_INT((int)PlaceResult::Ok, (int)g.onHumanPlace(5, 6, 3000));
  TEST_ASSERT_EQUAL_INT((int)LiftResult::Ok, (int)g.onHumanLift(3, 1, 3100));
  TEST_ASSERT_EQUAL_INT((int)PlaceResult::Pending, (int)g.onHumanPlace(6, 1, 3200));
  // M (5,4) -> (5,1): with (6,1) counted as a human piece, (6,0),(6,1) would be enclosed.
  const CaptureResult cr = machineMove(g, mId(4), 5, 1, 3300);
  TEST_ASSERT_EQUAL_INT(0, cr.count);
  TEST_ASSERT_TRUE(piece(g, hId(0)).alive);
}

// RU: a piece not moved for untouchedMs is taken by one enemy piece arriving next to it.
void test_game_ru_untouched_piece() {
  for (int late = 0; late < 2; late++) {
    Rules rules;
    rules.untouchedMs = 20000;
    Game g(rules);
    startPlay(g);
    // The machine piece (0,3) has not moved since the start (clock 0).
    const uint32_t at = late ? 20000u : 19900u;
    g.tick(3000 + at);
    TEST_ASSERT_EQUAL_INT((int)LiftResult::Ok, (int)g.onHumanLift(6, 3, 3000 + at));
    TEST_ASSERT_EQUAL_INT((int)PlaceResult::Ok, (int)g.onHumanPlace(1, 3, 3000 + at));
    TEST_ASSERT_EQUAL_INT(late ? 1 : 0, g.captured(HUMAN));
    TEST_ASSERT_EQUAL_INT(late ? 0 : 1, (int)piece(g, mId(3)).alive);
  }
}

// RU: moving resets the piece's time, a cancel does not.
void test_game_ru_move_resets_cancel_does_not() {
  Rules rules;
  rules.untouchedMs = 20000;
  Game g(rules);
  startPlay(g);
  g.tick(8000);  // clock 5000
  teleport(g, mId(2), 0, 2);  // moved (in place) at clock 5000
  TEST_ASSERT_EQUAL_INT((int)LiftResult::Ok, (int)g.onHumanLift(6, 5, 8000));
  TEST_ASSERT_EQUAL_INT((int)PlaceResult::Cancelled, (int)g.onHumanPlace(6, 5, 8000));
  g.tick(23000);  // clock 20000
  // Human (6,2) runs up to (1,2) next to M(0,2), moved 15 s ago: no capture.
  TEST_ASSERT_EQUAL_INT((int)LiftResult::Ok, (int)g.onHumanLift(6, 2, 23000));
  TEST_ASSERT_EQUAL_INT((int)PlaceResult::Ok, (int)g.onHumanPlace(1, 2, 23000));
  TEST_ASSERT_EQUAL_INT(0, g.captured(HUMAN));
  // A machine piece steps down to (5,5), next to H(6,5): lifted and put back at clock 5000, it
  // has not moved since the start, so it is taken.
  teleport(g, mId(5), 4, 5);
  const CaptureResult cr = machineMove(g, mId(5), 5, 5, 23100);
  TEST_ASSERT_EQUAL_INT(1, cr.count);
  TEST_ASSERT_FALSE(piece(g, hId(5)).alive);
  TEST_ASSERT_TRUE(piece(g, hId(6)).alive);  // not next to (5,5)
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_r0_is_custodian_only);
  RUN_TEST(test_re_three_along_the_edge_from_the_corner);
  RUN_TEST(test_re_one_on_the_side_edge);
  RUN_TEST(test_re_corner_piece);
  RUN_TEST(test_re_one_gap_is_not_captured);
  RUN_TEST(test_re_own_arrival_is_not_judged);
  RUN_TEST(test_re_and_custodian_without_duplicates);
  RUN_TEST(test_edge_group_geometry);
  RUN_TEST(test_makes_wall);
  RUN_TEST(test_game_re_human_captures_corner);
  RUN_TEST(test_game_re_entering_by_cancel_is_not_captured);
  RUN_TEST(test_game_re_held_origin_is_empty);
  RUN_TEST(test_game_re_pending_target_is_empty);
  RUN_TEST(test_game_ru_untouched_piece);
  RUN_TEST(test_game_ru_move_resets_cancel_does_not);
  return UNITY_END();
}
