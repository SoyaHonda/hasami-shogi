// Production firmware (env:esp32dev): the game on the board, plus the bring-up
// commands.
//
// Core 1 (loop): HallScanner -> BoardWatcher -> Game / MachineController ->
// Gantry -> LEDs (30 Hz). When MachineController wants to read, the snapshot
// goes to a task pinned to core 0 through a queue of length 1; that task calls
// think() and returns the move through a second queue. Every snapshot sent
// gets exactly one result back (MachineController waits for it).
//
// Serial commands (115200 baud, one per line):
//   scan       print the debounced classification of every square as a grid
//              (. empty, H human / N pole, M machine / S pole) and raw values
//   cal        calibrate(): with no pieces on the board, store each square's
//              no-magnet reading as its centre value
//   home       home the XY stage on the limit switches
//   mv r c     travelTo(r, c) (floats; rows -1..n are allowed)
//   pull r c   grab the piece on the head's square and dragTo the neighbouring
//              square (r, c)
//   golden     run think() on every case of test/golden/ai_cases.txt (compiled
//              in as src/golden_cases.h) and print the ones that disagree
//   ai         run think() now on the current position and print the move
//   cd         check the board against the initial layout and, when it
//              matches, start the game (countdown)
//
// cal, home, mv, pull and golden are refused while a game is in progress.
//
// Results go into hardware/test-log.md.
#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "Ai.h"
#include "AiCase.h"
#include "BoardWatcher.h"
#include "Game.h"
#include "Gantry.h"
#include "HallScanner.h"
#include "LedPlan.h"
#include "Leds.h"
#include "MachineController.h"
#include "Magnet.h"
#include "Params.h"
#include "Rules.h"

#if __has_include("golden_cases.h")
#include "golden_cases.h"
#else
#error "src/golden_cases.h is missing: run `node tools/gen-golden.mjs` before building"
#endif

// think() (the `ai` command) and the golden table need more than the default
// 8 KB loop stack.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

namespace {

constexpr uint32_t THINK_STACK = 16 * 1024;
constexpr UBaseType_t THINK_PRIORITY = 1;
constexpr BaseType_t THINK_CORE = 0;
constexpr uint32_t LED_PERIOD_MS = 33;  // ~30 Hz

const Rules rules;
const Params params;
Magnet magnet;
Leds leds;
HallScanner scanner;
Gantry gantry(magnet, params, rules.n);
Game game(rules);
MachineController controller(game, gantry, params);
BoardWatcher watcher(rules.n);
bool gantryOk = false;

struct ThinkRequest {
  Snapshot snap;
  Params params;
};
QueueHandle_t thinkIn = nullptr;   // ThinkRequest, core 1 -> core 0
QueueHandle_t thinkOut = nullptr;  // AiMove, core 0 -> core 1
bool thinkReady = false;           // both queues and the think task exist
// Static so the large structs stay off the loop stack.
ThinkRequest thinkReq;
uint32_t lastLedMs = 0;

char lineBuf[96];
size_t lineLen = 0;

char cellChar(int8_t v) {
  switch (v) {
    case HUMAN: return 'H';
    case MACHINE: return 'M';
    default: return '.';
  }
}

void setMagnet(MagnetMode m) { gantry.magnet(m); }

bool magnetOn() { return magnet.mode() != MagnetMode::Off; }

// Background work while a command waits: keep scanning with the hold zone.
void service() {
  const uint32_t now = millis();
  gantry.update(now);
  scanner.setHold(magnetOn(), gantry.headR(), gantry.headC());
  scanner.update(now);
}

// Bring-up commands drive the gantry and block, so they only run when no game
// is in progress and the machine is not moving.
bool gameBusy() {
  const Phase ph = game.phase();
  if (ph != Phase::Ready && ph != Phase::Over) {
    Serial.println("error: a game is in progress");
    return true;
  }
  if (!gantry.idle()) {
    Serial.println("error: the gantry is moving");
    return true;
  }
  return false;
}

const char* phaseName(Phase ph) {
  switch (ph) {
    case Phase::Ready: return "Ready";
    case Phase::Countdown: return "Countdown";
    case Phase::Play: return "Play";
    case Phase::Paused: return "Paused";
    case Phase::Removing: return "Removing";
    case Phase::Over: return "Over";
  }
  return "?";
}

// Core 0: think on every snapshot and always send one result back.
void thinkTask(void*) {
  static ThinkRequest req;  // one task, so static is safe and saves stack
  for (;;) {
    if (xQueueReceive(thinkIn, &req, portMAX_DELAY) != pdTRUE) continue;
    const AiMove m = think(req.snap, req.params);
    xQueueSend(thinkOut, &m, portMAX_DELAY);
  }
}

void waitIdle() {
  while (!gantry.idle()) {
    service();
    delay(1);
  }
}

void cmdScan() {
  scanner.scan();
  scanner.scan();  // two scans so the debounced value can settle
  const int8_t* cells = scanner.cells();
  const int n = HallScanner::N;
  Serial.println("   0 1 2 3 4 5 6      raw");
  for (int r = 0; r < n; ++r) {
    Serial.printf("%d ", r);
    for (int c = 0; c < n; ++c) Serial.printf(" %c", cellChar(cells[r * n + c]));
    Serial.print("   ");
    for (int c = 0; c < n; ++c) Serial.printf(" %4d", scanner.raw(r, c));
    Serial.println();
  }
}

void cmdCal() {
  if (gameBusy()) return;
  Serial.println("cal: keep the board empty");
  setMagnet(MagnetMode::Off);
  delay(50);
  scanner.calibrate();
  Serial.println("cal: done, centre values:");
  for (int r = 0; r < HallScanner::N; ++r) {
    for (int c = 0; c < HallScanner::N; ++c) Serial.printf(" %4d", scanner.thresholds(r, c).center);
    Serial.println();
  }
}

void cmdHome() {
  if (gameBusy()) return;
  if (!gantryOk) {
    Serial.println("error: steppers not connected");
    return;
  }
  setMagnet(MagnetMode::Off);
  Serial.println(gantry.home() ? "home: ok" : "home: FAILED (limit switch not reached)");
}

void cmdMv(float r, float c) {
  if (gameBusy()) return;
  if (!gantryOk) {
    Serial.println("error: steppers not connected");
    return;
  }
  if (!gantry.homed()) Serial.println("warning: not homed");
  const uint32_t t0 = millis();
  gantry.travelTo(r, c);
  waitIdle();
  Serial.printf("mv: head at %.2f,%.2f in %lu ms\n", gantry.headR(), gantry.headC(),
                static_cast<unsigned long>(millis() - t0));
}

void cmdPull(int r, int c) {
  if (gameBusy()) return;
  if (!gantryOk) {
    Serial.println("error: steppers not connected");
    return;
  }
  const int fr = static_cast<int>(floorf(gantry.headR() + 0.5f));
  const int fc = static_cast<int>(floorf(gantry.headC() + 0.5f));
  if (abs(r - fr) + abs(c - fc) != 1) {
    Serial.printf("error: %d,%d is not next to the head's square %d,%d\n", r, c, fr, fc);
    return;
  }
  if (r < -1 || r > rules.n || c < 0 || c >= rules.n) {
    Serial.println("error: target outside rows -1..n / columns 0..n-1");
    return;
  }
  // Pull with the polarity of the piece the sensors see (machine if unknown).
  int8_t piece = MACHINE;
  if (fr >= 0 && fr < HallScanner::N && fc >= 0 && fc < HallScanner::N)
    piece = scanner.cells()[fr * HallScanner::N + fc];
  const MagnetMode mode = piece == HUMAN ? MagnetMode::PullHuman : MagnetMode::PullMachine;
  setMagnet(mode);
  const uint32_t t0 = millis();
  while (millis() - t0 < params.engageMs) {
    service();
    delay(1);
  }
  const uint32_t t1 = millis();
  gantry.dragTo(r, c);
  waitIdle();
  const uint32_t t2 = millis();
  setMagnet(MagnetMode::Off);
  Serial.printf("pull %d,%d>%d,%d (%s): %lu ms\n", fr, fc, r, c,
                mode == MagnetMode::PullHuman ? "PullHuman" : "PullMachine",
                static_cast<unsigned long>(t2 - t1));
  Serial.println("check with `scan` that the piece arrived");
}

void cmdGolden() {
  if (gameBusy()) return;
  int failures = 0, bad = 0;
  uint32_t worstUs = 0;
  const uint32_t t0 = millis();
  for (int i = 0; i < GOLDEN_CASE_COUNT; ++i) {
    const GoldenCase& g = GOLDEN_CASES[i];
    AiCase c;
    if (!parseAiCase(g.text, c)) {
      Serial.printf("golden line %d: does not parse\n", g.lineNo);
      bad++;
      continue;
    }
    AiMove m;
    const uint32_t s = micros();
    const bool ok = checkAiCase(c, &m);
    const uint32_t us = micros() - s;
    if (us > worstUs) worstUs = us;
    if (!ok) {
      failures++;
      if (m.none) {
        Serial.printf("golden line %d: got none (score %.3f)\n", g.lineNo, static_cast<double>(m.score));
      } else {
        const PieceView& p = c.snap.pieces[m.pieceId - 1];
        Serial.printf("golden line %d: got %d,%d>%d,%d (score %.3f)\n", g.lineNo, p.r, p.c, m.toR, m.toC,
                      static_cast<double>(m.score));
      }
    }
    delay(1);  // let the idle task run
  }
  Serial.printf("golden: %d cases, %d mismatches, %d unparsable, %lu ms total, worst think %lu us\n",
                GOLDEN_CASE_COUNT, failures, bad, static_cast<unsigned long>(millis() - t0),
                static_cast<unsigned long>(worstUs));
}

void printMove(const Snapshot& snap, const AiMove& m) {
  if (m.none) {
    Serial.printf("none (score %.3f)", static_cast<double>(m.score));
    return;
  }
  const PieceView& p = snap.pieces[m.pieceId - 1];
  Serial.printf("piece %d %d,%d>%d,%d (score %.3f)", m.pieceId, p.r, p.c, m.toR, m.toC,
                static_cast<double>(m.score));
}

void cmdAi() {
  static Snapshot snap;  // keep the large struct off the loop stack
  snap = game.snapshot(gantry.headR(), gantry.headC());
  const uint32_t s = micros();
  const AiMove m = think(snap, params);
  const uint32_t us = micros() - s;
  Serial.printf("ai (%s): ", phaseName(game.phase()));
  printMove(snap, m);
  Serial.printf(", %lu us\n", static_cast<unsigned long>(us));
}

void cmdCd() {
  const Phase ph = game.phase();
  if (ph != Phase::Ready && ph != Phase::Over) {
    Serial.printf("error: game already running (%s)\n", phaseName(ph));
    return;
  }
  if (!gantryOk || !gantry.homed()) {
    Serial.println("error: home the gantry first (`home`)");
    return;
  }
  if (!gantry.idle()) {
    Serial.println("error: the gantry is moving");
    return;
  }
  // The initial layout is the board of a freshly reset game.
  static Game initial(rules);
  initial.reset();
  const Board start = initial.boardWithoutHeld();
  int diff[MAX_N * MAX_N];
  const int k = watcher.diffInitial(scanner.cells(), start.cells, diff);
  if (k > 0) {
    Serial.printf("cd: %d square(s) differ from the initial layout:", k);
    const int n = rules.n;
    for (int i = 0; i < k; ++i) {
      const int r = diff[i] / n, c = diff[i] % n;
      Serial.printf(" %d,%d(%c want %c)", r, c, cellChar(scanner.cells()[diff[i]]), cellChar(start.cells[diff[i]]));
    }
    Serial.println();
    Serial.println("cd: not started");
    return;
  }
  watcher.reset();
  game.start(millis());
  Serial.println("cd: countdown started");
}

void handleLine(char* line) {
  char cmd[16] = {0};
  float a = 0, b = 0;
  const int got = sscanf(line, "%15s %f %f", cmd, &a, &b);
  if (got < 1) return;
  if (strcmp(cmd, "scan") == 0) {
    cmdScan();
  } else if (strcmp(cmd, "cal") == 0) {
    cmdCal();
  } else if (strcmp(cmd, "home") == 0) {
    cmdHome();
  } else if (strcmp(cmd, "mv") == 0 && got == 3) {
    cmdMv(a, b);
  } else if (strcmp(cmd, "pull") == 0 && got == 3) {
    cmdPull(static_cast<int>(lroundf(a)), static_cast<int>(lroundf(b)));
  } else if (strcmp(cmd, "golden") == 0) {
    cmdGolden();
  } else if (strcmp(cmd, "ai") == 0) {
    cmdAi();
  } else if (strcmp(cmd, "cd") == 0) {
    cmdCd();
  } else {
    Serial.println("commands: scan | cal | home | mv r c | pull r c | golden | ai | cd");
  }
}

// Squares the machine is working on, where S changes are expected: the job's
// piece and destination, captured pieces waiting to be carried off, and the
// squares around the head (the piece being dragged passes through them).
void buildMachineCells(bool out[]) {
  const int n = rules.n;
  for (int i = 0; i < n * n; ++i) out[i] = false;
  auto mark = [&](int r, int c) {
    if (r >= 0 && r < n && c >= 0 && c < n) out[r * n + c] = true;
  };
  const JobView j = controller.job();
  if (j.active) {
    if (j.pieceId >= 1 && j.pieceId <= game.pieceCount()) {
      const PieceView& p = game.pieces()[j.pieceId - 1];
      mark(p.r, p.c);
    }
    mark(j.toR, j.toC);
  }
  const Sq res = game.reservedCell();
  mark(res.r, res.c);
  Removal rm[2 * MAX_N];
  const int k = game.removals(rm);
  for (int i = 0; i < k; ++i) mark(rm[i].from.r, rm[i].from.c);
  const float hr = gantry.headR(), hc = gantry.headC();
  const int r0 = static_cast<int>(floorf(hr)), c0 = static_cast<int>(floorf(hc));
  for (int r = r0; r <= r0 + 1; ++r)
    for (int c = c0; c <= c0 + 1; ++c)
      if (fabsf(r - hr) < 1.0f && fabsf(c - hc) < 1.0f) mark(r, c);
}

// Applies the watcher's events to the game.
void handleEvents(uint32_t now) {
  static Board expected;
  static bool machineCells[MAX_N * MAX_N];
  static WatchEvent events[MAX_WATCH_EVENTS];
  expected = game.boardWithoutHeld();
  buildMachineCells(machineCells);
  const int k = watcher.update(scanner.cells(), expected.cells, machineCells, now, events);
  for (int i = 0; i < k; ++i) {
    const WatchEvent& e = events[i];
    switch (e.kind) {
      case WatchEvent::Lift:
        if (game.onHumanLift(e.r, e.c, now) == LiftResult::Ok) controller.onHumanLift(now);
        break;
      case WatchEvent::Place:
        game.onHumanPlace(e.r, e.c, now);
        break;
      case WatchEvent::MachineTouch: {
        const Phase ph = game.phase();
        if (ph != Phase::Ready && ph != Phase::Over) game.reportViolation(e.r, e.c, now);
        break;
      }
    }
  }
  // A captured piece the machine could not carry off (Paused): once its square
  // reads empty, the person has moved it off by hand.
  const int stuck = game.stuckId();
  if (game.phase() == Phase::Paused && stuck >= 1 && stuck <= game.pieceCount()) {
    const PieceView& p = game.pieces()[stuck - 1];
    if (p.r >= 0 && p.r < rules.n && p.c >= 0 && p.c < rules.n && scanner.cells()[p.r * rules.n + p.c] == EMPTY)
      game.onManualRemoval(stuck);
  }
}

// Sends a snapshot to core 0 when the controller wants to read, and hands any
// result back.
void serviceThink(uint32_t now) {
  if (controller.wantsThink(now)) {
    thinkReq.snap = controller.takeSnapshot(now);
    thinkReq.params = controller.params();
    if (!thinkReady || xQueueSend(thinkIn, &thinkReq, 0) != pdTRUE) {
      // No think task, or the queue is full (one request at a time, so it should
      // not be): never leave the controller waiting.
      AiMove none{};
      none.none = true;
      controller.onThinkResult(none, now);
    }
  }
  AiMove m;
  if (thinkReady && xQueueReceive(thinkOut, &m, 0) == pdTRUE) controller.onThinkResult(m, millis());
}

void serviceGame() {
  const uint32_t now = millis();
  scanner.update(now);
  handleEvents(now);
  game.tick(now);
  controller.update(now);
  gantry.update(now);
  scanner.setHold(magnetOn(), gantry.headR(), gantry.headC());
  serviceThink(now);
  if (now - lastLedMs >= LED_PERIOD_MS) {
    lastLedMs = now;
    static Rgb colors[MAX_N * MAX_N];
    ledPlan(game, controller, now, colors);
    leds.show(colors, rules.n);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  magnet.begin();
  leds.begin(rules.n);
  leds.clear();
  scanner.begin();
  gantryOk = gantry.begin();
  if (!gantryOk) Serial.println("error: could not connect the steppers");
  if (rules.n != HallScanner::N) Serial.printf("error: Rules::n %d but the sensors cover %d\n", rules.n, HallScanner::N);
  thinkIn = xQueueCreate(1, sizeof(ThinkRequest));
  thinkOut = xQueueCreate(1, sizeof(AiMove));
  thinkReady = thinkIn && thinkOut &&
               xTaskCreatePinnedToCore(thinkTask, "think", THINK_STACK, nullptr, THINK_PRIORITY, nullptr,
                                       THINK_CORE) == pdPASS;
  // Without the task a queued snapshot would never be answered; serviceThink
  // then answers every read with "no move" so the controller never stalls.
  if (!thinkReady) Serial.println("error: could not start the think task");
  Serial.println("hasami firmware: scan | cal | home | mv r c | pull r c | golden | ai | cd");
}

void loop() {
  while (Serial.available() > 0) {
    char ch = static_cast<char>(Serial.read());
    if (ch == '\r') continue;
    if (ch == '\n') {
      lineBuf[lineLen] = '\0';
      handleLine(lineBuf);
      lineLen = 0;
    } else if (lineLen + 1 < sizeof(lineBuf)) {
      lineBuf[lineLen++] = ch;
    }
  }
  serviceGame();
  delay(1);
}
