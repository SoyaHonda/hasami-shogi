#pragma once
#include <cstdint>

enum Cell : int8_t { EMPTY = 0, HUMAN = 1, MACHINE = 2 };
enum class Phase { Ready, Countdown, Play, Paused, Removing, Over };
enum class Winner { None, Human, Machine };
enum class Level { Easy, Normal, Hard };
enum class MagnetMode { Off, PullMachine, PullHuman, RepelWeak };
constexpr int MAX_N = 9;

// LED color.
struct Rgb { uint8_t r, g, b; };

// Square coordinate that can also lie off the board (e.g. reserve rows -1 and n).
struct Sq { int8_t r, c; };
