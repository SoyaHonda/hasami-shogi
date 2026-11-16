// The learning environment of plan 2026-11-12 section 4: action numbering, features, the mask
// against the read's candidates, the same game for the same seed, SimDriver's game when the read
// plays, the reward and the automatic reset; and the human played from outside (DuelEnv, 7.3).
#include <unity.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "BatchEnv.h"
#include "Features.h"
#include "SimDriver.h"

void setUp() {}
void tearDown() {}

static int16_t planes[FEAT_PLANES];
static int16_t scal[FEAT_SCALARS];

static int16_t at(int ch, int r, int c) { return planes[ch * FEAT_SQUARES + r * FEAT_N + c]; }

static int planeSum(int ch) {
  int s = 0;
  for (int i = 0; i < FEAT_SQUARES; i++) s += planes[ch * FEAT_SQUARES + i];
  return s;
}

// A legal action drawn from rng.
static int randomLegal(const EnvSlot& e, Rng& rng) {
  int k = rng.below(e.maskCount());
  for (int a = 0; a < ACTION_COUNT; a++)
    if (e.mask()[a] && k-- == 0) return a;
  return -1;
}

// ---- Action numbering.

void test_action_numbering() {
  TEST_ASSERT_EQUAL_INT(1176, ACTION_STAY);
  TEST_ASSERT_EQUAL_INT(1177, ACTION_COUNT);
  TEST_ASSERT_EQUAL_INT(0, actionIndex(0, 0, 0, 1));
  TEST_ASSERT_EQUAL_INT(1175, actionIndex(6, 6, 3, 6));
  for (int a = 0; a < ACTION_STAY; a++) {
    int r, c, d, dist;
    TEST_ASSERT_TRUE(decodeAction(a, r, c, d, dist));
    TEST_ASSERT_EQUAL_INT(a, actionIndex(r, c, d, dist));
  }
  int r, c, d, dist;
  TEST_ASSERT_FALSE(decodeAction(ACTION_STAY, r, c, d, dist));
  TEST_ASSERT_FALSE(decodeAction(-1, r, c, d, dist));
  // Directions in Board.h's order: 0 down, 1 up, 2 right, 3 left.
  TEST_ASSERT_EQUAL_INT(actionIndex(2, 3, 0, 2), actionOfMove(2, 3, 4, 3));
  TEST_ASSERT_EQUAL_INT(actionIndex(2, 3, 1, 1), actionOfMove(2, 3, 1, 3));
  TEST_ASSERT_EQUAL_INT(actionIndex(2, 3, 2, 3), actionOfMove(2, 3, 2, 6));
  TEST_ASSERT_EQUAL_INT(actionIndex(2, 3, 3, 3), actionOfMove(2, 3, 2, 0));
  TEST_ASSERT_EQUAL_INT(-1, actionOfMove(2, 3, 3, 4));
  TEST_ASSERT_EQUAL_INT(-1, actionOfMove(2, 3, 2, 3));
}

// ---- Features.

void test_speed_scale_config() {
  const EnvConfig fast = envConfig(EnvOpponent::B1, 1.3f, Level::Hard);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 5.2f, fast.params.freeSpeed);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.95f, fast.params.dragSpeed);
  TEST_ASSERT_EQUAL_UINT32(154, fast.params.engageMs);  // 200 / 1.3: a faster machine engages sooner
  TEST_ASSERT_EQUAL_INT(130, fast.speedScale100);
  const EnvConfig slow = envConfig(EnvOpponent::Race, 0.7f, Level::Normal);
  TEST_ASSERT_EQUAL_UINT32(286, slow.params.engageMs);
  TEST_ASSERT_EQUAL_INT(70, slow.speedScale100);
  TEST_ASSERT_TRUE(slow.rules.edgeCapture && slow.rules.wallLimit);
  TEST_ASSERT_EQUAL_UINT32(40000, slow.rules.untouchedMs);
  // The environment plays the official rules; Rules() stays R0.
  const Rules official = officialRules();
  TEST_ASSERT_TRUE(official.edgeCapture && official.wallLimit);
  TEST_ASSERT_EQUAL_UINT32(40000, official.untouchedMs);
  TEST_ASSERT_TRUE(slow.rules.edgeCapture == official.edgeCapture && slow.rules.wallLimit == official.wallLimit);
  TEST_ASSERT_EQUAL_UINT32(official.untouchedMs, slow.rules.untouchedMs);
  TEST_ASSERT_EQUAL_UINT32(official.timeLimitMs, slow.rules.timeLimitMs);
  TEST_ASSERT_EQUAL_INT(official.target, slow.rules.target);
  TEST_ASSERT_FALSE(Rules().edgeCapture || Rules().wallLimit || Rules().untouchedMs > 0);
  TEST_ASSERT_EQUAL_UINT32(600000, slow.rules.timeLimitMs);
  TEST_ASSERT_EQUAL_INT(4, slow.rules.target);
  TEST_ASSERT_EQUAL_UINT32(1000, fast.bot.reactMs);
  TEST_ASSERT_EQUAL_UINT32(3000, fast.bot.thinkMaxMs);
  TEST_ASSERT_TRUE(envConfig(EnvOpponent::Random, 1.0f, Level::Hard).bot.style == BotStyle::Random);
}

// The first decision point is in the opening: "stay" is not legal, every machine piece can go down
// 1-5 squares, and the planes show the starting position.
void test_first_decision_in_opening() {
  EnvSlot e(envConfig(EnvOpponent::B1, 1.0f, Level::Hard));
  e.reset(7);
  TEST_ASSERT_TRUE(e.game().phase() == Phase::Opening);
  TEST_ASSERT_FALSE(e.mask()[ACTION_STAY]);
  TEST_ASSERT_EQUAL_INT(35, e.maskCount());
  for (int c = 0; c < 7; c++)
    for (int dist = 1; dist <= 5; dist++) TEST_ASSERT_TRUE(e.mask()[actionIndex(0, c, 0, dist)]);
  e.features(planes, scal);
  for (int c = 0; c < 7; c++) {
    TEST_ASSERT_EQUAL_INT(1, at(0, 0, c));
    TEST_ASSERT_EQUAL_INT(1, at(1, 6, c) + at(2, 6, c));  // on the board or lifted for the opening
    TEST_ASSERT_EQUAL_INT(400, at(7, 0, c));               // RU40, nothing has moved yet
  }
  TEST_ASSERT_EQUAL_INT(7, planeSum(0));
  TEST_ASSERT_EQUAL_INT(1, planeSum(9));
  TEST_ASSERT_EQUAL_INT(1, at(9, 0, 3));  // the head starts at (0, n/2)
  TEST_ASSERT_EQUAL_INT(0, scal[0]);
  TEST_ASSERT_EQUAL_INT(0, scal[1]);
  TEST_ASSERT_EQUAL_INT(6000, scal[2]);
  TEST_ASSERT_EQUAL_INT(1, scal[3]);
  TEST_ASSERT_EQUAL_INT(100, scal[5]);
}

// Held origin, pending target, the placement interval, hold time and cooldowns on a plain game.
void test_features_human_timing() {
  Rules rules;
  Game g(rules);
  g.start(0);
  g.tick(3000);  // countdown over: Play, game clock 0
  TEST_ASSERT_TRUE(g.phase() == Phase::Play);
  TEST_ASSERT_TRUE(g.onHumanLift(6, 0, 3100) == LiftResult::Ok);
  g.tick(3550);
  buildFeatures(g, 0.0f, 3.0f, 100, planes, scal);
  TEST_ASSERT_EQUAL_INT(1, at(2, 6, 0));
  TEST_ASSERT_EQUAL_INT(0, at(1, 6, 0));
  TEST_ASSERT_EQUAL_INT(4, at(10, 6, 0));  // up for 450 ms
  TEST_ASSERT_EQUAL_INT(0, planeSum(4));   // no placement yet
  TEST_ASSERT_EQUAL_INT(1, scal[4]);       // no opening without RW
  TEST_ASSERT_TRUE(g.onHumanPlace(5, 0, 3600) == PlaceResult::Ok);  // lands at clock 600
  g.tick(3701);
  buildFeatures(g, 0.0f, 3.0f, 100, planes, scal);
  TEST_ASSERT_EQUAL_INT(1, at(1, 5, 0));
  TEST_ASSERT_EQUAL_INT(29, at(6, 5, 0));  // 3000 ms cooldown from 600, 2899 ms left
  for (int i = 0; i < FEAT_SQUARES; i++) TEST_ASSERT_EQUAL_INT(9, planes[4 * FEAT_SQUARES + i]);
  TEST_ASSERT_EQUAL_INT(0, planeSum(7) + planeSum(8));  // RU off
  TEST_ASSERT_TRUE(g.onHumanLift(6, 1, 3750) == LiftResult::Ok);
  TEST_ASSERT_TRUE(g.onHumanPlace(5, 1, 3800) == PlaceResult::Pending);
  buildFeatures(g, 0.0f, 3.0f, 100, planes, scal);
  TEST_ASSERT_EQUAL_INT(1, at(3, 5, 1));
  TEST_ASSERT_EQUAL_INT(1, at(2, 6, 1));
  TEST_ASSERT_EQUAL_INT(1, planeSum(3));
}

// Channel 11 marks the pieces of an edge group with one empty neighbour left (either side).
void test_features_edge_hint() {
  Rules rules;
  rules.edgeCapture = true;
  Game g(rules);
  g.start(0);
  g.tick(3000);
  // Machine pieces (odd ids 3..13) leave row 0 for the middle; (0,0) stays alone in the corner.
  const int to[6][2] = {{2, 1}, {2, 2}, {2, 3}, {2, 4}, {2, 5}, {4, 3}};
  for (int k = 0; k < 6; k++) g.onMachineStep(3 + 2 * k, to[k][0], to[k][1]);
  g.onMachineStep(2, 0, 1);  // a human piece next to the corner piece: (1,0) is its last liberty
  buildFeatures(g, 0.0f, 3.0f, 100, planes, scal);
  TEST_ASSERT_EQUAL_INT(1, at(11, 0, 0));
  TEST_ASSERT_EQUAL_INT(1, planeSum(11));
  // A machine piece on (1,1) leaves the human piece on (0,1) one liberty, (0,2), too. The machine
  // group of (1,1) and row 2 does not touch the edge, so it is not marked.
  g.onMachineStep(13, 1, 1);
  buildFeatures(g, 0.0f, 3.0f, 100, planes, scal);
  TEST_ASSERT_EQUAL_INT(1, at(11, 0, 1));
  TEST_ASSERT_EQUAL_INT(1, at(11, 0, 0));
  TEST_ASSERT_EQUAL_INT(2, planeSum(11));
}

// ---- The mask is the read's candidate set.

void test_mask_matches_read_candidates() {
  EnvSlot e(envConfig(EnvOpponent::Race, 1.0f, Level::Normal));
  e.reset(3);
  Rng rng(99);
  static AiCandidate cands[AI_MAX_CANDIDATES];
  int checked = 0, games = 0;
  while (checked < 300) {
    const Snapshot& s = e.snapshot();
    const int k = thinkCandidates(s, Params(), cands, AI_MAX_CANDIDATES);
    uint8_t fromRead[ACTION_COUNT];
    std::memset(fromRead, 0, sizeof(fromRead));
    for (int i = 0; i < k; i++) {
      if (cands[i].none) continue;
      const PieceView& p = s.pieces[cands[i].pieceId - 1];
      const int a = actionOfMove(p.r, p.c, cands[i].toR, cands[i].toC);
      TEST_ASSERT_TRUE(a >= 0);
      fromRead[a] = 1;
    }
    fromRead[ACTION_STAY] = s.mustMove ? 0 : 1;
    TEST_ASSERT_EQUAL_UINT8_ARRAY(fromRead, e.mask(), ACTION_COUNT);
    checked++;
    if (e.step(randomLegal(e, rng)).done) games++;
  }
  TEST_ASSERT_TRUE(games >= 1);
}

// ---- Determinism, reset and the reward.

void test_same_seed_same_game() {
  const EnvConfig cfg = envConfig(EnvOpponent::B1, 1.0f, Level::Hard);
  EnvSlot a(cfg), b(cfg);
  a.reset(11);
  b.reset(11);
  Rng ra(5), rb(5);
  int16_t pa[FEAT_PLANES], pb[FEAT_PLANES], sa[FEAT_SCALARS], sb[FEAT_SCALARS];
  int dones = 0;
  for (int i = 0; i < 400; i++) {
    a.features(pa, sa);
    b.features(pb, sb);
    TEST_ASSERT_EQUAL_INT16_ARRAY(pa, pb, FEAT_PLANES);
    TEST_ASSERT_EQUAL_INT16_ARRAY(sa, sb, FEAT_SCALARS);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(a.mask(), b.mask(), ACTION_COUNT);
    const EnvStep x = a.step(randomLegal(a, ra));
    const EnvStep y = b.step(randomLegal(b, rb));
    TEST_ASSERT_EQUAL_FLOAT(x.reward, y.reward);
    TEST_ASSERT_EQUAL_UINT32(x.dtMs, y.dtMs);
    TEST_ASSERT_EQUAL(x.done, y.done);
    if (x.done) dones++;
  }
  TEST_ASSERT_TRUE(dones >= 2);
}

// After the end of a game the slot is at the first decision point of the next one, whose seed is
// the previous one + the stride; the step carries the finished game.
void test_auto_reset() {
  EnvSlot e(envConfig(EnvOpponent::Random, 1.0f, Level::Hard));
  e.reset(20, 8);
  Rng rng(1);
  float total = 0.0f;
  for (int i = 0; i < 5000; i++) {
    const EnvStep st = e.step(randomLegal(e, rng));
    total += st.reward;
    TEST_ASSERT_TRUE(st.dtMs > 0);
    if (!st.done) continue;
    TEST_ASSERT_TRUE(st.info.winner == 1 || st.info.winner == 2 || st.info.winner == 0);
    // Four captures win, or more captures than the other side at the time limit.
    const bool limit = st.info.clockMs >= 600000;
    if (st.info.winner == 2)
      TEST_ASSERT_TRUE(st.info.capturedMachine >= 4 || (limit && st.info.capturedMachine > st.info.capturedHuman));
    if (st.info.winner == 1)
      TEST_ASSERT_TRUE(st.info.capturedHuman >= 4 || (limit && st.info.capturedHuman > st.info.capturedMachine));
    // The whole game's reward: the result and 0.2 per piece of difference, less the draw penalty.
    const float result = st.info.winner == 2 ? 1.0f : st.info.winner == 1 ? -1.0f : 0.0f;
    const float caps = 0.2f * static_cast<float>(st.info.capturedMachine - st.info.capturedHuman);
    const float penaltyMax = 0.5f / 600.0f * static_cast<float>(st.info.clockMs) / 1000.0f;
    TEST_ASSERT_TRUE(total <= result + caps + 1e-4f);
    TEST_ASSERT_TRUE(total >= result + caps - penaltyMax - 1e-4f);
    TEST_ASSERT_EQUAL_UINT32(28, e.seed());
    e.features(planes, scal);
    TEST_ASSERT_EQUAL_INT(0, scal[0] + scal[1]);
    TEST_ASSERT_EQUAL_INT(1, scal[3]);
    TEST_ASSERT_EQUAL_INT(6000, scal[2]);
    TEST_ASSERT_EQUAL_INT(0, e.info().capturedHuman + e.info().capturedMachine);
    return;
  }
  TEST_FAIL_MESSAGE("no game ended");
}

// Without captures, a game that reaches the time limit pays -0.5/600 per second of play.
void test_draw_penalty() {
  EnvConfig cfg = envConfig(EnvOpponent::Random, 1.0f, Level::Hard);
  cfg.rules.timeLimitMs = 6000;
  EnvSlot e(cfg);
  e.reset(1);
  float total = 0.0f;
  for (int i = 0; i < 1000; i++) {
    // Stay whenever allowed (the opening move: the first legal one).
    int a = ACTION_STAY;
    if (!e.mask()[a])
      for (a = 0; !e.mask()[a]; a++) {
      }
    const EnvStep st = e.step(a);
    total += st.reward;
    if (!st.done) continue;
    TEST_ASSERT_EQUAL_INT(0, st.info.capturedHuman + st.info.capturedMachine);
    TEST_ASSERT_EQUAL_INT(0, st.info.winner);
    TEST_ASSERT_FALSE(st.info.truncated);
    TEST_ASSERT_EQUAL_UINT32(6000, st.info.clockMs);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -0.5f / 600.0f * 6.0f, total);
    return;
  }
  TEST_FAIL_MESSAGE("no game ended");
}

// ---- With the read in place of the learner, the game is SimDriver's for the same seed.

static void compareWithSimDriver(EnvOpponent opp, uint32_t seed) {
  const EnvConfig cfg = envConfig(opp, 1.0f, Level::Hard);
  EnvSlot e(cfg);
  e.reset(seed);
  EnvStep last{};
  for (int i = 0; i < 100000 && !last.done; i++) last = e.step(e.readAction(Level::Hard));
  TEST_ASSERT_TRUE(last.done);

  SimDriver d(cfg.rules, cfg.params, seed);
  d.enableBot(cfg.bot);
  d.start();
  uint32_t simMs = 0;
  while (d.game().phase() != Phase::Over && simMs < cfg.maxSimMs) {
    d.advance(10);
    simMs += 10;
  }
  const State s = d.state();
  const int winner = s.winner == Winner::Human ? 1 : s.winner == Winner::Machine ? 2 : 0;
  TEST_ASSERT_EQUAL_INT(winner, last.info.winner);
  TEST_ASSERT_EQUAL_INT(s.capturedMachine, last.info.capturedMachine);
  TEST_ASSERT_EQUAL_INT(s.capturedHuman, last.info.capturedHuman);
  TEST_ASSERT_EQUAL_UINT32(s.clockMs, last.info.clockMs);
  TEST_ASSERT_EQUAL_INT(s.activity.moves, last.info.activity.moves);
  TEST_ASSERT_EQUAL_INT(s.activity.threats, last.info.activity.threats);
  TEST_ASSERT_TRUE(s.activity.outsideAcc == last.info.activity.outsideAcc);
}

void test_read_matches_simdriver_b1() {
  for (uint32_t seed = 1; seed <= 3; seed++) compareWithSimDriver(EnvOpponent::B1, seed);
}

void test_read_matches_simdriver_race() {
  for (uint32_t seed = 1; seed <= 3; seed++) compareWithSimDriver(EnvOpponent::Race, seed);
}

// ---- BatchEnv seeds.

void test_batch_seeds() {
  BatchEnv b(4, 100, envConfig(EnvOpponent::Random, 1.0f, Level::Hard));
  b.resetRange(0, 4);
  for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_UINT32(100 + i, b.slot(i).seed());
  static int16_t pl[4 * FEAT_PLANES], sc[4 * FEAT_SCALARS];
  static uint8_t mk[4 * ACTION_COUNT];
  b.observeRange(0, 4, pl, sc, mk);
  for (int i = 0; i < 4; i++) {
    TEST_ASSERT_EQUAL_INT(6000, sc[i * FEAT_SCALARS + 2]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(b.slot(i).mask(), mk + i * ACTION_COUNT, ACTION_COUNT);
  }
  // Slot 1's next game uses 101 + 4.
  Rng rng(3);
  int32_t act[4];
  EnvStep out[4];
  for (int k = 0; k < 20000; k++) {
    for (int i = 0; i < 4; i++) act[i] = randomLegal(b.slot(i), rng);
    b.stepRange(0, 4, act, out);
    if (out[1].done) {
      TEST_ASSERT_EQUAL_UINT32(105, b.slot(1).seed());
      return;
    }
  }
  TEST_FAIL_MESSAGE("slot 1 never finished a game");
}

// ---- A human played from outside (DuelEnv, plan 2026-11-12 7.3).

void test_duel_config() {
  const EnvConfig c = envConfig(EnvOpponent::External, 1.0f, Level::Hard);
  TEST_ASSERT_TRUE(c.externalHuman);
  TEST_ASSERT_TRUE(c.bot.style == BotStyle::External);
  TEST_ASSERT_EQUAL_UINT32(400, c.bot.reactMs);  // Race's hand
  TEST_ASSERT_EQUAL_UINT32(500, c.bot.moveMs);
  TEST_ASSERT_EQUAL_UINT32(250, c.bot.externalWaitMs);
  TEST_ASSERT_FALSE(envConfig(EnvOpponent::B1, 1.0f, Level::Hard).externalHuman);
  TEST_ASSERT_TRUE(envConfig(EnvOpponent::Race3, 1.0f, Level::Hard).bot.style == BotStyle::Race3);
}

// Random play of both sides: each decision point is the side's own, with its mask; the human
// decides only with an empty hand, no sooner than the hand allows, and its moves land where it
// chose; the end gives both sides the win and loss.
void test_duel_rules_under_random_play() {
  const EnvConfig cfg = envConfig(EnvOpponent::External, 1.0f, Level::Hard);
  static uint8_t m2[ACTION_COUNT];
  int games = 0, humanDecisions = 0, machineDecisions = 0, humanMoves = 0, landed = 0, capsH = 0, capsM = 0;
  for (uint32_t seed = 1; seed <= 3; seed++) {
    EnvSlot e(cfg);
    e.reset(seed);
    Rng rng(seed * 7);
    bool waited = false;
    uint32_t waitedAt = 0;
    int movedId = -1, toR = -1, toC = -1, fromR = -1, fromC = -1;
    int seedGames = 0;
    for (int k = 0; k < 40000 && seedGames < 2; k++) {
      const Game& g = e.game();
      TEST_ASSERT_TRUE(e.maskCount() >= 2);
      int a;
      if (e.side() == SIDE_HUMAN) {
        humanDecisions++;
        TEST_ASSERT_EQUAL_INT(0, g.heldCount());
        TEST_ASSERT_TRUE(g.pendingId() < 0);
        TEST_ASSERT_EQUAL_INT(e.maskCount(), buildHumanMask(g, m2));
        TEST_ASSERT_EQUAL_UINT8_ARRAY(m2, e.mask(), ACTION_COUNT);
        TEST_ASSERT_EQUAL_INT(ACTION_STAY, e.readAction(Level::Hard));
        if (g.phase() == Phase::Play && g.hasLastPlace())
          TEST_ASSERT_TRUE(g.clockMs() - g.lastPlaceMs() >= cfg.bot.reactMs);
        if (waited) TEST_ASSERT_TRUE(e.now() - waitedAt >= cfg.bot.externalWaitMs);
        // The previous move landed on its square (or went back when the square was taken).
        if (movedId > 0) {
          const PieceView& q = g.pieces()[movedId - 1];
          if (q.alive) {
            TEST_ASSERT_FALSE(q.held);
            const bool atTarget = q.r == toR && q.c == toC, atOrigin = q.r == fromR && q.c == fromC;
            TEST_ASSERT_TRUE(atTarget || atOrigin);
            if (atTarget) landed++;
          }
          movedId = -1;
        }
        waited = false;
        a = randomLegal(e, rng);
        if (a == ACTION_STAY) {
          waited = true;
          waitedAt = e.now();
        } else {
          const BotMove mv = humanMoveOfAction(g, a);
          TEST_ASSERT_TRUE(mv.kind == BotMove::Move);
          const PieceView& q = g.pieces()[mv.id - 1];
          TEST_ASSERT_TRUE(g.phase() == Phase::Opening || g.clockMs() >= q.cooldownUntil);
          movedId = mv.id;
          fromR = q.r;
          fromC = q.c;
          toR = mv.toR;
          toC = mv.toC;
          humanMoves++;
        }
      } else {
        machineDecisions++;
        TEST_ASSERT_EQUAL_INT(e.maskCount(), buildMask(g, m2));
        TEST_ASSERT_EQUAL_UINT8_ARRAY(m2, e.mask(), ACTION_COUNT);
        a = randomLegal(e, rng);
      }
      const DuelStep st = e.stepDuel(a);
      if (!st.done) continue;
      games++;
      seedGames++;
      capsH += st.info.capturedHuman;
      capsM += st.info.capturedMachine;
      if (st.info.winner == 2)
        TEST_ASSERT_TRUE(st.finalReward[SIDE_MACHINE] > 0.9f && st.finalReward[SIDE_HUMAN] < -0.9f);
      if (st.info.winner == 1)
        TEST_ASSERT_TRUE(st.finalReward[SIDE_HUMAN] > 0.9f && st.finalReward[SIDE_MACHINE] < -0.9f);
      waited = false;
      movedId = -1;
    }
  }
  char msg[160];
  std::snprintf(msg, sizeof(msg), "games %d, decisions human %d machine %d, human moves %d landed %d, caps h %d m %d",
                games, humanDecisions, machineDecisions, humanMoves, landed, capsH, capsM);
  TEST_MESSAGE(msg);
  TEST_ASSERT_EQUAL_INT(6, games);
  TEST_ASSERT_TRUE(humanDecisions > 100 && machineDecisions > 100);
  TEST_ASSERT_TRUE(capsH > 0 && capsM > 0);
  // Nearly every move lands where it was sent (a taken square sends it back).
  TEST_ASSERT_TRUE(landed * 10 >= humanMoves * 9);
}

// The human's view: its own pieces on plane 0, the machine's on plane 1, its captures first.
void test_duel_human_features() {
  EnvSlot e(envConfig(EnvOpponent::External, 1.0f, Level::Hard));
  e.reset(5);
  Rng rng(1);
  for (int k = 0; k < 2000 && e.side() != SIDE_HUMAN; k++) e.stepDuel(randomLegal(e, rng));
  TEST_ASSERT_EQUAL_INT(SIDE_HUMAN, e.side());
  e.features(planes, scal);
  int humans = 0, machines = 0;
  for (int i = 0; i < e.game().pieceCount(); i++) {
    const PieceView& q = e.game().pieces()[i];
    if (!q.alive || q.held) continue;
    TEST_ASSERT_EQUAL_INT16(1, at(q.side == HUMAN ? 0 : 1, q.r, q.c));
    (q.side == HUMAN ? humans : machines)++;
  }
  TEST_ASSERT_EQUAL_INT(humans, planeSum(0));
  TEST_ASSERT_EQUAL_INT(machines, planeSum(1));
  TEST_ASSERT_EQUAL_INT(1, planeSum(9));
  TEST_ASSERT_EQUAL_INT16(e.game().captured(HUMAN), scal[0]);
  TEST_ASSERT_EQUAL_INT16(e.game().captured(MACHINE), scal[1]);
  TEST_ASSERT_EQUAL_INT16(100, scal[5]);
}

void test_duel_same_seed_same_game() {
  const EnvConfig cfg = envConfig(EnvOpponent::External, 1.0f, Level::Hard);
  EnvSlot a(cfg), b(cfg);
  a.reset(11);
  b.reset(11);
  Rng ra(4), rb(4);
  for (int k = 0; k < 3000; k++) {
    TEST_ASSERT_EQUAL_INT(a.side(), b.side());
    TEST_ASSERT_EQUAL_UINT32(a.now(), b.now());
    const DuelStep sa = a.stepDuel(randomLegal(a, ra));
    const DuelStep sb = b.stepDuel(randomLegal(b, rb));
    TEST_ASSERT_TRUE(sa.reward == sb.reward);
    TEST_ASSERT_EQUAL_UINT32(sa.dtMs, sb.dtMs);
    TEST_ASSERT_EQUAL(sa.done, sb.done);
  }
}

// The hand-written read for the machine against an outside human: every move of the read is in
// the machine's mask, and a human that only waits loses.
void test_duel_read_against_waiting_human() {
  EnvSlot e(envConfig(EnvOpponent::External, 1.0f, Level::Hard));
  e.reset(2);
  Rng rng(9);
  for (int k = 0; k < 200000; k++) {
    int a;
    if (e.side() == SIDE_MACHINE) {
      a = e.readAction(Level::Hard);
      TEST_ASSERT_TRUE(e.mask()[a]);
    } else {
      a = e.mask()[ACTION_STAY] ? ACTION_STAY : randomLegal(e, rng);
    }
    const DuelStep st = e.stepDuel(a);
    if (st.done) {
      TEST_ASSERT_EQUAL_INT(2, st.info.winner);
      return;
    }
  }
  TEST_FAIL_MESSAGE("the game did not end");
}

void test_race3_opponent() {
  EnvSlot e(envConfig(EnvOpponent::Race3, 1.0f, Level::Hard));
  e.reset(1);
  for (int k = 0; k < 200000; k++) {
    if (e.step(e.readAction(Level::Hard)).done) return;
  }
  TEST_FAIL_MESSAGE("the game did not end");
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_action_numbering);
  RUN_TEST(test_speed_scale_config);
  RUN_TEST(test_first_decision_in_opening);
  RUN_TEST(test_features_human_timing);
  RUN_TEST(test_features_edge_hint);
  RUN_TEST(test_mask_matches_read_candidates);
  RUN_TEST(test_same_seed_same_game);
  RUN_TEST(test_auto_reset);
  RUN_TEST(test_draw_penalty);
  RUN_TEST(test_read_matches_simdriver_b1);
  RUN_TEST(test_read_matches_simdriver_race);
  RUN_TEST(test_batch_seeds);
  RUN_TEST(test_duel_config);
  RUN_TEST(test_duel_rules_under_random_play);
  RUN_TEST(test_duel_human_features);
  RUN_TEST(test_duel_same_seed_same_game);
  RUN_TEST(test_duel_read_against_waiting_human);
  RUN_TEST(test_race3_opponent);
  return UNITY_END();
}
