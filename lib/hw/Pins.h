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

// Board size the Hall wiring covers (4 muxes x 16 channels = 64 inputs).
constexpr int HALL_N = 7;
constexpr int HALL_CELLS = HALL_N * HALL_N;

// Square -> (multiplexer, channel), indexed r*HALL_N+c. PROVISIONAL: row-major
// fill (mux = index / 16, channel = index % 16); rewrite to match the actual
// sensor harness once it is wired.
struct HallChannel {
  uint8_t mux;
  uint8_t ch;
};
constexpr HallChannel HALL_MAP[HALL_CELLS] = {
    {0,  0}, {0,  1}, {0,  2}, {0,  3}, {0,  4}, {0,  5}, {0,  6},  // row 0
    {0,  7}, {0,  8}, {0,  9}, {0, 10}, {0, 11}, {0, 12}, {0, 13},  // row 1
    {0, 14}, {0, 15}, {1,  0}, {1,  1}, {1,  2}, {1,  3}, {1,  4},  // row 2
    {1,  5}, {1,  6}, {1,  7}, {1,  8}, {1,  9}, {1, 10}, {1, 11},  // row 3
    {1, 12}, {1, 13}, {1, 14}, {1, 15}, {2,  0}, {2,  1}, {2,  2},  // row 4
    {2,  3}, {2,  4}, {2,  5}, {2,  6}, {2,  7}, {2,  8}, {2,  9},  // row 5
    {2, 10}, {2, 11}, {2, 12}, {2, 13}, {2, 14}, {2, 15}, {3,  0},  // row 6
};

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
