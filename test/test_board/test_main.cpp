#include <unity.h>
#include <cstring>
#include "Board.h"

// Build a board from rows separated by '\n': '.' empty, 'H' human, 'M' machine.
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

// Sorted copy helper for order-independent comparisons.
static void sortInts(int* a, int k) {
  for (int i = 1; i < k; i++)
    for (int j = i; j > 0 && a[j - 1] > a[j]; j--) { int t = a[j]; a[j] = a[j - 1]; a[j - 1] = t; }
}

void setUp() {}
void tearDown() {}

void test_single_sandwich() {
  // Human moves to (3,4) and sandwiches M at (3,3) against H at (3,2).
  Board b = makeBoard(
      ".......\n"
      ".......\n"
      ".......\n"
      "..HMH..\n"
      ".......\n"
      ".......\n"
      ".......");
  int out[MAX_N * MAX_N];
  int k = capturesAt(b, HUMAN, 3, 4, out);
  TEST_ASSERT_EQUAL_INT(1, k);
  TEST_ASSERT_EQUAL_INT(3 * 7 + 3, out[0]);
}

void test_line_of_two() {
  Board b = makeBoard(
      ".......\n"
      ".......\n"
      ".......\n"
      ".HMMH..\n"
      ".......\n"
      ".......\n"
      ".......");
  int out[MAX_N * MAX_N];
  int k = capturesAt(b, HUMAN, 3, 4, out);
  TEST_ASSERT_EQUAL_INT(2, k);
  sortInts(out, k);
  TEST_ASSERT_EQUAL_INT(3 * 7 + 2, out[0]);
  TEST_ASSERT_EQUAL_INT(3 * 7 + 3, out[1]);
}

void test_edge_not_captured() {
  // M M H touching the left edge: the run reaches the edge, no capture.
  Board b = makeBoard(
      ".......\n"
      ".......\n"
      ".......\n"
      "MMH....\n"
      ".......\n"
      ".......\n"
      ".......");
  int out[MAX_N * MAX_N];
  TEST_ASSERT_EQUAL_INT(0, capturesAt(b, HUMAN, 3, 2, out));
}

void test_entering_is_safe() {
  // M moves into H _ H: only the mover's captures are checked, and M takes nothing.
  Board b = makeBoard(
      ".......\n"
      ".......\n"
      ".......\n"
      "..HMH..\n"
      ".......\n"
      ".......\n"
      ".......");
  int out[MAX_N * MAX_N];
  TEST_ASSERT_EQUAL_INT(0, capturesAt(b, MACHINE, 3, 3, out));
}

void test_two_directions() {
  // Human moves to (3,3); captures M at (3,2) horizontally and (2,3) vertically.
  Board b = makeBoard(
      ".......\n"
      "...H...\n"
      "...M...\n"
      ".HMH...\n"
      ".......\n"
      ".......\n"
      ".......");
  int out[MAX_N * MAX_N];
  int k = capturesAt(b, HUMAN, 3, 3, out);
  TEST_ASSERT_EQUAL_INT(2, k);
  sortInts(out, k);
  TEST_ASSERT_EQUAL_INT(2 * 7 + 3, out[0]);
  TEST_ASSERT_EQUAL_INT(3 * 7 + 2, out[1]);
}

void test_dests_order_and_blocking() {
  Board b = makeBoard(
      ".......\n"
      "...M...\n"
      ".......\n"
      ".H.H..M\n"
      ".......\n"
      "...H...\n"
      ".......");
  int out[MAX_N * MAX_N];
  int k = dests(b, 3, 3, out);
  // down: (4,3) then blocked by (5,3); up: (2,3) then blocked by (1,3);
  // right: (3,4),(3,5) then blocked by (3,6); left: (3,2) then blocked by (3,1).
  int expected[] = {4 * 7 + 3, 2 * 7 + 3, 3 * 7 + 4, 3 * 7 + 5, 3 * 7 + 2};
  TEST_ASSERT_EQUAL_INT(5, k);
  TEST_ASSERT_EQUAL_INT_ARRAY(expected, out, 5);
}

void test_dests_to_edges() {
  Board b = makeBoard(
      "M......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......");
  int out[MAX_N * MAX_N];
  int k = dests(b, 0, 0, out);
  TEST_ASSERT_EQUAL_INT(12, k);
  for (int i = 0; i < 6; i++) TEST_ASSERT_EQUAL_INT((i + 1) * 7, out[i]);
  for (int i = 0; i < 6; i++) TEST_ASSERT_EQUAL_INT(i + 1, out[6 + i]);
}

void test_board_accessors() {
  Board b = makeBoard(
      "...\n"
      ".H.\n"
      "..M");
  TEST_ASSERT_EQUAL_INT(3, b.n);
  TEST_ASSERT_EQUAL_INT(HUMAN, b.at(1, 1));
  TEST_ASSERT_EQUAL_INT(MACHINE, b.at(2, 2));
  TEST_ASSERT_TRUE(b.inside(0, 0));
  TEST_ASSERT_TRUE(b.inside(2, 2));
  TEST_ASSERT_FALSE(b.inside(-1, 0));
  TEST_ASSERT_FALSE(b.inside(0, 3));
  TEST_ASSERT_FALSE(b.inside(3, 0));
}

void test_9x9_same_results() {
  Board b = makeBoard(
      ".........\n"
      ".........\n"
      ".........\n"
      ".HMMH....\n"
      ".........\n"
      "MMH......\n"
      ".........\n"
      "......HMH\n"
      ".........");
  int out[MAX_N * MAX_N];
  int k = capturesAt(b, HUMAN, 3, 4, out);
  TEST_ASSERT_EQUAL_INT(2, k);
  sortInts(out, k);
  TEST_ASSERT_EQUAL_INT(3 * 9 + 2, out[0]);
  TEST_ASSERT_EQUAL_INT(3 * 9 + 3, out[1]);
  TEST_ASSERT_EQUAL_INT(0, capturesAt(b, HUMAN, 5, 2, out));
  // Right edge: H M H on the last columns captures (run ends at own piece, not edge).
  k = capturesAt(b, HUMAN, 7, 8, out);
  TEST_ASSERT_EQUAL_INT(1, k);
  TEST_ASSERT_EQUAL_INT(7 * 9 + 7, out[0]);
  TEST_ASSERT_EQUAL_INT(0, capturesAt(b, MACHINE, 7, 7, out));

  // dests order on 9x9: piece at (3,4) H; down to row 8, up to row 0, right to col 8, left blocked.
  k = dests(b, 3, 4, out);
  int expected[] = {4 * 9 + 4, 5 * 9 + 4, 6 * 9 + 4, 7 * 9 + 4, 8 * 9 + 4,
                    2 * 9 + 4, 1 * 9 + 4, 0 * 9 + 4,
                    3 * 9 + 5, 3 * 9 + 6, 3 * 9 + 7, 3 * 9 + 8};
  TEST_ASSERT_EQUAL_INT(12, k);
  TEST_ASSERT_EQUAL_INT_ARRAY(expected, out, 12);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_single_sandwich);
  RUN_TEST(test_line_of_two);
  RUN_TEST(test_edge_not_captured);
  RUN_TEST(test_entering_is_safe);
  RUN_TEST(test_two_directions);
  RUN_TEST(test_dests_order_and_blocking);
  RUN_TEST(test_dests_to_edges);
  RUN_TEST(test_board_accessors);
  RUN_TEST(test_9x9_same_results);
  return UNITY_END();
}
