#pragma once
#include "Board.h"
#include "Types.h"

// Maximum number of points travelPath writes.
constexpr int TRAVEL_MAX_POINTS = 5;
// Maximum number of squares removalPath writes (start + every board square + reserve square).
constexpr int REMOVAL_MAX_POINTS = MAX_N * MAX_N + 1;

// Free reserve column nearest to preferC (ties go to the smaller column); -1 if none is free.
int pickReserveSlot(const bool used[], int n, int preferC);

// Shortest route (breadth-first search, 4-neighbour) for a captured piece from `from` to a free
// column of reserve row `toRow` (n or -1), passing only through the centres of empty squares
// that are not `blocked` (indexed r*n+c). `from` holds the piece itself and is only a start
// point. On success out[0] = from, out[len-1] = (toRow, slotC), and every square between is an
// empty board square. Among the free columns reached at the shortest distance, the one
// pickReserveSlot prefers (with preferC = from.c) is chosen. Returns false when no route exists.
bool removalPath(const Board& b, Sq from, int toRow, const bool reserveUsed[], const bool blocked[],
                 Sq out[], int& len, int& slotC);

// Polyline for a free (unloaded) head move from (fromR, fromC) to the centre of (toR, toC):
// first to the nearest square corner (both coordinates x.5), then along the grid lines (x.5)
// in the row direction and then the column direction, and finally into the target centre.
// The start and the target are included. Writes at most TRAVEL_MAX_POINTS points; returns count.
int travelPath(float fromR, float fromC, int toR, int toC, float outR[], float outC[]);

// Sum of segment lengths (sqrtf) of a polyline with `count` points.
float polylineLength(const float r[], const float c[], int count);
