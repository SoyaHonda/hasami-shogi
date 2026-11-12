#include "Gantry.h"
#include <Arduino.h>
#include <FastAccelStepper.h>
#include <math.h>
#include "Pins.h"

namespace {
FastAccelStepperEngine engine;
bool engineReady = false;

int32_t mmToSteps(float mm) { return static_cast<int32_t>(lroundf(mm * Gantry::STEPS_PER_MM)); }
float stepsToSquares(int32_t steps) {
  return static_cast<float>(steps) / (Gantry::STEPS_PER_MM * Gantry::SQUARE_MM);
}
}  // namespace

Gantry::Gantry(Magnet& magnet, const Params& p, int n) : magnet_(magnet), params_(p), n_(n) {}

bool Gantry::begin() {
  pinMode(pins::LIMIT_X, INPUT_PULLUP);
  pinMode(pins::LIMIT_Y, INPUT_PULLUP);
  if (!engineReady) {
    engine.init();
    engineReady = true;
  }
  x_ = engine.stepperConnectToPin(pins::X_STEP);
  y_ = engine.stepperConnectToPin(pins::Y_STEP);
  FastAccelStepper* both[2] = {x_, y_};
  const uint8_t dirs[2] = {pins::X_DIR, pins::Y_DIR};
  for (int i = 0; i < 2; ++i) {
    if (!both[i]) continue;
    both[i]->setDirectionPin(dirs[i]);
    both[i]->setEnablePin(pins::STEPPER_EN);  // shared, active low
    both[i]->setAutoEnable(true);
    both[i]->setCurrentPosition(0);
  }
  return x_ && y_;
}

float Gantry::clampR(float r) const {
  if (r < -1.0f) return -1.0f;
  if (r > static_cast<float>(n_)) return static_cast<float>(n_);
  return r;
}

float Gantry::clampC(float c) const {
  if (c < 0.0f) return 0.0f;
  if (c > static_cast<float>(n_ - 1)) return static_cast<float>(n_ - 1);
  return c;
}

float Gantry::headR() const { return y_ ? HOME_ROW + stepsToSquares(y_->getCurrentPosition()) : HOME_ROW; }
float Gantry::headC() const { return x_ ? HOME_COL + stepsToSquares(x_->getCurrentPosition()) : HOME_COL; }

bool Gantry::idle() const {
  if (nextPt_ < ptCount_) return false;
  return !(x_ && x_->isRunning()) && !(y_ && y_->isRunning());
}

bool Gantry::startSegment(float toR, float toC, float speed) {
  if (!x_ || !y_ || speed <= 0.0f) return false;
  const int32_t tx = mmToSteps((clampC(toC) - HOME_COL) * SQUARE_MM);
  const int32_t ty = mmToSteps((clampR(toR) - HOME_ROW) * SQUARE_MM);
  const int32_t dx = tx - x_->getCurrentPosition();
  const int32_t dy = ty - y_->getCurrentPosition();
  const float len = sqrtf(static_cast<float>(dx) * dx + static_cast<float>(dy) * dy);
  if (len < 1.0f) return false;
  const float vSteps = speed * SQUARE_MM * STEPS_PER_MM;
  const float aSteps = ACCEL_MMS2 * STEPS_PER_MM;
  FastAccelStepper* axes[2] = {x_, y_};
  const int32_t d[2] = {dx, dy};
  const int32_t t[2] = {tx, ty};
  for (int i = 0; i < 2; ++i) {
    if (d[i] == 0) continue;
    const float f = fabsf(static_cast<float>(d[i])) / len;
    uint32_t v = static_cast<uint32_t>(lroundf(vSteps * f));
    int32_t a = static_cast<int32_t>(lroundf(aSteps * f));
    axes[i]->setSpeedInHz(v < 1 ? 1 : v);
    axes[i]->setAcceleration(a < 1 ? 1 : a);
  }
  for (int i = 0; i < 2; ++i)
    if (d[i] != 0) axes[i]->moveTo(t[i]);
  return true;
}

void Gantry::travelTo(float r, float c) {
  const int tr = static_cast<int>(floorf(clampR(r) + 0.5f));
  const int tc = static_cast<int>(floorf(clampC(c) + 0.5f));
  ptCount_ = travelPath(headR(), headC(), tr, tc, ptR_, ptC_);
  nextPt_ = 1;  // point 0 is the current head position
  speed_ = params_.freeSpeed;
  update(0);
}

void Gantry::dragTo(int r, int c) {
  ptR_[0] = headR();
  ptC_[0] = headC();
  ptR_[1] = static_cast<float>(r);
  ptC_[1] = static_cast<float>(c);
  ptCount_ = 2;
  nextPt_ = 1;
  speed_ = params_.dragSpeed;
  update(0);
}

void Gantry::update(uint32_t) {
  if (!x_ || !y_) {
    nextPt_ = ptCount_;
    return;
  }
  if (x_->isRunning() || y_->isRunning()) return;
  // Start the next segment that actually moves (skips zero-length ones).
  while (nextPt_ < ptCount_) {
    const int i = nextPt_++;
    if (startSegment(ptR_[i], ptC_[i], speed_)) return;
  }
}

bool Gantry::homeAxis(FastAccelStepper* s, uint8_t limitPin) {
  const uint32_t v = static_cast<uint32_t>(lroundf(HOMING_MMS * STEPS_PER_MM));
  s->setSpeedInHz(v);
  s->setAcceleration(static_cast<int32_t>(lroundf(ACCEL_MMS2 * STEPS_PER_MM)));
  // Already on the switch: step off it first.
  if (digitalRead(limitPin) == LOW) {
    s->move(mmToSteps(HOMING_BACKOFF_MM * 2));
    while (s->isRunning()) delay(1);
    if (digitalRead(limitPin) == LOW) return false;
  }
  s->setCurrentPosition(0);
  s->runBackward();
  const int32_t limit = -mmToSteps(HOMING_MAX_MM);
  while (digitalRead(limitPin) != LOW) {
    if (s->getCurrentPosition() < limit) {
      s->forceStop();
      return false;
    }
    delay(1);
  }
  s->forceStop();
  while (s->isRunning()) delay(1);
  s->setCurrentPosition(0);
  s->move(mmToSteps(HOMING_BACKOFF_MM));
  while (s->isRunning()) delay(1);
  s->setCurrentPosition(0);
  return true;
}

bool Gantry::home() {
  homed_ = false;
  ptCount_ = nextPt_ = 0;
  if (!x_ || !y_) return false;
  if (!homeAxis(y_, pins::LIMIT_Y)) return false;
  if (!homeAxis(x_, pins::LIMIT_X)) return false;
  homed_ = true;
  return true;
}
