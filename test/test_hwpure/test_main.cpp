#include <unity.h>
#include <cstring>
#include "BoardWatcher.h"
#include "HallClassify.h"
#include "LedIndex.h"

void setUp() {}
void tearDown() {}

// Board from rows like ".M.\nH.H\n..." (H = human/N, M = machine/S).
static int parseBoard(const char* s, int8_t out[]) {
  int n = 0;
  while (s[n] && s[n] != '\n') n++;
  std::memset(out, 0, MAX_N * MAX_N);
  int r = 0, c = 0;
  for (const char* p = s; *p; ++p) {
    if (*p == '\n') { r++; c = 0; continue; }
    out[r * n + c] = *p == 'H' ? HUMAN : *p == 'M' ? MACHINE : EMPTY;
    c++;
  }
  return n;
}

static const HallThresholds TH{2048, 300, 150, true};

static void test_classify_thresholds() {
  TEST_ASSERT_EQUAL_INT(EMPTY, classify(2048, EMPTY, TH));
  TEST_ASSERT_EQUAL_INT(EMPTY, classify(2048 + 299, EMPTY, TH));
  TEST_ASSERT_EQUAL_INT(EMPTY, classify(2048 - 299, EMPTY, TH));
  TEST_ASSERT_EQUAL_INT(MACHINE, classify(2048 + 300, EMPTY, TH));
  TEST_ASSERT_EQUAL_INT(HUMAN, classify(2048 - 300, EMPTY, TH));
  TEST_ASSERT_EQUAL_INT(MACHINE, classify(4095, EMPTY, TH));
  TEST_ASSERT_EQUAL_INT(HUMAN, classify(0, EMPTY, TH));
}

static void test_classify_south_low() {
  HallThresholds t = TH;
  t.southHigh = false;
  TEST_ASSERT_EQUAL_INT(HUMAN, classify(2048 + 400, EMPTY, t));
  TEST_ASSERT_EQUAL_INT(MACHINE, classify(2048 - 400, EMPTY, t));
}

static void test_classify_hysteresis() {
  // Between exit and enter: keep the previous decision.
  TEST_ASSERT_EQUAL_INT(EMPTY, classify(2048 + 200, EMPTY, TH));
  TEST_ASSERT_EQUAL_INT(MACHINE, classify(2048 + 200, MACHINE, TH));
  TEST_ASSERT_EQUAL_INT(HUMAN, classify(2048 - 200, HUMAN, TH));
  TEST_ASSERT_EQUAL_INT(MACHINE, classify(2048 + 150, MACHINE, TH));
  // Below exit: released.
  TEST_ASSERT_EQUAL_INT(EMPTY, classify(2048 + 149, MACHINE, TH));
  TEST_ASSERT_EQUAL_INT(EMPTY, classify(2048 - 149, HUMAN, TH));
  // Opposite pole beyond enter switches immediately.
  TEST_ASSERT_EQUAL_INT(HUMAN, classify(2048 - 300, MACHINE, TH));
  // Opposite pole between exit and enter does not keep the old piece.
  TEST_ASSERT_EQUAL_INT(EMPTY, classify(2048 - 200, MACHINE, TH));
}

static void test_led_index_serpentine() {
  TEST_ASSERT_EQUAL_INT(0, ledIndex(0, 0, 7));
  TEST_ASSERT_EQUAL_INT(6, ledIndex(0, 6, 7));
  TEST_ASSERT_EQUAL_INT(7, ledIndex(1, 6, 7));
  TEST_ASSERT_EQUAL_INT(13, ledIndex(1, 0, 7));
  TEST_ASSERT_EQUAL_INT(42, ledIndex(6, 0, 7));
  TEST_ASSERT_EQUAL_INT(48, ledIndex(6, 6, 7));
  // Every LED used exactly once.
  bool seen[49] = {};
  for (int r = 0; r < 7; r++)
    for (int c = 0; c < 7; c++) seen[ledIndex(r, c, 7)] = true;
  for (bool s : seen) TEST_ASSERT_TRUE(s);
}

static const char* START =
    "MMM\n"
    "...\n"
    "HHH";

static void test_watch_lift() {
  int8_t exp[MAX_N * MAX_N], sen[MAX_N * MAX_N];
  bool mc[MAX_N * MAX_N] = {};
  WatchEvent ev[MAX_WATCH_EVENTS];
  int n = parseBoard(START, exp);
  BoardWatcher w(n);
  parseBoard(START, sen);
  TEST_ASSERT_EQUAL_INT(0, w.update(sen, exp, mc, 0, ev));
  parseBoard("MMM\n...\nH.H", sen);
  int k = w.update(sen, exp, mc, 10, ev);
  TEST_ASSERT_EQUAL_INT(1, k);
  TEST_ASSERT_EQUAL_INT(WatchEvent::Lift, ev[0].kind);
  TEST_ASSERT_EQUAL_INT(2, ev[0].r);
  TEST_ASSERT_EQUAL_INT(1, ev[0].c);
  // Reported once even if Game has not updated expected yet.
  TEST_ASSERT_EQUAL_INT(0, w.update(sen, exp, mc, 20, ev));
  // Game removes the held piece from expected: still nothing.
  parseBoard("MMM\n...\nH.H", exp);
  TEST_ASSERT_EQUAL_INT(0, w.update(sen, exp, mc, 30, ev));
}

static void test_watch_place_after_200ms() {
  int8_t exp[MAX_N * MAX_N], sen[MAX_N * MAX_N];
  bool mc[MAX_N * MAX_N] = {};
  WatchEvent ev[MAX_WATCH_EVENTS];
  int n = parseBoard("MMM\n...\nH.H", exp);
  BoardWatcher w(n);
  parseBoard("MMM\n.H.\nH.H", sen);
  TEST_ASSERT_EQUAL_INT(0, w.update(sen, exp, mc, 1000, ev));
  TEST_ASSERT_EQUAL_INT(0, w.update(sen, exp, mc, 1100, ev));
  TEST_ASSERT_EQUAL_INT(0, w.update(sen, exp, mc, 1199, ev));
  int k = w.update(sen, exp, mc, 1200, ev);
  TEST_ASSERT_EQUAL_INT(1, k);
  TEST_ASSERT_EQUAL_INT(WatchEvent::Place, ev[0].kind);
  TEST_ASSERT_EQUAL_INT(1, ev[0].r);
  TEST_ASSERT_EQUAL_INT(1, ev[0].c);
  // Only once (e.g. Game rejected the place and expected stays empty).
  TEST_ASSERT_EQUAL_INT(0, w.update(sen, exp, mc, 1500, ev));
}

static void test_watch_place_199ms_not_confirmed() {
  int8_t exp[MAX_N * MAX_N], sen[MAX_N * MAX_N];
  bool mc[MAX_N * MAX_N] = {};
  WatchEvent ev[MAX_WATCH_EVENTS];
  int n = parseBoard("MMM\n...\nH.H", exp);
  BoardWatcher w(n);
  parseBoard("MMM\n.H.\nH.H", sen);
  TEST_ASSERT_EQUAL_INT(0, w.update(sen, exp, mc, 1000, ev));
  TEST_ASSERT_EQUAL_INT(0, w.update(sen, exp, mc, 1199, ev));
  // Gone before 200ms: nothing, and the timer restarts on reappearance.
  parseBoard("MMM\n...\nH.H", sen);
  TEST_ASSERT_EQUAL_INT(0, w.update(sen, exp, mc, 1210, ev));
  parseBoard("MMM\n.H.\nH.H", sen);
  TEST_ASSERT_EQUAL_INT(0, w.update(sen, exp, mc, 1300, ev));
  TEST_ASSERT_EQUAL_INT(0, w.update(sen, exp, mc, 1499, ev));
  TEST_ASSERT_EQUAL_INT(1, w.update(sen, exp, mc, 1500, ev));
}

static void test_watch_slide_only_last_cell() {
  int8_t exp[MAX_N * MAX_N], sen[MAX_N * MAX_N];
  bool mc[MAX_N * MAX_N] = {};
  WatchEvent ev[MAX_WATCH_EVENTS];
  // Human slides the lifted piece from (2,1) along row 1: (1,0) -> (1,1) -> (1,2).
  int n = parseBoard("MMM\n...\nH.H", exp);
  BoardWatcher w(n);
  int total = 0;
  WatchEvent last{};
  parseBoard("MMM\nH..\nH.H", sen);
  for (uint32_t t = 0; t < 100; t += 10) {
    int k = w.update(sen, exp, mc, t, ev);
    total += k;
  }
  parseBoard("MMM\n.H.\nH.H", sen);
  for (uint32_t t = 100; t < 250; t += 10) {
    int k = w.update(sen, exp, mc, t, ev);
    total += k;
  }
  parseBoard("MMM\n..H\nH.H", sen);
  for (uint32_t t = 250; t <= 600; t += 10) {
    int k = w.update(sen, exp, mc, t, ev);
    if (k > 0) last = ev[k - 1];
    total += k;
  }
  TEST_ASSERT_EQUAL_INT(1, total);
  TEST_ASSERT_EQUAL_INT(WatchEvent::Place, last.kind);
  TEST_ASSERT_EQUAL_INT(1, last.r);
  TEST_ASSERT_EQUAL_INT(2, last.c);
}

static void test_watch_machine_touch() {
  int8_t exp[MAX_N * MAX_N], sen[MAX_N * MAX_N];
  bool mc[MAX_N * MAX_N] = {};
  WatchEvent ev[MAX_WATCH_EVENTS];
  int n = parseBoard(START, exp);
  BoardWatcher w(n);
  // Human nudges the machine piece at (0,2) to (1,2).
  parseBoard("MM.\n..M\nHHH", sen);
  int k = w.update(sen, exp, mc, 0, ev);
  TEST_ASSERT_EQUAL_INT(2, k);
  TEST_ASSERT_EQUAL_INT(WatchEvent::MachineTouch, ev[0].kind);
  TEST_ASSERT_EQUAL_INT(0, ev[0].r);
  TEST_ASSERT_EQUAL_INT(2, ev[0].c);
  TEST_ASSERT_EQUAL_INT(WatchEvent::MachineTouch, ev[1].kind);
  TEST_ASSERT_EQUAL_INT(1, ev[1].r);
  TEST_ASSERT_EQUAL_INT(2, ev[1].c);
  // Once per cell while the mismatch lasts.
  TEST_ASSERT_EQUAL_INT(0, w.update(sen, exp, mc, 10, ev));
  // Put back, then touched again: reported again.
  parseBoard(START, sen);
  TEST_ASSERT_EQUAL_INT(0, w.update(sen, exp, mc, 20, ev));
  parseBoard("M.M\n...\nHHH", sen);
  k = w.update(sen, exp, mc, 30, ev);
  TEST_ASSERT_EQUAL_INT(1, k);
  TEST_ASSERT_EQUAL_INT(0, ev[0].r);
  TEST_ASSERT_EQUAL_INT(1, ev[0].c);
}

static void test_watch_machine_cells_ignored() {
  int8_t exp[MAX_N * MAX_N], sen[MAX_N * MAX_N];
  bool mc[MAX_N * MAX_N] = {};
  WatchEvent ev[MAX_WATCH_EVENTS];
  int n = parseBoard(START, exp);
  BoardWatcher w(n);
  // The machine itself moves (0,2) -> (1,2).
  mc[0 * n + 2] = true;
  mc[1 * n + 2] = true;
  parseBoard("MM.\n..M\nHHH", sen);
  TEST_ASSERT_EQUAL_INT(0, w.update(sen, exp, mc, 0, ev));
  TEST_ASSERT_EQUAL_INT(0, w.update(sen, exp, mc, 300, ev));
}

static void test_diff_initial() {
  int8_t exp[MAX_N * MAX_N], sen[MAX_N * MAX_N];
  int n = parseBoard(START, exp);
  BoardWatcher w(n);
  int out[MAX_N * MAX_N];
  parseBoard(START, sen);
  TEST_ASSERT_EQUAL_INT(0, w.diffInitial(sen, exp, out));
  parseBoard("M.M\nH..\nHH.", sen);
  int k = w.diffInitial(sen, exp, out);
  TEST_ASSERT_EQUAL_INT(3, k);
  TEST_ASSERT_EQUAL_INT(1, out[0]);
  TEST_ASSERT_EQUAL_INT(3, out[1]);
  TEST_ASSERT_EQUAL_INT(8, out[2]);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_classify_thresholds);
  RUN_TEST(test_classify_south_low);
  RUN_TEST(test_classify_hysteresis);
  RUN_TEST(test_led_index_serpentine);
  RUN_TEST(test_watch_lift);
  RUN_TEST(test_watch_place_after_200ms);
  RUN_TEST(test_watch_place_199ms_not_confirmed);
  RUN_TEST(test_watch_slide_only_last_cell);
  RUN_TEST(test_watch_machine_touch);
  RUN_TEST(test_watch_machine_cells_ignored);
  RUN_TEST(test_diff_initial);
  return UNITY_END();
}
