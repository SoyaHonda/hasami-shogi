#pragma once
#include <cstdint>
#include "Types.h"

struct Board {
  int n;
  int8_t cells[MAX_N * MAX_N];
  int8_t at(int r, int c) const { return cells[r * n + c]; }
  void set(int r, int c, int8_t v) { cells[r * n + c] = v; }
  bool inside(int r, int c) const { return r >= 0 && r < n && c >= 0 && c < n; }
};

// Direction order: down, up, right, left (same as DIRS in the original simulator).
constexpr int DIR_R[4] = {1, -1, 0, 0};
constexpr int DIR_C[4] = {0, 0, 1, -1};

// Empty squares reachable from (r,c) moving straight without jumping, as indices r*n+c,
// in the order down, up, right, left. Returns the count.
int dests(const Board& b, int r, int c, int out[]);

// Pieces captured by `side` having just arrived at (r,c), as indices r*n+c. In each direction
// a run of enemy pieces is captured only when followed by one of side's own pieces; a run that
// reaches the edge of the board is not captured. Returns the count.
int capturesAt(const Board& b, int8_t side, int r, int c, int out[]);
