#pragma once
#include <stdint.h>

// ESP32 pin assignment, kept in this one place. PROVISIONAL: the build plan
// has no pin table, so check the supplied ESP32 board before wiring and edit
// only this file.
//
// Rules followed:
//  - Hall inputs on ADC1 (GPIO32-39); ADC2 cannot be read while Wi-Fi is on.
//  - GPIO6-11 (SPI flash) and GPIO16/17 (PSRAM on WROVER) are not used.
//  - Strapping pins (0, 2, 5, 12, 15) are avoided, except STEPPER_EN on 15
//    (see below).
//  - GPIO34-39 are input-only and have no internal pull-ups.
//
// A4988 wiring assumed: MS1, MS2, MS3 tied High (1/16 step), RESET tied to
// SLEEP. Neither is driven by the ESP32.
namespace pins {

// Hall sensors: 4 x CD74HC4067, each common output to one ADC1 pin.
constexpr uint8_t HALL_ADC[4] = {36, 39, 34, 35};
// Mux select lines S0..S3, shared by all four multiplexers.
constexpr uint8_t MUX_S[4] = {25, 26, 27, 14};

// Stepper drivers (A4988).
constexpr uint8_t X_STEP = 19;
constexpr uint8_t X_DIR = 21;
constexpr uint8_t Y_STEP = 22;
constexpr uint8_t Y_DIR = 23;
// Shared active-low ENABLE. GPIO15 is a strapping pin (low at boot only
// silences the ROM boot log), which is harmless here; it was used because
// every other free output is taken.
constexpr uint8_t STEPPER_EN = 15;

// Limit switches to GND, internal pull-up (GPIO32/33 have pull-ups).
constexpr uint8_t LIMIT_X = 32;
constexpr uint8_t LIMIT_Y = 33;

// Electromagnet H-bridge (DRV8871 IN1/IN2 or TB6612 equivalent).
constexpr uint8_t MAGNET_IN1 = 18;
constexpr uint8_t MAGNET_IN2 = 4;

// WS2812B data, through a 74AHCT125 level shifter.
constexpr uint8_t LED_DATA = 13;

}  // namespace pins
