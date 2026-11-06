#include "Game.h"
#include <cstdlib>
#include <cstring>
#include "Path.h"

Game::Game(const Rules& rules) : rules_(rules), pendingRules_(rules) { reset(); }

void Game::reset() {
  rules_ = pendingRules_;
  if (rules_.n > MAX_N) rules_.n = MAX_N;
  if (rules_.n < 3) rules_.n = 3;
  const int n = rules_.n;
  phase_ = Phase::Ready;
  winner_ = Winner::None;
  clock_ = 0;
  lastNow_ = 0;
  lastNowValid_ = false;
  countdownLeft_ = 0;
  countdownFresh_ = false;
  count_ = 0;
  // Same id order as the original simulator: per column, machine (row 0) then human (row n-1).
  for (int c = 0; c < n; c++) {
    pieces_[count_] = PieceView{count_ + 1, MACHINE, 0, c, 0, true, false};
    count_++;
    pieces_[count_] = PieceView{count_ + 1, HUMAN, n - 1, c, 0, true, false};
    count_++;
  }
  if (rules_.maxHeld < 1) rules_.maxHeld = 1;
  if (rules_.maxHeld > 2) rules_.maxHeld = 2;
  held_[0] = held_[1] = -1;
  heldCount_ = 0;
  hasLastPlace_ = false;
  lastPlaceClock_ = 0;
  pendingId_ = -1;
  pendingR_ = pendingC_ = -1;
  holdTimeouts_ = 0;
  reservedR_ = reservedC_ = reservedBy_ = -1;
  captured_[0] = captured_[1] = captured_[2] = 0;
  std::memset(removalPending_, 0, sizeof(removalPending_));
  std::memset(reserveUsed_, 0, sizeof(reserveUsed_));
  stuckId_ = -1;
  violation_ = Violation{false, -1, -1, 0};
  mismatch_ = false;
  mismatchFrom_ = Phase::Play;
  mismatchCountdown_ = false;
  openingDone_[0] = true;
  openingDone_[HUMAN] = openingDone_[MACHINE] = !rules_.wallLimit;
  openingMover_ = -1;
  openingClock_ = 0;
  liftOpening_ = 0;
}

PieceView* Game::byId(int id) {
  if (id < 1 || id > count_) return nullptr;
  return &pieces_[id - 1];
}

PieceView* Game::pieceAt(int r, int c) {
  for (int i = 0; i < count_; i++) {
    PieceView& p = pieces_[i];
    if (p.alive && !p.held && p.r == r && p.c == c) return &p;
  }
  return nullptr;
}

void Game::advance(uint32_t now) {
  uint32_t dt = lastNowValid_ ? now - lastNow_ : 0;
  if (countdownFresh_) {
    // Countdown entered from a call without a time: start counting from here.
    dt = 0;
    countdownFresh_ = false;
  }
  lastNow_ = now;
  lastNowValid_ = true;
  if (phase_ == Phase::Play) {
    clock_ += dt;
    // The clock stops at the time limit (tick() then ends the game).
    if (rules_.timeLimitMs > 0 && clock_ > rules_.timeLimitMs) clock_ = rules_.timeLimitMs;
  } else if (phase_ == Phase::Opening) {
    openingClock_ += dt;
  } else if (phase_ == Phase::Countdown) {
    countdownLeft_ = dt >= countdownLeft_ ? 0 : countdownLeft_ - dt;
  }
}

void Game::enterCountdown(uint32_t ms, bool fresh) {
  phase_ = Phase::Countdown;
  mismatchCountdown_ = false;
  countdownLeft_ = ms;
  countdownFresh_ = fresh;
}

void Game::start(uint32_t now) {
  if (phase_ != Phase::Ready && phase_ != Phase::Over) {
    advance(now);
    return;
  }
  reset();
  lastNow_ = now;
  lastNowValid_ = true;
  enterCountdown(rules_.countdownMs, false);
}

void Game::tick(uint32_t now) {
  advance(now);
  if (phase_ == Phase::Countdown && countdownLeft_ == 0) {
    phase_ = openingDone_[HUMAN] && openingDone_[MACHINE] ? Phase::Play : Phase::Opening;
    mismatchCountdown_ = false;
  }
  runTimers(now);
}

void Game::runTimers(uint32_t now) {
  if (phase_ == Phase::Opening) {
    // The game clock is stopped; the hold limit counts on the opening clock (one piece in hand).
    if (rules_.maxHoldMs > 0) {
      for (int i = heldCount_ - 1; i >= 0; i--) {
        PieceView* p = byId(held_[i]);
        if (p && openingClock_ >= liftOpening_ + rules_.maxHoldMs) expireHold(*p, clock_, now);
      }
    }
    return;
  }
  if (phase_ != Phase::Play) return;
  // A pending placement lands at the end of the interval if it is still legal; otherwise it is
  // dropped (a violation) and the piece is held again.
  if (pendingId_ >= 0) {
    const uint32_t due = lastPlaceClock_ + rules_.humanIntervalMs;
    if (clock_ >= due) {
      PieceView* p = byId(pendingId_);
      const int r = pendingR_, c = pendingC_;
      pendingId_ = -1;
      pendingR_ = pendingC_ = -1;
      if (p && p->held) {
        const Board b = boardWithReserve();
        if (placeLegal(*p, b, r, c, false) || placeLegal(*p, b, r, c, true)) {
          land(*p, r, c, due);
        } else {
          reportViolation(r, c, now);
          if (rules_.maxHoldMs > 0 && due >= p->liftedAt + rules_.maxHoldMs) expireHold(*p, due, now);
        }
      }
    }
  }
  if (phase_ != Phase::Play) return;
  // Hold limit (a pending piece does not time out).
  if (rules_.maxHoldMs > 0) {
    for (int i = heldCount_ - 1; i >= 0; i--) {
      PieceView* p = byId(held_[i]);
      if (!p || p->id == pendingId_) continue;
      const uint32_t until = p->liftedAt + rules_.maxHoldMs;
      if (clock_ >= until) expireHold(*p, until, now);
    }
  }
  // Time limit: more captures wins, equal is a draw.
  if (rules_.timeLimitMs > 0 && clock_ >= rules_.timeLimitMs) {
    const int h = captured_[HUMAN], m = captured_[MACHINE];
    winner_ = h > m ? Winner::Human : m > h ? Winner::Machine : Winner::Draw;
    phase_ = Phase::Over;
  }
}

uint32_t Game::clockAt(uint32_t t) const {
  if (!lastNowValid_ || static_cast<int32_t>(lastNow_ - t) <= 0) return clock_;
  const uint32_t back = lastNow_ - t;
  return back >= clock_ ? 0 : clock_ - back;
}

uint32_t Game::openingClockAt(uint32_t t) const {
  if (!lastNowValid_ || static_cast<int32_t>(lastNow_ - t) <= 0) return openingClock_;
  const uint32_t back = lastNow_ - t;
  return back >= openingClock_ ? 0 : openingClock_ - back;
}

void Game::finishOpening() {
  if (phase_ == Phase::Opening && openingDone_[HUMAN] && openingDone_[MACHINE]) phase_ = Phase::Play;
}

void Game::machineOpeningMove(int pieceId) {
  if (openingDone_[MACHINE] || openingMover_ != pieceId) return;
  openingDone_[MACHINE] = true;
  openingMover_ = -1;
  finishOpening();
}

bool Game::placeLegal(const PieceView& p, const Board& b, int r, int c, bool escape) const {
  if (!b.inside(r, c) || b.at(r, c) != EMPTY) return false;
  if (makesWall(b, rules_, HUMAN, p.r, p.c, r, c)) return false;  // RW
  int d[MAX_N * MAX_N];
  const int k = dests(b, p.r, p.c, d);
  if (!escape) {
    for (int j = 0; j < k; j++)
      if (d[j] == r * b.n + c) return true;
    return false;
  }
  // Escape: a piece whose origin is filled and which has no legal destination may go to any
  // empty square (otherwise the game would stall).
  return k == 0 && b.at(p.r, p.c) != EMPTY;
}

void Game::land(PieceView& p, int r, int c, uint32_t at) {
  unhold(p.id);
  p.r = r;
  p.c = c;
  p.cooldownUntil = at + rules_.humanCooldownMs;
  p.movedAt = at;
  hasLastPlace_ = true;
  lastPlaceClock_ = at;
  const bool opening = phase_ == Phase::Opening;
  resolveCaptures(HUMAN, r, c);
  if (opening) {
    openingDone_[HUMAN] = true;
    finishOpening();
  }
}

void Game::expireHold(PieceView& p, uint32_t at, uint32_t now) {
  if (pendingId_ == p.id) {
    pendingId_ = -1;
    pendingR_ = pendingC_ = -1;
  }
  unhold(p.id);
  // In Opening the game clock is stopped, so a cooldown would last the whole opening: the piece
  // only goes back to its square there (a violation).
  if (phase_ != Phase::Opening) p.cooldownUntil = at + rules_.humanCooldownMs;
  holdTimeouts_++;
  reportViolation(p.r, p.c, now);
}

void Game::togglePause(uint32_t now) {
  advance(now);
  if (phase_ == Phase::Play || phase_ == Phase::Opening) {
    phase_ = Phase::Paused;
  } else if (phase_ == Phase::Paused && stuckId_ < 0 && !mismatch_) {
    enterCountdown(rules_.resumeCountdownMs, false);
  }
}

void Game::pauseForMismatch(const int cells[], int count, uint32_t now) {
  advance(now);
  if (phase_ != Phase::Play && phase_ != Phase::Removing && phase_ != Phase::Opening) return;
  const int n = rules_.n;
  const uint32_t until = clock_ + rules_.humanCooldownMs;
  auto cool = [&](PieceView& p) {
    if (p.side == HUMAN && p.cooldownUntil < until) p.cooldownUntil = until;
  };
  for (int i = 0; i < count; i++) {
    if (cells[i] < 0 || cells[i] >= n * n) continue;
    const int r = cells[i] / n, c = cells[i] % n;
    PieceView* p = pieceAt(r, c);
    if (p) {
      cool(*p);
    } else {
      for (int h = 0; h < heldCount_; h++) {
        PieceView* q = byId(held_[h]);
        if (q) cool(*q);
      }
    }
  }
  mismatchFrom_ = phase_;
  phase_ = Phase::Paused;
  mismatch_ = true;
}

void Game::resumeFromMismatch(uint32_t now) {
  advance(now);
  if (!mismatch_) return;
  mismatch_ = false;
  if (phase_ != Phase::Paused) return;
  if (mismatchFrom_ == Phase::Removing) {
    phase_ = Phase::Removing;
  } else {
    enterCountdown(rules_.resumeCountdownMs, false);
    mismatchCountdown_ = true;
  }
}

LiftResult Game::onHumanLift(int r, int c, uint32_t now) {
  advance(now);
  const bool opening = phase_ == Phase::Opening;
  if (phase_ != Phase::Play && !opening) return LiftResult::NotPlaying;
  PieceView* p = pieceAt(r, c);
  if (!p || p->side != HUMAN) return LiftResult::NotHumanPiece;
  if (opening && openingDone_[HUMAN]) {
    reportViolation(r, c, now);
    return LiftResult::OpeningDone;
  }
  // Cooldowns run on the game clock, which is stopped in Opening: there they do not hold a lift
  // (a piece cooled by a mismatch pause would otherwise stay locked for the whole opening, and
  // with every piece locked the opening could never end). They count from the start of Play.
  if (!opening && clock_ < p->cooldownUntil) {
    reportViolation(r, c, now);
    return LiftResult::InCooldown;
  }
  if (heldCount_ >= (opening ? 1 : rules_.maxHeld)) {
    reportViolation(r, c, now);
    return LiftResult::TooMany;
  }
  p->held = true;
  held_[heldCount_++] = p->id;
  p->liftedAt = clock_;
  liftOpening_ = openingClock_;
  return LiftResult::Ok;
}

void Game::unhold(int id) {
  PieceView* p = byId(id);
  if (p) p->held = false;
  if (pendingId_ == id) {
    pendingId_ = -1;
    pendingR_ = pendingC_ = -1;
  }
  int k = 0;
  for (int i = 0; i < heldCount_; i++)
    if (held_[i] != id) held_[k++] = held_[i];
  for (int i = k; i < 2; i++) held_[i] = -1;
  heldCount_ = k;
}

PlaceResult Game::onHumanPlace(int r, int c, uint32_t now, uint32_t placedAt) {
  advance(now);
  const bool opening = phase_ == Phase::Opening;
  if (phase_ != Phase::Play && !opening) return PlaceResult::NotPlaying;
  if (heldCount_ == 0) return PlaceResult::NoHeldPiece;
  const uint32_t at = clockAt(placedAt);
  const Board b = boardWithReserve();
  if (!b.inside(r, c)) return PlaceResult::Illegal;
  if (r == reservedR_ && c == reservedC_) {
    reportViolation(r, c, now);
    return PlaceResult::Illegal;
  }
  // While a placement is pending only that piece can be placed (its origin cancels).
  // Back on its own (still empty) square: cancel, no cooldown, no capture check. A cancel is
  // not a placement, so the interval keeps running from the previous one.
  for (int i = 0; i < heldCount_; i++) {
    PieceView* p = byId(held_[i]);
    if (pendingId_ >= 0 && p->id != pendingId_) continue;
    if (p->r == r && p->c == c && b.at(r, c) == EMPTY) {
      unhold(p->id);
      return PlaceResult::Cancelled;
    }
  }
  if (b.at(r, c) != EMPTY) {
    reportViolation(r, c, now);
    return PlaceResult::Illegal;
  }
  PieceView* chosen = nullptr;
  int chosenDist = 0;
  // Held pieces are kept in lift order; compare by id explicitly for ties.
  // A piece for which (r,c) is a destination first (nearest origin), then the escape rule.
  for (int pass = 0; pass < 2 && !chosen; pass++) {
    for (int i = 0; i < heldCount_; i++) {
      PieceView* p = byId(held_[i]);
      if (pendingId_ >= 0 && p->id != pendingId_) continue;
      if (!placeLegal(*p, b, r, c, pass == 1)) continue;
      int dist = pass == 0 ? std::abs(p->r - r) + std::abs(p->c - c) : 0;
      if (!chosen || dist < chosenDist || (dist == chosenDist && p->id < chosen->id)) {
        chosen = p;
        chosenDist = dist;
      }
    }
  }
  if (!chosen) {
    reportViolation(r, c, now);
    return PlaceResult::Illegal;
  }
  // Hold limit, judged at the time the piece was put down (a pending piece does not time out). In
  // Opening on the opening clock, the game clock being stopped.
  if (opening) {
    if (rules_.maxHoldMs > 0 && openingClockAt(placedAt) >= liftOpening_ + rules_.maxHoldMs) {
      expireHold(*chosen, clock_, now);
      return PlaceResult::Illegal;
    }
  } else if (chosen->id != pendingId_ && rules_.maxHoldMs > 0 && at >= chosen->liftedAt + rules_.maxHoldMs) {
    expireHold(*chosen, chosen->liftedAt + rules_.maxHoldMs, now);
    return PlaceResult::Illegal;
  }
  // Placement interval: too early -> pending until the interval has passed (tick lands it). Not
  // in Opening (one placement there).
  if (!opening && hasLastPlace_ && at < lastPlaceClock_ + rules_.humanIntervalMs) {
    pendingId_ = chosen->id;
    pendingR_ = r;
    pendingC_ = c;
    return PlaceResult::Pending;
  }
  if (pendingId_ == chosen->id) {
    pendingId_ = -1;
    pendingR_ = pendingC_ = -1;
  }
  land(*chosen, r, c, at);
  return PlaceResult::Ok;
}

bool Game::machineBlocked(int r, int c) const {
  for (int i = 0; i < heldCount_; i++) {
    const PieceView& p = pieces_[held_[i] - 1];
    if (p.r == r && p.c == c) return true;
  }
  return pendingId_ >= 0 && pendingR_ == r && pendingC_ == c;
}

bool Game::machineCanEnter(int pieceId, int r, int c) {
  if (phase_ == Phase::Opening && openingDone_[MACHINE]) return false;  // one move in the opening
  const Board b = boardWithoutHeld();
  if (!b.inside(r, c) || b.at(r, c) != EMPTY || machineBlocked(r, c)) return false;
  if (reservedBy_ >= 0) return reservedBy_ == pieceId && reservedR_ == r && reservedC_ == c;
  reservedR_ = r;
  reservedC_ = c;
  reservedBy_ = pieceId;
  return true;
}

void Game::onMachineStep(int pieceId, int r, int c) {
  PieceView* p = byId(pieceId);
  if (!p) return;
  // A captured piece's removal origin is its square, so moving it updates the origin too.
  p->r = r;
  p->c = c;
  if (p->alive) {
    p->movedAt = clock_;
    // The piece that steps before the machine's opening move is made is that move's piece.
    if (!openingDone_[MACHINE] && p->side == MACHINE) openingMover_ = pieceId;
  }
  if (reservedBy_ == pieceId) reservedR_ = reservedC_ = reservedBy_ = -1;
}

CaptureResult Game::onMachineArrive(int pieceId, uint32_t now) {
  advance(now);
  PieceView* p = byId(pieceId);
  if (!p) return CaptureResult{0, false};
  p->cooldownUntil = clock_ + rules_.machineCooldownMs;
  if (!p->alive || phase_ == Phase::Over) return CaptureResult{0, false};
  const CaptureResult cr = resolveCaptures(MACHINE, p->r, p->c);
  machineOpeningMove(pieceId);  // a piece that never left its square is not the opening move
  return cr;
}

void Game::onMachineAbort(int pieceId, uint32_t now, bool moved) {
  advance(now);
  PieceView* p = byId(pieceId);
  if (!p) return;
  if (reservedBy_ == pieceId) reservedR_ = reservedC_ = reservedBy_ = -1;
  if (moved) {
    p->cooldownUntil = clock_ + rules_.machineCooldownMs;
    machineOpeningMove(pieceId);
  }
}

Winner Game::decideWinner() const {
  const int h = captured_[HUMAN], m = captured_[MACHINE];
  if (h >= rules_.target || (rules_.diffOn && h - m >= rules_.diff)) return Winner::Human;
  if (m >= rules_.target || (rules_.diffOn && m - h >= rules_.diff)) return Winner::Machine;
  return Winner::None;
}

CaptureResult Game::resolveCaptures(int8_t side, int r, int c) {
  Board b = boardWithoutHeld();
  // A machine piece being dragged (it holds the reservation of the square it is sliding into)
  // is off the board for the human's captures, as a lifted human piece is for the machine's.
  if (side == HUMAN && reservedBy_ >= 0) {
    const PieceView* m = byId(reservedBy_);
    if (m && m->alive) b.set(m->r, m->c, EMPTY);
  }
  int caps[MAX_N * MAX_N];
  const SquareSet untouched = stale();
  const int k = captures(b, rules_, side, r, c, caps, &untouched);
  if (k == 0) return CaptureResult{0, false};
  int victims[MAX_N * MAX_N];
  int nv = 0;
  for (int i = 0; i < k; i++) {
    PieceView* q = pieceAt(caps[i] / b.n, caps[i] % b.n);
    if (q && q->side != side) {
      q->alive = false;
      victims[nv++] = q->id;
    }
  }
  captured_[side] += nv;
  winner_ = decideWinner();
  if (winner_ != Winner::None) {
    phase_ = Phase::Over;
    return CaptureResult{nv, true};
  }
  for (int i = 0; i < nv; i++) removalPending_[victims[i]] = true;
  phase_ = Phase::Removing;
  return CaptureResult{nv, false};
}

int Game::removals(Removal out[]) const {
  int k = 0;
  for (int i = 0; i < count_; i++) {
    const PieceView& p = pieces_[i];
    if (!removalPending_[p.id]) continue;
    const int toRow = nearerReserveRow(p);
    out[k++] = Removal{p.id, Sq{static_cast<int8_t>(p.r), static_cast<int8_t>(p.c)}, toRow,
                       pickReserveSlot(reserveUsed(toRow), rules_.n, p.c)};
  }
  return k;
}

bool Game::removalsLeft() const {
  for (int id = 1; id <= count_; id++)
    if (removalPending_[id]) return true;
  return false;
}

void Game::finishRemoval(int pieceId) {
  removalPending_[pieceId] = false;
  if (stuckId_ == pieceId) stuckId_ = -1;
  if (phase_ != Phase::Removing && phase_ != Phase::Paused) return;
  if (!removalsLeft()) {
    stuckId_ = -1;
    enterCountdown(rules_.resumeCountdownMs, true);
  } else if (phase_ == Phase::Paused && stuckId_ < 0) {
    phase_ = Phase::Removing;
  }
}

int Game::nearerReserveRow(const PieceView& p) const {
  const int toTop = p.r + 1, toBottom = rules_.n - p.r;
  if (toTop != toBottom) return toTop < toBottom ? -1 : rules_.n;
  return p.side == HUMAN ? rules_.n : -1;
}

void Game::onRemoved(int pieceId, int toRow, int slotC) {
  PieceView* p = byId(pieceId);
  if (!p || !removalPending_[pieceId]) return;
  if (slotC >= 0 && slotC < rules_.n) reserveUsed_[toRow < 0 ? 0 : 1][slotC] = true;
  finishRemoval(pieceId);
}

void Game::onManualRemoval(int pieceId) {
  PieceView* p = byId(pieceId);
  if (!p || !removalPending_[pieceId]) return;
  // The exact slot is unknown; assume the one the machine would have used.
  const int row = nearerReserveRow(*p);
  const int slot = pickReserveSlot(reserveUsed(row), rules_.n, p->c);
  if (slot >= 0) reserveUsed_[row < 0 ? 0 : 1][slot] = true;
  finishRemoval(pieceId);
}

void Game::onRemovalStuck(int pieceId, uint32_t now) {
  advance(now);
  phase_ = Phase::Paused;
  stuckId_ = pieceId;
}

void Game::reportViolation(int r, int c, uint32_t now) { violation_ = Violation{true, r, c, now}; }

SquareSet Game::stale() const {
  SquareSet s;
  s.clear();
  if (rules_.untouchedMs == 0) return s;
  for (int i = 0; i < count_; i++) {
    const PieceView& p = pieces_[i];
    if (p.alive && !p.held && clock_ >= p.movedAt + rules_.untouchedMs) s.set(p.r * rules_.n + p.c);
  }
  return s;
}

Sq Game::reservedCell() const {
  if (reservedBy_ < 0) return Sq{-1, -1};
  return Sq{static_cast<int8_t>(reservedR_), static_cast<int8_t>(reservedC_)};
}

Board Game::boardWithoutHeld() const {
  Board b{};
  b.n = rules_.n;
  std::memset(b.cells, 0, sizeof(b.cells));
  for (int i = 0; i < count_; i++) {
    const PieceView& p = pieces_[i];
    if (p.alive && !p.held) b.set(p.r, p.c, p.side);
  }
  return b;
}

Board Game::boardWithReserve() const {
  Board b = boardWithoutHeld();
  if (reservedBy_ >= 0) b.set(reservedR_, reservedC_, MACHINE);
  return b;
}

Board Game::boardForMachine() const {
  Board b = boardWithReserve();
  for (int i = 0; i < heldCount_; i++) {
    const PieceView& p = pieces_[held_[i] - 1];
    if (b.at(p.r, p.c) == EMPTY) b.set(p.r, p.c, HUMAN);
  }
  if (pendingId_ >= 0 && b.at(pendingR_, pendingC_) == EMPTY) b.set(pendingR_, pendingC_, HUMAN);
  return b;
}

Sq Game::pendingCell() const {
  if (pendingId_ < 0) return Sq{-1, -1};
  return Sq{static_cast<int8_t>(pendingR_), static_cast<int8_t>(pendingC_)};
}

Snapshot Game::snapshot(float headR, float headC) const {
  Snapshot s{};
  s.board = boardWithoutHeld();
  for (int i = 0; i < count_; i++) s.pieces[i] = pieces_[i];
  s.count = count_;
  s.clockMs = clock_;
  s.headR = headR;
  s.headC = headC;
  s.heldIds[0] = held_[0];
  s.heldIds[1] = held_[1];
  s.heldCount = heldCount_;
  s.pending = pendingId_ >= 0;
  s.pendingR = pendingR_;
  s.pendingC = pendingC_;
  s.pendingId = pendingId_;
  s.hasLastPlace = hasLastPlace_;
  s.lastPlaceAt = lastPlaceClock_;
  s.reservedR = reservedBy_ >= 0 ? reservedR_ : -1;
  s.reservedC = reservedBy_ >= 0 ? reservedC_ : -1;
  s.humanCooldownMs = rules_.humanCooldownMs;
  s.machineCooldownMs = rules_.machineCooldownMs;
  s.humanIntervalMs = rules_.humanIntervalMs;
  s.maxHoldMs = rules_.maxHoldMs;
  s.edgeCapture = rules_.edgeCapture;
  s.wallLimit = rules_.wallLimit;
  s.untouchedMs = rules_.untouchedMs;
  s.mustMove = phase_ == Phase::Opening && !openingDone_[MACHINE];
  return s;
}
