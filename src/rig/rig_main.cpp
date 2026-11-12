// One-square prototype measurement firmware (env:rig).
//
// Serial commands (115200 baud, one per line):
//   pull v a   magnet PullMachine, move the X axis one square (33.3 mm) out
//              and back at v mm/s with acceleration a mm/s^2 (boost, then the
//              hold duty set with `hold`)
//   hall       print the prototype sensor's raw value, millivolts, estimated
//              field in mT for both DRV5053 variants (OA, PA) and classify()
//              at 10 Hz; send anything to stop
//   cal        with no magnet near the sensor, store its reading as the
//              classify() centre and its millivolts as the field zero
//   pass d     baseline the sensor with the magnet off, energize it, move the
//              head d mm and back, and print the largest |raw - baseline|
//   led b      light the prototype square's LED red and blue alternately at
//              brightness b (0-255); send anything to stop
//   heat s d   energize the magnet (PullMachine) at duty d (0-255) for s
//              seconds (at most 120) with no boost or hold, then print the
//              on-time; measure the coil temperature right after. Send
//              anything to stop early
//   hold d     set the hold duty (0-255) that Pull modes drop to after
//              Magnet::BOOST_MS, for testing `pull` at reduced duty (less heat)
//
// The thresholds follow the DRV5053 variant chosen in Pins.h
// (pins::HALL_SENSOR). Results go into hardware/test-log.md.
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
constexpr uint32_t GRAB_MS = 200;  // grab time before moving, as Params::engageMs
constexpr int CAL_SAMPLES = 32;
// `heat`: longest run accepted (guards against typos) and progress interval.
constexpr float HEAT_MAX_S = 120.0f;
constexpr uint32_t HEAT_REPORT_MS = 5000;
// The prototype square's sensor: mux 0, channel 0 (select lines held low),
// or the sensor wired straight to HALL_ADC[0].
constexpr int PROTO_MUX = 0;
constexpr int HALL_SAMPLES = 4;

FastAccelStepperEngine engine;
FastAccelStepper* stepperX = nullptr;
Magnet magnet;
Leds leds;
HallThresholds thresholds = hallPreset(pins::HALL_SENSOR);
// Sensor output with no magnet, for the mT estimate: VQ typ until `cal`.
float zeroMv = hallcal::VQ_MV;
bool zeroMeasured = false;
Cell hallPrev = EMPTY;

const char* sensorName(Drv5053 v) { return v == Drv5053::PA ? "DRV5053PA" : "DRV5053OA"; }

int readHall(int samples = HALL_SAMPLES) {
  long sum = 0;
  for (int i = 0; i < samples; ++i) sum += analogRead(pins::HALL_ADC[PROTO_MUX]);
  return static_cast<int>(sum / samples);
}

// Millivolts through Arduino-ESP32 2.0.17's analogReadMilliVolts (esp_adc_cal:
// the chip's eFuse calibration when present, else Vref 1100 mV), independent
// of HallClassify's fixed ADC model.
float readHallMv(int samples = HALL_SAMPLES) {
  uint32_t sum = 0;
  for (int i = 0; i < samples; ++i) sum += analogReadMilliVolts(pins::HALL_ADC[PROTO_MUX]);
  return static_cast<float>(sum) / static_cast<float>(samples);
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

// Waits ms milliseconds while the magnet drops from boost to hold duty.
void waitMs(uint32_t ms) {
  const uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    magnet.update(millis());
    delay(1);
  }
}

// Moves X to an absolute step position; calls onTick while moving.
template <typename F>
void moveAndWait(int32_t target, F onTick) {
  stepperX->moveTo(target);
  while (stepperX->isRunning()) {
    magnet.update(millis());
    onTick();
    delay(1);
  }
}

void cmdPull(float v, float a) {
  if (!setMotion(v, a)) return;
  int32_t home = stepperX->getCurrentPosition();
  magnet.set(MagnetMode::PullMachine);
  waitMs(GRAB_MS);  // grab time, as in the real job
  uint32_t t0 = millis();
  moveAndWait(home + mmToSteps(SQUARE_MM), [] {});
  uint32_t t1 = millis();
  waitMs(GRAB_MS);
  moveAndWait(home, [] {});
  uint32_t t2 = millis();
  const uint32_t onMs = magnet.onMs(t2);
  magnet.set(MagnetMode::Off);
  Serial.printf("pull v=%.1f a=%.1f: out %lu ms, back %lu ms\n", v, a,
                static_cast<unsigned long>(t1 - t0),
                static_cast<unsigned long>(t2 - (t1 + GRAB_MS)));
  Serial.printf("  duty %lu for %lu ms, then hold %lu; magnet on %lu ms (total %lu ms)\n",
                static_cast<unsigned long>(Magnet::PULL_DUTY),
                static_cast<unsigned long>(Magnet::BOOST_MS),
                static_cast<unsigned long>(magnet.holdDuty()), static_cast<unsigned long>(onMs),
                static_cast<unsigned long>(magnet.totalOnMs(millis())));
  Serial.println("check: did the piece follow both ways? record in hardware/test-log.md");
}

void cmdHall() {
  Serial.printf("hall: %s thresholds center %d, enter %d, exit %d counts; field zero %.0f mV%s\n",
                sensorName(pins::HALL_SENSOR), thresholds.center, thresholds.enter, thresholds.exit,
                zeroMv, zeroMeasured ? "" : " (VQ typ, run `cal` to measure)");
  Serial.println(" raw  raw-ctr      mV   OA mT   PA mT  class  (mT: + S pole, - N pole; send anything to stop)");
  drainInput();
  uint32_t next = millis();
  while (!inputPending()) {
    if (static_cast<int32_t>(millis() - next) >= 0) {
      next += HALL_PERIOD_MS;
      int raw = readHall();
      float mv = readHallMv();
      hallPrev = classify(raw, hallPrev, thresholds);
      Serial.printf("%4d  %+7d  %6.0f  %+6.1f  %+6.1f  %s\n", raw, raw - thresholds.center, mv,
                    hallcal::fieldMt(mv, zeroMv, Drv5053::OA),
                    hallcal::fieldMt(mv, zeroMv, Drv5053::PA), cellName(hallPrev));
    }
    delay(1);
  }
  drainInput();
}

void cmdCal() {
  magnet.set(MagnetMode::Off);
  Serial.println("cal: keep magnets away from the sensor");
  delay(100);
  thresholds.center = readHall(CAL_SAMPLES);
  zeroMv = readHallMv(CAL_SAMPLES);
  zeroMeasured = true;
  hallPrev = EMPTY;
  Serial.printf("cal: center %d counts, field zero %.0f mV\n", thresholds.center, zeroMv);
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
  waitMs(GRAB_MS);
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

// Duty typed on the serial line, clamped to 0..MAX_DUTY before the cast (an
// out-of-range float to int conversion is undefined; NaN reads as 0).
int dutyArg(float d) {
  if (!(d > 0.0f)) return 0;
  if (d > static_cast<float>(Magnet::MAX_DUTY)) return static_cast<int>(Magnet::MAX_DUTY);
  return static_cast<int>(d);
}

void cmdHeat(float s, float duty) {
  if (!(s > 0 && s <= HEAT_MAX_S)) {  // also rejects NaN
    Serial.printf("error: s must be > 0 and <= %.0f\n", HEAT_MAX_S);
    return;
  }
  const int d = dutyArg(duty);
  const uint32_t ms = static_cast<uint32_t>(lroundf(s * 1000.0f));
  Serial.printf("heat: PullMachine at duty %d for %.1f s (send anything to stop early)\n", d, s);
  drainInput();
  magnet.drive(MagnetMode::PullMachine, static_cast<uint32_t>(d));
  const uint32_t t0 = millis();
  uint32_t nextReport = t0 + HEAT_REPORT_MS;
  bool stopped = false;
  while (millis() - t0 < ms) {
    if (inputPending()) {
      stopped = true;
      break;
    }
    if (static_cast<int32_t>(millis() - nextReport) >= 0) {
      Serial.printf("  %lu s\n", static_cast<unsigned long>((millis() - t0) / 1000));
      nextReport += HEAT_REPORT_MS;
    }
    delay(1);
  }
  const uint32_t onMs = magnet.onMs(millis());
  magnet.set(MagnetMode::Off);
  if (stopped) drainInput();
  Serial.printf("heat: %s, energized %.1f s at duty %d (total on-time %.1f s)\n",
                stopped ? "stopped early" : "done", onMs / 1000.0f, d,
                magnet.totalOnMs(millis()) / 1000.0f);
  Serial.println("measure the coil temperature now; record it in hardware/test-log.md");
}

void cmdHold(float d) {
  magnet.setHoldDuty(static_cast<uint32_t>(dutyArg(d)));
  Serial.printf("hold: Pull modes run at duty %lu for %lu ms, then %lu (%.0f%%); test with `pull v a`\n",
                static_cast<unsigned long>(Magnet::PULL_DUTY),
                static_cast<unsigned long>(Magnet::BOOST_MS),
                static_cast<unsigned long>(magnet.holdDuty()),
                100.0f * magnet.holdDuty() / Magnet::MAX_DUTY);
}

void help() {
  Serial.println("commands: pull v a | hall | cal | pass d | led b | heat s d | hold d");
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
  } else if (strcmp(cmd, "cal") == 0) {
    cmdCal();
  } else if (strcmp(cmd, "heat") == 0 && n == 3) {
    cmdHeat(x, y);
  } else if (strcmp(cmd, "hold") == 0 && n == 2) {
    cmdHold(x);
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
  // 11 dB: the attenuation HallClassify's ADC model and HallScanner assume.
  analogSetPinAttenuation(pins::HALL_ADC[PROTO_MUX], ADC_11db);
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
  magnet.update(millis());
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
