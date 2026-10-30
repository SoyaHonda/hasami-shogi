#pragma once
#include <stdint.h>
#include "Types.h"

// Electromagnet driven by an H-bridge with two PWM inputs (LEDC).
//  Off:         both inputs 0.
//  PullMachine: full duty, polarity that attracts S-down (machine) pieces.
//  PullHuman:   full duty, the opposite polarity (attracts N-down pieces).
//  RepelWeak:   weak duty in the PullMachine polarity, which repels the
//               N-down human pieces the head passes under during travel.
class Magnet {
 public:
  // Duty values (8-bit). PROVISIONAL: tune on the one-square prototype.
  static constexpr uint32_t PWM_FREQ_HZ = 20000;
  static constexpr uint8_t PWM_BITS = 8;
  static constexpr uint32_t PULL_DUTY = 255;
  static constexpr uint32_t REPEL_WEAK_DUTY = 60;
  // LEDC channels used (0-15 on ESP32).
  static constexpr uint8_t CH_IN1 = 0;
  static constexpr uint8_t CH_IN2 = 1;

  void begin();
  void set(MagnetMode m);
  MagnetMode mode() const { return mode_; }

 private:
  MagnetMode mode_ = MagnetMode::Off;
};
