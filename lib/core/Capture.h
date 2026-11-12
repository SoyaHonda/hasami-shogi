#pragma once
#include <cstdint>
#include "Board.h"
#include "Rules.h"

// The one capture judgement of plan 2026-11-06 3.2: Game, Ai, RaceTime, HumanBot, LedPlan and
// SimDriver all ask captures() with the rules in force, so that every reader sees the captures
// the game makes. Also the geometry RE and RW are judged on (edge groups, walls).
//
// Only the side that has just moved is judged against (its own pieces are never captured by its
// own arrival), as in R0. The board passed in is the one the capture is judged on: Game passes
// the board without held pieces, so a held piece's origin and a pending placement's target are
// empty squares there, and a machine piece being dragged is left off it for the human's
// captures.

// The 4-connected group of one side's pieces that holds a given piece, when it touches the
// board's edge (RE), and its empty neighbours.
constexpr int EDGE_GROUP_LIBS = 4;
struct EdgeGroup {
  int8_t side;
  SquareSet cells;
  int size;
  int first;     // smallest square index (r*n+c) of the group
  int libCount;  // empty neighbours, all of them
  int libs[EDGE_GROUP_LIBS];  // the first EDGE_GROUP_LIBS of them in square order
};

// The group of the piece on (r,c). Returns false (out undefined) when (r,c) holds no piece or
// the group does not touch the board's edge.
bool edgeGroupAt(const Board& b, int r, int c, EdgeGroup& out);

// Every piece captured by `side` having just arrived at (r,c) on b, as indices r*n+c, each once:
//  - custodian (all rules): in each direction, a run of enemy pieces followed by one of side's
//    own pieces; a run that reaches the edge is not captured (first, in the order of Board.h's
//    capturesAt);
//  - RE (rules.edgeCapture): every enemy group next to (r,c) that touches the edge and has no
//    empty neighbour;
//  - RU (rules.untouchedMs > 0): every enemy piece next to (r,c) whose square is in `stale` (the
//    pieces not moved for untouchedMs; nullptr: none).
// Returns the count.
int captures(const Board& b, const Rules& rules, int8_t side, int r, int c, int out[],
             const SquareSet* stale = nullptr);

// Whether moving `side`'s piece from (fr,fc) to (tr,tc) leaves row tr filled with side's pieces
// from edge to edge (a wall). (fr,fc) is treated as empty (pass -1,-1 for a piece not on b);
// (tr,tc) is expected empty.
bool completesWall(const Board& b, int8_t side, int fr, int fc, int tr, int tc);
// RW: completesWall when rules.wallLimit is on, otherwise false. Such a move is illegal.
bool makesWall(const Board& b, const Rules& rules, int8_t side, int fr, int fc, int tr, int tc);
// Whether some square of the straight way from (fr,fc) to (tr,tc), the destination included, is
// one where `side`'s piece from (fr,fc), coming to rest there, would complete a wall
// (completesWall). Under RW a machine piece never rests on such a square, not even stopped short
// (Game::machineCanEnter refuses it, so the drag stops one square before it), and the read does
// not choose such a move (user decision 2026-11-07). Only a move across rows can have one.
bool wallOnPath(const Board& b, int8_t side, int fr, int fc, int tr, int tc);
