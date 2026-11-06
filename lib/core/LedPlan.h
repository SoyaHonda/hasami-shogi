#pragma once
#include <cstdint>
#include "Game.h"
#include "MachineController.h"
#include "Types.h"

// LED colors shared by the firmware and the simulator.
constexpr Rgb LED_OFF{0, 0, 0};
constexpr Rgb LED_BLUE{0, 0, 255};      // destinations of a held piece
constexpr Rgb LED_WHITE{255, 255, 255}; // captured pieces waiting to be carried off (blinks)
constexpr Rgb LED_PURPLE{160, 0, 255};  // destination of the machine's move job
constexpr Rgb LED_YELLOW{255, 200, 0};  // readLift: destinations where the held piece captures
constexpr Rgb LED_RED{255, 0, 0};       // violation (fast blink); cooldown is red scaled by remaining
// RE (plan 2026-11-06 3.1): the last empty neighbour of an edge group with one left, where the
// other side completes the enclosure. Yellow (a capture square, as LED_YELLOW) at 40 %.
constexpr Rgb LED_EDGE_HINT{102, 80, 0};
constexpr uint32_t LED_WHITE_BLINK_MS = 500;
constexpr uint32_t LED_VIOLATION_BLINK_MS = 100;
constexpr uint32_t LED_VIOLATION_MS = 1000;
constexpr uint32_t LED_MISMATCH_BLINK_MS = 250;

// Color of every square (out[r*n+c], n*n entries) for the game state at caller time `now`.
// Priority where layers overlap: mismatch > violation > white > purple > yellow > blue > edge hint
// > red (cooldown). Blue and yellow follow the rules in force: no square where the held piece
// would make a wall (RW), yellow wherever it captures (captures() of Capture.h). The edge hint
// (RE) is shown in Play and Opening, for the groups of both sides, on the board without held
// pieces. The violation blink starts lit at the violation time and alternates every 100 ms
// for 1 s; its dark half is black (it hides the other layers). White is lit while (now / 500) is
// even.
// mismatch (firmware, n*n entries or nullptr): squares where the sensors disagree with the game
// now. Shown only while the game is paused by a mismatch (Game::mismatch): a square where the
// game has a piece is white, one where it has none blinks red (lit while (now / 250) is even;
// the dark half is black). Held pieces' destinations stay blue during that pause.
void ledPlan(const Game& game, const MachineController& mc, uint32_t now, Rgb out[],
             const bool mismatch[] = nullptr);
