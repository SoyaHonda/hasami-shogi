#pragma once
#include <cstdint>
#include "Board.h"
#include "Params.h"
#include "Types.h"

// One piece as Game holds it and as Ai reads it. cooldownUntil and liftedAt are on the game
// clock; liftedAt is the time a held piece was lifted (meaningful only while held).
struct PieceView {
  int id;
  int8_t side;
  int r, c;
  uint32_t cooldownUntil;
  bool alive, held;
  uint32_t liftedAt = 0;
};

// Copy of the position handed to think(). `board` excludes held pieces.
struct Snapshot {
  Board board;
  PieceView pieces[2 * MAX_N];
  int count;
  uint32_t clockMs;
  float headR, headC;
  int heldIds[2];
  int heldCount;
  // A held piece's placement waiting for the placement interval (plan 2026-10-31 3.1): its
  // target square. The machine treats held origins and this square as occupied.
  bool pending;
  int pendingR, pendingC;
  int pendingId = -1;  // the held piece whose placement is pending, or -1
  // Game clock of the latest human placement that landed (valid when hasLastPlace): the next
  // placement lands no earlier than lastPlaceAt + humanIntervalMs.
  bool hasLastPlace = false;
  uint32_t lastPlaceAt = 0;
  // Square the machine has reserved for a piece being dragged, or -1,-1 (humans cannot go there).
  int reservedR = -1, reservedC = -1;
  // Rule times the read needs (copied from Rules by Game::snapshot; defaults match Rules).
  uint32_t humanCooldownMs = 3000, machineCooldownMs = 1000, humanIntervalMs = 1000, maxHoldMs = 1000;
};

struct AiMove {
  bool none;
  int pieceId;
  int toR, toC;
  float score;
};

// Machine move choice (pure function of the snapshot). Implemented in Ai.cpp.
AiMove think(const Snapshot& s, const Params& p);
