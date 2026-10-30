#pragma once
#include <cstdint>
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
  int stuckId;
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
  void pushEvent(EventKind kind, int8_t side, int count, int r, int c);

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
  SimEvent events_[MAX_EVENTS];  // ring buffer
  int eventHead_;                // index of the next write
  int eventCount_;
  uint32_t eventSeq_;
};
