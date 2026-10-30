#pragma once

// LED number for square (r, c) on an n x n serpentine strip:
// even rows run left to right, odd rows right to left.
int ledIndex(int r, int c, int n);
