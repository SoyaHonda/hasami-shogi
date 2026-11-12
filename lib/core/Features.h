#pragma once
#include <cstdint>
#include "Ai.h"
#include "Game.h"

// The learning environment's view of the game for the machine (plan 2026-11-12 section 4): the
// observation planes, the scalars, the action numbering and the legal-action mask. Built from the
// Game alone (plus the head position), all values integers, the board fixed at 7x7 and seen as it
// stands (r = 0 is the machine's home row, c = 0 the left edge; no mirroring).
//
// Planes (FEAT_CHANNELS x 7 x 7, int16; times in units of 100 ms):
//   0  machine pieces on the board (1)
//   1  human pieces on the board, not held (1)
//   2  origin square of a held human piece (1)
//   3  target square of the human's pending placement (1)
//   4  human placement interval left (ceil), on every square (0 before the first placement)
//   5  machine piece cooldown left (ceil), on its square
//   6  human piece cooldown left (ceil), on its square (pieces on the board)
//   7  machine piece time until RU can take it (ceil), on its square (0 when stale or RU is off)
//   8  human piece time until RU can take it (ceil), on its square (0 when stale or RU is off)
//   9  head position (1 on the nearest square, clamped to the board)
//   10 time a held human piece has been up (floor), on its origin (0 in Opening: the game clock
//      stands still there)
//   11 pieces of either side in an edge group with exactly one empty neighbour left (RE only; 1)
// Scalars (FEAT_SCALARS, int16): machine captures, human captures, game time left (ceil, 0 without
// a time limit), Opening (0/1), the human has made its opening move (0/1; 1 without RW),
// speed_scale x 100.
//
// Actions: ((r*7 + c)*4 + d)*6 + (dist-1) moves the machine piece on (r,c) dist squares in
// direction d (Board.h's order: 0 down, 1 up, 2 right, 3 left); ACTION_STAY (1176) is "stay".
// The piece stops short when its way fills up (Game and MachineController as they are).
constexpr int FEAT_N = 7;
constexpr int FEAT_SQUARES = FEAT_N * FEAT_N;
constexpr int FEAT_CHANNELS = 12;
constexpr int FEAT_PLANES = FEAT_CHANNELS * FEAT_SQUARES;
constexpr int FEAT_SCALARS = 6;
constexpr int ACTION_DISTS = FEAT_N - 1;
constexpr int ACTION_STAY = FEAT_SQUARES * 4 * ACTION_DISTS;  // 1176
constexpr int ACTION_COUNT = ACTION_STAY + 1;                 // 1177

int actionIndex(int r, int c, int d, int dist);
// Decodes a move action (0 <= a < ACTION_STAY) into its square, direction and distance; false for
// ACTION_STAY or a number out of range.
bool decodeAction(int a, int& r, int& c, int& d, int& dist);
// The action of a move from (r,c) to (tr,tc) on one row or column; -1 when it is not one.
int actionOfMove(int r, int c, int tr, int tc);

// Fills planes[FEAT_PLANES] (channel-major) and scal[FEAT_SCALARS]. The game's board must be 7x7.
void buildFeatures(const Game& g, float headR, float headC, int speedScale100, int16_t planes[],
                   int16_t scal[]);

// Fills mask[ACTION_COUNT] with the actions the machine may take now and returns how many there
// are. Moves: a machine piece that is alive, not held and cooled down, to a square of dests() on
// Game::boardForMachine (held origins, the pending target and the reserved square occupied); under
// RW not one whose drag passes a square where the piece would complete a wall (wallOnPath), the
// candidates of Ai's read. "Stay" is legal except in Opening before the machine's opening move
// (the read's Snapshot::mustMove): there the opening could otherwise last for ever, the game clock
// standing still.
int buildMask(const Game& g, uint8_t mask[]);

// The move an action asks for (AiMove::none for ACTION_STAY), and the action of a move.
AiMove moveOfAction(const Game& g, int action);
int actionOfAiMove(const Game& g, const AiMove& m);
