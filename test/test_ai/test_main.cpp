#include <unity.h>
#include <cstdio>
#include <cstring>
#include "Ai.h"
#include "AiCase.h"
#include "Board.h"
#include "Game.h"
#include "RaceTime.h"
#include "SimDriver.h"
#include <chrono>

// Build a snapshot from rows separated by '\n': '.' empty, 'H' human, 'M' machine,
// 'h' a held human piece (its origin square is empty on the board, just lifted). Pieces get ids
// 1.. in row-major order. All cooldowns are over (clock 10000).
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
        pv.liftedAt = snap.clockMs;
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
  // Not even a candidate: only "stay" and the cooled-down M(3,2) are weighed.
  AiCandidate c[AI_MAX_CANDIDATES];
  const int k = thinkCandidates(s, p, c, AI_MAX_CANDIDATES);
  TEST_ASSERT_TRUE(k > 1);
  for (int i = 0; i < k; i++) TEST_ASSERT_TRUE(c[i].none || c[i].pieceId != 1);
}

// test/golden/ai_cases.txt, relative to the project directory (where `pio test` runs).
static FILE* openGolden() {
  const char* paths[] = {"test/golden/ai_cases.txt", "../test/golden/ai_cases.txt", "../../test/golden/ai_cases.txt"};
  for (const char* p : paths) {
    FILE* f = std::fopen(p, "r");
    if (f) return f;
  }
  return nullptr;
}

// Opening: no square in the open where two human pieces could each step in beside it
// (the human's cooldowns are per piece, so two pieces move back to back).
void test_hard_does_not_walk_into_two_piece_trap() {
  Snapshot s = makeSnap("MMMMMMM\n.......\n.......\n.......\n.......\n.......\nHHHHHHH", 0.0f, 3.0f);
  Params p;
  p.level = Level::Hard;
  AiMove m = think(s, p);
  if (m.none) return;
  Board b = applyMove(s, m);
  // The moved piece must not have both side squares (or both vertical neighbours) empty and
  // reachable from the human back row.
  const bool openRow = m.toC > 0 && m.toC < 6 && b.at(m.toR, m.toC - 1) == EMPTY && b.at(m.toR, m.toC + 1) == EMPTY;
  TEST_ASSERT_FALSE(openRow && m.toR >= 1);
}

// A machine piece between H(4,2) and an empty square that the human piece at (5,4) reaches once its
// cooldown is over (trap D): the read counts that piece. It stays while it can still get out in
// time and pulls the piece at (4,3) out before (5,4) can close the line.
void test_hard_leaves_before_flank_cooldown_ends() {
  Snapshot s = makeSnap("MMM.MMM\n.......\n.......\n.......\nHHHM...\n....H..\n...H.HH", 4.0f, 3.0f);
  const uint32_t ready = s.clockMs + 3000;
  for (int i = 0; i < s.count; i++)
    if (s.pieces[i].r == 5 && s.pieces[i].c == 4) s.pieces[i].cooldownUntil = ready;
  Params p;
  p.level = Level::Hard;
  const RaceTimes t = raceTimes(s, p);
  // Reads every 250 ms with nothing else moving.
  bool moved = false;
  for (uint32_t clock = s.clockMs; clock < ready + 500 && !moved; clock += 250) {
    Snapshot at = s;
    at.clockMs = clock;
    AiDebug dbg;
    const AiMove m = think(at, p, &dbg);
    // T_h of the row line: (5,4) lands on (4,4) humanMoveMs after its cooldown.
    const float th = static_cast<float>(ready - clock) + 500.0f;
    if (m.none) {
      // Staying is chosen only while the line is guarded.
      TEST_ASSERT_EQUAL_FLOAT(0.0f, dbg.pieceRisk[4 * 7 + 3]);
      continue;
    }
    const PieceView& q = pieceById(at, m.pieceId);
    TEST_ASSERT_EQUAL_INT(4, q.r);
    TEST_ASSERT_EQUAL_INT(3, q.c);
    // In time: it starts pulling (engage, no travel) at least the margin before the line closes.
    TEST_ASSERT_TRUE(t.engage + t.margin <= th);
    moved = true;
  }
  TEST_ASSERT_TRUE(moved);
}

// Sitting between two human pieces: one steps away and another steps into its square (H3 at
// 1500 ms, trap C). With the head on the piece the line is guarded (it can leave in time) and the
// read stays; with the head far away it cannot, and the read sees the risk. Easy reads only a held
// piece's single placement and sees nothing.
void test_refill_threat_is_read() {
  const char* layout = "MMM.MMM\n.......\n.......\n.......\nHHHMHHH\n.......\n....H..";
  Params p;
  p.level = Level::Hard;
  AiDebug near;
  const AiMove m = think(makeSnap(layout, 4.0f, 3.0f), p, &near);
  TEST_ASSERT_TRUE(m.none);
  TEST_ASSERT_TRUE(near.safeCandidates > 0);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, near.pieceRisk[4 * 7 + 3]);
  AiDebug far;
  think(makeSnap(layout, 0.0f, 0.0f), p, &far);
  TEST_ASSERT_TRUE(far.pieceRisk[4 * 7 + 3] >= 0.5f);
  bool rowLine = false;
  for (int i = 0; i < far.lineCount; i++) {
    const AiLineRisk& L = far.lines[i];
    if (L.r == 4 && L.c == 3 && L.dr == 0) {
      rowLine = true;
      TEST_ASSERT_EQUAL_FLOAT(far.pieceRisk[4 * 7 + 3], L.risk);
    }
  }
  TEST_ASSERT_TRUE(rowLine);
  p.level = Level::Easy;
  AiDebug easy;
  think(makeSnap(layout, 0.0f, 0.0f), p, &easy);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, easy.pieceRisk[4 * 7 + 3]);
}

// Equal scores are decided by the hash of the snapshot (board, pieces, clock in 250 ms units) and
// the candidate's number: the same snapshot always gives the same move, another clock may give
// another of the tied moves. Here the held piece at (5,3) closes (2,3) in 350 ms; nothing is safe
// and M(1,3) stepping to (2,3), (1,2) or (1,4) lose the same.
void test_tie_broken_by_hash() {
  const char* layout = "...H...\n...M...\n.......\n.......\n.......\n...h...\nM......";
  Params p;
  p.level = Level::Easy;
  AiCandidate c[AI_MAX_CANDIDATES];
  const int k = thinkCandidates(makeSnap(layout, 0.0f, 3.0f), p, c, AI_MAX_CANDIDATES);
  float best = -1e9f;
  for (int i = 0; i < k; i++)
    if (c[i].contending && c[i].score > best) best = c[i].score;
  int tied = 0;
  for (int i = 0; i < k; i++)
    if (c[i].contending && c[i].score >= best - 0.01f) tied++;
  TEST_ASSERT_TRUE(tied >= 2);
  bool seen[MAX_N * MAX_N] = {};
  int kinds = 0;
  for (uint32_t step = 0; step < 16; step++) {
    Snapshot s = makeSnap(layout, 0.0f, 3.0f);
    s.clockMs += 250 * step;
    for (int i = 0; i < s.count; i++)
      if (s.pieces[i].held) s.pieces[i].liftedAt = s.clockMs;
    const AiMove a = think(s, p), b = think(s, p);
    TEST_ASSERT_FALSE(a.none);
    TEST_ASSERT_EQUAL_INT(a.pieceId, b.pieceId);
    TEST_ASSERT_EQUAL_INT(a.toR, b.toR);
    TEST_ASSERT_EQUAL_INT(a.toC, b.toC);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, best, a.score);
    if (!seen[a.toR * 7 + a.toC]) kinds++;
    seen[a.toR * 7 + a.toC] = true;
  }
  TEST_ASSERT_TRUE(kinds >= 2);
}

// No safe candidate (normal): the read picks the least expected loss, here moving the threatened
// M(1,3) (it may still be caught, but less likely than staying).
void test_no_safe_move_least_loss() {
  Snapshot s = makeSnap("...H...\n...M...\n.......\n.......\n.......\n...h...\nM......", 0.0f, 3.0f);
  Params p;
  p.level = Level::Normal;
  AiDebug dbg;
  const AiMove m = think(s, p, &dbg);
  TEST_ASSERT_EQUAL_INT(0, dbg.safeCandidates);
  TEST_ASSERT_TRUE(dbg.pieceRisk[1 * 7 + 3] > 0.5f);
  TEST_ASSERT_FALSE(m.none);
  TEST_ASSERT_EQUAL_INT(2, m.pieceId);  // M(1,3)
  AiCandidate c[AI_MAX_CANDIDATES];
  const int k = thinkCandidates(s, p, c, AI_MAX_CANDIDATES);
  TEST_ASSERT_EQUAL_INT(dbg.candidates, k);
  for (int i = 0; i < k; i++) {
    TEST_ASSERT_TRUE(c[i].contending);
    TEST_ASSERT_TRUE(c[i].score <= m.score + 0.01f);
  }
}

// Hard's fork penalty: the three best safe candidates (stay, (4,3)>(4,1), (4,3)>(4,2)) each lose to
// some human threat move and take -60; the fourth, (4,3)>(3,3), is not checked and wins. Normal
// has no fork check and stays.
void test_fork_penalty_on_top_three_only() {
  const char* line =
      "...M.....M.H................H..M...M..M.M..M..... 2,2 5.5,4.5 1,2=1700;4,0=1500;5,3=1400 - 100 "
      "3,1,200,250,H,0.75,0 none";
  AiCase c;
  TEST_ASSERT_TRUE(parseAiCase(line, c));
  AiDebug dbg;
  const AiMove m = think(c.snap, c.params, &dbg);
  TEST_ASSERT_EQUAL_INT(3, dbg.forkChecked);
  TEST_ASSERT_EQUAL_INT(3, dbg.forkPenalized);
  TEST_ASSERT_FALSE(m.none);
  const PieceView& q = c.snap.pieces[m.pieceId - 1];
  TEST_ASSERT_EQUAL_INT(4, q.r);
  TEST_ASSERT_EQUAL_INT(3, q.c);
  TEST_ASSERT_EQUAL_INT(3, m.toR);
  TEST_ASSERT_EQUAL_INT(3, m.toC);
  AiCandidate a[AI_MAX_CANDIDATES];
  const int k = thinkCandidates(c.snap, c.params, a, AI_MAX_CANDIDATES);
  int penalized = 0;
  float lowestPenalized = 1e9f, highestOther = -1e9f;
  for (int i = 0; i < k; i++) {
    if (!a[i].safe) continue;
    const float before = a[i].score + a[i].penalty;
    if (a[i].penalty > 0.0f) {
      penalized++;
      TEST_ASSERT_EQUAL_FLOAT(60.0f, a[i].penalty);
      if (before < lowestPenalized) lowestPenalized = before;
    } else if (before > highestOther) {
      highestOther = before;
    }
  }
  TEST_ASSERT_EQUAL_INT(3, penalized);
  TEST_ASSERT_TRUE(lowestPenalized > highestOther);  // the penalized ones were the top three
  Params normal = c.params;
  normal.level = Level::Normal;
  AiDebug nd;
  TEST_ASSERT_TRUE(think(c.snap, normal, &nd).none);
  TEST_ASSERT_EQUAL_INT(0, nd.forkChecked);
}

// think() time on the PC (plan 2026-10-31 3.13): mean over positions of a game and the golden
// table within 2 ms, and a position with many pieces and threatened lines within 10 ms.
static double thinkMicros(const Snapshot& s, const Params& p) {
  double best = 1e18;
  for (int k = 0; k < 3; k++) {
    const auto t0 = std::chrono::steady_clock::now();
    const AiMove m = think(s, p);
    (void)m;
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
    if (us < best) best = us;
  }
  return best;
}

void test_think_time() {
  double total = 0.0;
  int count = 0;
  // Positions of a game (normal and hard) where the human pieces step up one after another.
  for (int level = 0; level < 2; level++) {
    Params p;
    p.level = level == 0 ? Level::Normal : Level::Hard;
    Game g;
    g.start(0);
    g.tick(3000);
    uint32_t now = 3000;
    // Human moves by hand: each back-row piece steps up in turn, every 1.5 s.
    for (int step = 0; step < 14 && g.phase() == Phase::Play; step++) {
      const int col = step % 7, row = step < 7 ? 6 : 5, to = step < 7 ? 5 : 4;
      if (g.onHumanLift(row, col, now) == LiftResult::Ok) g.onHumanPlace(to, col, now + 400);
      now += 1500;
      g.tick(now);
      const Snapshot s = g.snapshot(0.0f, 3.0f);
      total += thinkMicros(s, p);
      count++;
    }
  }
  FILE* f = openGolden();
  TEST_ASSERT_NOT_NULL(f);
  char line[512];
  while (std::fgets(line, sizeof(line), f)) {
    AiCase c;
    if (isAiCaseSkippable(line) || !parseAiCase(line, c)) continue;
    total += thinkMicros(c.snap, c.params);
    count++;
  }
  std::fclose(f);
  const double mean = total / count;
  // Seven machine pieces in the open next to seven human pieces: every line threatened, no safe
  // candidate, so every candidate's assessment runs (the heaviest case found by a random search).
  AiCase worst;
  TEST_ASSERT_TRUE(parseAiCase(
      "HMM....M.......H.M.H...H......H...HM...MM..H..... - 0,0 - - - 4,1.5,200,250,H,0.5,1 none", worst));
  const double worstUs = thinkMicros(worst.snap, worst.params);
  printf("think: mean %.0f us over %d positions, worst position %.0f us\n", mean, count, worstUs);
  TEST_ASSERT_TRUE(mean <= 2000.0);
  TEST_ASSERT_TRUE(worstUs <= 10000.0);
}

void test_ai_case_parser() {
  AiCase c;
  TEST_ASSERT_FALSE(parseAiCase("# comment", c));
  TEST_ASSERT_FALSE(parseAiCase("", c));
  TEST_ASSERT_TRUE(parseAiCase("M...H.... 2,1 0.5,1.25 0,0=700 3,1.5,100,250,H,0.25,0 0,0>1,0", c));
  TEST_ASSERT_EQUAL_INT(3, c.snap.board.n);
  TEST_ASSERT_EQUAL_INT(3, c.snap.count);  // M(0,0), H(1,1), held H at (2,1)
  TEST_ASSERT_EQUAL_INT(MACHINE, c.snap.pieces[0].side);
  TEST_ASSERT_EQUAL_UINT32(AI_CASE_CLOCK_MS + 700, c.snap.pieces[0].cooldownUntil);
  TEST_ASSERT_EQUAL_INT(1, c.snap.heldCount);
  TEST_ASSERT_EQUAL_INT(3, c.snap.heldIds[0]);
  TEST_ASSERT_TRUE(c.snap.pieces[2].held);
  TEST_ASSERT_EQUAL_INT(EMPTY, c.snap.board.at(2, 1));
  TEST_ASSERT_EQUAL_FLOAT(0.5f, c.snap.headR);
  TEST_ASSERT_EQUAL_FLOAT(1.25f, c.snap.headC);
  TEST_ASSERT_EQUAL_FLOAT(3.0f, c.params.freeSpeed);
  TEST_ASSERT_EQUAL_FLOAT(1.5f, c.params.dragSpeed);
  TEST_ASSERT_EQUAL_UINT32(100, c.params.engageMs);
  TEST_ASSERT_EQUAL_INT((int)Level::Hard, (int)c.params.level);
  TEST_ASSERT_EQUAL_FLOAT(0.25f, c.params.aggression);
  TEST_ASSERT_FALSE(c.params.readLift);
  TEST_ASSERT_FALSE(c.expectNone);
  TEST_ASSERT_EQUAL_INT(1, c.toR);
  // Malformed lines.
  TEST_ASSERT_FALSE(parseAiCase("M...H... - 0,0 - 3,1.5,100,250,H,0.25,0 none", c));    // 8 cells
  TEST_ASSERT_FALSE(parseAiCase("M...H.... - 0,0 - 3,1.5,100,250,X,0.25,0 none", c));   // level
  TEST_ASSERT_FALSE(parseAiCase("M...H.... 1,1 0,0 - 3,1.5,100,250,H,0.25,0 none", c)); // held on a piece
  TEST_ASSERT_FALSE(parseAiCase("M...H.... - 0,0 - 3,1.5,100,250,H,0.25,0", c));        // missing field
  // A six-field line reads gap and lift as '-': no placement yet, held pieces just lifted.
  TEST_ASSERT_TRUE(parseAiCase("M...H.... 2,1 0.5,1.25 - 3,1.5,100,250,H,0.25,0 none", c));
  TEST_ASSERT_FALSE(c.snap.hasLastPlace);
  TEST_ASSERT_EQUAL_UINT32(AI_CASE_CLOCK_MS, c.snap.pieces[2].liftedAt);
  TEST_ASSERT_TRUE(parseAiCase("M...H.... 2,1 0.5,1.25 - - - 3,1.5,100,250,H,0.25,0 none", c));
  TEST_ASSERT_FALSE(c.snap.hasLastPlace);
  TEST_ASSERT_EQUAL_UINT32(AI_CASE_CLOCK_MS, c.snap.pieces[2].liftedAt);
  // gap 400: the last placement was 600 ms ago; lift 250 ms ago.
  TEST_ASSERT_TRUE(parseAiCase("M...H.... 2,1 0.5,1.25 - 400 250 3,1.5,100,250,H,0.25,0 none", c));
  TEST_ASSERT_TRUE(c.snap.hasLastPlace);
  TEST_ASSERT_EQUAL_UINT32(AI_CASE_CLOCK_MS - 600, c.snap.lastPlaceAt);
  TEST_ASSERT_EQUAL_UINT32(AI_CASE_CLOCK_MS - 250, c.snap.pieces[2].liftedAt);
  TEST_ASSERT_EQUAL_UINT32(0, c.snap.pieces[0].liftedAt);
  // Lift times follow the order of the held field.
  TEST_ASSERT_TRUE(parseAiCase("M........ 2,1;1,2 0,0 - 0 100;700 3,1.5,100,250,H,0.25,0 none", c));
  TEST_ASSERT_EQUAL_UINT32(AI_CASE_CLOCK_MS - 700, c.snap.pieces[1].liftedAt);  // (1,2), id 2
  TEST_ASSERT_EQUAL_UINT32(AI_CASE_CLOCK_MS - 100, c.snap.pieces[2].liftedAt);  // (2,1), id 3
  TEST_ASSERT_EQUAL_UINT32(AI_CASE_CLOCK_MS - 1000, c.snap.lastPlaceAt);
  TEST_ASSERT_FALSE(parseAiCase("M...H.... 2,1 0,0 - 1001 - 3,1.5,100,250,H,0.25,0 none", c));  // gap > interval
  TEST_ASSERT_FALSE(parseAiCase("M...H.... 2,1 0,0 - - 1;2 3,1.5,100,250,H,0.25,0 none", c));   // one held
  TEST_ASSERT_FALSE(parseAiCase("M...H.... - 0,0 - - 5 3,1.5,100,250,H,0.25,0 none", c));       // none held
  TEST_ASSERT_FALSE(parseAiCase("M...H.... - 0,0 - - 3,1.5,100,250,H,0.25,0 none", c));         // 7 fields
  TEST_ASSERT_FALSE(parseAiCase("M...H.... - 0,0 - - - x 3,1.5,100,250,H,0.25,0 none", c));     // 9 fields
}

// Game::snapshot carries what RaceTime needs (plan 2026-10-31 3.3): lift time, last placement,
// the pending piece, the reserved square and the rule times.
void test_snapshot_fields() {
  Rules rules;
  rules.humanCooldownMs = 2500;
  rules.machineCooldownMs = 900;
  rules.humanIntervalMs = 1100;
  rules.maxHoldMs = 1200;
  Game g(rules);
  g.start(0);
  g.tick(3000);  // countdown over: Play at caller time 3000, game clock 0
  TEST_ASSERT_EQUAL_INT((int)Phase::Play, (int)g.phase());
  const int n = g.rules().n;
  Snapshot s = g.snapshot(0.0f, 0.0f);
  TEST_ASSERT_FALSE(s.hasLastPlace);
  TEST_ASSERT_EQUAL_INT(-1, s.pendingId);
  TEST_ASSERT_EQUAL_INT(-1, s.reservedR);
  TEST_ASSERT_EQUAL_UINT32(2500, s.humanCooldownMs);
  TEST_ASSERT_EQUAL_UINT32(900, s.machineCooldownMs);
  TEST_ASSERT_EQUAL_UINT32(1100, s.humanIntervalMs);
  TEST_ASSERT_EQUAL_UINT32(1200, s.maxHoldMs);
  // Lift (6,0) at clock 100, put it on (5,0) at 300.
  TEST_ASSERT_EQUAL_INT((int)LiftResult::Ok, (int)g.onHumanLift(n - 1, 0, 3100));
  s = g.snapshot(0.0f, 0.0f);
  const int id0 = s.heldIds[0];
  TEST_ASSERT_TRUE(s.pieces[id0 - 1].held);
  TEST_ASSERT_EQUAL_UINT32(100, s.pieces[id0 - 1].liftedAt);
  TEST_ASSERT_EQUAL_INT((int)PlaceResult::Ok, (int)g.onHumanPlace(n - 2, 0, 3300));
  s = g.snapshot(0.0f, 0.0f);
  TEST_ASSERT_TRUE(s.hasLastPlace);
  TEST_ASSERT_EQUAL_UINT32(300, s.lastPlaceAt);
  // A second piece lifted and put down too early: pending.
  TEST_ASSERT_EQUAL_INT((int)LiftResult::Ok, (int)g.onHumanLift(n - 1, 1, 3400));
  TEST_ASSERT_EQUAL_INT((int)PlaceResult::Pending, (int)g.onHumanPlace(n - 2, 1, 3500));
  s = g.snapshot(0.0f, 0.0f);
  TEST_ASSERT_TRUE(s.pending);
  TEST_ASSERT_EQUAL_INT(s.heldIds[0], s.pendingId);
  TEST_ASSERT_EQUAL_INT(n - 2, s.pendingR);
  TEST_ASSERT_EQUAL_INT(1, s.pendingC);
  // A machine reservation shows up.
  int mid = -1;
  for (int i = 0; i < g.pieceCount(); i++)
    if (g.pieces()[i].side == MACHINE && g.pieces()[i].c == 3) mid = g.pieces()[i].id;
  TEST_ASSERT_TRUE(g.machineCanEnter(mid, 1, 3));
  s = g.snapshot(0.0f, 0.0f);
  TEST_ASSERT_EQUAL_INT(1, s.reservedR);
  TEST_ASSERT_EQUAL_INT(3, s.reservedC);
}

void test_golden_cases() {
  FILE* f = openGolden();
  TEST_ASSERT_NOT_NULL_MESSAGE(f, "test/golden/ai_cases.txt not found");
  char line[512];
  int cases = 0, failures = 0, lineNo = 0;
  while (std::fgets(line, sizeof(line), f)) {
    lineNo++;
    if (isAiCaseSkippable(line)) continue;
    AiCase c;
    char msg[96];
    snprintf(msg, sizeof(msg), "line %d does not parse", lineNo);
    TEST_ASSERT_TRUE_MESSAGE(parseAiCase(line, c), msg);
    AiMove m;
    if (!checkAiCase(c, &m)) {
      failures++;
      if (m.none) {
        printf("golden line %d: got none (score %.3f)\n", lineNo, (double)m.score);
      } else {
        const PieceView& p = c.snap.pieces[m.pieceId - 1];
        printf("golden line %d: got %d,%d>%d,%d (score %.3f)\n", lineNo, p.r, p.c, m.toR, m.toC, (double)m.score);
      }
    }
    cases++;
  }
  std::fclose(f);
  TEST_ASSERT_TRUE(cases >= 20);
  TEST_ASSERT_EQUAL_INT(0, failures);
}

// ---- Rule candidates (plan 2026-11-06 3.1) ----

// RW: the read never picks a move that fills a row edge to edge.
void test_wall_limit_move_is_no_candidate() {
  Snapshot s = makeSnap(
      "MMM.MMM\n"
      ".......\n"
      "...M...\n"
      ".......\n"
      ".......\n"
      ".......\n"
      "H.....H",
      2.0f, 3.0f);
  for (int wall = 0; wall < 2; wall++) {
    s.wallLimit = wall == 1;
    static AiCandidate cand[AI_MAX_CANDIDATES];
    const int k = thinkCandidates(s, Params(), cand, AI_MAX_CANDIDATES);
    bool intoGap = false, alongRow = false;
    for (int i = 0; i < k; i++) {
      if (cand[i].none) continue;
      const PieceView& p = pieceById(s, cand[i].pieceId);
      if (p.r == 2 && p.c == 3 && cand[i].toR == 0 && cand[i].toC == 3) intoGap = true;
      if (p.r == 0 && p.c == 2 && cand[i].toR == 0 && cand[i].toC == 3) alongRow = true;
    }
    TEST_ASSERT_EQUAL_INT(wall == 0, (int)intoGap);
    TEST_ASSERT_TRUE(alongRow);  // sliding along the row leaves a gap behind
    const AiMove m = think(s, Params());
    if (wall == 1 && !m.none) {
      const PieceView& p = pieceById(s, m.pieceId);
      TEST_ASSERT_FALSE(p.r == 2 && p.c == 3 && m.toR == 0 && m.toC == 3);
    }
  }
}

// RW (user decision 2026-11-07): nor a move whose drag passes a square where the piece, coming
// to rest there, would complete a wall: the controller stops the drag before such a square
// (Game::machineCanEnter), so the move could not reach its destination.
void test_wall_limit_path_through_a_wall_square_is_no_candidate() {
  Snapshot s = makeSnap(
      "..M....\n"
      ".......\n"
      ".......\n"
      "MM.MMMM\n"
      ".......\n"
      ".......\n"
      "H.....H",
      0.0f, 2.0f);
  for (int wall = 0; wall < 2; wall++) {
    s.wallLimit = wall == 1;
    static AiCandidate cand[AI_MAX_CANDIDATES];
    const int k = thinkCandidates(s, Params(), cand, AI_MAX_CANDIDATES);
    bool down[7] = {};  // destinations (r,2) of the piece on (0,2)
    bool along = false;
    for (int i = 0; i < k; i++) {
      if (cand[i].none) continue;
      const PieceView& p = pieceById(s, cand[i].pieceId);
      if (p.r == 0 && p.c == 2 && cand[i].toC == 2) down[cand[i].toR] = true;
      if (p.r == 3 && p.c == 1 && cand[i].toR == 3 && cand[i].toC == 2) along = true;
    }
    TEST_ASSERT_TRUE(down[1]);
    TEST_ASSERT_TRUE(down[2]);
    for (int r = 3; r < 7; r++) TEST_ASSERT_EQUAL_INT(wall == 0, (int)down[r]);
    TEST_ASSERT_TRUE(along);  // sliding along row 3 into its gap leaves a gap behind
    // Forced forward, the read still never picks such a move.
    for (int level = 0; level < 3; level++) {
      Params p;
      p.level = static_cast<Level>(level);
      p.advanceWeight = 240.0f;
      const AiMove m = think(s, p);
      if (wall == 1 && !m.none) {
        const PieceView& q = pieceById(s, m.pieceId);
        TEST_ASSERT_FALSE(q.r == 0 && q.c == 2 && m.toR >= 3);
      }
    }
  }
}

// Opening (mustMove): "stay" is no candidate; the read makes one move.
void test_opening_read_makes_a_move() {
  Rules rules;
  rules.wallLimit = true;
  Game g(rules);
  g.start(0);
  g.tick(3000);
  TEST_ASSERT_EQUAL_INT((int)Phase::Opening, (int)g.phase());
  const Snapshot s = g.snapshot(0.0f, 3.0f);
  TEST_ASSERT_TRUE(s.mustMove);
  TEST_ASSERT_TRUE(s.wallLimit);
  for (int level = 0; level < 3; level++) {
    Params p;
    p.level = static_cast<Level>(level);
    const AiMove m = think(s, p);
    TEST_ASSERT_FALSE(m.none);
    static AiCandidate cand[AI_MAX_CANDIDATES];
    const int k = thinkCandidates(s, p, cand, AI_MAX_CANDIDATES);
    TEST_ASSERT_TRUE(k > 1);
    TEST_ASSERT_TRUE(cand[0].none);
    TEST_ASSERT_FALSE(cand[0].contending);
  }
}

// RE: the read counts the edge capture its move makes.
void test_re_read_takes_an_enclosed_corner() {
  Snapshot s = makeSnap(
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      "M......\n"
      "H...M..",
      6.0f, 3.0f);
  s.edgeCapture = true;
  AiMove m = think(s, Params());
  TEST_ASSERT_FALSE(m.none);
  TEST_ASSERT_EQUAL_INT(6, pieceById(s, m.pieceId).r);
  TEST_ASSERT_EQUAL_INT(4, pieceById(s, m.pieceId).c);
  TEST_ASSERT_EQUAL_INT(6, m.toR);
  TEST_ASSERT_EQUAL_INT(1, m.toC);
  TEST_ASSERT_TRUE(m.score > 50.0f);
}

// The golden table's rules field (lib/core/AiCase.h).
void test_ai_case_rules_field() {
  AiCase c;
  TEST_ASSERT_TRUE(parseAiCase("M...H.... - 0,0 - - - 3,1.5,100,250,H,0.25,0 RE+RW+OPEN none", c));
  TEST_ASSERT_TRUE(c.snap.edgeCapture);
  TEST_ASSERT_TRUE(c.snap.wallLimit);
  TEST_ASSERT_TRUE(c.snap.mustMove);
  TEST_ASSERT_EQUAL_UINT32(0, c.snap.untouchedMs);
  TEST_ASSERT_TRUE(parseAiCase("M...H.... - 0,0 - - - 3,1.5,100,250,H,0.25,0 RU20000 none", c));
  TEST_ASSERT_FALSE(c.snap.edgeCapture);
  TEST_ASSERT_EQUAL_UINT32(20000, c.snap.untouchedMs);
  TEST_ASSERT_TRUE(parseAiCase("M...H.... - 0,0 - - - 3,1.5,100,250,H,0.25,0 - none", c));
  TEST_ASSERT_FALSE(c.snap.edgeCapture || c.snap.wallLimit || c.snap.mustMove);
  TEST_ASSERT_TRUE(parseAiCase("M...H.... - 0,0 - - - 3,1.5,100,250,H,0.25,0 none", c));  // 8 fields: R0
  TEST_ASSERT_FALSE(c.snap.edgeCapture);
  TEST_ASSERT_FALSE(parseAiCase("M...H.... - 0,0 - - - 3,1.5,100,250,H,0.25,0 RX none", c));
  TEST_ASSERT_FALSE(parseAiCase("M...H.... - 0,0 - - - 3,1.5,100,250,H,0.25,0 RE+ none", c));
  TEST_ASSERT_FALSE(parseAiCase("M...H.... - 0,0 - 3,1.5,100,250,H,0.25,0 RE none", c));  // rules need gap/lift
}

// The advance weight (Params::advanceWeight, plan 2026-11-06 Step 0b): as tuned (30) the read
// stays on its home row at the start; the forced-advance test setting brings a piece out.
void test_advance_weight_brings_the_machine_out() {
  Game g;
  g.start(0);
  g.tick(3000);
  const Snapshot s = g.snapshot(0.0f, 3.0f);
  for (int level = 0; level < 3; level++) {
    Params p;
    p.level = static_cast<Level>(level);
    TEST_ASSERT_EQUAL_FLOAT(30.0f, p.advanceWeight);
    TEST_ASSERT_TRUE(think(s, p).none);
    p.advanceWeight = 120.0f;
    const AiMove m = think(s, p);
    TEST_ASSERT_FALSE(m.none);
    TEST_ASSERT_EQUAL_INT(0, pieceById(s, m.pieceId).r);
    TEST_ASSERT_TRUE(m.toR >= 2);
  }
}

// think() time under RE + RW (plan 2026-11-06 3.2): the same budgets as R0's on the PC.
void test_think_time_with_rules() {
  double total = 0.0;
  int count = 0;
  FILE* f = openGolden();
  TEST_ASSERT_NOT_NULL(f);
  char line[512];
  while (std::fgets(line, sizeof(line), f)) {
    AiCase c;
    if (isAiCaseSkippable(line) || !parseAiCase(line, c)) continue;
    c.snap.edgeCapture = true;
    c.snap.wallLimit = true;
    total += thinkMicros(c.snap, c.params);
    count++;
  }
  std::fclose(f);
  AiCase worst;
  TEST_ASSERT_TRUE(parseAiCase(
      "HMM....M.......H.M.H...H......H...HM...MM..H..... - 0,0 - - - 4,1.5,200,250,H,0.5,1 RE+RW none", worst));
  const double worstUs = thinkMicros(worst.snap, worst.params);
  printf("think RE+RW: mean %.0f us over %d positions, worst position %.0f us\n", total / count, count, worstUs);
  TEST_ASSERT_TRUE(total / count <= 2000.0);
  TEST_ASSERT_TRUE(worstUs <= 10000.0);
}

// ---- RU (plan 2026-11-06 3.1): the read's RU threats (RaceTime's RU read) ----

// RU with untouchedMs 20000 at clock 50000: every piece moved at the snapshot unless set with
// untouchedIn.
static Snapshot ruSnap(const char* rows, float headR, float headC) {
  Snapshot s = makeSnap(rows, headR, headC);
  s.clockMs = 50000;
  s.untouchedMs = 20000;
  for (int i = 0; i < s.count; i++) {
    s.pieces[i].movedAt = s.clockMs;
    s.pieces[i].liftedAt = s.clockMs;
  }
  return s;
}

static PieceView& pieceOn(Snapshot& s, int r, int c) {
  for (int i = 0; i < s.count; i++)
    if (s.pieces[i].r == r && s.pieces[i].c == c) return s.pieces[i];
  TEST_FAIL_MESSAGE("no piece there");
  return s.pieces[0];
}

// The piece on (r,c) goes untouched ms after the snapshot (0: it is untouched already).
static void untouchedIn(Snapshot& s, int r, int c, uint32_t ms) {
  pieceOn(s, r, c).movedAt = s.clockMs + ms - s.untouchedMs;
}

// M(0,2) goes untouched in 600 ms and every human piece reaches (1,2) in one move: RU takes it at
// 600, before a guard could start at the next read. The read refreshes it at once (sliding into
// the gap at normal and hard); untouched later (3000 ms), it still refreshes it while that safe
// move is there rather than at the last moment. Without RU, or with the piece fresh, it stays.
void test_ru_read_refreshes_a_piece_going_untouched() {
  Snapshot s = ruSnap(
      "MMM.MMM\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      "HHHHHHH",
      0.0f, 2.0f);
  const uint32_t soon[2] = {600, 3000};
  for (uint32_t in : soon) {
    untouchedIn(s, 0, 2, in);
    for (int level = 0; level < 3; level++) {
      Params p;
      p.level = static_cast<Level>(level);
      const AiMove m = think(s, p);
      TEST_ASSERT_FALSE(m.none);
      TEST_ASSERT_EQUAL_INT(0, pieceById(s, m.pieceId).r);
      TEST_ASSERT_EQUAL_INT(2, pieceById(s, m.pieceId).c);
      if (level > 0) {
        TEST_ASSERT_EQUAL_INT(0, m.toR);
        TEST_ASSERT_EQUAL_INT(3, m.toC);
      }
    }
  }
  untouchedIn(s, 0, 2, 20000);
  TEST_ASSERT_TRUE(think(s, Params()).none);
  untouchedIn(s, 0, 2, 600);
  s.untouchedMs = 0;
  TEST_ASSERT_TRUE(think(s, Params()).none);
}

// M(0,0) is untouched, enclosed by M(0,1) and M(1,0). Forced forward (advance weight 240), the
// read would take M(0,1) down its file, which opens (0,1) to H(0,6) along row 0: RU takes M(0,0)
// at 500. It moves another piece instead (normal and hard; Easy reads only a held piece). With
// M(0,0) fresh it takes M(0,1) down.
void test_ru_read_does_not_open_a_square_next_to_an_untouched_piece() {
  Snapshot s = ruSnap(
      "MM....H\n"
      "M......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      ".......\n"
      "H......",
      0.0f, 1.0f);
  for (int level = 1; level < 3; level++) {
    Params p;
    p.level = static_cast<Level>(level);
    p.advanceWeight = 240.0f;
    untouchedIn(s, 0, 0, 0);
    AiMove m = think(s, p);
    TEST_ASSERT_FALSE(m.none);
    const PieceView& q = pieceById(s, m.pieceId);
    TEST_ASSERT_FALSE(q.r == 0 && q.c == 1);
    // Whatever moved, no human piece reaches a square next to M(0,0) in one move afterwards.
    Board b = s.board;
    b.set(q.r, q.c, EMPTY);
    b.set(m.toR, m.toC, MACHINE);
    for (int d = 0; d < 4; d++) {
      const int rr = DIR_R[d], cc = DIR_C[d];
      if (!b.inside(rr, cc) || b.at(rr, cc) != EMPTY) continue;
      for (int i = 0; i < s.count; i++) {
        if (s.pieces[i].side != HUMAN) continue;
        int dd[MAX_N * MAX_N];
        const int k = dests(b, s.pieces[i].r, s.pieces[i].c, dd);
        for (int j = 0; j < k; j++) TEST_ASSERT_NOT_EQUAL(rr * 7 + cc, dd[j]);
      }
    }
    untouchedIn(s, 0, 0, 20000);
    m = think(s, p);
    TEST_ASSERT_FALSE(m.none);
    TEST_ASSERT_EQUAL_INT(0, pieceById(s, m.pieceId).r);
    TEST_ASSERT_EQUAL_INT(1, pieceById(s, m.pieceId).c);
  }
}

// think() time under RE + RW + RU (plan 2026-11-06 3.1): the golden table with RU20000 (every
// piece untouched: an RU unit for each machine piece), the heaviest R0 position likewise, and the
// reads of B1 games under RE + RW + RU20. The same budgets as R0's on the PC.
void test_think_time_with_untouched() {
  double total = 0.0, worstUs = 0.0;
  int count = 0;
  FILE* f = openGolden();
  TEST_ASSERT_NOT_NULL(f);
  char line[512];
  while (std::fgets(line, sizeof(line), f)) {
    AiCase c;
    if (isAiCaseSkippable(line) || !parseAiCase(line, c)) continue;
    c.snap.edgeCapture = true;
    c.snap.wallLimit = true;
    c.snap.untouchedMs = 20000;
    total += thinkMicros(c.snap, c.params);
    count++;
  }
  std::fclose(f);
  Rules rules;
  rules.edgeCapture = rules.wallLimit = true;
  rules.untouchedMs = 20000;
  BotParams b1;
  b1.style = BotStyle::Race;
  b1.reactMs = 1000;
  b1.thinkMaxMs = 3000;
  b1.threatMiss = 0.3f;
  for (uint32_t seed = 1; seed <= 2; seed++) {
    for (int level = 1; level < 3; level++) {
      Params p;
      p.level = static_cast<Level>(level);
      SimDriver d(rules, p, seed);
      d.enableBot(b1);
      d.start();
      uint32_t next = 0;
      for (uint32_t t = 0; t < 600000 && d.state().phase != Phase::Over; t += 10) {
        d.advance(10);
        const Game& g = d.game();
        if (g.phase() != Phase::Play || g.clockMs() < next) continue;
        next = g.clockMs() + 1000;
        const State st = d.state();
        total += thinkMicros(g.snapshot(st.headR, st.headC), p);
        count++;
      }
    }
  }
  AiCase worst;
  TEST_ASSERT_TRUE(parseAiCase(
      "HMM....M.......H.M.H...H......H...HM...MM..H..... - 0,0 - - - 4,1.5,200,250,H,0.5,1 RE+RW+RU20000 none",
      worst));
  worstUs = thinkMicros(worst.snap, worst.params);
  printf("think RE+RW+RU20: mean %.0f us over %d positions, worst position %.0f us\n", total / count, count, worstUs);
  TEST_ASSERT_TRUE(count > 100);
  TEST_ASSERT_TRUE(total / count <= 2000.0);
  TEST_ASSERT_TRUE(worstUs <= 10000.0);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_picks_capture);
  RUN_TEST(test_easy_read_lift_blocks_or_flees);
  RUN_TEST(test_no_read_easy_passive_does_nothing);
  RUN_TEST(test_tie_prefers_piece_nearer_head);
  RUN_TEST(test_cooldown_piece_not_moved);
  RUN_TEST(test_hard_does_not_walk_into_two_piece_trap);
  RUN_TEST(test_hard_leaves_before_flank_cooldown_ends);
  RUN_TEST(test_refill_threat_is_read);
  RUN_TEST(test_tie_broken_by_hash);
  RUN_TEST(test_no_safe_move_least_loss);
  RUN_TEST(test_fork_penalty_on_top_three_only);
  RUN_TEST(test_think_time);
  RUN_TEST(test_ai_case_parser);
  RUN_TEST(test_snapshot_fields);
  RUN_TEST(test_golden_cases);
  RUN_TEST(test_wall_limit_move_is_no_candidate);
  RUN_TEST(test_wall_limit_path_through_a_wall_square_is_no_candidate);
  RUN_TEST(test_opening_read_makes_a_move);
  RUN_TEST(test_re_read_takes_an_enclosed_corner);
  RUN_TEST(test_ai_case_rules_field);
  RUN_TEST(test_think_time_with_rules);
  RUN_TEST(test_advance_weight_brings_the_machine_out);
  RUN_TEST(test_ru_read_refreshes_a_piece_going_untouched);
  RUN_TEST(test_ru_read_does_not_open_a_square_next_to_an_untouched_piece);
  RUN_TEST(test_think_time_with_untouched);
  return UNITY_END();
}
