#pragma once
#include <cstdint>
#include "Game.h"
#include "Params.h"

// Human player for automatic games (5.5, plan 2026-10-31 3.12). Times are on the game clock.
//   Random: a random movable piece, a capturing destination with probability greed.
//   Trap:   regression bot that sets traps A, B and C (plan 2026-10-31 section 1).
//   Race:   reads the race of RaceTime from the human side (the main opponent for measuring).
//   Race3:  Race plus a search for captures that take two or three placements.
enum class BotStyle { Random, Trap, Race, Race3 };

struct BotParams {
  // Wait after a placement lands before lifting the next piece.
  uint32_t reactMs = 400;
  // Time from lift to put down: moveMs (Params::humanMoveMs) +- moveJitter (fraction), drawn
  // from the seeded random sequence for every move.
  uint32_t moveMs = 500;
  float moveJitter = 0.4f;
  float greed = 0.8f;  // Random: chance of taking a capture when there is one
  BotStyle style = BotStyle::Random;
  // The fixed baseline human B1 of plan 2026-11-06 3.5 (Race with both set: reactMs 1000,
  // thinkMaxMs 3000, threatMiss 0.3). thinkMaxMs > reactMs: the wait after each placement (the
  // thinking time) is drawn uniformly from [reactMs, thinkMaxMs] instead of being reactMs.
  // threatMiss: the chance that a move of Race or Race3 is chosen blind to the machine's
  // threats (no getting away first, and neither the danger to the moved piece nor the machine's
  // gain on the human lines counts). Both off by default, and then no random number is drawn
  // for them (the other styles' games stay as they were).
  uint32_t thinkMaxMs = 0;
  float threatMiss = 0.0f;
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

// The move a style picks for the next lift. Random: pick a random movable piece (the
// destination is chosen when it is put down). Wait: do nothing for now (Trap waits for a piece
// in cooldown that will capture).
struct BotMove {
  enum Kind { None, Wait, Random, Move } kind;
  int id;
  int toR, toC;
};

// How the race looks to the human after a move (Race, Race3).
struct BotRaceEval {
  // Least spare time of the machine over its lines: T_h - (save + margin) for a threatened
  // line (T_h - 10000 when it cannot be guarded at all), T_h for a line that is not threatened,
  // 1e9 without lines the human can close. Smaller is better for the human.
  float spare;
  float ownGain;  // sum of the machine's gain (3.8) over the human lines
  bool unsafe;    // a line through the moved piece has gain >= half its length
};

// Lifts the chosen piece reactMs after its previous placement (or after Play starts), holds it
// for the jittered move time, then puts it down. Random picks the destination when putting it
// down; the other styles plan from+to when lifting and put it on the planned square if it is
// still legal (a capture found when putting down comes first). Without a legal destination the
// piece goes back (cancel) or, when the origin is filled, on a random empty square (escape rule
// of 5.2). Keeps to the human rules of plan 2026-10-31 3.1: lifts only when no piece is held,
// holds for less than the hold limit, and after a Pending placement waits until it lands (or is
// dropped, then places again). The caller executes the action and reports the result back.
// Rule candidates (plan 2026-11-06 3.1): captures are those of the game's rules (Capture.h), a
// move that makes a wall (RW) is never chosen, and in Phase::Opening the bot makes its one
// opening move the same way, paced on the caller's clock.
class HumanBot {
 public:
  HumanBot(const BotParams& p = BotParams(), uint32_t seed = 1);

  void setParams(const BotParams& p) { params_ = p; }
  const BotParams& params() const { return params_; }
  // Machine parameters the race styles read with (speeds, level -> margin).
  void setMachineParams(const Params& p) { machine_ = p; }
  // Forgets the current plan (new game); the random sequence continues.
  void reset();
  void reseed(uint32_t seed) { rng_.reseed(seed); }

  // (headR, headC): where the machine's head is (the race styles read it). now: the caller's
  // clock, which paces the opening move (the game clock is stopped in Phase::Opening; the
  // overloads without it pass the game clock, so the bot never moves there).
  BotAction decide(const Game& game, float headR, float headC, uint32_t now);
  BotAction decide(const Game& game, float headR, float headC) { return decide(game, headR, headC, game.clockMs()); }
  BotAction decide(const Game& game) { return decide(game, 0.0f, static_cast<float>(game.rules().n / 2)); }
  // Results of executing the action returned by decide().
  void onLifted(const Game& game, bool ok, int r, int c);
  void onPlaced(const Game& game, PlaceResult res);

  // The move the style picks now among the human pieces that can be lifted (no piece held).
  // Uses the random sequence for ties.
  BotMove choose(const Game& game, float headR, float headC);
  // The race after the human piece `id` is put on (toR,toC) now (Race's evaluation).
  BotRaceEval raceAfter(const Game& game, float headR, float headC, int id, int toR, int toC) const;

 private:
  BotMove chooseTrap(const Game& game);
  BotMove chooseRace(const Game& game, float headR, float headC, bool search);
  BotAction putDown(const Game& game, float headR, float headC);
  // Lifts the chosen move's piece, or a random movable piece for BotMove::Random.
  BotAction liftFor(const Game& game, const BotMove& m);
  BotAction decideOpening(const Game& game, float headR, float headC, uint32_t now);
  uint32_t moveTime(const Game& game);
  // The wait before the next lift: reactMs, or drawn up to thinkMaxMs (B1).
  uint32_t waitTime();

  BotParams params_;
  Params machine_;
  Rng rng_;
  bool holding_;
  int heldId_;
  // Planned destination of the held piece (-1 when the style chooses when putting down).
  int planR_, planC_;
  // The latest Race choice was blind to the machine's threats (threatMiss); putting the piece
  // down judges a new destination the same way.
  bool blind_;
  // Game clock of the next action; valid once `armed_` (set on the first Play tick).
  bool armed_;
  uint32_t nextAt_;
  int failures_;
  // Opening: the caller's clock of the latest decide() and of the next action there.
  uint32_t now_;
  bool openArmed_;
  uint32_t openNextAt_;
};
