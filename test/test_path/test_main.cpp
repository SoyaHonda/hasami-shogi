#include <unity.h>
#include <cmath>
#include <cstring>
#include "Board.h"
#include "Path.h"

static Board makeBoard(const char* s) {
  Board b{};
  int n = 0;
  while (s[n] && s[n] != '\n') n++;
  b.n = n;
  std::memset(b.cells, 0, sizeof(b.cells));
  int r = 0, c = 0;
  for (const char* p = s; *p; ++p) {
    if (*p == '\n') { r++; c = 0; continue; }
    b.set(r, c, *p == 'H' ? HUMAN : *p == 'M' ? MACHINE : EMPTY);
    c++;
  }
  return b;
}

static bool isHalf(float v) {
  float f = v - std::floor(v);
  return std::fabs(f - 0.5f) < 1e-5f;
}

static void assertAdjacentSteps(const Sq* path, int len) {
  for (int i = 1; i < len; i++) {
    int d = std::abs(path[i].r - path[i - 1].r) + std::abs(path[i].c - path[i - 1].c);
    TEST_ASSERT_EQUAL_INT(1, d);
  }
}

void setUp() {}
void tearDown() {}

void test_pick_reserve_slot() {
  bool used[MAX_N] = {false};
  TEST_ASSERT_EQUAL_INT(3, pickReserveSlot(used, 7, 3));
  used[3] = true;
  // 2 and 4 are equally near; the smaller column wins.
  TEST_ASSERT_EQUAL_INT(2, pickReserveSlot(used, 7, 3));
  used[2] = true;
  TEST_ASSERT_EQUAL_INT(4, pickReserveSlot(used, 7, 3));
  for (int i = 0; i < 7; i++) used[i] = true;
  TEST_ASSERT_EQUAL_INT(-1, pickReserveSlot(used, 7, 3));
  used[6] = false;
  TEST_ASSERT_EQUAL_INT(6, pickReserveSlot(used, 7, 0));
}

void test_removal_path_only_empty_cells() {
  // Captured machine piece at (3,3) goes to the far reserve (row -1).
  Board b = makeBoard(
      ".......\n"
      "HHHH.HH\n"
      "...H...\n"
      "...M...\n"
      ".......\n"
      ".......\n"
      ".......");
  bool reserve[MAX_N] = {false};
  bool blocked[MAX_N * MAX_N] = {false};
  Sq out[MAX_N * MAX_N + 2];
  int len = 0, slotC = -1;
  TEST_ASSERT_TRUE(removalPath(b, Sq{3, 3}, -1, reserve, blocked, out, len, slotC));
  TEST_ASSERT_EQUAL_INT(3, out[0].r);
  TEST_ASSERT_EQUAL_INT(3, out[0].c);
  TEST_ASSERT_EQUAL_INT(-1, out[len - 1].r);
  TEST_ASSERT_EQUAL_INT(slotC, out[len - 1].c);
  for (int i = 1; i < len - 1; i++) {
    TEST_ASSERT_TRUE(b.inside(out[i].r, out[i].c));
    TEST_ASSERT_EQUAL_INT(EMPTY, b.at(out[i].r, out[i].c));
  }
  assertAdjacentSteps(out, len);
  // Shortest: (3,3)->(3,4)->(2,4)->(1,4)->(0,4)->(-1,4) = 6 points.
  TEST_ASSERT_EQUAL_INT(6, len);
  TEST_ASSERT_EQUAL_INT(4, slotC);
}

void test_removal_path_respects_blocked() {
  Board b = makeBoard(
      ".......\n"
      "HHHH.HH\n"
      "...H...\n"
      "...M...\n"
      ".......\n"
      ".......\n"
      ".......");
  bool reserve[MAX_N] = {false};
  bool blocked[MAX_N * MAX_N] = {false};
  blocked[1 * 7 + 4] = true;  // the only gap is held by a lifted piece's origin
  Sq out[MAX_N * MAX_N + 2];
  int len = 0, slotC = -1;
  TEST_ASSERT_FALSE(removalPath(b, Sq{3, 3}, -1, reserve, blocked, out, len, slotC));
}

void test_removal_path_picks_free_slot_near_prefer() {
  // Captured human piece at (6,3) goes to the near reserve (row 7 = n).
  Board b = makeBoard(
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      "...H...");
  bool reserve[MAX_N] = {false};
  reserve[3] = true;  // directly below is taken
  bool blocked[MAX_N * MAX_N] = {false};
  Sq out[MAX_N * MAX_N + 2];
  int len = 0, slotC = -1;
  TEST_ASSERT_TRUE(removalPath(b, Sq{6, 3}, 7, reserve, blocked, out, len, slotC));
  // Columns 2 and 4 are both reachable in 2 steps; the smaller column wins.
  TEST_ASSERT_EQUAL_INT(2, slotC);
  TEST_ASSERT_EQUAL_INT(3, len);
  TEST_ASSERT_EQUAL_INT(7, out[2].r);
  TEST_ASSERT_EQUAL_INT(2, out[2].c);
  assertAdjacentSteps(out, len);

  // With the slot free, it goes straight down.
  reserve[3] = false;
  TEST_ASSERT_TRUE(removalPath(b, Sq{6, 3}, 7, reserve, blocked, out, len, slotC));
  TEST_ASSERT_EQUAL_INT(3, slotC);
  TEST_ASSERT_EQUAL_INT(2, len);
}

void test_removal_path_avoids_full_columns() {
  Board b = makeBoard(
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      "...H...");
  bool reserve[MAX_N] = {true, true, true, true, true, false, true};
  bool blocked[MAX_N * MAX_N] = {false};
  Sq out[MAX_N * MAX_N + 2];
  int len = 0, slotC = -1;
  TEST_ASSERT_TRUE(removalPath(b, Sq{6, 3}, 7, reserve, blocked, out, len, slotC));
  TEST_ASSERT_EQUAL_INT(5, slotC);
  TEST_ASSERT_EQUAL_INT(7, out[len - 1].r);
  TEST_ASSERT_EQUAL_INT(5, out[len - 1].c);
  for (int i = 1; i < len - 1; i++) TEST_ASSERT_EQUAL_INT(EMPTY, b.at(out[i].r, out[i].c));
  assertAdjacentSteps(out, len);
}

void test_removal_path_none() {
  // Machine piece fully enclosed: no route.
  Board b = makeBoard(
      ".......\n"
      ".......\n"
      "...H...\n"
      "..HMH..\n"
      "...H...\n"
      ".......\n"
      ".......");
  bool reserve[MAX_N] = {false};
  bool blocked[MAX_N * MAX_N] = {false};
  Sq out[MAX_N * MAX_N + 2];
  int len = 0, slotC = -1;
  TEST_ASSERT_FALSE(removalPath(b, Sq{3, 3}, -1, reserve, blocked, out, len, slotC));

  // Reserve full: no route either.
  Board e = makeBoard(
      "...\n"
      ".M.\n"
      "...");
  bool full[MAX_N] = {true, true, true};
  TEST_ASSERT_FALSE(removalPath(e, Sq{1, 1}, -1, full, blocked, out, len, slotC));
}

void test_travel_path_on_grid_lines() {
  float r[8], c[8];
  int k = travelPath(0.0f, 0.0f, 4, 5, r, c);
  TEST_ASSERT_TRUE(k >= 3);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, r[0]);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, c[0]);
  TEST_ASSERT_EQUAL_FLOAT(4.0f, r[k - 1]);
  TEST_ASSERT_EQUAL_FLOAT(5.0f, c[k - 1]);
  for (int i = 1; i < k - 1; i++) {
    TEST_ASSERT_TRUE(isHalf(r[i]) || isHalf(c[i]));
  }
  // Segments between interior points run along a single axis (grid lines).
  for (int i = 2; i < k - 1; i++) {
    TEST_ASSERT_TRUE(r[i] == r[i - 1] || c[i] == c[i - 1]);
  }
  // Row direction first: the second interior point shares the first's column.
  TEST_ASSERT_EQUAL_FLOAT(c[1], c[2]);
}

void test_travel_path_from_reserve_and_fractional() {
  float r[8], c[8];
  int k = travelPath(-1.0f, 3.0f, 6, 1, r, c);
  TEST_ASSERT_EQUAL_FLOAT(-1.0f, r[0]);
  TEST_ASSERT_EQUAL_FLOAT(6.0f, r[k - 1]);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, c[k - 1]);
  for (int i = 1; i < k - 1; i++) TEST_ASSERT_TRUE(isHalf(r[i]) || isHalf(c[i]));

  k = travelPath(2.3f, 4.8f, 2, 4, r, c);
  TEST_ASSERT_EQUAL_FLOAT(2.3f, r[0]);
  TEST_ASSERT_EQUAL_FLOAT(4.8f, c[0]);
  TEST_ASSERT_EQUAL_FLOAT(2.0f, r[k - 1]);
  TEST_ASSERT_EQUAL_FLOAT(4.0f, c[k - 1]);
  for (int i = 1; i < k - 1; i++) TEST_ASSERT_TRUE(isHalf(r[i]) && isHalf(c[i]));
}

void test_travel_path_already_there() {
  float r[8], c[8];
  int k = travelPath(3.0f, 3.0f, 3, 3, r, c);
  TEST_ASSERT_EQUAL_INT(1, k);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, polylineLength(r, c, k));
}

void test_polyline_length() {
  float r[] = {0.0f, 3.0f, 3.0f};
  float c[] = {0.0f, 4.0f, 6.0f};
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 7.0f, polylineLength(r, c, 3));

  float tr[8], tc[8];
  int k = travelPath(0.0f, 0.0f, 4, 5, tr, tc);
  // Corner (0.5,0.5) -> (3.5,0.5) -> (3.5,4.5), plus two half-diagonals.
  float diag = std::sqrt(0.5f);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 3.0f + 4.0f + 2.0f * diag, polylineLength(tr, tc, k));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_pick_reserve_slot);
  RUN_TEST(test_removal_path_only_empty_cells);
  RUN_TEST(test_removal_path_respects_blocked);
  RUN_TEST(test_removal_path_picks_free_slot_near_prefer);
  RUN_TEST(test_removal_path_avoids_full_columns);
  RUN_TEST(test_removal_path_none);
  RUN_TEST(test_travel_path_on_grid_lines);
  RUN_TEST(test_travel_path_from_reserve_and_fractional);
  RUN_TEST(test_travel_path_already_there);
  RUN_TEST(test_polyline_length);
  return UNITY_END();
}
