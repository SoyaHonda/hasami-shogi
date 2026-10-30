#pragma once
#include <cstdint>
#include "Game.h"

// Human player for automatic games (5.5). Times are on the game clock.
struct BotParams {
  uint32_t reactMs = 400, holdMs = 600;
  float greed = 0.8f;
};

// Seeded random numbers (xorshift32): the same sequence on every build.
class Rng {
 public:
  explicit Rng(uint32_t seed = 1) { reseed(seed); }
  void reseed(uint32_t seed) { x_ = seed ? seed : 0x9E3779B9u; }
  uint32_t next() {
    x_ ^= x_ << 13;
    x_ ^= x_ >> 17;
    x_ ^= x_ << 5;
    return x_;
  }
  // 0 <= v < k (k > 0).
  int below(int k) { return static_cast<int>(next() % static_cast<uint32_t>(k)); }
  // 0 <= v < 1 with 24 bits.
  float unit() { return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f); }

 private:
  uint32_t x_;
};

// What the bot wants to do now.
struct BotAction {
  enum Kind { None, Lift, Place } kind;
  int r, c;
};

// Picks a random movable piece reactMs after its previous placement (or after Play starts),
// holds it holdMs, then puts it on a capturing destination with probability greed (when there
// is one) or on a random legal destination. Without a legal destination it puts the piece back
// (cancel) or, when the origin is filled, on a random empty square (escape rule of 5.2).
// The caller executes the action and reports the result back.
class HumanBot {
 public:
  HumanBot(const BotParams& p = BotParams(), uint32_t seed = 1);

  void setParams(const BotParams& p) { params_ = p; }
  // Forgets the current plan (new game); the random sequence continues.
  void reset();
  void reseed(uint32_t seed) { rng_.reseed(seed); }

  BotAction decide(const Game& game);
  // Results of executing the action returned by decide().
  void onLifted(const Game& game, bool ok, int r, int c);
  void onPlaced(const Game& game, bool ok);

 private:
  BotParams params_;
  Rng rng_;
  bool holding_;
  int heldId_;
  // Game clock of the next action; valid once `armed_` (set on the first Play tick).
  bool armed_;
  uint32_t nextAt_;
  int failures_;
};
