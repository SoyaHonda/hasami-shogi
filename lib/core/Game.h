#pragma once
#include <cstdint>
#include "Ai.h"
#include "Board.h"
#include "Capture.h"
#include "Rules.h"
#include "Types.h"

// OpeningDone: during Phase::Opening the human has made the opening move already (RW).
enum class LiftResult { Ok, InCooldown, NotHumanPiece, NotPlaying, TooMany, OpeningDone };
// Pending: the placement came less than Rules::humanIntervalMs after the previous one; the piece
// stays held and lands when the interval has passed (see onHumanPlace).
enum class PlaceResult { Ok, Cancelled, Illegal, NoHeldPiece, NotPlaying, Pending };

// Result of an arrival: number of pieces captured and whether that decided the game.
struct CaptureResult {
  int count;
  bool over;
};

// A captured piece waiting to be carried off: from its square to reserve row toRow (-1 behind
// the machine's side or n in front of the human's, whichever is nearer; on a tie the piece's
// own side), column slotC chosen by pickReserveSlot.
struct Removal {
  int pieceId;
  Sq from;
  int toRow;
  int slotC;
};

// Latest violation: square and caller time (`now`, not the game clock).
struct Violation {
  bool valid;
  int r, c;
  uint32_t at;
};

// Game state machine. Times passed in (`now`) are the caller's clock in ms; the game clock
// advances only during Play and cooldowns are measured on it.
//
// Rule candidates (plan 2026-11-06 3.1, Rules): captures are judged by captures() of Capture.h
// (RE, RU); under RW a placement that makes a wall is Illegal, a machine piece does not enter a
// square where it would complete one (machineCanEnter), and the start countdown leads to
// Phase::Opening. There each side moves one piece to another square (a cancel does not count; a
// machine piece stopped short by a human piece in its way counts once it has moved a square),
// then Play begins. The game clock stays stopped during the opening, so cooldowns, the
// placement interval and the time limit start counting with Play; only the hold limit counts,
// on the caller's clock while in Opening. A cooldown does not hold a lift in Opening, and the
// hold limit there puts the piece back without one (on the stopped clock either would last the
// whole opening, and with every human piece locked the opening could never end).
class Game {
 public:
  explicit Game(const Rules& rules = Rules());

  // Rules take effect at the next reset().
  void setRules(const Rules& rules) { pendingRules_ = rules; }
  const Rules& rules() const { return rules_; }

  void reset();
  void start(uint32_t now);
  // Advances time and runs the timers: a pending placement lands (or is dropped) once the
  // interval has passed, a piece held for maxHoldMs goes back to its square, and the time limit
  // ends the game. A countdown ends in Play, or in Opening while an opening move is still to be
  // made.
  void tick(uint32_t now);
  // Play (or Opening) -> Paused -> Countdown -> Play (or Opening). Does not resume while
  // mismatch() is set.
  void togglePause(uint32_t now);

  // Board-mismatch pause (plan 2026-10-31 3.2; firmware only). cells: squares (r*n+c) where the
  // sensors disagree with the game. From Play, Opening or Removing (otherwise ignored): goes to Paused,
  // remembers the phase and sets mismatch(). A human piece that should be on one of the cells
  // gets a human cooldown; when a cell holds no piece in the game, the held pieces (one may have
  // been put there) get it too, so putting a held piece back still carries it.
  void pauseForMismatch(const int cells[], int count, uint32_t now);
  // Board agrees again: clears mismatch() and goes back to Removing, or to Play through a
  // resumeCountdownMs countdown (mismatchCountdown() is set during that countdown).
  void resumeFromMismatch(uint32_t now);
  bool mismatch() const { return mismatch_; }
  // In the countdown entered by resumeFromMismatch (MachineController reads during it).
  bool mismatchCountdown() const { return phase_ == Phase::Countdown && mismatchCountdown_; }

  // From Play or Opening. In Opening only one piece is in hand, and none once the human's opening
  // move is made (OpeningDone, a violation); cooldowns do not refuse a lift there.
  LiftResult onHumanLift(int r, int c, uint32_t now);
  // placedAt: caller time the piece was put down (<= now; the firmware's sensor confirms a
  // placement some time after it happened). The placement interval and the hold limit are
  // judged at placedAt. A placement less than humanIntervalMs after the previous one returns
  // Pending: the piece stays held (off the board) until tick() reaches the end of the interval.
  // While pending, the same piece put on its origin cancels and put elsewhere replaces the target.
  // In Opening the placement interval does not apply and a placement that lands is the human's
  // opening move.
  PlaceResult onHumanPlace(int r, int c, uint32_t now, uint32_t placedAt);
  PlaceResult onHumanPlace(int r, int c, uint32_t now) { return onHumanPlace(r, c, now, now); }

  // True (and reserves the square) when (r,c) is empty, not reserved and not blocked for the
  // machine (origin of a held piece, target of a pending placement). False in Opening once the
  // machine's opening move is made. False under RW where the piece, coming to rest on (r,c),
  // would complete a wall (makesWall): a drag can stop on any square it enters, so it stops one
  // square before such a square (user decision 2026-11-07).
  bool machineCanEnter(int pieceId, int r, int c);
  // Origin of a held piece or target of the pending placement: the machine must not go there.
  bool machineBlocked(int r, int c) const;
  // Moves the piece to (r,c) and releases the reservation. Also valid while Removing.
  void onMachineStep(int pieceId, int r, int c);
  // Sets the machine cooldown and checks captures by the machine at the piece's square. In
  // Opening, the arrival of a piece that has moved at least one square is the machine's opening
  // move.
  CaptureResult onMachineArrive(int pieceId, uint32_t now);
  // Job aborted: releases the reservation (no arrival). The cooldown is set only if the piece
  // moved at least one square; a piece that never left its square can move again at once.
  void onMachineAbort(int pieceId, uint32_t now, bool moved);

  // Pieces waiting to be carried off, in id order. `out` must hold 2*MAX_N entries.
  int removals(Removal out[]) const;
  // Slots taken in reserve row toRow (-1 or n). Both sides share the two rows.
  const bool* reserveUsed(int toRow) const { return reserveUsed_[toRow < 0 ? 0 : 1]; }
  void onRemoved(int pieceId, int toRow, int slotC);
  void onManualRemoval(int pieceId);
  void onRemovalStuck(int pieceId, uint32_t now);
  int stuckId() const { return stuckId_; }

  void reportViolation(int r, int c, uint32_t now);
  Violation lastViolation() const { return violation_; }

  Phase phase() const { return phase_; }
  Winner winner() const { return winner_; }
  uint32_t clockMs() const { return clock_; }
  int captured(int8_t side) const { return captured_[side]; }
  const PieceView* pieces() const { return pieces_; }
  int pieceCount() const { return count_; }
  const int* heldIds() const { return held_; }
  int heldCount() const { return heldCount_; }
  // Reserved square, or {-1,-1} when none.
  Sq reservedCell() const;
  int reservedBy() const { return reservedBy_; }
  Board boardWithoutHeld() const;
  // boardWithoutHeld plus the reserved square marked as MACHINE.
  Board boardWithReserve() const;
  // boardWithReserve plus the squares of machineBlocked marked as HUMAN (for machine moves).
  Board boardForMachine() const;
  // Held piece whose placement is pending, or -1; its target square ({-1,-1} when none).
  int pendingId() const { return pendingId_; }
  Sq pendingCell() const;
  // Pieces put back by the hold limit since reset().
  int holdTimeouts() const { return holdTimeouts_; }
  // Whether a human placement has landed since reset(), and its game clock (the next placement
  // lands no earlier than lastPlaceMs() + Rules::humanIntervalMs).
  bool hasLastPlace() const { return hasLastPlace_; }
  uint32_t lastPlaceMs() const { return lastPlaceClock_; }
  // Whether `side` (HUMAN or MACHINE) has made its opening move (RW); always true without RW.
  bool openingDone(int8_t side) const { return openingDone_[side == HUMAN ? HUMAN : MACHINE]; }
  // Squares of the pieces on the board not moved to another square for Rules::untouchedMs (RU);
  // empty when RU is off.
  SquareSet stale() const;
  Snapshot snapshot(float headR, float headC) const;

 private:
  PieceView* byId(int id);
  // Alive, not held piece at (r,c), or nullptr.
  PieceView* pieceAt(int r, int c);
  // Advances time to `now`: the game clock in Play, the countdown in Countdown.
  void advance(uint32_t now);
  void enterCountdown(uint32_t ms, bool fresh);
  void unhold(int id);
  // Game clock at caller time t (<= now, after advance(now)); never before 0.
  uint32_t clockAt(uint32_t t) const;
  // The same for the opening clock (caller time spent in Opening).
  uint32_t openingClockAt(uint32_t t) const;
  // Opening -> Play once both sides have made their opening move.
  void finishOpening();
  // The machine's opening move is made (when the piece that moved arrives or is aborted).
  void machineOpeningMove(int pieceId);
  // Whether the held piece p may be put on (r,c) of b: as a destination, or (escape) by the
  // escape rule (origin filled and no destination).
  bool placeLegal(const PieceView& p, const Board& b, int r, int c, bool escape) const;
  // Puts the held piece down at (r,c) at game clock `at`: cooldown, interval start, captures.
  void land(PieceView& p, int r, int c, uint32_t at);
  // Hold limit ran out: the piece goes back to its square with a cooldown and a violation.
  void expireHold(PieceView& p, uint32_t at, uint32_t now);
  void runTimers(uint32_t now);
  CaptureResult resolveCaptures(int8_t side, int r, int c);
  Winner decideWinner() const;
  int nearerReserveRow(const PieceView& p) const;
  bool removalsLeft() const;
  void finishRemoval(int pieceId);

  Rules rules_, pendingRules_;
  Phase phase_;
  Winner winner_;
  uint32_t clock_;
  uint32_t lastNow_;
  bool lastNowValid_;
  uint32_t countdownLeft_;
  bool countdownFresh_;
  PieceView pieces_[2 * MAX_N];
  int count_;
  int held_[2];
  int heldCount_;
  bool hasLastPlace_;
  uint32_t lastPlaceClock_;  // game clock of the latest placement that landed
  int pendingId_;
  int pendingR_, pendingC_;
  int holdTimeouts_;
  int reservedR_, reservedC_, reservedBy_;
  int captured_[3];
  bool removalPending_[2 * MAX_N + 1];
  bool reserveUsed_[2][MAX_N];
  int stuckId_;
  Violation violation_;
  bool mismatch_;
  Phase mismatchFrom_;
  bool mismatchCountdown_;
  // Opening (RW): which side has made its move, the machine piece that has stepped for it (-1),
  // caller time spent in Opening and its value when the held piece was lifted.
  bool openingDone_[3];
  int openingMover_;
  uint32_t openingClock_;
  uint32_t liftOpening_;
};
