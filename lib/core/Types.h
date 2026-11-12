#pragma once
#include <cstdint>

enum Cell : int8_t { EMPTY = 0, HUMAN = 1, MACHINE = 2 };
// Opening (RW, plan 2026-11-06 3.1): after the start countdown, each side makes one move before
// Play; the game clock stays stopped. Appended so the other values keep their numbers.
enum class Phase { Ready, Countdown, Play, Paused, Removing, Over, Opening };
// Draw: the time limit ran out with equal captures.
enum class Winner { None, Human, Machine, Draw };
enum class Level { Easy, Normal, Hard };
enum class MagnetMode { Off, PullMachine, PullHuman, RepelWeak };
constexpr int MAX_N = 9;

// LED color.
struct Rgb { uint8_t r, g, b; };

// Square coordinate that can also lie off the board (e.g. reserve rows -1 and n).
struct Sq { int8_t r, c; };
