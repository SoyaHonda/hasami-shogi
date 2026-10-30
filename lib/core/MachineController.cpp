#include "MachineController.h"
#include <cstring>

MachineController::MachineController(Game& game, IGantry& gantry, const Params& params)
    : game_(game),
      gantry_(gantry),
      params_(params),
      lastRead_(0),
      liftAt_(0),
      liftPending_(false),
      awaiting_(false),
      active_(false),
      removing_(false),
      pieceId_(-1),
      toR_(-1),
      toC_(-1),
      stage_(Stage::Travel),
      engageUntil_(0),
      dragging_(false),
      nextR_(-1),
      nextC_(-1),
      pathLen_(0),
      pathIdx_(0),
      slotC_(-1) {}

bool MachineController::wantsThink(uint32_t) const {
  if (game_.phase() != Phase::Play || active_ || awaiting_) return false;
  const uint32_t clock = game_.clockMs();
  const uint32_t interval = params_.reactMs > 150 ? params_.reactMs : 150;
  if (clock >= lastRead_ && clock - lastRead_ >= interval) return true;
  if (params_.readLift && liftPending_ && clock >= liftAt_ && clock - liftAt_ >= params_.reactMs) return true;
  return false;
}

Snapshot MachineController::takeSnapshot(uint32_t) {
  lastRead_ = game_.clockMs();
  liftPending_ = false;
  awaiting_ = true;
  return game_.snapshot(gantry_.headR(), gantry_.headC());
}

void MachineController::onHumanLift(uint32_t) {
  liftAt_ = game_.clockMs();
  liftPending_ = true;
}

void MachineController::onThinkResult(const AiMove& m, uint32_t now) {
  awaiting_ = false;
  if (m.none || active_ || game_.phase() != Phase::Play) return;
  if (m.pieceId < 1 || m.pieceId > game_.pieceCount()) return;
  const PieceView& p = game_.pieces()[m.pieceId - 1];
  if (!p.alive || p.held || p.side != MACHINE || game_.clockMs() < p.cooldownUntil) return;
  const Board b = game_.boardWithReserve();
  int d[MAX_N * MAX_N];
  const int k = dests(b, p.r, p.c, d);
  const int target = m.toR * b.n + m.toC;
  bool legal = false;
  for (int i = 0; i < k; i++)
    if (d[i] == target) legal = true;
  if (!legal) return;

  gantry_.update(now);  // motions start at this time
  active_ = true;
  removing_ = false;
  pieceId_ = m.pieceId;
  toR_ = m.toR;
  toC_ = m.toC;
  stage_ = Stage::Travel;
  dragging_ = false;
  gantry_.magnet(MagnetMode::Off);
  gantry_.travelTo(static_cast<float>(p.r), static_cast<float>(p.c));
}

void MachineController::endJob() {
  gantry_.magnet(MagnetMode::Off);
  active_ = false;
  removing_ = false;
  dragging_ = false;
}

void MachineController::update(uint32_t now) {
  gantry_.update(now);
  const Phase ph = game_.phase();
  // The game clock is frozen outside Play, so this puts the last read at the moment Play
  // resumes (the first read then comes reactMs after the countdown, as in the original).
  if (ph != Phase::Play) lastRead_ = game_.clockMs();
  if (ph == Phase::Ready) {
    // Game was reset: drop whatever was going on.
    if (active_) endJob();
    liftPending_ = false;
    return;
  }
  if (active_) {
    if (removing_) {
      updateRemovalJob(now);
    } else {
      updateMoveJob(now);
    }
    return;
  }
  if (ph == Phase::Removing && gantry_.idle()) startRemoval(now);
}

void MachineController::updateMoveJob(uint32_t now) {
  if (!gantry_.idle()) return;
  if (dragging_) {
    // The square in progress is always finished, whatever happened meanwhile.
    dragging_ = false;
    game_.onMachineStep(pieceId_, nextR_, nextC_);
    if (game_.phase() == Phase::Play && nextR_ == toR_ && nextC_ == toC_) {
      game_.onMachineArrive(pieceId_, now);
      endJob();
      return;
    }
  }
  const Phase ph = game_.phase();
  if (ph == Phase::Removing || ph == Phase::Over) {
    // A capture (or the end of the game) during the job: abort without an arrival.
    game_.onMachineAbort(pieceId_, now);
    endJob();
    return;
  }
  if (ph != Phase::Play) return;  // paused or counting down: hold the job

  if (stage_ == Stage::Travel) {
    gantry_.magnet(MagnetMode::PullMachine);
    stage_ = Stage::Engage;
    engageUntil_ = now + params_.engageMs;
  }
  if (stage_ == Stage::Engage) {
    if (static_cast<int32_t>(now - engageUntil_) < 0) return;
    stage_ = Stage::Drag;
  }
  // Drag: claim the next square, or stop here and arrive.
  const PieceView& p = game_.pieces()[pieceId_ - 1];
  const int dr = toR_ > p.r ? 1 : toR_ < p.r ? -1 : 0;
  const int dc = toC_ > p.c ? 1 : toC_ < p.c ? -1 : 0;
  const int nr = p.r + dr, nc = p.c + dc;
  if ((dr == 0 && dc == 0) || !game_.machineCanEnter(pieceId_, nr, nc)) {
    game_.onMachineArrive(pieceId_, now);
    endJob();
    return;
  }
  nextR_ = nr;
  nextC_ = nc;
  dragging_ = true;
  gantry_.dragTo(nr, nc);
}

void MachineController::startRemoval(uint32_t now) {
  Removal rm[2 * MAX_N];
  const int k = game_.removals(rm);
  if (k == 0) return;
  const Board b = game_.boardWithoutHeld();
  const int n = b.n;
  // Squares the route must avoid: origins of held pieces (they may be put back) and the
  // squares of the other captured pieces still on the board.
  bool blocked[MAX_N * MAX_N];
  std::memset(blocked, 0, sizeof(blocked));
  const int* held = game_.heldIds();
  for (int i = 0; i < game_.heldCount(); i++) {
    const PieceView& h = game_.pieces()[held[i] - 1];
    blocked[h.r * n + h.c] = true;
  }
  for (int i = 0; i < k; i++) {
    bool block[MAX_N * MAX_N];
    std::memcpy(block, blocked, sizeof(block));
    for (int j = 0; j < k; j++)
      if (j != i) block[rm[j].from.r * n + rm[j].from.c] = true;
    const PieceView& p = game_.pieces()[rm[i].pieceId - 1];
    int len = 0, slot = -1;
    if (!removalPath(b, rm[i].from, rm[i].toRow, game_.reserveUsed(p.side), block, path_, len, slot)) continue;
    active_ = true;
    removing_ = true;
    pieceId_ = rm[i].pieceId;
    toR_ = rm[i].toRow;
    toC_ = slot;
    slotC_ = slot;
    pathLen_ = len;
    pathIdx_ = 0;  // index of the square the piece is on
    stage_ = Stage::Travel;
    dragging_ = false;
    gantry_.magnet(MagnetMode::Off);
    gantry_.travelTo(static_cast<float>(rm[i].from.r), static_cast<float>(rm[i].from.c));
    return;
  }
  game_.onRemovalStuck(rm[0].pieceId, now);
}

void MachineController::updateRemovalJob(uint32_t now) {
  if (!gantry_.idle()) return;
  const int n = game_.rules().n;
  if (dragging_) {
    dragging_ = false;
    pathIdx_++;
    const Sq s = path_[pathIdx_];
    if (s.r >= 0 && s.r < n) game_.onMachineStep(pieceId_, s.r, s.c);
  }
  const Phase ph = game_.phase();
  if (ph != Phase::Removing) {
    // Removals only run while Removing (Over/Ready cannot normally occur here).
    if (ph == Phase::Over) endJob();
    return;
  }
  if (stage_ == Stage::Travel) {
    const PieceView& p = game_.pieces()[pieceId_ - 1];
    gantry_.magnet(p.side == HUMAN ? MagnetMode::PullHuman : MagnetMode::PullMachine);
    stage_ = Stage::Engage;
    engageUntil_ = now + params_.engageMs;
  }
  if (stage_ == Stage::Engage) {
    if (static_cast<int32_t>(now - engageUntil_) < 0) return;
    stage_ = Stage::Drag;
  }
  if (pathIdx_ + 1 < pathLen_) {
    const Sq s = path_[pathIdx_ + 1];
    dragging_ = true;
    gantry_.dragTo(s.r, s.c);
    return;
  }
  const int id = pieceId_, slot = slotC_;
  endJob();
  game_.onRemoved(id, slot);
}

JobView MachineController::job() const {
  return JobView{active_, pieceId_, toR_, toC_, stage_, removing_};
}
