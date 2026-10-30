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
constexpr uint32_t LED_WHITE_BLINK_MS = 500;
constexpr uint32_t LED_VIOLATION_BLINK_MS = 100;
constexpr uint32_t LED_VIOLATION_MS = 1000;

// Color of every square (out[r*n+c], n*n entries) for the game state at caller time `now`.
// Priority where layers overlap: violation > white > purple > yellow > blue > red (cooldown).
// The violation blink starts lit at the violation time and alternates every 100 ms for 1 s; its
// dark half is black (it hides the other layers). White is lit while (now / 500) is even.
void ledPlan(const Game& game, const MachineController& mc, uint32_t now, Rgb out[]);
