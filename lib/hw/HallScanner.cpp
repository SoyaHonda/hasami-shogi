#include "HallScanner.h"
#include <Arduino.h>

void HallScanner::begin() {
  for (auto& m : cellAt_)
    for (auto& v : m) v = -1;
  for (int i = 0; i < CELLS; ++i) {
    const pins::HallChannel& hc = pins::HALL_MAP[i];
    if (hc.mux < 4 && hc.ch < 16) cellAt_[hc.mux][hc.ch] = static_cast<int8_t>(i);
  }
  for (uint8_t p : pins::MUX_S) {
    pinMode(p, OUTPUT);
    digitalWrite(p, LOW);
  }
  analogReadResolution(12);
  for (uint8_t p : pins::HALL_ADC) analogSetPinAttenuation(p, ADC_11db);
}

void HallScanner::selectChannel(int ch) {
  for (int b = 0; b < 4; ++b) digitalWrite(pins::MUX_S[b], (ch >> b) & 1 ? HIGH : LOW);
  delayMicroseconds(SETTLE_US);
}

bool HallScanner::held(int idx) const {
  if (!holdOn_) return false;
  const float dr = static_cast<float>(idx / N) - holdR_;
  const float dc = static_cast<float>(idx % N) - holdC_;
  return dr * dr + dc * dc <= HOLD_RADIUS * HOLD_RADIUS;
}

void HallScanner::scan() {
  for (int ch = 0; ch < 16; ++ch) {
    bool any = false;
    for (int m = 0; m < 4; ++m) any = any || cellAt_[m][ch] >= 0;
    if (!any) continue;
    selectChannel(ch);
    for (int m = 0; m < 4; ++m) {
      const int idx = cellAt_[m][ch];
      if (idx < 0) continue;
      const int v = analogRead(pins::HALL_ADC[m]);
      raw_[idx] = v;
      if (held(idx)) continue;  // keep cells_ and cand_ as they are
      const Cell now = classify(v, static_cast<Cell>(cells_[idx]), thr_[idx]);
      if (now == cand_[idx]) cells_[idx] = now;
      cand_[idx] = now;
    }
  }
}

bool HallScanner::update(uint32_t now) {
  if (scanned_ && now - lastScan_ < PERIOD_MS) return false;
  // Keep the 100 Hz grid unless we fell more than one period behind.
  lastScan_ = (scanned_ && now - lastScan_ < 2 * PERIOD_MS) ? lastScan_ + PERIOD_MS : now;
  scanned_ = true;
  scan();
  return true;
}

void HallScanner::setHold(bool magnetOn, float headR, float headC) {
  holdOn_ = magnetOn;
  holdR_ = headR;
  holdC_ = headC;
}

int HallScanner::raw(int r, int c) const {
  if (r < 0 || r >= N || c < 0 || c >= N) return -1;
  return raw_[r * N + c];
}

void HallScanner::calibrate() {
  long sum[CELLS] = {};
  for (int s = 0; s < CAL_SAMPLES; ++s) {
    for (int ch = 0; ch < 16; ++ch) {
      bool any = false;
      for (int m = 0; m < 4; ++m) any = any || cellAt_[m][ch] >= 0;
      if (!any) continue;
      selectChannel(ch);
      for (int m = 0; m < 4; ++m) {
        const int idx = cellAt_[m][ch];
        if (idx >= 0) sum[idx] += analogRead(pins::HALL_ADC[m]);
      }
    }
  }
  for (int i = 0; i < CELLS; ++i) {
    thr_[i].center = static_cast<int>(sum[i] / CAL_SAMPLES);
    raw_[i] = thr_[i].center;
    cells_[i] = EMPTY;
    cand_[i] = EMPTY;
  }
}
