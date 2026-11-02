#pragma once
#include <cstdint>
#include "Ai.h"
#include "Board.h"
#include "Params.h"
#include "Types.h"

// Time estimates of plan 2026-10-31 3.3-3.8 and 3.10: how soon the human can finish closing
// both ends of a machine line (T_h), how soon the machine can start pulling a piece out of the
// way (S) and where it may go, which threatened lines the single head can guard and in what
// order, the resulting risk of losing each line, and the same race from the machine's side
// (T_m and gain). Pure functions of the snapshot; every time is float milliseconds measured
// from the snapshot (now = 0). Only + - * / and sqrt are used so that every build agrees
// (builds use -ffp-contract=off).

// "Never" (the human cannot close the line, the machine cannot guard it). Check with raceInf()
// before subtracting: INF - INF and INF - t are meaningless.
constexpr float RACE_INF = 1.0e9f;
inline bool raceInf(float t) { return !(t < RACE_INF); }

// Lines whose T_h is below this are threatened (3.7).
constexpr float RACE_DANGER_MS = 6000.0f;
// Most threatened lines whose guard order is searched exhaustively (more: greedy, 3.7).
constexpr int RACE_EXHAUSTIVE = 3;
// Upper bound on the lines of one side (each piece is in one row line and one column line).
constexpr int RACE_MAX_LINES = 4 * MAX_N;
constexpr int RACE_MAX_HUMANS = 2 * MAX_N;

// Squares as a bit set (MAX_N*MAX_N <= 96 bits).
struct RaceMask {
  uint32_t w[3];
  void clear() { w[0] = w[1] = w[2] = 0; }
  void set(int i) { w[i >> 5] |= 1u << (i & 31); }
  bool has(int i) const { return (w[i >> 5] >> (i & 31)) & 1u; }
  bool any() const { return (w[0] | w[1] | w[2]) != 0; }
};

// Parameters of the race, from the snapshot's rule times and Params (3.3, 3.6, 3.9, 3.11).
struct RaceTimes {
  float humanMove, humanLift, humanPlace, humanReact;  // human hand (Params, 3.3)
  float humanCooldown, humanInterval, maxHold;         // human rules (maxHold 0 = no limit)
  float machineCooldown, engage, react;                // machine (react is the read interval)
  float freeSpeed, dragSpeed;                          // squares per second
  float margin;                                        // 0 / 200 / 300 ms by level (3.9)
  bool easy;  // Easy: only the held piece's H1 is read (3.11); H2-H4 are INF
};

// One human piece as the race sees it.
struct RaceHuman {
  int id;
  int r, c;      // square on the board; a held piece's origin; the pending piece's target
  bool alive;    // false once captured (on a hypothetical board)
  bool held;     // held and not pending: placed by humanPlace, within holdLeft
  bool pending;  // its placement is pending: on the board (at r,c) from time `gap`
  float ready;   // when it can next be lifted (cooldown left; gap + humanCooldown if pending)
  float holdLeft;  // held: hold time left (RACE_INF without a limit)
  RaceMask reach1, reach2;  // squares it can be put on in one move / in two moves
};

// The position as the race sees it; copied and changed to look at hypothetical boards.
struct RaceState {
  Board board;  // pieces on the board (held excluded) plus the pending target as HUMAN
  bool blocked[MAX_N * MAX_N];  // the machine cannot enter (held origins, pending target, reserved)
  int reserved;                 // reserved square (r*n+c) or -1: humans cannot go there
  float gap;         // placement interval left (3.3)
  float firstPlace;  // earliest placement of a non-pending piece (gap, or gap + interval if pending)
  float firstLift;   // earliest lift (gap while a placement is pending, else 0)
  RaceHuman h[RACE_MAX_HUMANS];
  int hCount;
  float mReady[MAX_N * MAX_N];  // machine piece on the square: cooldown left
  int mId[MAX_N * MAX_N];       // machine piece id on the square (0 for none)
  float headFree;               // head is busy until then
  float headR, headC;           // where the head is when it becomes free
};

// A maximal run of `len` pieces of one side starting at (r,c) in direction (dr,dc) (0,1 = row,
// 1,0 = column). Its ends are (r-dr, c-dc) and (r+dr*len, c+dc*len).
struct RaceLine {
  int r, c, dr, dc, len;
  int8_t side;
};

// A threatened (or not) machine line after the guard assignment of 3.7.
struct RaceLineEval {
  RaceLine line;
  float th;       // T_h
  float save;     // save(L) with the head as assigned (RACE_INF when no piece has a safe move)
  bool danger;    // th < RACE_DANGER_MS
  bool guarded;   // danger and save + margin <= th in the chosen order
  int order;      // position in the guard order (0 first), -1 when not guarded
  int guardR, guardC, toR, toC;  // the move that guards it (when guarded)
  float risk;     // 3.7: 0 when not threatened or guarded
};

RaceTimes raceTimes(const Snapshot& s, const Params& p);
// Builds the state (reach of every human piece included). Head free now at the snapshot head.
void raceInit(RaceState& st, const Snapshot& s, const RaceTimes& t);
// Recomputes alive and reach of the human pieces in the row or column of (r0,c0) or (r1,c1)
// (3.6, 3.10); pass -1,-1 for an unused square.
void raceRebuild(RaceState& st, int r0, int c0, int r1, int c1);
// Moves the machine piece (fr,fc) -> (tr,tc), arriving at `arrival`: captures (not the pending
// target, which is not on the real board yet), cooldown, head busy until arrival at (tr,tc),
// reach rebuilt. Returns the number of human pieces captured.
int raceMoveMachine(RaceState& st, const RaceTimes& t, int fr, int fc, int tr, int tc, float arrival);

// Lines of `side` on b, row lines first then column lines, in square order. Returns the count.
int raceLines(const Board& b, int8_t side, RaceLine out[]);
// The line of `side` through (r,c) in direction (dr,dc) on b (len 0 if (r,c) is not `side`).
RaceLine raceLineThrough(const Board& b, int8_t side, int r, int c, int dr, int dc);

// e(h,s) of 3.5: earliest time human piece index hi can be put on square sq (r*n+c), without the
// placement interval (callers apply firstPlace and humanInterval). RACE_INF if it cannot.
float raceHumanPlace(const RaceState& st, const RaceTimes& t, int hi, int sq);
// T_h of 3.5 for a machine line on st.board.
float raceHumanTime(const RaceState& st, const RaceTimes& t, const RaceLine& L);

// S(p) of 3.6 for the machine piece on (r,c), the head free at headFree at (headR, headC).
float raceStart(const RaceState& st, const RaceTimes& t, int r, int c, float headFree, float headR, float headC);
// Arrival of the piece at (r,c) moving to (tr,tc) when it starts being pulled at `start`.
float raceArrival(const RaceTimes& t, int r, int c, int tr, int tc, float start);
// What moving (pr,pc) to (dr,dc) leaves for the human (3.6), independent of when it starts:
// thDest = least T_h of the lines through the destination, thRest = least T_h of the lines the
// rest of its old lines form. Both on the board after the move, reach rebuilt per 3.6.
void raceDestTimes(const RaceState& st, const RaceTimes& t, int pr, int pc, int dr, int dc, float* thDest,
                   float* thRest);
// Whether (dr,dc) is a safe destination for (pr,pc) starting at `start` (3.6 (1) and (2)).
bool raceSafeDest(const RaceState& st, const RaceTimes& t, int pr, int pc, int dr, int dc, float start);

// T_h of every machine line, the guard assignment of 3.7 (head from st.headFree/headR/headC) and
// the risk of each line. Returns the number of lines written to out (RACE_MAX_LINES at most).
int raceAssess(const RaceState& st, const RaceTimes& t, RaceLineEval out[]);
// Risk of each machine piece: the larger risk of the lines it is in (riskAt[r*n+c], 0 elsewhere).
void racePieceRisk(const Board& b, const RaceLineEval ev[], int count, float riskAt[]);

// 3.8 for a human line: T_m (machine closes both ends), the human's escape time and the gain.
float raceMachineTime(const RaceState& st, const RaceTimes& t, const RaceLine& L);
float raceEscape(const RaceState& st, const RaceTimes& t, const RaceLine& L);
float raceGain(const RaceState& st, const RaceTimes& t, const RaceLine& L);
