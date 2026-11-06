// Generates test/golden/ai_cases.txt: random positions where think() (plan 2026-10-31 3.9-3.11)
// has a clear choice, so that the PC, WASM and ESP32 builds can be checked to pick the same move.
//
// Build and run from the project directory (-ffp-contract=off as every build of lib/core):
//   g++ -std=gnu++17 -ffp-contract=off -O2 -Ilib/core test/golden/gen_cases.cpp lib/core/*.cpp -o <out>
//   <out> [seed] > test/golden/ai_cases.txt
//   <out> 20261106 RE+RW >> test/golden/ai_cases.txt
//   <out> 20261107 RE+RW+OPEN >> test/golden/ai_cases.txt
// Every candidate's score goes to stderr ("# scores" lines); stdout is the table.
//
// With a rules field (lib/core/AiCase.h, plan 2026-11-06 3.1) every line carries it, and a
// position is kept only when think() under those rules picks another move than under R0 (so
// the rules decide the case); smaller quotas.
//
// A position is kept only when the best contending candidate's score is at least 0.1 above the
// second best (thinkCandidates gives the same candidates and scores think() compares). Quotas per
// level make the table cover a chosen move and "stay", positions with and without a safe
// candidate, and (hard) moves chosen after the fork penalty.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "Ai.h"
#include "AiCase.h"

namespace {

constexpr float MIN_GAP = 0.1f;

struct Rng {
  uint32_t x;
  uint32_t next() {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
  }
  int below(int k) { return static_cast<int>(next() % static_cast<uint32_t>(k)); }
};

// One random line in the format of lib/core/AiCase.h (with the gap and lift fields).
std::string randomLine(Rng& rng) {
  const int ns[4] = {5, 7, 7, 9};
  const int n = ns[rng.below(4)];
  std::string board(static_cast<size_t>(n * n), '.');
  const int machines = 2 + rng.below(n - 1), humans = 2 + rng.below(n - 1);
  auto place = [&](char ch, int count) {
    for (int k = 0; k < count;) {
      const int i = rng.below(n * n);
      if (board[i] != '.') continue;
      board[i] = ch;
      k++;
    }
  };
  place('M', machines);
  place('H', humans);
  char buf[64];
  // At most one held human piece (Rules::maxHeld = 1): its origin is an empty square.
  std::string held = "-", lift = "-";
  if (rng.below(3) == 0) {
    for (;;) {
      const int i = rng.below(n * n);
      if (board[i] != '.') continue;
      std::snprintf(buf, sizeof(buf), "%d,%d", i / n, i % n);
      held = buf;
      std::snprintf(buf, sizeof(buf), "%d", 50 * rng.below(19));
      lift = buf;
      break;
    }
  }
  std::snprintf(buf, sizeof(buf), "%g,%g", 0.25f * static_cast<float>(rng.below(4 * n - 3)),
                0.25f * static_cast<float>(rng.below(4 * n - 3)));
  const std::string head = buf;
  std::string cool;
  for (int i = 0; i < n * n; i++) {
    if (board[i] == '.' || rng.below(3) != 0) continue;
    std::snprintf(buf, sizeof(buf), "%s%d,%d=%d", cool.empty() ? "" : ";", i / n, i % n, 100 * (1 + rng.below(30)));
    cool += buf;
  }
  if (cool.empty()) cool = "-";
  std::string gap = "-";
  if (rng.below(2) == 0) {
    std::snprintf(buf, sizeof(buf), "%d", 100 * rng.below(11));
    gap = buf;
  }
  const char* fs[4] = {"2", "3", "4", "6"};
  const char* ds[3] = {"1", "1.5", "2"};
  const int engage[3] = {100, 200, 300};
  const char lv[3] = {'E', 'N', 'H'};
  const char* ag[5] = {"0", "0.25", "0.5", "0.75", "1"};
  std::snprintf(buf, sizeof(buf), "%s,%s,%d,250,%c,%s,%d", fs[rng.below(4)], ds[rng.below(3)], engage[rng.below(3)],
                lv[rng.below(3)], ag[rng.below(5)], rng.below(4) == 0 ? 0 : 1);
  return board + " " + held + " " + head + " " + cool + " " + gap + " " + lift + " " + buf;
}

// Kinds of positions the quotas count, per level.
enum Kind { SafeMove, SafeStay, UnsafeMove, UnsafeStay, ForkMove, KINDS };
const int QUOTA[3][KINDS] = {
    {4, 3, 3, 1, 0},  // easy
    {4, 3, 4, 2, 0},  // normal
    {4, 3, 4, 2, 4},  // hard
};
// With a rules field.
const int RULES_QUOTA[3][KINDS] = {
    {2, 1, 1, 0, 0},  // easy
    {2, 1, 2, 1, 0},  // normal
    {2, 1, 2, 1, 1},  // hard
};

bool sameMove(const AiMove& a, const AiMove& b) {
  return a.none == b.none && (a.none || (a.pieceId == b.pieceId && a.toR == b.toR && a.toC == b.toC));
}

}  // namespace

int main(int argc, char** argv) {
  Rng rng{argc > 1 ? static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 10)) : 20261102u};
  if (rng.x == 0) rng.x = 1;
  const std::string rules = argc > 2 ? argv[2] : "";
  // In the opening (OPEN) "stay" never contends, so there are no stay cases to find.
  const bool opening = rules.find("OPEN") != std::string::npos;
  int quota[3][KINDS];
  for (int l = 0; l < 3; l++)
    for (int k = 0; k < KINDS; k++) {
      quota[l][k] = rules.empty() ? QUOTA[l][k] : RULES_QUOTA[l][k];
      if (opening && (k == SafeStay || k == UnsafeStay)) quota[l][k] = 0;
    }
  int have[3][KINDS] = {};
  int need = 0;
  for (int l = 0; l < 3; l++)
    for (int k = 0; k < KINDS; k++) need += quota[l][k];
  if (!rules.empty()) {
    std::printf("# Cases under %s (gen_cases %s %s): think() picks another move there than under R0.\n",
                rules.c_str(), argv[1], rules.c_str());
  } else {
    std::printf(
      "# Golden cases for think() (plan 2026-10-31 3.9-3.11): the same table is checked on PC (test_ai),\n"
      "# in WASM (Node.js tests) and on the ESP32 (serial command `golden`), so that every build picks the\n"
      "# same move. Generated by test/golden/gen_cases.cpp (build and run as its first lines say).\n"
      "# Format (see lib/core/AiCase.h): board held head cooldowns gap lift params expect\n"
      "#   board     n*n of . H M, row-major      held      - or r,c (origin of the held human piece)\n"
      "#   head      r,c                          cooldowns - or r,c=ms[;...] (remaining)\n"
      "#   gap       - or ms (placement interval left)   lift   - or ms since the held piece was lifted\n"
      "#   params    freeSpeed,dragSpeed,engageMs,reactMs,level(E/N/H),aggression,readLift(0/1)\n"
      "#   expect    none or r,c>r,c\n"
      "# Lines after the first block add a rules field after params (plan 2026-11-06 3.1: RE, RW, RU<ms>,\n"
      "# OPEN joined by '+'); they come from gen_cases with a rules argument (see its first lines).\n"
      "# Every case has a gap of at least %.1f between the best and the second-best contending score.\n",
      static_cast<double>(MIN_GAP));
  }
  static AiCandidate cand[AI_MAX_CANDIDATES];
  for (int tries = 0; need > 0 && tries < 2000000; tries++) {
    const std::string base = randomLine(rng);
    const std::string line = rules.empty() ? base : base + " " + rules;
    AiCase c;
    if (!parseAiCase((line + " none").c_str(), c)) continue;
    const int k = thinkCandidates(c.snap, c.params, cand, AI_MAX_CANDIDATES);
    int best = -1, second = -1, safe = 0;
    bool penalized = false;
    for (int i = 0; i < k; i++) {
      if (cand[i].safe) safe++;
      if (cand[i].penalty > 0.0f) penalized = true;
      if (!cand[i].contending) continue;
      if (best < 0 || cand[i].score > cand[best].score) {
        second = best;
        best = i;
      } else if (second < 0 || cand[i].score > cand[second].score) {
        second = i;
      }
    }
    if (best < 0 || second < 0 || cand[best].score - cand[second].score < MIN_GAP) continue;
    const int level = static_cast<int>(c.params.level);
    const bool stay = cand[best].none;
    Kind kind = safe > 0 ? (stay ? SafeStay : SafeMove) : (stay ? UnsafeStay : UnsafeMove);
    // A move chosen over a penalized better one.
    if (penalized && !stay && cand[best].penalty == 0.0f) {
      bool overtook = false;
      for (int i = 0; i < k; i++)
        if (cand[i].penalty > 0.0f && cand[i].score + cand[i].penalty > cand[best].score) overtook = true;
      if (overtook) kind = ForkMove;
    }
    if (have[level][kind] >= quota[level][kind]) continue;
    // think() must agree with the listing.
    const AiMove m = think(c.snap, c.params);
    if (!rules.empty()) {
      AiCase r0;
      if (!parseAiCase((base + " none").c_str(), r0) || sameMove(think(r0.snap, r0.params), m)) continue;
    }
    char expect[32];
    if (stay) {
      std::snprintf(expect, sizeof(expect), "none");
    } else {
      const PieceView& p = c.snap.pieces[cand[best].pieceId - 1];
      std::snprintf(expect, sizeof(expect), "%d,%d>%d,%d", p.r, p.c, cand[best].toR, cand[best].toC);
    }
    if (m.none != stay || (!stay && (m.pieceId != cand[best].pieceId || m.toR != cand[best].toR ||
                                     m.toC != cand[best].toC))) {
      std::fprintf(stderr, "think disagrees with thinkCandidates: %s\n", line.c_str());
      return 1;
    }
    have[level][kind]++;
    need--;
    std::fprintf(stderr, "# scores %s:", expect);
    for (int i = 0; i < k; i++) {
      if (cand[i].none) {
        std::fprintf(stderr, " stay");
      } else {
        const PieceView& p = c.snap.pieces[cand[i].pieceId - 1];
        std::fprintf(stderr, " %d,%d>%d,%d", p.r, p.c, cand[i].toR, cand[i].toC);
      }
      std::fprintf(stderr, "=%s%.3f%s", cand[i].safe ? "" : "u", static_cast<double>(cand[i].score),
                   cand[i].penalty > 0.0f ? "p" : "");
    }
    std::fprintf(stderr, "\n");
    std::printf("%s %s\n", line.c_str(), expect);
  }
  if (need > 0) {
    std::fprintf(stderr, "gen_cases: %d cases short of the quotas\n", need);
    return 1;
  }
  return 0;
}
