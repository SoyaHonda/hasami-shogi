#include "SimDriver.h"
#include <cstring>
#include "LedPlan.h"

SimDriver::SimDriver(const Rules& rules, const Params& params, uint32_t seed)
    : rules_(rules),
      params_(params),
      game_(rules),
      gantry_(params),
      controller_(game_, gantry_, params),
      bot_(BotParams(), seed),
      botOn_(false),
      now_(0) {
  reset();
}

void SimDriver::setParams(const Params& p) {
  params_ = p;
  gantry_.setParams(p);
  controller_.setParams(p);
}

void SimDriver::setRules(const Rules& r) {
  rules_ = r;
  game_.setRules(r);
}

void SimDriver::resetMachine() {
  // With the game in Ready, update() drops any job in progress.
  controller_.update(now_);
  const int n = game_.rules().n;
  gantry_.magnet(MagnetMode::Off);
  gantry_.setHead(0.0f, static_cast<float>(n / 2));  // same start as the original simulator
  gantry_.update(now_);
}

void SimDriver::reset() {
  game_.reset();
  resetMachine();
  bot_.reset();
  std::memset(lift_, 0, sizeof(lift_));
  lastCaptured_[0] = lastCaptured_[1] = lastCaptured_[2] = 0;
  saves_ = 0;
  machineMoves_ = 0;
  eventHead_ = 0;
  eventCount_ = 0;
  eventSeq_ = 0;
}

void SimDriver::start() {
  const Phase ph = game_.phase();
  if (ph == Phase::Ready || ph == Phase::Over) reset();
  game_.start(now_);
}

void SimDriver::togglePause() { game_.togglePause(now_); }

void SimDriver::enableBot(const BotParams& p) {
  bot_.setParams(p);
  bot_.reset();
  botOn_ = true;
}

void SimDriver::pushEvent(EventKind kind, int8_t side, int count, int r, int c) {
  events_[eventHead_] = SimEvent{++eventSeq_, kind, side, count, r, c, game_.clockMs()};
  eventHead_ = (eventHead_ + 1) % MAX_EVENTS;
  if (eventCount_ < MAX_EVENTS) eventCount_++;
}

void SimDriver::noteCaptures(int8_t side, int r, int c) {
  const int now = game_.captured(side);
  if (now > lastCaptured_[side]) pushEvent(EventKind::Capture, side, now - lastCaptured_[side], r, c);
  lastCaptured_[side] = now;
}

void SimDriver::advance(uint32_t dtMs) {
  now_ += dtMs;
  game_.tick(now_);
  controller_.update(now_);
  gantry_.update(now_);
  {
    // Machine captures happen at the square of the job's piece.
    const JobView j = controller_.job();
    int r = -1, c = -1;
    if (j.pieceId >= 1 && j.pieceId <= game_.pieceCount()) {
      r = game_.pieces()[j.pieceId - 1].r;
      c = game_.pieces()[j.pieceId - 1].c;
    }
    noteCaptures(MACHINE, r, c);
  }
  // No route for a captured piece: treat it as moved off by hand so the game goes on.
  if (game_.phase() == Phase::Paused && game_.stuckId() >= 0) game_.onManualRemoval(game_.stuckId());

  if (controller_.wantsThink(now_)) {
    const Snapshot snap = controller_.takeSnapshot(now_);
    const AiMove m = think(snap, params_);
    const bool wasActive = controller_.job().active;
    controller_.onThinkResult(m, now_);  // always delivered: it clears the awaiting mark
    const JobView j = controller_.job();
    if (!wasActive && j.active && !j.removing) {
      machineMoves_++;
      for (int i = 0; i < game_.heldCount(); i++) lift_[game_.heldIds()[i]].responded = true;
    }
  }
  if (botOn_) runBot();
}

void SimDriver::runBot() {
  const BotAction a = bot_.decide(game_);
  if (a.kind == BotAction::Lift) {
    const LiftResult res = lift(a.r, a.c);
    bot_.onLifted(game_, res == LiftResult::Ok, a.r, a.c);
  } else if (a.kind == BotAction::Place) {
    const PlaceResult res = place(a.r, a.c);
    bot_.onPlaced(game_, res == PlaceResult::Ok || res == PlaceResult::Cancelled);
  }
}

LiftResult SimDriver::lift(int r, int c) {
  const LiftResult res = game_.onHumanLift(r, c, now_);
  if (res != LiftResult::Ok) return res;
  controller_.onHumanLift(now_);
  const int id = game_.heldIds()[game_.heldCount() - 1];
  // Did the lifted piece have a capturing destination (same as `lift` in the original)?
  Board b = game_.boardWithoutHeld();
  int d[MAX_N * MAX_N];
  const int k = dests(b, r, c, d);
  bool threat = false;
  for (int i = 0; i < k && !threat; i++) {
    int caps[MAX_N * MAX_N];
    b.cells[d[i]] = HUMAN;
    if (capturesAt(b, HUMAN, d[i] / b.n, d[i] % b.n, caps) > 0) threat = true;
    b.cells[d[i]] = EMPTY;
  }
  lift_[id] = LiftInfo{threat, false};
  return res;
}

PlaceResult SimDriver::place(int r, int c) {
  int heldBefore[2] = {-1, -1};
  const int nBefore = game_.heldCount();
  for (int i = 0; i < nBefore; i++) heldBefore[i] = game_.heldIds()[i];
  const int capsBefore = game_.captured(HUMAN);
  const PlaceResult res = game_.onHumanPlace(r, c, now_);
  if (res != PlaceResult::Ok && res != PlaceResult::Cancelled) return res;
  // Which piece left the hand?
  int placed = -1;
  for (int i = 0; i < nBefore; i++) {
    bool still = false;
    for (int j = 0; j < game_.heldCount(); j++)
      if (game_.heldIds()[j] == heldBefore[i]) still = true;
    if (!still) placed = heldBefore[i];
  }
  if (res == PlaceResult::Ok) {
    noteCaptures(HUMAN, r, c);
    if (placed > 0 && lift_[placed].threat && lift_[placed].responded && game_.captured(HUMAN) == capsBefore) {
      saves_++;
      pushEvent(EventKind::Save, HUMAN, 0, r, c);
    }
  }
  if (placed > 0) lift_[placed] = LiftInfo{false, false};
  return res;
}

State SimDriver::state() const {
  State s;
  std::memset(&s, 0, sizeof(s));
  s.phase = game_.phase();
  s.winner = game_.winner();
  const int n = game_.rules().n;
  s.n = n;
  const Board b = game_.boardWithoutHeld();
  std::memcpy(s.cells, b.cells, sizeof(s.cells));
  s.headR = gantry_.headR();
  s.headC = gantry_.headC();
  s.job = controller_.job();
  const uint32_t clock = game_.clockMs();
  s.pieceCount = game_.pieceCount();
  for (int i = 0; i < s.pieceCount; i++) {
    const PieceView& p = game_.pieces()[i];
    PieceState& o = s.pieces[i];
    o.id = p.id;
    o.side = p.side;
    o.r = p.r;
    o.c = p.c;
    o.alive = p.alive;
    o.held = p.held;
    o.cooldownLeft = p.cooldownUntil > clock ? p.cooldownUntil - clock : 0;
    o.x = static_cast<float>(p.c);
    o.y = static_cast<float>(p.r);
    if (s.job.active && s.job.pieceId == p.id && s.job.stage == Stage::Drag) {
      o.x = s.headC;
      o.y = s.headR;
    }
  }
  ledPlan(game_, controller_, now_, s.leds);
  s.clockMs = clock;
  s.capturedHuman = game_.captured(HUMAN);
  s.capturedMachine = game_.captured(MACHINE);
  s.saves = saves_;
  s.machineMoves = machineMoves_;
  s.stuckId = game_.stuckId();
  s.eventCount = eventCount_;
  for (int i = 0; i < eventCount_; i++) {
    const int idx = (eventHead_ - 1 - i + 2 * MAX_EVENTS) % MAX_EVENTS;
    s.events[i] = events_[idx];
  }
  return s;
}
