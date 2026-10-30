#pragma once
#include <cstdint>
#include "Ai.h"
#include "Game.h"
#include "IGantry.h"
#include "Params.h"
#include "Path.h"

// Stage of the current job: free move under the piece, magnet engage, pulling square by square.
enum class Stage { Travel, Engage, Drag };

// Current job for the screen and LEDs. For a removal, (toR, toC) is the reserve square.
struct JobView {
  bool active;
  int pieceId;
  int toR, toC;
  Stage stage;
  bool removing;
};

// Read timing, machine move jobs and carrying captured pieces off, driving an IGantry.
// The caller ticks the Game before update(); read timing uses the game clock, the engage wait
// uses the caller's clock (`now`).
class MachineController {
 public:
  MachineController(Game& game, IGantry& gantry, const Params& params);

  void setParams(const Params& p) { params_ = p; }
  const Params& params() const { return params_; }

  // True when it is time to read: max(150, reactMs) after the last read, or (with readLift)
  // reactMs after the latest lift, whichever is earlier. False outside Play, during a job and
  // while a result is awaited.
  bool wantsThink(uint32_t now) const;
  // Records the read time and the awaiting-result mark, and returns the snapshot to think on.
  Snapshot takeSnapshot(uint32_t now);
  // Clears the awaiting mark. Starts the job only when the piece is alive and cooled down and
  // the destination is still legal on the current board; otherwise the result is dropped.
  void onThinkResult(const AiMove& m, uint32_t now);
  // A human piece was lifted (brings the next read forward when readLift is on).
  void onHumanLift(uint32_t now);
  // Advances the gantry, the job and removals.
  void update(uint32_t now);
  JobView job() const;

 private:
  void updateMoveJob(uint32_t now);
  void updateRemovalJob(uint32_t now);
  void startRemoval(uint32_t now);
  void endJob();

  Game& game_;
  IGantry& gantry_;
  Params params_;

  uint32_t lastRead_;
  uint32_t liftAt_;
  bool liftPending_;
  bool awaiting_;

  bool active_;
  bool removing_;
  int pieceId_;
  int toR_, toC_;
  Stage stage_;
  uint32_t engageUntil_;
  // Square being pulled into (valid while dragging_).
  bool dragging_;
  int nextR_, nextC_;
  // Removal route.
  Sq path_[REMOVAL_MAX_POINTS];
  int pathLen_;
  int pathIdx_;
  int slotC_;
};
