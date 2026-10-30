// Bring-up firmware (env:esp32dev). The game is not connected yet (Step 10).
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
//
// Results go into hardware/test-log.md.
#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "AiCase.h"
#include "Gantry.h"
#include "HallScanner.h"
#include "Leds.h"
#include "Magnet.h"
#include "Params.h"
#include "Rules.h"

#if __has_include("golden_cases.h")
#include "golden_cases.h"
#else
#error "src/golden_cases.h is missing: run `node tools/gen-golden.mjs` before building"
#endif

// think() and the golden table need more than the default 8 KB loop stack.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

namespace {

const Rules rules;
Params params;
Magnet magnet;
Leds leds;
HallScanner scanner;
Gantry gantry(magnet, params, rules.n);
bool gantryOk = false;
bool magnetOn = false;

char lineBuf[96];
size_t lineLen = 0;

char cellChar(int8_t v) {
  switch (v) {
    case HUMAN: return 'H';
    case MACHINE: return 'M';
    default: return '.';
  }
}

void setMagnet(MagnetMode m) {
  gantry.magnet(m);
  magnetOn = m != MagnetMode::Off;
}

// Background work while a command waits: keep scanning with the hold zone.
void service() {
  const uint32_t now = millis();
  gantry.update(now);
  scanner.setHold(magnetOn, gantry.headR(), gantry.headC());
  scanner.update(now);
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
  if (!gantryOk) {
    Serial.println("error: steppers not connected");
    return;
  }
  setMagnet(MagnetMode::Off);
  Serial.println(gantry.home() ? "home: ok" : "home: FAILED (limit switch not reached)");
}

void cmdMv(float r, float c) {
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
  } else {
    Serial.println("commands: scan | cal | home | mv r c | pull r c | golden");
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
  Serial.println("bring-up firmware: scan | cal | home | mv r c | pull r c | golden");
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
  service();
  delay(1);
}
