#include "Magnet.h"
#include <Arduino.h>
#include "Pins.h"

namespace {

bool isPull(MagnetMode m) { return m == MagnetMode::PullMachine || m == MagnetMode::PullHuman; }

// Elapsed ms from `since` to `now`, 0 when now is earlier (a caller's `now`
// may predate a millis() taken inside set()).
uint32_t elapsed(uint32_t now, uint32_t since) {
  const int32_t d = static_cast<int32_t>(now - since);
  return d > 0 ? static_cast<uint32_t>(d) : 0;
}

}  // namespace

void Magnet::begin() {
  ledcSetup(CH_IN1, PWM_FREQ_HZ, PWM_BITS);
  ledcSetup(CH_IN2, PWM_FREQ_HZ, PWM_BITS);
  ledcAttachPin(pins::MAGNET_IN1, CH_IN1);
  ledcAttachPin(pins::MAGNET_IN2, CH_IN2);
  mode_ = MagnetMode::Off;
  fixed_ = false;
  boosting_ = false;
  totalOnMs_ = 0;
  apply(MagnetMode::Off, 0);
}

void Magnet::apply(MagnetMode m, uint32_t duty) {
  const uint32_t now = millis();
  if (mode_ == MagnetMode::Off && m != MagnetMode::Off) onSince_ = now;
  if (mode_ != MagnetMode::Off && m == MagnetMode::Off) totalOnMs_ += now - onSince_;
  uint32_t in1 = 0, in2 = 0;
  switch (m) {
    case MagnetMode::Off: duty = 0; break;
    case MagnetMode::PullMachine: in1 = duty; break;
    case MagnetMode::PullHuman: in2 = duty; break;
    case MagnetMode::RepelWeak: in1 = duty; break;
  }
  ledcWrite(CH_IN1, in1);
  ledcWrite(CH_IN2, in2);
  mode_ = m;
  duty_ = duty;
}

void Magnet::set(MagnetMode m) {
  if (m == mode_ && !fixed_) return;
  fixed_ = false;
  uint32_t duty = 0;
  if (isPull(m)) {
    pullSince_ = millis();
    boosting_ = true;
    duty = PULL_DUTY;
  } else if (m == MagnetMode::RepelWeak) {
    duty = REPEL_WEAK_DUTY;
  }
  apply(m, duty);
}

void Magnet::drive(MagnetMode m, uint32_t duty) {
  fixed_ = m != MagnetMode::Off;
  apply(m, duty > MAX_DUTY ? MAX_DUTY : duty);
}

void Magnet::update(uint32_t now) {
  if (fixed_ || !isPull(mode_)) return;
  // Latched: once the boost is over it stays over until the next switch, so a
  // Pull held past 2^31 ms (where elapsed() reads 0 again) cannot re-boost.
  if (boosting_ && elapsed(now, pullSince_) >= BOOST_MS) boosting_ = false;
  const uint32_t want = boosting_ ? PULL_DUTY : holdDuty_;
  if (want != duty_) apply(mode_, want);
}

void Magnet::setHoldDuty(uint32_t d) { holdDuty_ = d > MAX_DUTY ? MAX_DUTY : d; }

uint32_t Magnet::onMs(uint32_t now) const {
  return mode_ == MagnetMode::Off ? 0 : elapsed(now, onSince_);
}

uint32_t Magnet::totalOnMs(uint32_t now) const { return totalOnMs_ + onMs(now); }
