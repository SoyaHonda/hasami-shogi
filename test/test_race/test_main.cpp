#include <unity.h>
#include <cstring>
#include "Ai.h"
#include "Board.h"
#include "Params.h"
#include "RaceTime.h"

// RaceTime (plan 2026-10-31 3.3-3.8, 3.10). Times are ms from the snapshot. Default Params:
// humanMove 500, humanPlace 350, react 250, engage 200, freeSpeed 4, dragSpeed 1.5; default rule
// times: humanCooldown 3000, humanInterval 1000, maxHold 1000, machineCooldown 1000.

static const uint32_t CLOCK = 10000;

// Rows separated by '\n': '.' empty, 'H' human, 'M' machine, 'h' a held human piece (its origin
// is empty on the board). Ids 1.. in row-major order; every cooldown is over.
static Snapshot makeSnap(const char* s, float headR = 0.0f, float headC = 0.0f) {
  Snapshot snap{};
  int n = 0;
  while (s[n] && s[n] != '\n') n++;
  snap.board.n = n;
  std::memset(snap.board.cells, 0, sizeof(snap.board.cells));
  snap.clockMs = CLOCK;
  snap.headR = headR;
  snap.headC = headC;
  snap.heldIds[0] = snap.heldIds[1] = -1;
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
      pv.liftedAt = CLOCK;
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

static PieceView* pieceAt(Snapshot& s, int r, int c) {
  for (int i = 0; i < s.count; i++)
    if (s.pieces[i].r == r && s.pieces[i].c == c) return &s.pieces[i];
  return nullptr;
}

static void setCooldown(Snapshot& s, int r, int c, uint32_t ms) { pieceAt(s, r, c)->cooldownUntil = CLOCK + ms; }
// Placement interval left = gapMs.
static void setGap(Snapshot& s, uint32_t gapMs) {
  s.hasLastPlace = true;
  s.lastPlaceAt = CLOCK + gapMs - s.humanIntervalMs;
}

static float thAt(const Snapshot& s, int r, int c, int dr, int dc, Params p = Params()) {
  const RaceTimes t = raceTimes(s, p);
  RaceState st;
  raceInit(st, s, t);
  const RaceLine L = raceLineThrough(st.board, MACHINE, r, c, dr, dc);
  TEST_ASSERT_TRUE(L.len > 0);
  return raceHumanTime(st, t, L);
}

// Trap A: the machine came out to (5,3); humans at (6,2) and (6,4) close the row in turn.
static const char* TRAP_A =
    "MMM.MMM\n"
    ".......\n"
    ".......\n"
    ".......\n"
    ".......\n"
    "...M...\n"
    "HHHHHHH";

void setUp() {}
void tearDown() {}

void test_trap_a_h2() {
  Snapshot s = makeSnap(TRAP_A);
  // t1 = max(500, gap 0) = 500, t2 = max(500 + 1000, 500 + 500, 0 + 500) = 1500.
  TEST_ASSERT_EQUAL_FLOAT(1500.0f, thAt(s, 5, 3, 0, 1));
  // One move of either piece alone does not close it; e(h,s) itself is 500.
  const RaceTimes t = raceTimes(s, Params());
  RaceState st;
  raceInit(st, s, t);
  int hi = -1;
  for (int i = 0; i < st.hCount; i++)
    if (st.h[i].r == 6 && st.h[i].c == 2) hi = i;
  TEST_ASSERT_TRUE(hi >= 0);
  TEST_ASSERT_EQUAL_FLOAT(500.0f, raceHumanPlace(st, t, hi, 5 * 7 + 2));
  // Two moves (up file 2, then along row 4) to (4,3): 500 + 3000 + 500.
  TEST_ASSERT_EQUAL_FLOAT(4000.0f, raceHumanPlace(st, t, hi, 4 * 7 + 3));
  // Not at all onto a machine piece.
  TEST_ASSERT_TRUE(raceInf(raceHumanPlace(st, t, hi, 5 * 7 + 3)));
}

void test_gap_delays_the_first_placement() {
  Snapshot s = makeSnap(TRAP_A);
  setGap(s, 700);
  // t1 = max(500, 700) = 700, t2 = 1700.
  TEST_ASSERT_EQUAL_FLOAT(1700.0f, thAt(s, 5, 3, 0, 1));
  setGap(s, 0);
  TEST_ASSERT_EQUAL_FLOAT(1500.0f, thAt(s, 5, 3, 0, 1));
}

void test_cooldown_of_second_piece() {
  Snapshot s = makeSnap(TRAP_A);
  setCooldown(s, 6, 4, 2000);
  // (6,2) first at 500, (6,4) at max(1500, 1000, 2500) = 2500.
  TEST_ASSERT_EQUAL_FLOAT(2500.0f, thAt(s, 5, 3, 0, 1));
}

void test_trap_b_h4() {
  // The machine entered between two human pieces; nobody else reaches either end.
  Snapshot s = makeSnap(
      "MMM.MMM\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      "..HMH..\n"
      "HH.H.HH");
  // H4: one side steps away and comes back after its cooldown: 500 + 3000 + 500. Lifting and
  // putting it straight back (a cancel) does not count.
  TEST_ASSERT_EQUAL_FLOAT(4000.0f, thAt(s, 5, 3, 0, 1));
  setCooldown(s, 5, 2, 1000);
  setCooldown(s, 5, 4, 1200);
  TEST_ASSERT_EQUAL_FLOAT(5000.0f, thAt(s, 5, 3, 0, 1));  // (5,2): 1000 + 500 + 3000 + 500
}

void test_trap_c_h3() {
  // (3,2) steps away and (6,2) comes up into its square.
  Snapshot s = makeSnap(
      "MMM.MMM\n"
      ".......\n"
      ".......\n"
      "..HMH..\n"
      ".......\n"
      ".......\n"
      "HHH..HH");
  TEST_ASSERT_EQUAL_FLOAT(1500.0f, thAt(s, 3, 3, 0, 1));  // max(500 + 1000, 0 + 500)
  setCooldown(s, 6, 2, 2000);
  TEST_ASSERT_EQUAL_FLOAT(2500.0f, thAt(s, 3, 3, 0, 1));  // max(500 + 1000, 2000 + 500)
}

void test_pending_target_closes_at_gap() {
  // The held piece from (6,4) is pending on (5,4): the row closes when it lands.
  Snapshot s = makeSnap(
      "MMM.MMM\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      "..HM...\n"
      "HH.Hh.H");
  setGap(s, 600);
  s.pending = true;
  s.pendingR = 5;
  s.pendingC = 4;
  s.pendingId = pieceAt(s, 6, 4)->id;
  TEST_ASSERT_EQUAL_FLOAT(600.0f, thAt(s, 5, 3, 0, 1));
}

void test_pending_delays_other_placements() {
  // Trap A, but (6,6) is pending on (4,6): other pieces lift from gap and place from gap + 1000.
  Snapshot s = makeSnap(
      "MMM.MMM\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      "...M...\n"
      "HHHHHHh");
  setGap(s, 600);
  s.pending = true;
  s.pendingR = 4;
  s.pendingC = 6;
  s.pendingId = pieceAt(s, 6, 6)->id;
  // t1 = max(600 + 500, 1600) = 1600, t2 = max(2600, 2100, 1100) = 2600.
  TEST_ASSERT_EQUAL_FLOAT(2600.0f, thAt(s, 5, 3, 0, 1));
}

void test_held_piece_and_hold_limit() {
  Snapshot s = makeSnap(
      "MMM.MMM\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      "...M...\n"
      "HHhHHHH");
  // The held piece goes to (5,2) in humanPlace 350, then (6,4): max(1350, 850, 500).
  TEST_ASSERT_EQUAL_FLOAT(1350.0f, thAt(s, 5, 3, 0, 1));
  // Lifted 700 ms ago: 300 ms left < 350, so its moves do not count. Best left: (6,1) two moves
  // to (5,2) at 4000, then (6,4) at 5000.
  pieceAt(s, 6, 2)->liftedAt = CLOCK - 700;
  TEST_ASSERT_EQUAL_FLOAT(5000.0f, thAt(s, 5, 3, 0, 1));
  // No hold limit: counts again.
  s.maxHoldMs = 0;
  TEST_ASSERT_EQUAL_FLOAT(1350.0f, thAt(s, 5, 3, 0, 1));
}

void test_reserved_square_unreachable() {
  Snapshot s = makeSnap(TRAP_A);
  s.reservedR = 5;
  s.reservedC = 2;
  TEST_ASSERT_TRUE(raceInf(thAt(s, 5, 3, 0, 1)));
}

void test_easy_reads_only_held_h1() {
  Params easy;
  easy.level = Level::Easy;
  Snapshot a = makeSnap(TRAP_A);
  TEST_ASSERT_TRUE(raceInf(thAt(a, 5, 3, 0, 1, easy)));  // H2
  Snapshot b = makeSnap(
      "MMM.MMM\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      "..HMH..\n"
      "HH.H.HH");
  TEST_ASSERT_TRUE(raceInf(thAt(b, 5, 3, 0, 1, easy)));  // H3 / H4
  // H1 by the held piece is read: (6,4) held, goes up to (5,4).
  Snapshot c = makeSnap(
      "MMM.MMM\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      "..HM...\n"
      "HH.Hh.H");
  TEST_ASSERT_EQUAL_FLOAT(350.0f, thAt(c, 5, 3, 0, 1, easy));
  // ... but not by a piece on the board.
  Snapshot d = makeSnap(
      "MMM.MMM\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      "..HM...\n"
      "HH.HH.H");
  TEST_ASSERT_TRUE(raceInf(thAt(d, 5, 3, 0, 1, easy)));
  TEST_ASSERT_EQUAL_FLOAT(500.0f, thAt(d, 5, 3, 0, 1));  // H1 at Normal
}

void test_edge_line_is_inf_and_has_no_risk() {
  Snapshot s = makeSnap(TRAP_A);
  TEST_ASSERT_TRUE(raceInf(thAt(s, 0, 0, 0, 1)));  // row 0 line touches the edge
  TEST_ASSERT_TRUE(raceInf(thAt(s, 0, 0, 1, 0)));
  const RaceTimes t = raceTimes(s, Params());
  RaceState st;
  raceInit(st, s, t);
  RaceLineEval ev[RACE_MAX_LINES];
  const int k = raceAssess(st, t, ev);
  int edge = 0;
  for (int i = 0; i < k; i++) {
    if (ev[i].line.r == 0) {
      edge++;
      TEST_ASSERT_TRUE(raceInf(ev[i].th));
      TEST_ASSERT_FALSE(ev[i].danger);
      TEST_ASSERT_EQUAL_FLOAT(0.0f, ev[i].risk);
    }
  }
  TEST_ASSERT_TRUE(edge > 0);
}

void test_moving_home_row_piece_opens_file() {
  // (0,2) slides to (0,5): (6,2) can now run up file 2 to (0,2), an end of what is left of the
  // row line ((0,1) alone), and (6,0) reaches (0,0): H2 = 1500. The reach of (6,2) must be
  // rebuilt (on the old board (0,2) was occupied).
  Snapshot s = makeSnap(
      ".MM....\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      "H.H....");
  const RaceTimes t = raceTimes(s, Params());
  RaceState st;
  raceInit(st, s, t);
  RaceState stale = st;
  stale.board.set(0, 2, EMPTY);
  stale.board.set(0, 5, MACHINE);
  const RaceLine L = raceLineThrough(stale.board, MACHINE, 0, 1, 0, 1);
  TEST_ASSERT_EQUAL_INT(1, L.len);
  TEST_ASSERT_TRUE(raceInf(raceHumanTime(stale, t, L)) || raceHumanTime(stale, t, L) > 1500.0f);
  raceMoveMachine(st, t, 0, 2, 0, 5, 1000.0f);
  TEST_ASSERT_EQUAL_FLOAT(1500.0f, raceHumanTime(st, t, L));
  float thDest, thRest;
  RaceState st0;
  raceInit(st0, s, t);
  raceDestTimes(st0, t, 0, 2, 0, 5, &thDest, &thRest);
  TEST_ASSERT_EQUAL_FLOAT(1500.0f, thRest);
}

void test_capture_opens_paths() {
  // M(0,4) drops to (3,4) and captures H(3,3); H(6,3) can then run up file 3 to (2,3) and close
  // M(2,2) against H(2,1). Its reach must be rebuilt though it is not in the mover's rows or files.
  Snapshot s = makeSnap(
      "....M..\n"
      ".......\n"
      ".HM....\n"
      "..MH...\n"
      ".......\n"
      ".......\n"
      "...H...");
  const RaceTimes t = raceTimes(s, Params());
  RaceState st;
  raceInit(st, s, t);
  TEST_ASSERT_EQUAL_INT(1, raceMoveMachine(st, t, 0, 4, 3, 4, 2000.0f));
  TEST_ASSERT_EQUAL_INT(EMPTY, st.board.at(3, 3));
  TEST_ASSERT_EQUAL_FLOAT(500.0f, raceHumanTime(st, t, raceLineThrough(st.board, MACHINE, 2, 2, 0, 1)));
}

void test_pending_origin_closed_to_machine() {
  // The pending piece's origin (6,4) stays closed to the machine, as is its target (5,4).
  Snapshot s = makeSnap(
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      "..H....\n"
      "M...h..");
  setGap(s, 600);
  s.pending = true;
  s.pendingR = 5;
  s.pendingC = 4;
  s.pendingId = pieceAt(s, 6, 4)->id;
  const RaceTimes t = raceTimes(s, Params());
  RaceState st;
  raceInit(st, s, t);
  TEST_ASSERT_TRUE(st.blocked[6 * 7 + 4]);
  TEST_ASSERT_TRUE(st.blocked[5 * 7 + 4]);
  TEST_ASSERT_EQUAL_INT(EMPTY, st.board.at(6, 4));
  TEST_ASSERT_EQUAL_INT(HUMAN, st.board.at(5, 4));
}

void test_move_that_exposes_rest_is_not_safe() {
  // M(3,2) M(3,3) between H(3,1) and H(3,4): whichever piece leaves, the neighbor on that side
  // steps into the vacated square and closes the rest at 500.
  Snapshot s = makeSnap(
      ".......\n"
      ".......\n"
      ".......\n"
      ".HMMH..\n"
      ".......\n"
      ".......\n"
      "......H",
      3.0f, 2.0f);
  const RaceTimes t = raceTimes(s, Params());
  RaceState st;
  raceInit(st, s, t);
  const float S = raceStart(st, t, 3, 2, 0.0f, 3.0f, 2.0f);
  TEST_ASSERT_EQUAL_FLOAT(450.0f, S);  // react 250 + 0 travel + engage 200
  float thDest, thRest;
  raceDestTimes(st, t, 3, 2, 1, 2, &thDest, &thRest);
  TEST_ASSERT_EQUAL_FLOAT(500.0f, thRest);
  TEST_ASSERT_FALSE(raceSafeDest(st, t, 3, 2, 1, 2, S));
  // Without H(3,4) the rest is safe: nobody reaches (3,4).
  Snapshot s2 = makeSnap(
      ".......\n"
      ".......\n"
      ".......\n"
      ".HMM...\n"
      ".......\n"
      ".......\n"
      "......H",
      3.0f, 2.0f);
  RaceState st2;
  raceInit(st2, s2, t);
  TEST_ASSERT_TRUE(raceSafeDest(st2, t, 3, 2, 1, 2, S));
}

void test_guard_prefers_more_pieces() {
  // Two lines closing at 1500: L1 = M(6,1) (1 piece), L2 = M(6,4) M(6,5) (2 pieces). The head at
  // (6,3) can guard only one in time; L1 sorts first, but guarding L2 saves more pieces.
  Snapshot s = makeSnap(
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      "H.HH..H\n"
      ".M..MM.",
      6.0f, 3.0f);
  const RaceTimes t = raceTimes(s, Params());
  RaceState st;
  raceInit(st, s, t);
  RaceLineEval ev[RACE_MAX_LINES];
  const int k = raceAssess(st, t, ev);
  const RaceLineEval* l1 = nullptr;
  const RaceLineEval* l2 = nullptr;
  for (int i = 0; i < k; i++) {
    if (ev[i].line.dr == 0 && ev[i].line.r == 6 && ev[i].line.c == 1) l1 = &ev[i];
    if (ev[i].line.dr == 0 && ev[i].line.r == 6 && ev[i].line.c == 4) l2 = &ev[i];
  }
  TEST_ASSERT_NOT_NULL(l1);
  TEST_ASSERT_NOT_NULL(l2);
  TEST_ASSERT_EQUAL_FLOAT(1500.0f, l1->th);
  TEST_ASSERT_EQUAL_FLOAT(1500.0f, l2->th);
  TEST_ASSERT_TRUE(l1->danger && l2->danger);
  TEST_ASSERT_TRUE(l2->guarded);
  TEST_ASSERT_FALSE(l1->guarded);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, l2->risk);
  TEST_ASSERT_TRUE(l1->risk > 0.0f);
  float riskAt[MAX_N * MAX_N];
  racePieceRisk(st.board, ev, k, riskAt);
  TEST_ASSERT_EQUAL_FLOAT(l1->risk, riskAt[6 * 7 + 1]);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, riskAt[6 * 7 + 4]);
  // Alone, L1 can be guarded.
  Snapshot one = makeSnap(
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      "H.H....\n"
      ".M.....",
      6.0f, 3.0f);
  RaceState st1;
  raceInit(st1, one, t);
  const int k1 = raceAssess(st1, t, ev);
  bool guarded = false;
  for (int i = 0; i < k1; i++)
    if (ev[i].line.dr == 0 && ev[i].line.r == 6) guarded = ev[i].guarded;
  TEST_ASSERT_TRUE(guarded);
}

void test_risk_formula() {
  // Trap A with the head far away and the machine still cooling down: no safe start in time.
  Snapshot s = makeSnap(TRAP_A, 0.0f, 0.0f);
  setCooldown(s, 5, 3, 5000);
  const RaceTimes t = raceTimes(s, Params());
  RaceState st;
  raceInit(st, s, t);
  RaceLineEval ev[RACE_MAX_LINES];
  const int k = raceAssess(st, t, ev);
  const RaceLineEval* row = nullptr;
  for (int i = 0; i < k; i++)
    if (ev[i].line.r == 5 && ev[i].line.dr == 0) row = &ev[i];
  TEST_ASSERT_NOT_NULL(row);
  TEST_ASSERT_FALSE(row->guarded);
  TEST_ASSERT_FALSE(raceInf(row->save));
  // risk = clamp(0.5 + (save + margin - th) / 2000, 0, 1)
  float expect = 0.5f + (row->save + t.margin - row->th) / 2000.0f;
  if (expect > 1.0f) expect = 1.0f;
  TEST_ASSERT_EQUAL_FLOAT(expect, row->risk);
  TEST_ASSERT_TRUE(row->risk > 0.0f);
}

void test_machine_time_and_gain() {
  // M(3,2) H(3,3); M(0,4) drops to (3,4): head at (0,4): 0 + engage 200 + 3 / 1.5 s = 2200.
  Snapshot s = makeSnap(
      "....M..\n"
      ".......\n"
      ".......\n"
      "..MH...\n"
      ".......\n"
      ".......\n"
      ".......",
      0.0f, 4.0f);
  const RaceTimes t = raceTimes(s, Params());
  RaceState st;
  raceInit(st, s, t);
  const RaceLine L = raceLineThrough(st.board, HUMAN, 3, 3, 0, 1);
  TEST_ASSERT_EQUAL_INT(1, L.len);
  TEST_ASSERT_EQUAL_FLOAT(2200.0f, raceMachineTime(st, t, L));
  TEST_ASSERT_EQUAL_FLOAT(450.0f, raceEscape(st, t, L));  // 0 + react 300 + lift 150
  TEST_ASSERT_EQUAL_FLOAT(0.0f, raceGain(st, t, L));
  setCooldown(s, 3, 3, 2500);
  raceInit(st, s, t);
  TEST_ASSERT_EQUAL_FLOAT(2950.0f, raceEscape(st, t, L));
  TEST_ASSERT_EQUAL_FLOAT(0.875f, raceGain(st, t, L));  // 0.5 + (2950 - 2200) / 2000
  // A human line touching the edge cannot be closed.
  Snapshot e = makeSnap(
      "....M..\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      "..MH...",
      0.0f, 4.0f);
  raceInit(st, e, t);
  TEST_ASSERT_TRUE(raceInf(raceMachineTime(st, t, raceLineThrough(st.board, HUMAN, 6, 3, 1, 0))));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, raceGain(st, t, raceLineThrough(st.board, HUMAN, 6, 3, 1, 0)));
  // Its row can: (0,4) drops to (6,4).
  TEST_ASSERT_FALSE(raceInf(raceMachineTime(st, t, raceLineThrough(st.board, HUMAN, 6, 3, 0, 1))));
}

void test_lines() {
  Snapshot s = makeSnap(
      "MM.\n"
      "..M\n"
      "..M");
  RaceLine out[RACE_MAX_LINES];
  const int k = raceLines(s.board, MACHINE, out);
  // Rows: (0,0)-(0,1), (1,2), (2,2); columns: (0,0), (0,1), (1,2)-(2,2).
  TEST_ASSERT_EQUAL_INT(6, k);
  TEST_ASSERT_EQUAL_INT(2, out[0].len);
  TEST_ASSERT_EQUAL_INT(0, out[0].dr);
  TEST_ASSERT_EQUAL_INT(1, out[5].dr);
  TEST_ASSERT_EQUAL_INT(2, out[5].len);
  TEST_ASSERT_EQUAL_INT(1, out[5].r);
  TEST_ASSERT_EQUAL_INT(2, out[5].c);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_trap_a_h2);
  RUN_TEST(test_gap_delays_the_first_placement);
  RUN_TEST(test_cooldown_of_second_piece);
  RUN_TEST(test_trap_b_h4);
  RUN_TEST(test_trap_c_h3);
  RUN_TEST(test_pending_target_closes_at_gap);
  RUN_TEST(test_pending_delays_other_placements);
  RUN_TEST(test_held_piece_and_hold_limit);
  RUN_TEST(test_reserved_square_unreachable);
  RUN_TEST(test_easy_reads_only_held_h1);
  RUN_TEST(test_edge_line_is_inf_and_has_no_risk);
  RUN_TEST(test_moving_home_row_piece_opens_file);
  RUN_TEST(test_capture_opens_paths);
  RUN_TEST(test_pending_origin_closed_to_machine);
  RUN_TEST(test_move_that_exposes_rest_is_not_safe);
  RUN_TEST(test_guard_prefers_more_pieces);
  RUN_TEST(test_risk_formula);
  RUN_TEST(test_machine_time_and_gain);
  RUN_TEST(test_lines);
  return UNITY_END();
}
