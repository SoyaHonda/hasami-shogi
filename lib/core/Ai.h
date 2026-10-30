#pragma once
#include <cstdint>
#include "Board.h"
#include "Params.h"
#include "Types.h"

// One piece as Game holds it and as Ai reads it. cooldownUntil is on the game clock.
struct PieceView {
  int id;
  int8_t side;
  int r, c;
  uint32_t cooldownUntil;
  bool alive, held;
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
};

struct AiMove {
  bool none;
  int pieceId;
  int toR, toC;
  float score;
};

// Machine move choice (pure function of the snapshot). Implemented in Ai.cpp.
AiMove think(const Snapshot& s, const Params& p);
