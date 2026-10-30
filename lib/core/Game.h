#pragma once
#include <cstdint>
#include "Ai.h"
#include "Board.h"
#include "Rules.h"
#include "Types.h"

enum class LiftResult { Ok, InCooldown, NotHumanPiece, NotPlaying, TooMany };
enum class PlaceResult { Ok, Cancelled, Illegal, NoHeldPiece, NotPlaying };

// Result of an arrival: number of pieces captured and whether that decided the game.
struct CaptureResult {
  int count;
  bool over;
};

// A captured piece waiting to be carried off: from its square to reserve row toRow
// (n for human pieces, -1 for machine pieces), column slotC chosen by pickReserveSlot.
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
class Game {
 public:
  explicit Game(const Rules& rules = Rules());

  // Rules take effect at the next reset().
  void setRules(const Rules& rules) { pendingRules_ = rules; }
  const Rules& rules() const { return rules_; }

  void reset();
  void start(uint32_t now);
  void tick(uint32_t now);
  void togglePause(uint32_t now);

  LiftResult onHumanLift(int r, int c, uint32_t now);
  PlaceResult onHumanPlace(int r, int c, uint32_t now);

  // True (and reserves the square) when (r,c) is empty and not reserved. Squares of held
  // pieces count as empty.
  bool machineCanEnter(int pieceId, int r, int c);
  // Moves the piece to (r,c) and releases the reservation. Also valid while Removing.
  void onMachineStep(int pieceId, int r, int c);
  // Sets the machine cooldown and checks captures by the machine at the piece's square.
  CaptureResult onMachineArrive(int pieceId, uint32_t now);
  // Job aborted: releases the reservation and sets the cooldown (no arrival).
  void onMachineAbort(int pieceId, uint32_t now);

  // Pieces waiting to be carried off, in id order. `out` must hold 2*MAX_N entries.
  int removals(Removal out[]) const;
  const bool* reserveUsed(int8_t side) const { return reserveUsed_[side]; }
  void onRemoved(int pieceId, int slotC);
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
  Snapshot snapshot(float headR, float headC) const;

 private:
  PieceView* byId(int id);
  // Alive, not held piece at (r,c), or nullptr.
  PieceView* pieceAt(int r, int c);
  // Advances time to `now`: the game clock in Play, the countdown in Countdown.
  void advance(uint32_t now);
  void enterCountdown(uint32_t ms, bool fresh);
  void unhold(int id);
  CaptureResult resolveCaptures(int8_t side, int r, int c);
  Winner decideWinner() const;
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
  int reservedR_, reservedC_, reservedBy_;
  int captured_[3];
  bool removalPending_[2 * MAX_N + 1];
  bool reserveUsed_[3][MAX_N];
  int stuckId_;
  Violation violation_;
};
