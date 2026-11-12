#pragma once
#include <cstdint>
#include "Ai.h"
#include "Board.h"
#include "Capture.h"
#include "Params.h"
#include "Rules.h"
#include "Types.h"

// Time estimates of plan 2026-10-31 3.3-3.8 and 3.10: how soon the human can finish closing
// both ends of a machine line (T_h), how soon the machine can start pulling a piece out of the
// way (S) and where it may go, which threatened lines the single head can guard and in what
// order, the resulting risk of losing each line, and the same race from the machine's side
// (T_m and gain). Pure functions of the snapshot; every time is float milliseconds measured
// from the snapshot (now = 0). Only + - * / and sqrt are used so that every build agrees
// (builds use -ffp-contract=off).
//
// The rules in force (plan 2026-11-06 3.1-3.2) come from the snapshot: captures are judged by
// captures() of Capture.h; under RE an edge group of machine pieces is a threat of its own (a
// group unit, see RaceLine), closed when the human has filled all its empty neighbours (the
// custodian lines that reach the edge stay out of the custodian rule's reach); under RW neither
// side's moves may make a wall. Under RU, the read of think() (RaceTimes::untouched) also reads
// each machine piece about to go untouched as a threat of its own (an RU unit, see RaceLine):
// taken by one human arrival next to it once it is untouched, saved by any move of the piece
// (its timer restarts at the arrival).

// "Never" (the human cannot close the line, the machine cannot guard it). Check with raceInf()
// before subtracting: INF - INF and INF - t are meaningless.
constexpr float RACE_INF = 1.0e9f;
inline bool raceInf(float t) { return !(t < RACE_INF); }

// Lines whose T_h is below this are threatened (3.7).
constexpr float RACE_DANGER_MS = 6000.0f;
// Most threatened lines whose guard order is searched exhaustively (more: greedy, 3.7).
constexpr int RACE_EXHAUSTIVE = 3;
// Upper bound on the lines of one side (each piece is in one row line and one column line) plus
// its RE group units and RU units (at most one of each per piece).
constexpr int RACE_MAX_LINES = 4 * MAX_N;
constexpr int RACE_MAX_HUMANS = 2 * MAX_N;
// RE: an edge group with more empty neighbours than this is not read as threatened (the human
// would need as many placements, one interval apart).
constexpr int RACE_GROUP_MAX_FILL = 3;
// RU: a machine piece going untouched within this horizon is an RU unit, and the unit is
// threatened (danger) while its T_h is below it: longer than RACE_DANGER_MS so that the head can
// refresh several pieces going untouched together one after another (a refresh takes about
// 1.4 s), shorter than the shortest RU candidate (20 s) so that a piece just moved is none. A
// unit beyond RACE_DANGER_MS whose piece has no safe move now is left to later reads (no danger).
constexpr float RACE_UNTOUCHED_DANGER_MS = 15000.0f;

// Squares as a bit set (Board.h).
using RaceMask = SquareSet;

// Parameters of the race, from the snapshot's rule times and Params (3.3, 3.6, 3.9, 3.11).
struct RaceTimes {
  float humanMove, humanLift, humanPlace, humanReact;  // human hand (Params, 3.3)
  float humanCooldown, humanInterval, maxHold;         // human rules (maxHold 0 = no limit)
  float machineCooldown, engage, react;                // machine (react: worst reaction, 3.6)
  float readInterval;  // time between reads (MachineController: reactMs, at least 150 ms)
  float freeSpeed, dragSpeed;                          // squares per second
  float margin;                                        // 0 / 200 / 300 ms by level (3.9)
  bool easy;  // Easy: only the held piece's H1 is read (3.11); H2-H4 are INF
  Rules rules;  // the snapshot's rule candidates (RE, RW, RU) for captures() and walls
  // RU read: RU units of the machine's pieces (raceAssess), the machine's captures of human pieces
  // untouched by its arrival (raceMoveMachine) and gains that need a machine piece RU takes first
  // (raceGain). think() sets it under RU; raceTimes() leaves it off, so that the human bots, the
  // activity measure and the search exploiter read as they did when they were measured (plan
  // 2026-11-06 Step 0b).
  bool untouched;
};
// Whether the RU read is on (RaceTimes::untouched under a rule with untouchedMs > 0).
inline bool raceUntouchedRead(const RaceTimes& t) { return t.untouched && t.rules.untouchedMs > 0; }

// One human piece as the race sees it.
struct RaceHuman {
  int id;
  int r, c;      // square on the board; a held piece's origin; the pending piece's target
  bool alive;    // false once captured (on a hypothetical board)
  bool held;     // held and not pending: placed by humanPlace, within holdLeft
  bool pending;  // its placement is pending: on the board (at r,c) from time `gap`
  float ready;   // when it can next be lifted (cooldown left; gap + humanCooldown if pending)
  float holdLeft;  // held: hold time left (RACE_INF without a limit)
  // RU read: when it goes untouched (0: it is already; RACE_INF: held, or the read off); a
  // pending piece's timer starts when it lands at gap.
  float staleAt;
  RaceMask reach1, reach2;  // squares it can be put on in one move / in two moves
};

// The position as the race sees it; copied and changed to look at hypothetical boards.
struct RaceState {
  Board board;  // pieces on the board (held excluded) plus the pending target as HUMAN
  bool blocked[MAX_N * MAX_N];  // the machine cannot enter (held origins, pending target, reserved)
  int reserved;                 // reserved square (r*n+c) or -1: humans cannot go there
  float gap;         // placement interval left (3.3)
  float firstPlace;  // earliest placement of a non-pending piece (gap, or gap + interval if pending)
  float firstLift;   // earliest lift of a piece not in hand: gap while a placement is pending;
                     // while a piece is held, when it is put down (humanPlace, or the hold
                     // limit if sooner): only one piece is in hand (3.1, 3.5); else 0
  RaceHuman h[RACE_MAX_HUMANS];
  int hCount;
  float mReady[MAX_N * MAX_N];  // machine piece on the square: cooldown left
  int mId[MAX_N * MAX_N];       // machine piece id on the square (0 for none)
  // RU read: when each machine piece goes untouched (0: it is already), by piece id (mId; by id
  // to keep the copies of the state small); RACE_INF for the piece being dragged
  // (Snapshot::reservedId), ids of no machine piece and with the read off. raceMachineStale reads
  // it by square.
  float mStale[2 * MAX_N + 1];
  float headFree;               // head is busy until then
  float headR, headC;           // where the head is when it becomes free
  // The human's reaction to a machine move (3.6): while reactAt > 0, a human piece put on a
  // square outside `planned` (the ends of the lines threatened before the move, where the human
  // is heading anyway) is lifted, or if held placed, no sooner than reactAt. raceInit: 0.
  float reactAt;
  RaceMask planned;
  bool wall;        // RW: human moves that make a wall are left out of the reach
  RaceMask stale;   // RU: pieces untouched for Rules::untouchedMs at the snapshot
};

// RU read: when the machine piece on square sq (r*n+c) of st goes untouched (RACE_INF for none).
inline float raceMachineStale(const RaceState& st, int sq) {
  const int id = st.mId[sq];
  return id > 0 && id <= 2 * MAX_N ? st.mStale[id] : RACE_INF;
}

// A maximal run of `len` pieces of one side starting at (r,c) in direction (dr,dc) (0,1 = row,
// 1,0 = column). Its ends are (r-dr, c-dc) and (r+dr*len, c+dc*len).
// A group unit (RE) has dr = dc = 0: the edge group of `len` pieces whose first square is (r,c)
// on the board it was found on; its "ends" are its empty neighbours (raceUnitSquares,
// raceUnitEnds).
// An RU unit has `untouched` set (dr = dc = 0, len 1): the one piece on (r,c), taken by a human
// arrival next to it once untouched; its "ends" are its empty neighbours. Lines and groups leave
// `untouched` out of their brace initialisers (false).
struct RaceLine {
  int r, c, dr, dc, len;
  int8_t side;
  bool untouched;
};
inline bool raceIsUntouched(const RaceLine& L) { return L.untouched; }
inline bool raceIsGroup(const RaceLine& L) { return L.dr == 0 && L.dc == 0 && !L.untouched; }

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
// reach rebuilt. Returns the number of human pieces captured. With the RU read the piece's timer
// restarts at `arrival`, and RU takes the human pieces untouched by then (otherwise those
// untouched at the snapshot).
int raceMoveMachine(RaceState& st, const RaceTimes& t, int fr, int fc, int tr, int tc, float arrival);

// Lines of `side` on b, row lines first then column lines, in square order. Returns the count.
int raceLines(const Board& b, int8_t side, RaceLine out[]);
// The line of `side` through (r,c) in direction (dr,dc) on b (len 0 if (r,c) is not `side`).
RaceLine raceLineThrough(const Board& b, int8_t side, int r, int c, int dr, int dc);
// RE: the group unit of the `side` piece on (r,c) on b; len 0 when RE is off, (r,c) holds no
// `side` piece or its group does not touch the edge.
RaceLine raceGroupThrough(const Board& b, const RaceTimes& t, int8_t side, int r, int c);
// RU read: the RU unit of the machine piece on (r,c) of st; len 0 when the read is off, (r,c)
// holds no machine piece or it goes untouched no sooner than RACE_UNTOUCHED_DANGER_MS.
RaceLine raceUntouchedThrough(const RaceState& st, const RaceTimes& t, int r, int c);
// The squares (r*n+c) of a unit on b (the board it was found on): a line's run in order, a
// group's pieces in square order, an RU unit's piece. Returns the count (out holds MAX_N*MAX_N;
// bytes to keep the ESP32 think stack low).
int raceUnitSquares(const Board& b, const RaceLine& L, uint8_t out[]);
// The ends of a unit on b: a line's ends inside the board (the one before (r,c) first); a
// group's or an RU unit's empty neighbours in square order. Returns the count (out holds
// MAX_N*MAX_N).
int raceUnitEnds(const Board& b, const RaceLine& L, uint8_t out[]);

// e(h,s) of 3.5: earliest time human piece index hi can be put on square sq (r*n+c), without the
// placement interval (callers apply firstPlace and humanInterval). RACE_INF if it cannot.
float raceHumanPlace(const RaceState& st, const RaceTimes& t, int hi, int sq);
// T_h of 3.5 for a machine line on st.board. For a group unit (RE): every empty neighbour filled
// by a different human piece not next to the group (the pieces next to it stay), as H1 and H2:
// the first by any piece, each later one by a piece one move away, an interval apart; a group
// with no empty neighbour falls to an encloser stepping away and a piece coming back (H3, H4).
// For an RU unit: raceUntouchedTime.
float raceHumanTime(const RaceState& st, const RaceTimes& t, const RaceLine& L);
// RU read: when the human can take the machine piece on (r,c) by RU: max(when it goes untouched,
// the earliest human arrival on one of its neighbours). An arrival is a placement on an empty
// neighbour (e(h,s), not before firstPlace; Easy: the held piece's only), a pending placement
// next to it (at gap), or (not Easy) a human piece next to it stepping away and one coming into
// its square (H3, H4). RACE_INF with the read off, without a machine piece on (r,c), for the
// piece being dragged, or when nothing can arrive.
float raceUntouchedTime(const RaceState& st, const RaceTimes& t, int r, int c);

// S(p) of 3.6 for the machine piece on (r,c), the head free at headFree at (headR, headC).
float raceStart(const RaceState& st, const RaceTimes& t, int r, int c, float headFree, float headR, float headC);
// Free travel of the head from (headR, headC) to (r,c), along travelPath as the gantries move.
float raceTravel(const RaceTimes& t, int r, int c, float headR, float headC);
// Arrival of the piece at (r,c) moving to (tr,tc) when it starts being pulled at `start`.
float raceArrival(const RaceTimes& t, int r, int c, int tr, int tc, float start);
// What moving (pr,pc) to (dr,dc) leaves for the human (3.6): thDest = least T_h of the lines
// through the destination, with the human reacting to the move at `reactAt` (0: at once) for
// squares outside `planned` (see RaceState::reactAt; null: nothing planned); thRest = least T_h
// of the lines the rest of its old lines form (no reaction needed). Both on the board after the
// move, reach rebuilt per 3.6. Under RE the destination's group and the groups left next to the
// old square count too; with the RU read, the RU units of the machine pieces next to the old
// square (it opens a square next to them) count in thRest. The moved piece's own timer restarts
// at its arrival.
void raceDestTimes(const RaceState& st, const RaceTimes& t, int pr, int pc, int dr, int dc, float* thDest,
                   float* thRest, float reactAt = 0.0f, const RaceMask* planned = nullptr);
// Ends of the threatened machine lines (T_h < RACE_DANGER_MS) among ev[0..count).
void racePlanned(const Board& b, const RaceLineEval ev[], int count, RaceMask* out);
// Whether (dr,dc) is a safe destination for (pr,pc) starting at `start` (3.6 (1) and (2)); the
// human reacts to the move at start + humanReact for squares outside `planned`.
bool raceSafeDest(const RaceState& st, const RaceTimes& t, int pr, int pc, int dr, int dc, float start,
                  const RaceMask* planned = nullptr);
// When the human can react to a move of the piece on (r,c) starting no sooner than the head
// allows on st (its cooldown or the head's busy time, then reactMs and engageMs; travel not
// counted, so never later than the real start): that start + humanReact.
float raceReactAt(const RaceState& st, const RaceTimes& t, int r, int c);

// T_h of every machine line, the guard assignment of 3.7 (head from st.headFree/headR/headC) and
// the risk of each line. Returns the number of lines written to out (RACE_MAX_LINES at most).
// Under RE the machine's edge groups follow the lines as group units (a guard moves one of the
// group's pieces out); with the RU read the RU units follow them (any move of the piece saves it;
// danger and the horizon as RACE_UNTOUCHED_DANGER_MS says). A guard that moves a piece guards
// with the same move every other threatened unit holding that piece when one of the two is an
// RU unit and the move starts in time for it (its order is the guard's).
// `lead` is added to every save (guarding needs save + lead + margin <= T_h, and the risk of an
// unguarded line uses save + lead): "stay" passes the read interval, since its guard starts at
// the next read at the earliest (plan 2026-10-31 3.9).
int raceAssess(const RaceState& st, const RaceTimes& t, RaceLineEval out[], float lead = 0.0f);
// Risk of each machine piece: the larger risk of the lines (and group units) it is in
// (riskAt[r*n+c], 0 elsewhere).
void racePieceRisk(const Board& b, const RaceLineEval ev[], int count, float riskAt[]);

// 3.8 for a human line: T_m (machine closes both ends), the human's escape time and the gain.
// With the RU read, the gain of a line whose end is held by a machine piece the human takes by
// RU before T_m + margin is scaled by 1 - the risk of 3.7 with T_m as save.
float raceMachineTime(const RaceState& st, const RaceTimes& t, const RaceLine& L);
float raceEscape(const RaceState& st, const RaceTimes& t, const RaceLine& L);
float raceGain(const RaceState& st, const RaceTimes& t, const RaceLine& L);
