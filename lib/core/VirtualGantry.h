#pragma once
#include <cstdint>
#include "IGantry.h"
#include "Params.h"
#include "Path.h"

// Simulated stage: models only speeds and times (no acceleration). A motion commanded between
// two update() calls starts at the time of the latest update().
class VirtualGantry : public IGantry {
 public:
  explicit VirtualGantry(const Params& p, float headR = 0.0f, float headC = 0.0f);

  void setParams(const Params& p) { params_ = p; }
  // Puts the head at (r, c) and stops any motion.
  void setHead(float r, float c);

  void travelTo(float r, float c) override;
  void dragTo(int r, int c) override;
  void magnet(MagnetMode m) override { magnet_ = m; }
  bool idle() const override { return !moving_; }
  float headR() const override { return headR_; }
  float headC() const override { return headC_; }
  void update(uint32_t now) override;

  MagnetMode magnetMode() const { return magnet_; }

 private:
  void startPolyline(float speed);

  Params params_;
  float headR_, headC_;
  MagnetMode magnet_;
  bool moving_;
  uint32_t lastNow_;
  uint32_t startMs_;
  float speed_;
  float total_;
  float ptR_[TRAVEL_MAX_POINTS], ptC_[TRAVEL_MAX_POINTS];
  int ptCount_;
};
