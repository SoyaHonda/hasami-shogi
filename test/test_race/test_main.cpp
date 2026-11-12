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
  // Lifted 700 ms ago: 300 ms left < 350, so its moves do not count, and nothing else is lifted
  // before the hold limit sends it back at 300 (one piece in hand). Best left: (6,1) two moves
  // to (5,2) at 300 + 500 + 3000 + 500 = 4300, then (6,4) at 5300.
  pieceAt(s, 6, 2)->liftedAt = CLOCK - 700;
  TEST_ASSERT_EQUAL_FLOAT(5300.0f, thAt(s, 5, 3, 0, 1));
  // No hold limit: counts again.
  s.maxHoldMs = 0;
  TEST_ASSERT_EQUAL_FLOAT(1350.0f, thAt(s, 5, 3, 0, 1));
}

void test_held_origin_needs_placement_and_interval() {
  // Trap C with v held from (3,2): putting it back fills (3,2) again, so another piece lands
  // there only after v is placed elsewhere (humanPlace 350) and the interval: 1350, not the 850
  // of (6,2) lifted once v is down.
  Snapshot s = makeSnap(
      ".......\n"
      ".......\n"
      ".......\n"
      "..hMH..\n"
      ".......\n"
      ".......\n"
      "..H....");
  TEST_ASSERT_EQUAL_FLOAT(1350.0f, thAt(s, 3, 3, 0, 1));
}

void test_human_reacts_to_machine_move() {
  // (3,3) -> (5,3) between (6,2) and (6,4)'s reach. At once: (6,2) -> (5,2) at 500, (6,4) ->
  // (5,4) at 1500. Reacting at 1000: 1500 and 2500. With (5,2) planned (the human is heading
  // there anyway): 500, then max(1500, 1000 + 500) = 1500.
  Snapshot s = makeSnap(
      ".......\n"
      ".......\n"
      ".......\n"
      "...M...\n"
      ".......\n"
      ".......\n"
      "..H.H..");
  const RaceTimes t = raceTimes(s, Params());
  RaceState st;
  raceInit(st, s, t);
  float thDest, thRest;
  raceDestTimes(st, t, 3, 3, 5, 3, &thDest, &thRest);
  TEST_ASSERT_EQUAL_FLOAT(1500.0f, thDest);
  raceDestTimes(st, t, 3, 3, 5, 3, &thDest, &thRest, 1000.0f);
  TEST_ASSERT_EQUAL_FLOAT(2500.0f, thDest);
  RaceMask planned;
  planned.clear();
  planned.set(5 * 7 + 2);
  raceDestTimes(st, t, 3, 3, 5, 3, &thDest, &thRest, 1000.0f, &planned);
  TEST_ASSERT_EQUAL_FLOAT(1500.0f, thDest);
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
  // (6,2) (the same travel to both along the grid lines) can guard only one in time; L1 sorts first, but guarding L2 saves more pieces.
  Snapshot s = makeSnap(
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      "H.HH..H\n"
      ".M..MM.",
      6.0f, 2.0f);
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
      6.0f, 2.0f);
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

// ---- RE (plan 2026-11-06 3.1-3.2): the time the human needs to enclose an edge group ----

static float groupTime(const Snapshot& s, int r, int c, Params p = Params()) {
  const RaceTimes t = raceTimes(s, p);
  RaceState st;
  raceInit(st, s, t);
  const RaceLine G = raceGroupThrough(st.board, t, MACHINE, r, c);
  if (G.len == 0) return RACE_INF;
  TEST_ASSERT_TRUE(raceIsGroup(G));
  return raceHumanTime(st, t, G);
}

void test_re_group_needs_every_liberty_filled() {
  // The corner piece (0,0) has two liberties, (0,1) and (1,0): (0,3) and (4,0) fill them in
  // turn, as H2 (500, then the interval: 1500).
  Snapshot s = makeSnap(
      "M..H...\n"
      ".......\n"
      ".......\n"
      ".......\n"
      "H......\n"
      ".......\n"
      ".......");
  TEST_ASSERT_TRUE(raceInf(groupTime(s, 0, 0)));  // R0: no edge capture
  TEST_ASSERT_TRUE(raceInf(thAt(s, 0, 0, 0, 1)));
  s.edgeCapture = true;
  TEST_ASSERT_EQUAL_FLOAT(1500.0f, groupTime(s, 0, 0));
  // The custodian lines through it still reach the edge.
  TEST_ASSERT_TRUE(raceInf(thAt(s, 0, 0, 0, 1)));
  TEST_ASSERT_TRUE(raceInf(thAt(s, 0, 0, 1, 0)));
  // A piece away from the edge is no group of RE.
  Snapshot mid = makeSnap(
      ".......\n"
      ".......\n"
      "...M...\n"
      ".......\n"
      ".......\n"
      ".......\n"
      "HHH....");
  mid.edgeCapture = true;
  TEST_ASSERT_TRUE(raceInf(groupTime(mid, 2, 3)));
}

void test_re_group_one_liberty_is_h1_and_enclosers_stay() {
  // (0,1) already encloses the corner and must stay; (4,0) fills (1,0): 500.
  Snapshot s = makeSnap(
      "MH.....\n"
      ".......\n"
      ".......\n"
      ".......\n"
      "H......\n"
      ".......\n"
      ".......");
  s.edgeCapture = true;
  TEST_ASSERT_EQUAL_FLOAT(500.0f, groupTime(s, 0, 0));
  // Easy reads only the held piece's single placement.
  Params easy;
  easy.level = Level::Easy;
  TEST_ASSERT_TRUE(raceInf(groupTime(s, 0, 0, easy)));
  // Without (4,0) only the encloser could fill it, which opens its own square: never.
  Snapshot alone = makeSnap(
      "MH.....\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......");
  alone.edgeCapture = true;
  TEST_ASSERT_TRUE(raceInf(groupTime(alone, 0, 0)));
}

void test_re_enclosed_group_falls_to_step_away_and_back() {
  // Already enclosed (no liberty): (0,1) steps away and (0,4) slides into its square (H3).
  Snapshot s = makeSnap(
      "MH..H..\n"
      "H......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......");
  s.edgeCapture = true;
  TEST_ASSERT_EQUAL_FLOAT(1500.0f, groupTime(s, 0, 0));
}

void test_re_group_risk_reaches_its_pieces() {
  // The corner group closes at 500, before any guard can start (450 + margin 200): its risk
  // goes to the piece.
  Snapshot s = makeSnap(
      "MH.....\n"
      ".......\n"
      ".......\n"
      ".......\n"
      "H......\n"
      ".......\n"
      ".......");
  for (int edge = 0; edge < 2; edge++) {
    s.edgeCapture = edge == 1;
    const RaceTimes t = raceTimes(s, Params());
    RaceState st;
    raceInit(st, s, t);
    RaceLineEval ev[RACE_MAX_LINES];
    const int k = raceAssess(st, t, ev);
    int groups = 0;
    for (int i = 0; i < k; i++) {
      if (!raceIsGroup(ev[i].line)) continue;
      groups++;
      TEST_ASSERT_EQUAL_FLOAT(500.0f, ev[i].th);
      TEST_ASSERT_TRUE(ev[i].danger);
      TEST_ASSERT_FALSE(ev[i].guarded);
      TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.575f, ev[i].risk);
    }
    TEST_ASSERT_EQUAL_INT(edge, groups);
    float risk[MAX_N * MAX_N];
    racePieceRisk(st.board, ev, k, risk);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, edge ? 0.575f : 0.0f, risk[0]);
  }
}

void test_re_corner_is_no_safe_destination() {
  // (2,0) -> (0,0): a corner next to H(0,1), whose last liberty (1,0) H(1,4) fills at once.
  // Safe under R0 (the corner cannot be taken), not under RE.
  Snapshot s = makeSnap(
      ".H.....\n"
      "....H..\n"
      "M......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......");
  for (int edge = 0; edge < 2; edge++) {
    s.edgeCapture = edge == 1;
    const RaceTimes t = raceTimes(s, Params());
    RaceState st;
    raceInit(st, s, t);
    TEST_ASSERT_EQUAL_INT(edge == 0, (int)raceSafeDest(st, t, 2, 0, 0, 0, 500.0f));
  }
}

// ---- RU (plan 2026-11-06 3.1): the time the human needs to take an untouched machine piece ----
// The read of think() sets RaceTimes::untouched; raceTimes() leaves it off (the human bots, the
// activity measure and the search exploiter read as before). These tests use untouchedMs = 5000
// so that the movedAt values fit the clock of makeSnap.

static const uint32_t RU_MS = 5000;

static RaceTimes ruTimes(const Snapshot& s, Params p = Params()) {
  RaceTimes t = raceTimes(s, p);
  t.untouched = true;
  return t;
}

// Every piece moved at the snapshot (untouched `ms` later), RU on.
static void ruFresh(Snapshot& s, uint32_t ms = RU_MS) {
  s.untouchedMs = ms;
  for (int i = 0; i < s.count; i++) s.pieces[i].movedAt = CLOCK;
}

// The piece on (r,c) goes untouched inMs after the snapshot (0: it is untouched already; inMs
// must be at least untouchedMs - CLOCK).
static void staleIn(Snapshot& s, int r, int c, uint32_t inMs) {
  pieceAt(s, r, c)->movedAt = CLOCK + inMs - s.untouchedMs;
}

static float ruTime(const Snapshot& s, int r, int c, const RaceTimes& t) {
  RaceState st;
  raceInit(st, s, t);
  return raceUntouchedTime(st, t, r, c);
}

static const RaceLineEval* ruUnit(const RaceLineEval ev[], int k, int r, int c) {
  for (int i = 0; i < k; i++)
    if (raceIsUntouched(ev[i].line) && ev[i].line.r == r && ev[i].line.c == c) return &ev[i];
  return nullptr;
}

void test_ru_threat_waits_for_untouched_and_a_neighbour() {
  // M(3,3) goes untouched in 2000; H(6,4) reaches its neighbour (3,4) in one move (500): the
  // human waits and places there at 2000.
  Snapshot s = makeSnap(
      ".......\n"
      ".......\n"
      ".......\n"
      "...M...\n"
      ".......\n"
      ".......\n"
      "....H..");
  ruFresh(s);
  staleIn(s, 3, 3, 2000);
  TEST_ASSERT_EQUAL_FLOAT(2000.0f, ruTime(s, 3, 3, ruTimes(s)));
  // Untouched already: the placement itself (500), not before the placement interval.
  staleIn(s, 3, 3, 0);
  TEST_ASSERT_EQUAL_FLOAT(500.0f, ruTime(s, 3, 3, ruTimes(s)));
  setGap(s, 700);
  TEST_ASSERT_EQUAL_FLOAT(700.0f, ruTime(s, 3, 3, ruTimes(s)));
  setGap(s, 0);
  // Two moves to (2,3) or (4,3) would be 4000; one is enough. A cooldown delays it.
  setCooldown(s, 6, 4, 1000);
  TEST_ASSERT_EQUAL_FLOAT(1500.0f, ruTime(s, 3, 3, ruTimes(s)));
  setCooldown(s, 6, 4, 0);
  // Easy reads only a held piece's placement; the read off, RU off or the piece far from going
  // untouched: never.
  Params easy;
  easy.level = Level::Easy;
  TEST_ASSERT_TRUE(raceInf(ruTime(s, 3, 3, ruTimes(s, easy))));
  TEST_ASSERT_TRUE(raceInf(ruTime(s, 3, 3, raceTimes(s, Params()))));
  Snapshot off = s;
  off.untouchedMs = 0;
  TEST_ASSERT_TRUE(raceInf(ruTime(off, 3, 3, ruTimes(off))));
  // A human piece is not a machine piece's threat.
  TEST_ASSERT_TRUE(raceInf(ruTime(s, 6, 4, ruTimes(s))));
}

void test_ru_held_piece_and_pending_target() {
  // A held piece whose origin (6,4) is below: put down on (3,4) at humanPlace (350); easy reads it.
  Snapshot s = makeSnap(
      ".......\n"
      ".......\n"
      ".......\n"
      "...M...\n"
      ".......\n"
      ".......\n"
      "....h..");
  ruFresh(s);
  staleIn(s, 3, 3, 0);
  Params easy;
  easy.level = Level::Easy;
  TEST_ASSERT_EQUAL_FLOAT(350.0f, ruTime(s, 3, 3, ruTimes(s)));
  TEST_ASSERT_EQUAL_FLOAT(350.0f, ruTime(s, 3, 3, ruTimes(s, easy)));
  // A pending placement next to it lands at gap: a capture when the piece is untouched by then.
  Snapshot p = makeSnap(
      ".......\n"
      ".......\n"
      ".......\n"
      "...M...\n"
      ".......\n"
      ".......\n"
      "h......");
  ruFresh(p);
  setGap(p, 600);
  p.pending = true;
  p.pendingR = 3;
  p.pendingC = 4;
  p.pendingId = pieceAt(p, 6, 0)->id;
  staleIn(p, 3, 3, 400);
  TEST_ASSERT_EQUAL_FLOAT(600.0f, ruTime(p, 3, 3, ruTimes(p)));
  // Landing before the piece goes untouched captures nothing; then only two moves of the landed
  // piece (via (2,4) to (2,3)) reach a neighbour: its cooldown gap + 3000, then
  // max(3600 + 500, first placement 1600) + 3000 + 500 = 7600.
  staleIn(p, 3, 3, 800);
  TEST_ASSERT_EQUAL_FLOAT(7600.0f, ruTime(p, 3, 3, ruTimes(p)));
}

void test_ru_enclosed_piece_falls_to_step_away_and_back() {
  // M(0,0): its neighbours are H(0,1) and M(1,0). H(0,1) steps away (500) and H(0,4) slides into
  // its square: max(500 + 1000, 500 + 500, 0 + 500) = 1500 (H3); H(0,1) coming back is 4000 (H4).
  Snapshot s = makeSnap(
      "MH..H..\n"
      "M......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......");
  ruFresh(s);
  staleIn(s, 0, 0, 0);
  TEST_ASSERT_EQUAL_FLOAT(1500.0f, ruTime(s, 0, 0, ruTimes(s)));
  staleIn(s, 0, 0, 2500);
  TEST_ASSERT_EQUAL_FLOAT(2500.0f, ruTime(s, 0, 0, ruTimes(s)));
  // M(1,0) is still fresh: untouched in RU_MS, its neighbour (1,1) one move away for H(0,1).
  TEST_ASSERT_EQUAL_FLOAT(static_cast<float>(RU_MS), ruTime(s, 1, 0, ruTimes(s)));
}

void test_ru_moving_resets_the_timer_and_dragged_piece_is_safe() {
  Snapshot s = makeSnap(
      ".......\n"
      ".......\n"
      ".......\n"
      "...M...\n"
      ".......\n"
      ".......\n"
      "....H..");
  ruFresh(s);
  staleIn(s, 3, 3, 0);
  const RaceTimes t = ruTimes(s);
  RaceState st;
  raceInit(st, s, t);
  TEST_ASSERT_EQUAL_FLOAT(500.0f, raceUntouchedTime(st, t, 3, 3));
  // Moved to (2,3), arriving at 1000: untouched again RU_MS after the arrival.
  raceMoveMachine(st, t, 3, 3, 2, 3, 1000.0f);
  TEST_ASSERT_EQUAL_FLOAT(1000.0f + RU_MS, raceUntouchedTime(st, t, 2, 3));
  TEST_ASSERT_TRUE(raceInf(raceUntouchedTime(st, t, 3, 3)));
  // The piece being dragged (it holds the reservation) is off the board for the human.
  s.reservedR = 2;
  s.reservedC = 3;
  s.reservedId = pieceAt(s, 3, 3)->id;
  TEST_ASSERT_TRUE(raceInf(ruTime(s, 3, 3, t)));
}

void test_ru_unit_is_guarded_with_a_refresh_move() {
  // The RU unit of M(3,3) (untouched in 2000, threatened at 2000) is guarded by moving the piece:
  // S = react 250 + engage 200 = 450 with the head on it.
  Snapshot s = makeSnap(
      ".......\n"
      ".......\n"
      ".......\n"
      "...M...\n"
      ".......\n"
      ".......\n"
      "....H..",
      3.0f, 3.0f);
  ruFresh(s);
  staleIn(s, 3, 3, 2000);
  RaceLineEval ev[RACE_MAX_LINES];
  {
    const RaceTimes t = ruTimes(s);
    RaceState st;
    raceInit(st, s, t);
    const int k = raceAssess(st, t, ev);
    const RaceLineEval* u = ruUnit(ev, k, 3, 3);
    TEST_ASSERT_NOT_NULL(u);
    TEST_ASSERT_EQUAL_INT(1, u->line.len);
    TEST_ASSERT_EQUAL_FLOAT(2000.0f, u->th);
    TEST_ASSERT_TRUE(u->danger);
    TEST_ASSERT_TRUE(u->guarded);
    TEST_ASSERT_EQUAL_FLOAT(450.0f, u->save);
    TEST_ASSERT_EQUAL_INT(3, u->guardR);
    TEST_ASSERT_EQUAL_INT(3, u->guardC);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, u->risk);
    uint8_t ends[MAX_N * MAX_N];
    TEST_ASSERT_EQUAL_INT(4, raceUnitEnds(st.board, u->line, ends));
    TEST_ASSERT_EQUAL_INT(2 * 7 + 3, ends[0]);
    TEST_ASSERT_EQUAL_INT(4 * 7 + 3, ends[3]);
    uint8_t sqs[MAX_N * MAX_N];
    TEST_ASSERT_EQUAL_INT(1, raceUnitSquares(st.board, u->line, sqs));
    TEST_ASSERT_EQUAL_INT(3 * 7 + 3, sqs[0]);
    // Without the read the lines are as before and no RU unit is read.
    const RaceTimes off = raceTimes(s, Params());
    RaceState st0;
    raceInit(st0, s, off);
    const int k0 = raceAssess(st0, off, ev);
    TEST_ASSERT_NULL(ruUnit(ev, k0, 3, 3));
    TEST_ASSERT_EQUAL_INT(k - 1, k0);
  }
  // Cooling down for 1500 more: S = 1950 is too late, risk = 0.5 + (1950 + 200 - 2000) / 2000.
  setCooldown(s, 3, 3, 1500);
  const RaceTimes t = ruTimes(s);
  RaceState st;
  raceInit(st, s, t);
  const int k = raceAssess(st, t, ev);
  const RaceLineEval* u = ruUnit(ev, k, 3, 3);
  TEST_ASSERT_NOT_NULL(u);
  TEST_ASSERT_FALSE(u->guarded);
  TEST_ASSERT_EQUAL_FLOAT(1950.0f, u->save);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.575f, u->risk);
  float risk[MAX_N * MAX_N];
  racePieceRisk(st.board, ev, k, risk);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.575f, risk[3 * 7 + 3]);
}

void test_ru_far_unit_without_a_safe_move_is_no_danger_yet() {
  // M(0,0) cannot move: M(0,1) and the held piece's origin (1,0) (closed to the machine) fill
  // its neighbours. H(6,0) can be put on (1,0) once the held piece is placed elsewhere and the
  // interval has passed: max(350 + 500, 350 + 1000) = 1350.
  Snapshot s = makeSnap(
      "MM.....\n"
      "h......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      "H......",
      0.0f, 0.0f);
  RaceLineEval ev[RACE_MAX_LINES];
  {
    // Untouched now: a danger with no save, risk clamp((2500 - 1350) / 1000) = 1.
    ruFresh(s);
    staleIn(s, 0, 0, 0);
    const RaceTimes t = ruTimes(s);
    RaceState st;
    raceInit(st, s, t);
    const int k = raceAssess(st, t, ev);
    const RaceLineEval* u = ruUnit(ev, k, 0, 0);
    TEST_ASSERT_NOT_NULL(u);
    TEST_ASSERT_EQUAL_FLOAT(1350.0f, u->th);
    TEST_ASSERT_TRUE(u->danger);
    TEST_ASSERT_FALSE(u->guarded);
    TEST_ASSERT_TRUE(raceInf(u->save));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, u->risk);
  }
  // Untouched in 12000 (beyond RACE_DANGER_MS, inside RACE_UNTOUCHED_DANGER_MS) with no safe
  // move: read, but left to later reads (no danger, no risk). Pieces untouched later than
  // RACE_UNTOUCHED_DANGER_MS are no unit at all.
  ruFresh(s, 20000);
  staleIn(s, 0, 0, 12000);
  const RaceTimes t = ruTimes(s);
  RaceState st;
  raceInit(st, s, t);
  const int k = raceAssess(st, t, ev);
  const RaceLineEval* u = ruUnit(ev, k, 0, 0);
  TEST_ASSERT_NOT_NULL(u);
  TEST_ASSERT_EQUAL_FLOAT(12000.0f, u->th);
  TEST_ASSERT_FALSE(u->danger);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, u->risk);
  TEST_ASSERT_NULL(ruUnit(ev, k, 0, 1));
}

void test_ru_rest_opens_a_neighbour_of_an_untouched_piece() {
  // M(0,0) is untouched, enclosed by M(0,1) and M(1,0). Moving M(1,0) to (1,3) opens (1,0), which
  // H(6,0) reaches in one move: the rest is taken at 500.
  Snapshot s = makeSnap(
      "MM.....\n"
      "M......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      "H......",
      1.0f, 0.0f);
  ruFresh(s);
  staleIn(s, 0, 0, 0);
  float thDest, thRest;
  {
    const RaceTimes t = ruTimes(s);
    RaceState st;
    raceInit(st, s, t);
    TEST_ASSERT_TRUE(raceInf(raceUntouchedTime(st, t, 0, 0)));
    raceDestTimes(st, t, 1, 0, 1, 3, &thDest, &thRest);
    TEST_ASSERT_EQUAL_FLOAT(500.0f, thRest);
    TEST_ASSERT_FALSE(raceSafeDest(st, t, 1, 0, 1, 3, 450.0f));
  }
  const RaceTimes off = raceTimes(s, Params());
  RaceState st0;
  raceInit(st0, s, off);
  raceDestTimes(st0, off, 1, 0, 1, 3, &thDest, &thRest);
  TEST_ASSERT_TRUE(raceInf(thRest));
}

void test_ru_one_move_guards_the_piece_and_its_lines() {
  // Trap A with M(5,3) untouched in 1400: its RU unit (1400), its row (H2 1500) and its column
  // (4000) are all threatened. One move of the piece to a safe square saves all three at once
  // (without the cover, the row and column would need the head again after the RU unit's guard).
  Snapshot s = makeSnap(TRAP_A, 5.0f, 3.0f);
  ruFresh(s);
  staleIn(s, 5, 3, 1400);
  const RaceTimes t = ruTimes(s);
  RaceState st;
  raceInit(st, s, t);
  RaceLineEval ev[RACE_MAX_LINES];
  const int k = raceAssess(st, t, ev);
  const RaceLineEval* u = ruUnit(ev, k, 5, 3);
  const RaceLineEval* row = nullptr;
  const RaceLineEval* col = nullptr;
  for (int i = 0; i < k; i++) {
    if (raceIsUntouched(ev[i].line) || ev[i].line.r != 5 || ev[i].line.c != 3) continue;
    if (ev[i].line.dr == 0 && ev[i].line.dc == 1) row = &ev[i];
    if (ev[i].line.dr == 1) col = &ev[i];
  }
  TEST_ASSERT_NOT_NULL(u);
  TEST_ASSERT_NOT_NULL(row);
  TEST_ASSERT_NOT_NULL(col);
  TEST_ASSERT_EQUAL_FLOAT(1400.0f, u->th);
  TEST_ASSERT_EQUAL_FLOAT(1500.0f, row->th);
  const RaceLineEval* units[3] = {u, row, col};
  for (const RaceLineEval* e : units) {
    TEST_ASSERT_TRUE(e->danger);
    TEST_ASSERT_TRUE(e->guarded);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, e->risk);
    TEST_ASSERT_EQUAL_FLOAT(450.0f, e->save);
    TEST_ASSERT_EQUAL_INT(5, e->guardR);
    TEST_ASSERT_EQUAL_INT(3, e->guardC);
    TEST_ASSERT_EQUAL_INT(u->toR, e->toR);
    TEST_ASSERT_EQUAL_INT(u->toC, e->toC);
    TEST_ASSERT_EQUAL_INT(u->order, e->order);
  }
  TEST_ASSERT_TRUE(raceSafeDest(st, t, 5, 3, u->toR, u->toC, 450.0f));
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
  RUN_TEST(test_held_origin_needs_placement_and_interval);
  RUN_TEST(test_human_reacts_to_machine_move);
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
  RUN_TEST(test_re_group_needs_every_liberty_filled);
  RUN_TEST(test_re_group_one_liberty_is_h1_and_enclosers_stay);
  RUN_TEST(test_re_enclosed_group_falls_to_step_away_and_back);
  RUN_TEST(test_re_group_risk_reaches_its_pieces);
  RUN_TEST(test_re_corner_is_no_safe_destination);
  RUN_TEST(test_ru_threat_waits_for_untouched_and_a_neighbour);
  RUN_TEST(test_ru_held_piece_and_pending_target);
  RUN_TEST(test_ru_enclosed_piece_falls_to_step_away_and_back);
  RUN_TEST(test_ru_moving_resets_the_timer_and_dragged_piece_is_safe);
  RUN_TEST(test_ru_unit_is_guarded_with_a_refresh_move);
  RUN_TEST(test_ru_far_unit_without_a_safe_move_is_no_danger_yet);
  RUN_TEST(test_ru_rest_opens_a_neighbour_of_an_untouched_piece);
  RUN_TEST(test_ru_one_move_guards_the_piece_and_its_lines);
  return UNITY_END();
}
