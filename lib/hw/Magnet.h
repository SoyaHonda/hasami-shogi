#pragma once
#include <stdint.h>
#include "Types.h"

// Electromagnet (Kyoritsu ZYE1-P25/20, 12 V, 0.33 A per the seller) driven by
// a TB67H450 H-bridge with two PWM inputs (LEDC).
//  Off:         both inputs 0.
//  PullMachine: PWM on IN1, the polarity that attracts S-down (machine) pieces.
//  PullHuman:   PWM on IN2, the opposite polarity (attracts N-down pieces).
//  RepelWeak:   weak duty in the PullMachine polarity, which repels the
//               N-down human pieces the head passes under during travel.
// The TB67H450 truth table and maximum PWM frequency this relies on are
// UNVERIFIED (see Pins.h): confirm against its datasheet.
//
// Boost then hold: the coil heats up when energised continuously, so after a
// switch to a Pull mode the magnet runs at PULL_DUTY for BOOST_MS (to grab the
// piece across the gap) and then at the hold duty (HOLD_DUTY until changed
// with setHoldDuty). update(now) applies the drop, so call it from the main
// loop. With the hold duty equal to PULL_DUTY (the default) nothing is written
// after the boost: the drive is the same as without boost and hold.
class Magnet {
 public:
  static constexpr uint32_t PWM_FREQ_HZ = 20000;
  static constexpr uint8_t PWM_BITS = 8;
  static constexpr uint32_t MAX_DUTY = (1u << PWM_BITS) - 1;
  // Duty values (8-bit) and boost time. PROVISIONAL: tune on the one-square
  // prototype (hardware/test-log.md).
  static constexpr uint32_t PULL_DUTY = 255;
  static constexpr uint32_t BOOST_MS = 300;
  static constexpr uint32_t HOLD_DUTY = 255;  // 255: no reduction until measured
  static constexpr uint32_t REPEL_WEAK_DUTY = 60;
  // LEDC channels used (0-15 on ESP32).
  static constexpr uint8_t CH_IN1 = 0;
  static constexpr uint8_t CH_IN2 = 1;

  void begin();
  // Switches the mode. A switch to a Pull mode starts the boost; setting the
  // mode the magnet already has changes nothing (the boost is not restarted).
  void set(MagnetMode m);
  // Measurement only (rig `heat`): drives m at a fixed duty (clamped to
  // MAX_DUTY), with no boost or hold, until the next set() or drive().
  void drive(MagnetMode m, uint32_t duty);
  // Drops a Pull mode to the hold duty once BOOST_MS has passed since the
  // switch (and follows later setHoldDuty changes).
  void update(uint32_t now);
  MagnetMode mode() const { return mode_; }
  // Duty on the active input now (0 when Off).
  uint32_t duty() const { return duty_; }
  // Hold duty for Pull modes after the boost; clamped to MAX_DUTY.
  void setHoldDuty(uint32_t d);
  uint32_t holdDuty() const { return holdDuty_; }
  // Time the coil has been energised (mode not Off) without a break; 0 when Off.
  uint32_t onMs(uint32_t now) const;
  // Total energised time since begin(), the current stretch included.
  uint32_t totalOnMs(uint32_t now) const;

 private:
  void apply(MagnetMode m, uint32_t duty);

  MagnetMode mode_ = MagnetMode::Off;
  uint32_t duty_ = 0;
  uint32_t holdDuty_ = HOLD_DUTY;
  bool fixed_ = false;       // drive(): no boost or hold
  bool boosting_ = false;    // Pull mode still within BOOST_MS of the switch
  uint32_t pullSince_ = 0;   // switch to the current Pull mode
  uint32_t onSince_ = 0;     // start of the current energised stretch
  uint32_t totalOnMs_ = 0;   // finished stretches
};
