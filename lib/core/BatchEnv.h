#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include "Activity.h"
#include "Ai.h"
#include "Features.h"
#include "Game.h"
#include "HumanBot.h"
#include "MachineController.h"
#include "Params.h"
#include "Rules.h"
#include "VirtualGantry.h"

// The learning environment of plan 2026-11-12 section 4 (plan 2026-11-06 3.4): the machine is the
// learner, the human a fixed bot inside the environment. Each game runs Game + MachineController +
// VirtualGantry + HumanBot in the order of SimDriver::advance, in fixed steps of tickMs, and stops
// at the machine's decision points: the reads of MachineController::wantsThink (every
// max(150, reactMs), never during a job, never while Removing or counting down) where the mask
// (buildMask) has two or more actions. At a read with a single action that action is taken and the
// game goes on. With the read's own move in place of the learner's (readMove) and tickMs 10, a game
// is the game SimDriver plays with think() for the same seed (tools/autoplay.mjs).
//
// Threads: an EnvSlot or a BatchEnv range is used by one thread at a time; separate slots share
// nothing (lib/core keeps no mutable static state), so the Python binding steps ranges of a
// BatchEnv on several threads.

enum class EnvOpponent { B1, Race, Random };

struct EnvConfig {
  Rules rules;
  Params params;  // the machine's, speed_scale already applied (see envConfig)
  BotParams bot;
  int speedScale100 = 100;
  uint32_t tickMs = 10;
  // Machine reward (plan 2026-11-06 3.3): win +winReward, loss -winReward, captureReward per piece
  // of change in the capture difference (machine - human), and drawPenaltyPerSec for every second
  // of game clock while the difference is 0. A draw at the time limit adds nothing at the end.
  float winReward = 1.0f;
  float captureReward = 0.2f;
  float drawPenaltyPerSec = 0.5f / 600.0f;
  // ActivityMeter for info (recording only; costs time).
  bool trackActivity = true;
  // Safety net on simulated time (the game clock stands still outside Play): the game is cut off
  // (done, truncated, winner none).
  uint32_t maxSimMs = 3u * 600000u + 60000u;
};

// The configuration of plan 2026-11-12: rules RE + RW + RU40, 4 to win, 10 min; the opponent
// (B1: Race with 1-3 s thinking and threats missed with probability 0.3, as tools/autoplay.mjs);
// speed_scale multiplies freeSpeed and dragSpeed and divides engageMs (a faster machine engages
// sooner); `level` is the machine level the bot reads the machine with (and the read's level).
EnvConfig envConfig(EnvOpponent opponent, float speedScale, Level level);

// One finished (or running) game for info.
struct EnvGameInfo {
  int winner;  // 0 none (running, a draw or cut off), 1 human, 2 machine
  int capturedMachine, capturedHuman;
  uint32_t clockMs;
  bool truncated;
  ActivityStats activity;
};

struct EnvStep {
  float reward;
  uint32_t dtMs;  // simulated (caller) time since the previous decision point
  bool done;
  EnvGameInfo info;  // the finished game when done, otherwise the running one
};

class EnvSlot {
 public:
  explicit EnvSlot(const EnvConfig& cfg);
  EnvSlot(const EnvSlot&) = delete;
  EnvSlot& operator=(const EnvSlot&) = delete;

  // Starts a game with the bot seeded by `seed` and runs to its first decision point (a game that
  // ends before one is followed by another with seed + seedStride, and so on).
  void reset(uint32_t seed, uint32_t seedStride = 1);
  // Takes `action` (must be in the mask) at the current decision point and runs to the next one.
  // When the game ends, the step reports it (done, the reward up to the end, info of the finished
  // game) and the slot starts the next game with the seed after the last one (+ seedStride), so
  // that features and mask are those of the new game's first decision point.
  EnvStep step(int action);

  void features(int16_t planes[], int16_t scal[]) const;
  const uint8_t* mask() const { return mask_; }
  int maskCount() const { return maskCount_; }
  // The move Ai's read (think()) makes at the current decision point at `level`, as an action.
  int readAction(Level level) const;
  EnvGameInfo info() const;

  const Game& game() const { return game_; }
  // The snapshot of the read at the current decision point (what think() reads).
  const Snapshot& snapshot() const { return snap_; }
  uint32_t now() const { return now_; }
  uint32_t seed() const { return seed_; }

 private:
  void newGame(uint32_t seed);
  // Runs ticks until a decision point (true) or the end of the game (false).
  bool runToDecision();
  // The result of the read (`action`) goes to the controller, which starts the move if it can.
  void applyRead(int action);
  // The rest of a tick after the read: the bot, then the activity meter.
  void tailOfTick();
  int diff() const { return game_.captured(MACHINE) - game_.captured(HUMAN); }
  // The capture reward for the change in the difference since the last call.
  float captureDelta();

  EnvConfig cfg_;
  Game game_;
  VirtualGantry gantry_;
  MachineController ctl_;
  HumanBot bot_;
  ActivityMeter activity_;
  uint32_t now_;
  uint32_t seed_, stride_;
  bool truncated_;
  // At the current decision point.
  Snapshot snap_;
  uint8_t mask_[ACTION_COUNT];
  int maskCount_;
  uint32_t decisionAt_;
  // Reward accrued since the previous decision point.
  float reward_;
  int lastDiff_;
};

// N slots. Game k (0, 1, ...) of slot i uses seed + i + k * N, so that the first games of a batch
// have the seeds seed, seed + 1, ... as tools/autoplay.mjs.
class BatchEnv {
 public:
  BatchEnv(int nEnvs, uint32_t seed, const EnvConfig& cfg);
  int size() const { return static_cast<int>(slots_.size()); }
  EnvSlot& slot(int i) { return *slots_[i]; }
  const EnvSlot& slot(int i) const { return *slots_[i]; }
  const EnvConfig& config() const { return cfg_; }

  // The per-slot work over [begin, end), for one thread each.
  void resetRange(int begin, int end);
  void stepRange(int begin, int end, const int32_t actions[], EnvStep out[]);
  void readRange(int begin, int end, Level level, int32_t out[]) const;
  // Features and masks of [begin, end) into batch arrays (planes[N][FEAT_PLANES] and so on).
  void observeRange(int begin, int end, int16_t planes[], int16_t scal[], uint8_t mask[]) const;

 private:
  EnvConfig cfg_;
  uint32_t seed_;
  std::vector<std::unique_ptr<EnvSlot>> slots_;
};
