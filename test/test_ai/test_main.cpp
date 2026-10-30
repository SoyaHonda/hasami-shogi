#include <unity.h>
#include <cstring>
#include "Ai.h"
#include "Board.h"

// Build a snapshot from rows separated by '\n': '.' empty, 'H' human, 'M' machine,
// 'h' a held human piece (its origin square is empty on the board). Pieces get ids 1.. in
// row-major order. All cooldowns are over (clock 10000).
static Snapshot makeSnap(const char* s, float headR, float headC) {
  Snapshot snap{};
  int n = 0;
  while (s[n] && s[n] != '\n') n++;
  snap.board.n = n;
  std::memset(snap.board.cells, 0, sizeof(snap.board.cells));
  snap.count = 0;
  snap.clockMs = 10000;
  snap.headR = headR;
  snap.headC = headC;
  snap.heldIds[0] = snap.heldIds[1] = -1;
  snap.heldCount = 0;
  int r = 0, c = 0;
  for (const char* p = s; *p; ++p) {
    if (*p == '\n') {
      r++;
      c = 0;
      continue;
    }
    if (*p != '.') {
      PieceView& pv = snap.pieces[snap.count];
      pv.id = snap.count + 1;
      pv.side = (*p == 'M') ? MACHINE : HUMAN;
      pv.r = r;
      pv.c = c;
      pv.cooldownUntil = 0;
      pv.alive = true;
      pv.held = (*p == 'h');
      if (pv.held) {
        snap.heldIds[snap.heldCount++] = pv.id;
      } else {
        snap.board.set(r, c, pv.side);
      }
      snap.count++;
    }
    c++;
  }
  return snap;
}

static const PieceView& pieceById(const Snapshot& s, int id) { return s.pieces[id - 1]; }

// Board after applying a machine move (including its captures).
static Board applyMove(const Snapshot& s, const AiMove& m) {
  Board b = s.board;
  const PieceView& p = pieceById(s, m.pieceId);
  b.set(p.r, p.c, EMPTY);
  b.set(m.toR, m.toC, MACHINE);
  int caps[MAX_N * MAX_N];
  int k = capturesAt(b, MACHINE, m.toR, m.toC, caps);
  for (int i = 0; i < k; i++) b.cells[caps[i]] = EMPTY;
  return b;
}

// Most pieces a human piece starting at (r,c) could capture with one move on b.
static int bestHumanCapture(Board b, int r, int c) {
  int d[MAX_N * MAX_N];
  int k = dests(b, r, c, d);
  int best = 0;
  int caps[MAX_N * MAX_N];
  for (int i = 0; i < k; i++) {
    b.cells[d[i]] = HUMAN;
    int n = capturesAt(b, HUMAN, d[i] / b.n, d[i] % b.n, caps);
    b.cells[d[i]] = EMPTY;
    if (n > best) best = n;
  }
  return best;
}

void setUp() {}
void tearDown() {}

void test_picks_capture() {
  // H(3,3) is captured against M(3,2) when the M at (0,4) drops to (3,4).
  Snapshot s = makeSnap(
      "....M..\n"
      ".......\n"
      ".......\n"
      "..MH...\n"
      ".......\n"
      ".......\n"
      "H.....H",
      0.0f, 3.0f);
  Params p;
  AiMove m = think(s, p);
  TEST_ASSERT_FALSE(m.none);
  TEST_ASSERT_EQUAL_INT(1, m.pieceId);  // the M at (0,4)
  TEST_ASSERT_EQUAL_INT(3, m.toR);
  TEST_ASSERT_EQUAL_INT(4, m.toC);
  TEST_ASSERT_TRUE(m.score > 50.0f);
}

void test_easy_read_lift_blocks_or_flees() {
  // The held human piece at (5,3) threatens to go to (2,3) and capture M(1,3) against H(0,3).
  Snapshot s = makeSnap(
      "...H...\n"
      "...M...\n"
      ".......\n"
      ".......\n"
      ".......\n"
      "...h...\n"
      "M......",
      0.0f, 3.0f);
  const int heldId = s.heldIds[0];
  const PieceView& held = pieceById(s, heldId);
  TEST_ASSERT_EQUAL_INT(1, bestHumanCapture(s.board, held.r, held.c));
  Params p;
  p.level = Level::Easy;
  p.readLift = true;
  AiMove m = think(s, p);
  TEST_ASSERT_FALSE(m.none);
  Board after = applyMove(s, m);
  TEST_ASSERT_EQUAL_INT(0, bestHumanCapture(after, held.r, held.c));
}

void test_no_read_easy_passive_does_nothing() {
  Snapshot s = makeSnap(
      "MMMMMMM\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      "HHHHHHH",
      0.0f, 3.0f);
  Params p;
  p.level = Level::Easy;
  p.readLift = false;
  p.aggression = 0.0f;
  AiMove m = think(s, p);
  TEST_ASSERT_TRUE(m.none);

  // Even with a lifted piece threatening, readLift=false + Easy sees no danger.
  Snapshot s2 = makeSnap(
      "...H...\n"
      "...M...\n"
      ".......\n"
      ".......\n"
      ".......\n"
      "...h...\n"
      "M......",
      0.0f, 3.0f);
  AiMove m2 = think(s2, p);
  TEST_ASSERT_TRUE(m2.none);
}

void test_tie_prefers_piece_nearer_head() {
  // Mirror-image captures: M(0,1) -> (3,1) takes H(3,2) and M(0,5) -> (3,5) takes H(3,4), both
  // against M(3,3). They score the same apart from the travel time from the head.
  const char* layout =
      ".M...M.\n"
      ".......\n"
      ".......\n"
      "..HMH..\n"
      ".......\n"
      ".......\n"
      ".......";
  Params p;
  // Head near the right piece.
  Snapshot right = makeSnap(layout, 0.0f, 6.0f);
  AiMove mr = think(right, p);
  TEST_ASSERT_FALSE(mr.none);
  TEST_ASSERT_EQUAL_INT(2, mr.pieceId);  // M at (0,5)
  TEST_ASSERT_EQUAL_INT(3, mr.toR);
  TEST_ASSERT_EQUAL_INT(5, mr.toC);
  // Head near the left piece.
  Snapshot left = makeSnap(layout, 0.0f, 0.0f);
  AiMove ml = think(left, p);
  TEST_ASSERT_FALSE(ml.none);
  TEST_ASSERT_EQUAL_INT(1, ml.pieceId);  // M at (0,1)
  TEST_ASSERT_EQUAL_INT(3, ml.toR);
  TEST_ASSERT_EQUAL_INT(1, ml.toC);
}

void test_cooldown_piece_not_moved() {
  Snapshot s = makeSnap(
      "....M..\n"
      ".......\n"
      ".......\n"
      "..MH...\n"
      ".......\n"
      ".......\n"
      "H.....H",
      0.0f, 3.0f);
  s.pieces[0].cooldownUntil = s.clockMs + 1;  // the M at (0,4) is still cooling down
  Params p;
  AiMove m = think(s, p);
  TEST_ASSERT_TRUE(m.none || m.pieceId != 1);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_picks_capture);
  RUN_TEST(test_easy_read_lift_blocks_or_flees);
  RUN_TEST(test_no_read_easy_passive_does_nothing);
  RUN_TEST(test_tie_prefers_piece_nearer_head);
  RUN_TEST(test_cooldown_piece_not_moved);
  return UNITY_END();
}
