#pragma once
#include <stdint.h>
#include "Types.h"

// WS2812B strip laid out serpentine under an n x n board (see LedIndex).
class Leds {
 public:
  static constexpr int MAX_LEDS = MAX_N * MAX_N;

  // Registers n*n LEDs on pins::LED_DATA and caps power at 5 V / 1500 mA.
  void begin(int n);
  void setBrightness(uint8_t b);
  // colors: row-major n x n (index r*n+c); reordered with ledIndex.
  // n must match the n given to begin().
  void show(const Rgb colors[], int n);
  void clear();

 private:
  int n_ = 0;
};
