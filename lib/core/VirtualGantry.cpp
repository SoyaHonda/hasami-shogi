#include "VirtualGantry.h"
#include <cmath>

VirtualGantry::VirtualGantry(const Params& p, float headR, float headC)
    : params_(p),
      headR_(headR),
      headC_(headC),
      magnet_(MagnetMode::Off),
      moving_(false),
      lastNow_(0),
      startMs_(0),
      speed_(1.0f),
      total_(0.0f),
      ptCount_(0) {}

void VirtualGantry::setHead(float r, float c) {
  headR_ = r;
  headC_ = c;
  moving_ = false;
}

void VirtualGantry::startPolyline(float speed) {
  total_ = polylineLength(ptR_, ptC_, ptCount_);
  speed_ = speed;
  startMs_ = lastNow_;
  moving_ = total_ > 0.0f && speed_ > 0.0f;
  if (!moving_) {
    headR_ = ptR_[ptCount_ - 1];
    headC_ = ptC_[ptCount_ - 1];
  }
}

void VirtualGantry::travelTo(float r, float c) {
  const int tr = static_cast<int>(std::floor(r + 0.5f));
  const int tc = static_cast<int>(std::floor(c + 0.5f));
  ptCount_ = travelPath(headR_, headC_, tr, tc, ptR_, ptC_);
  startPolyline(params_.freeSpeed);
}

void VirtualGantry::dragTo(int r, int c) {
  ptR_[0] = headR_;
  ptC_[0] = headC_;
  ptR_[1] = static_cast<float>(r);
  ptC_[1] = static_cast<float>(c);
  ptCount_ = 2;
  startPolyline(params_.dragSpeed);
}

void VirtualGantry::update(uint32_t now) {
  lastNow_ = now;
  if (!moving_) return;
  const float elapsedMs = static_cast<float>(now - startMs_);
  const float durationMs = total_ / speed_ * 1000.0f;
  if (elapsedMs >= durationMs - 1e-3f) {
    headR_ = ptR_[ptCount_ - 1];
    headC_ = ptC_[ptCount_ - 1];
    moving_ = false;
    return;
  }
  // Walk the polyline to the distance covered so far.
  float left = speed_ * elapsedMs / 1000.0f;
  for (int i = 1; i < ptCount_; i++) {
    const float dr = ptR_[i] - ptR_[i - 1], dc = ptC_[i] - ptC_[i - 1];
    const float len = std::sqrt(dr * dr + dc * dc);
    if (left <= len && len > 0.0f) {
      const float f = left / len;
      headR_ = ptR_[i - 1] + dr * f;
      headC_ = ptC_[i - 1] + dc * f;
      return;
    }
    left -= len;
  }
  headR_ = ptR_[ptCount_ - 1];
  headC_ = ptC_[ptCount_ - 1];
}
