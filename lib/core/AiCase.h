#pragma once
#include "Ai.h"
#include "Params.h"

// One line of test/golden/ai_cases.txt: a position, the parameters and the expected move. The
// PC tests, the WASM tests and the ESP32 `golden` command read the same table so that every
// build is checked to pick the same move.
//
// Line format (fields separated by spaces; empty lines and lines starting with '#' are skipped):
//   board held head cooldowns params expect
//   board     n*n characters '.', 'H', 'M' in row-major order (n = 3..MAX_N)
//   held      '-' or origins of held human pieces "r,c" joined by ';' (empty on the board)
//   head      "r,c" head position (floats)
//   cooldowns '-' or "r,c=ms" joined by ';': remaining cooldown of the piece at that square
//   params    "freeSpeed,dragSpeed,engageMs,reactMs,level,aggression,readLift" with level E/N/H
//             and readLift 0/1
//   expect    "none" or "r,c>r,c" (from square > to square)
// Piece ids are given in row-major order over the squares with a piece or a held origin. The
// snapshot clock is AI_CASE_CLOCK_MS; cooldownUntil = clock + remaining.
constexpr uint32_t AI_CASE_CLOCK_MS = 100000;

struct AiCase {
  Snapshot snap;
  Params params;
  bool expectNone;
  int fromR, fromC, toR, toC;
};

// Parses one line. Returns false for a malformed line (and for blank/comment lines).
bool parseAiCase(const char* line, AiCase& out);
// True for lines parseAiCase is meant to skip (blank or comment).
bool isAiCaseSkippable(const char* line);
// think() on the case and whether its move matches the expectation.
bool checkAiCase(const AiCase& c, AiMove* got = nullptr);
