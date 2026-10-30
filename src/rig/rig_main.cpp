// One-square prototype measurement firmware (env:rig).
//
// Serial commands (115200 baud, one per line):
//   pull v a   magnet PullMachine, move the X axis one square (33.3 mm) out
//              and back at v mm/s with acceleration a mm/s^2
//   hall       print the prototype sensor's raw value and classify() at
//              10 Hz; send anything to stop
//   pass d     baseline the sensor with the magnet off, energize it, move the
//              head d mm and back, and print the largest |raw - baseline|
//   led b      light the prototype square's LED red and blue alternately at
//              brightness b (0-255); send anything to stop
//
// Results go into hardware/test-log.md.
#include <Arduino.h>
#include <FastAccelStepper.h>
#include <stdio.h>
#include <stdlib.h>
#include "HallClassify.h"
#include "Leds.h"
#include "Magnet.h"
#include "Pins.h"

namespace {

constexpr float STEPS_PER_MM = 80.0f;  // GT2 20T, 200 step/rev, 1/16
constexpr float SQUARE_MM = 33.3f;
// Head speed for `pass` (the travel speed, not the drag speed).
constexpr float PASS_SPEED_MMS = 100.0f;
constexpr float PASS_ACCEL_MMS2 = 1000.0f;
constexpr uint32_t HALL_PERIOD_MS = 100;  // 10 Hz
constexpr uint32_t LED_PERIOD_MS = 1000;
// The prototype square's sensor: mux 0, channel 0 (select lines held low),
// or the sensor wired straight to HALL_ADC[0].
constexpr int PROTO_MUX = 0;
constexpr int HALL_SAMPLES = 4;

FastAccelStepperEngine engine;
FastAccelStepper* stepperX = nullptr;
Magnet magnet;
Leds leds;
HallThresholds thresholds;
Cell hallPrev = EMPTY;

int readHall() {
  long sum = 0;
  for (int i = 0; i < HALL_SAMPLES; ++i) sum += analogRead(pins::HALL_ADC[PROTO_MUX]);
  return static_cast<int>(sum / HALL_SAMPLES);
}

const char* cellName(Cell c) {
  switch (c) {
    case HUMAN: return "N(human)";
    case MACHINE: return "S(machine)";
    default: return "empty";
  }
}

bool inputPending() { return Serial.available() > 0; }

void drainInput() {
  delay(20);
  while (Serial.available() > 0) Serial.read();
}

int32_t mmToSteps(float mm) { return static_cast<int32_t>(lroundf(mm * STEPS_PER_MM)); }

bool setMotion(float v, float a) {
  if (!stepperX) {
    Serial.println("error: stepper not connected");
    return false;
  }
  if (v <= 0 || a <= 0) {
    Serial.println("error: v and a must be > 0");
    return false;
  }
  stepperX->setSpeedInHz(static_cast<uint32_t>(lroundf(v * STEPS_PER_MM)));
  stepperX->setAcceleration(static_cast<int32_t>(lroundf(a * STEPS_PER_MM)));
  return true;
}

// Moves X to an absolute step position; calls onTick while moving.
template <typename F>
void moveAndWait(int32_t target, F onTick) {
  stepperX->moveTo(target);
  while (stepperX->isRunning()) {
    onTick();
    delay(1);
  }
}

void cmdPull(float v, float a) {
  if (!setMotion(v, a)) return;
  int32_t home = stepperX->getCurrentPosition();
  magnet.set(MagnetMode::PullMachine);
  delay(200);  // grab time, as in the real job
  uint32_t t0 = millis();
  moveAndWait(home + mmToSteps(SQUARE_MM), [] {});
  uint32_t t1 = millis();
  delay(200);
  moveAndWait(home, [] {});
  uint32_t t2 = millis();
  magnet.set(MagnetMode::Off);
  Serial.printf("pull v=%.1f a=%.1f: out %lu ms, back %lu ms\n", v, a,
                static_cast<unsigned long>(t1 - t0), static_cast<unsigned long>(t2 - (t1 + 200)));
  Serial.println("check: did the piece follow both ways? record in hardware/test-log.md");
}

void cmdHall() {
  Serial.println("hall: raw, class (send anything to stop)");
  drainInput();
  uint32_t next = millis();
  while (!inputPending()) {
    if (static_cast<int32_t>(millis() - next) >= 0) {
      next += HALL_PERIOD_MS;
      int raw = readHall();
      hallPrev = classify(raw, hallPrev, thresholds);
      Serial.printf("%4d  %+5d  %s\n", raw, raw - thresholds.center, cellName(hallPrev));
    }
    delay(1);
  }
  drainInput();
}

void cmdPass(float d) {
  if (!setMotion(PASS_SPEED_MMS, PASS_ACCEL_MMS2)) return;
  magnet.set(MagnetMode::Off);
  delay(100);
  const int baseline = readHall();
  int maxDelta = 0, maxRaw = baseline;
  auto sample = [&] {
    int raw = readHall();
    int dlt = abs(raw - baseline);
    if (dlt > maxDelta) {
      maxDelta = dlt;
      maxRaw = raw;
    }
  };
  int32_t home = stepperX->getCurrentPosition();
  magnet.set(MagnetMode::PullMachine);
  delay(200);
  sample();
  const int atRest = maxDelta;
  moveAndWait(home + mmToSteps(d), sample);
  sample();
  moveAndWait(home, sample);
  sample();
  magnet.set(MagnetMode::Off);
  Serial.printf("pass d=%.1f: baseline %d, energized at start +-%d, max change %d (raw %d)\n", d,
                baseline, atRest, maxDelta, maxRaw);
  Serial.printf("  enter threshold is %d: max change should stay well below it\n",
                thresholds.enter);
}

void cmdLed(int b) {
  if (b < 0) b = 0;
  if (b > 255) b = 255;
  leds.setBrightness(static_cast<uint8_t>(b));
  Serial.printf("led b=%d: red/blue every %lu ms (send anything to stop)\n", b,
                static_cast<unsigned long>(LED_PERIOD_MS));
  drainInput();
  const Rgb red[1] = {{255, 0, 0}};
  const Rgb blue[1] = {{0, 0, 255}};
  bool isRed = true;
  uint32_t next = millis();
  while (!inputPending()) {
    if (static_cast<int32_t>(millis() - next) >= 0) {
      next += LED_PERIOD_MS;
      leds.show(isRed ? red : blue, 1);
      isRed = !isRed;
    }
    delay(1);
  }
  leds.clear();
  drainInput();
}

void help() {
  Serial.println("commands: pull v a | hall | pass d | led b");
}

void handleLine(char* line) {
  char cmd[8] = {0};
  float x = 0, y = 0;
  int n = sscanf(line, "%7s %f %f", cmd, &x, &y);
  if (n < 1) return;
  if (strcmp(cmd, "pull") == 0 && n == 3) {
    cmdPull(x, y);
  } else if (strcmp(cmd, "hall") == 0) {
    cmdHall();
  } else if (strcmp(cmd, "pass") == 0 && n == 2) {
    cmdPass(x);
  } else if (strcmp(cmd, "led") == 0 && n == 2) {
    cmdLed(static_cast<int>(x));
  } else {
    help();
  }
}

char lineBuf[64];
size_t lineLen = 0;

}  // namespace

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  for (uint8_t p : pins::MUX_S) {
    pinMode(p, OUTPUT);
    digitalWrite(p, LOW);  // channel 0
  }
  magnet.begin();
  leds.begin(1);
  engine.init();
  stepperX = engine.stepperConnectToPin(pins::X_STEP);
  if (stepperX) {
    stepperX->setDirectionPin(pins::X_DIR);
    stepperX->setEnablePin(pins::STEPPER_EN);  // active low
    stepperX->setAutoEnable(true);
    stepperX->setCurrentPosition(0);
  }
  Serial.println("hasami rig ready");
  help();
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
}
