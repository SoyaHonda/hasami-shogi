#include "Leds.h"
#include <FastLED.h>
#include "LedIndex.h"
#include "Pins.h"

namespace {
CRGB strip[Leds::MAX_LEDS];
}

void Leds::begin(int n) {
  if (n < 1) n = 1;
  if (n > MAX_N) n = MAX_N;
  n_ = n;
  FastLED.addLeds<WS2812B, pins::LED_DATA, GRB>(strip, n * n);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 1500);
  clear();
}

void Leds::setBrightness(uint8_t b) { FastLED.setBrightness(b); }

void Leds::show(const Rgb colors[], int n) {
  if (n != n_) return;
  for (int r = 0; r < n; ++r)
    for (int c = 0; c < n; ++c) {
      const Rgb& k = colors[r * n + c];
      strip[ledIndex(r, c, n)] = CRGB(k.r, k.g, k.b);
    }
  FastLED.show();
}

void Leds::clear() {
  for (int i = 0; i < n_ * n_; ++i) strip[i] = CRGB::Black;
  FastLED.show();
}
