#include "Magnet.h"
#include <Arduino.h>
#include "Pins.h"

void Magnet::begin() {
  ledcSetup(CH_IN1, PWM_FREQ_HZ, PWM_BITS);
  ledcSetup(CH_IN2, PWM_FREQ_HZ, PWM_BITS);
  ledcAttachPin(pins::MAGNET_IN1, CH_IN1);
  ledcAttachPin(pins::MAGNET_IN2, CH_IN2);
  set(MagnetMode::Off);
}

void Magnet::set(MagnetMode m) {
  uint32_t in1 = 0, in2 = 0;
  switch (m) {
    case MagnetMode::Off: break;
    case MagnetMode::PullMachine: in1 = PULL_DUTY; break;
    case MagnetMode::PullHuman: in2 = PULL_DUTY; break;
    case MagnetMode::RepelWeak: in1 = REPEL_WEAK_DUTY; break;
  }
  ledcWrite(CH_IN1, in1);
  ledcWrite(CH_IN2, in2);
  mode_ = m;
}
