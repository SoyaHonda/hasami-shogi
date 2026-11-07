#pragma once
#include <cstdint>
#include "Board.h"
#include "Game.h"
#include "Params.h"
#include "RaceTime.h"

// The activity measures of plan 2026-11-06 3.3 over one game, for automatic games (SimDriver,
// tools/autoplay.mjs) and the search exploiter. Measuring only: nothing here changes a game.
//
// Constraint measures (counted in Play only, per minute of Play where they are counts):
//  - outside: the share of the time the machine's pieces spend outside its home row (row 0),
//    each moment weighted by 1 / the machine pieces not yet captured then;
//  - threats: threats with a capture in prospect the machine made: a human line whose gain
//    (RaceTime, raceGain) is 0.5 or more, or under RE a human edge group with one empty
//    neighbour left that a machine piece reaches in one move. A line or group counts once until
//    its shape changes (it leaves the board as it was).
// Recorded only: the machine pieces' mean row, its moves (jobs started) and how many of them
// take a piece straight back to where its previous move started, its first move (opening
// diversity), the game clock of the first capture and the lead changes.
struct ActivityStats {
  uint32_t playMs;      // game clock observed in Play
  uint64_t outsideAcc;  // sum of dt * outside * ACTIVITY_SCALE / alive
  uint64_t rowAcc;      // sum of dt * (sum of rows) * ACTIVITY_SCALE / alive
  int threats, lineThreats, groupThreats;
  int moves, backForth;
  int firstMoveId, firstMoveR, firstMoveC;  // -1 before the first move
  uint32_t firstCaptureMs;                  // ACTIVITY_NONE before the first capture
  int leadChanges;
};

// 2520 = lcm(1..9): dt * count * ACTIVITY_SCALE / alive is exact for up to MAX_N pieces.
constexpr uint64_t ACTIVITY_SCALE = 2520;
constexpr uint32_t ACTIVITY_NONE = 0xFFFFFFFFu;
// How often the threats are looked for (game clock): the read interval.
constexpr uint32_t ACTIVITY_THREAT_MS = 250;

class ActivityMeter {
 public:
  ActivityMeter() { reset(); }
  void reset();
  // Once per step, after the game, the machine and the gantry have advanced: the game clock
  // since the previous call counts with the position as it is now. (headR, headC) and `machine`
  // are what the machine reads with (for the threats' RaceTime).
  void observe(const Game& g, float headR, float headC, const Params& machine);
  // A machine move job started: piece `id` from (fr,fc) to (tr,tc).
  void machineMove(int id, int fr, int fc, int tr, int tc);
  const ActivityStats& stats() const { return s_; }

  // Derived values (0 without Play time).
  static double outsideShare(const ActivityStats& s) {
    return s.playMs ? static_cast<double>(s.outsideAcc) / (static_cast<double>(ACTIVITY_SCALE) * s.playMs) : 0.0;
  }
  static double meanRow(const ActivityStats& s) {
    return s.playMs ? static_cast<double>(s.rowAcc) / (static_cast<double>(ACTIVITY_SCALE) * s.playMs) : 0.0;
  }
  static double perMinute(const ActivityStats& s, int count) {
    return s.playMs ? count * 60000.0 / s.playMs : 0.0;
  }

 private:
  void findThreats(const Game& g, float headR, float headC, const Params& machine);

  ActivityStats s_;
  bool started_;
  uint32_t lastClock_;
  uint32_t nextThreatAt_;
  int leadSign_;
  // Lines (r, c, dr, dc, len packed) and groups (their squares) counted and still on the board.
  uint32_t lines_[RACE_MAX_LINES];
  int lineCount_;
  SquareSet groups_[2 * MAX_N];
  int groupCount_;
  // Each machine piece's latest move (from, to as r*n+c; -1 before one).
  int lastFrom_[2 * MAX_N + 1], lastTo_[2 * MAX_N + 1];
};
