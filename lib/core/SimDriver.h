#pragma once
#include <cstdint>
#include "Activity.h"
#include "Ai.h"
#include "Game.h"
#include "HumanBot.h"
#include "MachineController.h"
#include "Params.h"
#include "Rules.h"
#include "Types.h"
#include "VirtualGantry.h"

// One piece for the screen. cooldownLeft is on the game clock. (x, y) is the display position
// in squares (x = column, y = row): the head position for a piece the magnet is pulling.
struct PieceState {
  int id;
  int8_t side;
  int r, c;
  bool alive, held;
  uint32_t cooldownLeft;
  float x, y;
};

enum class EventKind { Capture, Save };

// Something worth logging. Capture: `side` captured `count` pieces with the piece that arrived at
// (r, c). Save: the machine read a lifted piece's threat and the placement at (r, c) captured
// nothing. seq numbers events from 1 in the order they happened.
struct SimEvent {
  uint32_t seq;
  EventKind kind;
  int8_t side;
  int count;
  int r, c;
  uint32_t clockMs;
};

constexpr int MAX_EVENTS = 40;

struct State {
  Phase phase;
  Winner winner;
  int n;
  int8_t cells[MAX_N * MAX_N];  // board without held pieces
  PieceState pieces[2 * MAX_N];
  int pieceCount;
  float headR, headC;
  JobView job;
  Rgb leds[MAX_N * MAX_N];
  uint32_t clockMs;
  int capturedHuman, capturedMachine;  // pieces captured by the human / by the machine
  int saves;                           // pre-emption successes (5.5)
  int machineMoves;                    // move jobs started
  // Measuring the read (plan 2026-10-31 3.13): reads made, reads with at least one candidate
  // the read counted as safe (AiDebug), and machine pieces lost that the read saw coming
  // (risk >= 0.5 in the latest read at least the level's margin before the capture).
  int reads, safeReads, lostPredicted;
  int stuckId;
  // Held piece whose placement is pending and its target square (-1 when none).
  int pendingId, pendingR, pendingC;
  // Opening (RW, plan 2026-11-06 3.1): whether each side has made its opening move (both true
  // without RW).
  bool openingHuman, openingMachine;
  // The activity measures of plan 2026-11-06 3.3 so far (ActivityMeter).
  ActivityStats activity;
  SimEvent events[MAX_EVENTS];  // newest first
  int eventCount;
};

// Game + MachineController + VirtualGantry (+ optional HumanBot) driven by elapsed time; the
// machine thinks synchronously on the same thread.
class SimDriver {
 public:
  SimDriver(const Rules& rules = Rules(), const Params& params = Params(), uint32_t seed = 1);
  SimDriver(const SimDriver&) = delete;
  SimDriver& operator=(const SimDriver&) = delete;

  void setParams(const Params& p);
  // Takes effect at the next reset (start() from Ready/Over resets).
  void setRules(const Rules& r);
  void reset();
  void start();
  void togglePause();
  // Advances time: game.tick -> controller.update -> gantry.update -> think if wanted -> bot.
  void advance(uint32_t dtMs);
  LiftResult lift(int r, int c);
  PlaceResult place(int r, int c);
  void enableBot(const BotParams& p);
  // For tests: puts piece `id` on (r,c) (Game::onMachineStep) to set up a position.
  void teleport(int id, int r, int c) { game_.onMachineStep(id, r, c); }
  void disableBot() { botOn_ = false; }

  State state() const;

  const Game& game() const { return game_; }
  uint32_t now() const { return now_; }

 private:
  struct LiftInfo {
    bool threat;     // the piece had a capturing destination when lifted
    bool responded;  // the machine started a job while it was held
  };

  void resetMachine();
  void runBot();
  void noteCaptures(int8_t side, int r, int c);
  // A human placement landed at (r,c): log captures and a pre-emption success.
  void notePlaced(int id, int r, int c, int capsBefore);
  void pushEvent(EventKind kind, int8_t side, int count, int r, int c);
  // Records each machine piece's risk from a read at game clock `clock`.
  void noteRisk(const AiDebug& dbg, uint32_t clock);
  // Machine pieces captured since the last call: count those the read saw coming.
  void noteLosses();

  Rules rules_;
  Params params_;
  Game game_;
  VirtualGantry gantry_;
  MachineController controller_;
  HumanBot bot_;
  bool botOn_;
  uint32_t now_;

  LiftInfo lift_[2 * MAX_N + 1];
  int lastCaptured_[3];
  int saves_;
  int machineMoves_;
  int reads_, safeReads_, lostPredicted_;
  // Recent reads per machine piece id (ring buffer): game clock and the piece's risk.
  static constexpr int RISK_HISTORY = 32;
  struct RiskMark {
    uint32_t clock;
    float risk;
  };
  RiskMark riskHist_[2 * MAX_N + 1][RISK_HISTORY];
  int riskHead_[2 * MAX_N + 1];
  int riskCount_[2 * MAX_N + 1];
  bool aliveSeen_[2 * MAX_N + 1];
  uint32_t riskMarginMs_;
  ActivityMeter activity_;
  SimEvent events_[MAX_EVENTS];  // ring buffer
  int eventHead_;                // index of the next write
  int eventCount_;
  uint32_t eventSeq_;
};
