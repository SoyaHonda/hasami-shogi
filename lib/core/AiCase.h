#pragma once
#include "Ai.h"
#include "Params.h"

// One line of test/golden/ai_cases.txt: a position, the parameters and the expected move. The
// PC tests, the WASM tests and the ESP32 `golden` command read the same table so that every
// build is checked to pick the same move.
//
// Line format (fields separated by spaces; empty lines and lines starting with '#' are skipped):
//   board held head cooldowns [gap lift] params [rules] expect   (rules only with gap and lift)
//   board     n*n characters '.', 'H', 'M' in row-major order (n = 3..MAX_N)
//   held      '-' or origins of held human pieces "r,c" joined by ';' (empty on the board)
//   head      "r,c" head position (floats)
//   cooldowns '-' or "r,c=ms" joined by ';': remaining cooldown of the piece at that square
//   gap       '-' (no placement yet) or "ms": placement interval left (0..humanIntervalMs); the
//             last placement was at clock + ms - humanIntervalMs
//   lift      '-' (just lifted) or "ms" joined by ';' in the order of `held`: time since each
//             held piece was lifted
//   params    "freeSpeed,dragSpeed,engageMs,reactMs,level,aggression,readLift" with level E/N/H
//             and readLift 0/1
//   rules     '-' (R0) or the rule candidates of plan 2026-11-06 3.1 joined by '+': RE, RW,
//             RU<ms> (RU with untouchedMs = ms; every piece counts as untouched since clock 0),
//             OPEN (Snapshot::mustMove: the machine's opening move)
//   expect    "none" or "r,c>r,c" (from square > to square)
// gap and lift are optional together: a line with six fields (the older format) reads both as
// '-'. rules needs them: a line with nine fields has gap, lift, params, rules; eight fields read
// rules as '-'. Piece ids are given in row-major order over the squares with a piece or a held
// origin. The snapshot clock is AI_CASE_CLOCK_MS; cooldownUntil = clock + remaining. Rule times
// are the Snapshot defaults (those of Rules).
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
