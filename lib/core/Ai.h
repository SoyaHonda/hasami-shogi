#pragma once
#include <cstdint>
#include "Board.h"
#include "Params.h"
#include "Types.h"

// One piece as Game holds it and as Ai reads it. cooldownUntil and liftedAt are on the game
// clock; liftedAt is the time a held piece was lifted (meaningful only while held). movedAt is
// when it was last moved to another square (0 at the start; a cancel or the hold limit putting it
// back does not count): RU (plan 2026-11-06 3.1) takes a piece untouched for Rules::untouchedMs.
struct PieceView {
  int id;
  int8_t side;
  int r, c;
  uint32_t cooldownUntil;
  bool alive, held;
  uint32_t liftedAt = 0;
  uint32_t movedAt = 0;
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
  // Square the machine has reserved for a piece being dragged, or -1,-1 (humans cannot go there),
  // and the id of that piece (-1 for none): it is off the board for the human's captures, so RU
  // does not take it (its timer restarts at every square it enters).
  int reservedR = -1, reservedC = -1;
  int reservedId = -1;
  // Rule times the read needs (copied from Rules by Game::snapshot; defaults match Rules).
  uint32_t humanCooldownMs = 3000, machineCooldownMs = 1000, humanIntervalMs = 1000, maxHoldMs = 1000;
  // Rule candidates of plan 2026-11-06 3.1 (copied from Rules by Game::snapshot; off = R0): RE,
  // RW and RU. The read judges captures and legal moves with them (Capture.h).
  bool edgeCapture = false, wallLimit = false;
  uint32_t untouchedMs = 0;
  // Opening (RW): the machine has its one opening move to make, so "stay" is not a candidate.
  bool mustMove = false;
};

struct AiMove {
  bool none;
  int pieceId;
  int toR, toC;
  float score;
};

// A machine line (a run of machine pieces, plan 2026-10-31 3.4) starting at (r,c) in direction
// (dr,dc) and how likely the read thinks the human captures it. Units of the rule candidates
// follow the lines with dr = dc = 0: RE's edge groups (from (r,c), the group's first square) and,
// under RU, single pieces about to go untouched (len 1; RaceTime's RU units).
struct AiLineRisk {
  int r, c, dr, dc, len;
  float risk;  // 0..1
};

// What a read saw, for measuring the read (plan 2026-10-31 3.13). Filled only when asked for.
struct AiDebug {
  AiLineRisk lines[4 * MAX_N];
  int lineCount;
  // Risk of the machine piece on each square (r*n+c) on the snapshot's board; 0 elsewhere.
  float pieceRisk[MAX_N * MAX_N];
  // Candidate moves ("stay" included) and how many of them the read counts as safe.
  int candidates, safeCandidates;
  // Hard's fork check (plan 2026-10-31 3.9): safe candidates checked against the human's threat
  // moves (at most the top three) and how many of them lost the fork penalty.
  int forkChecked, forkPenalized;
};

// One candidate as think() weighs it (plan 2026-10-31 3.9), for the golden-table generator and
// tests.
struct AiCandidate {
  bool none;  // "stay"
  int pieceId;
  int toR, toC;
  bool safe;
  // Compared for the choice: the safe candidates when there is one, otherwise all of them.
  bool contending;
  float score;    // the score compared (fork penalty included); 0 when not contending
  float penalty;  // fork penalty taken off (Hard, top three safe candidates)
};

// "Stay" plus every legal move of up to MAX_N machine pieces. Under RW a move that makes a wall
// is not legal, so it is no candidate; nor is one whose drag passes a square where the piece
// would complete a wall (wallOnPath of Capture.h; the drag would stop before it). With
// Snapshot::mustMove "stay" is listed but never contends.
constexpr int AI_MAX_CANDIDATES = 1 + MAX_N * 2 * (MAX_N - 1);

// Machine move choice (plan 2026-10-31 3.9-3.11; pure function of the snapshot). Implemented in
// Ai.cpp. With `dbg`, also writes what the read saw there (does not change the move): the risk of
// each machine line and piece on the snapshot's board (3.7), the candidates and the safe ones.
AiMove think(const Snapshot& s, const Params& p, AiDebug* dbg = nullptr);
// The candidates think() weighs on s, in its order ("stay" first, then the machine pieces in id
// order and their destinations in dests() order). Writes at most `cap` and returns the count.
// think() picks the contending candidate with the highest score (ties within 0.01 by a hash of
// the snapshot and the candidate's number).
int thinkCandidates(const Snapshot& s, const Params& p, AiCandidate out[], int cap);
