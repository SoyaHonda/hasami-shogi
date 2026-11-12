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
// RU (plan 2026-11-06 3.1, user decision 2026-11-07): the square of a piece (either side's) not
// moved for a while. Warning: in the last LED_RU_WARN_MS before RU makes it takable by a single
// adjacent enemy piece, green rising from LED_RU_WARN_MIN to full brightness (LED_RU_WARN is the
// full color). Vulnerable: from then on (Game::stale()), steady cyan. Hues no other layer uses.
constexpr Rgb LED_RU_WARN{0, 255, 0};
constexpr Rgb LED_RU_VULNERABLE{0, 255, 255};
constexpr uint32_t LED_RU_WARN_MS = 10000;
constexpr uint8_t LED_RU_WARN_MIN = 32;
constexpr uint32_t LED_WHITE_BLINK_MS = 500;
constexpr uint32_t LED_VIOLATION_BLINK_MS = 100;
constexpr uint32_t LED_VIOLATION_MS = 1000;
constexpr uint32_t LED_MISMATCH_BLINK_MS = 250;

// Color of every square (out[r*n+c], n*n entries) for the game state at caller time `now`.
// Priority where layers overlap: mismatch > violation > white > purple > yellow > blue > edge hint
// > RU (vulnerable, warning) > red (cooldown). Blue and yellow follow the rules in force: no square
// where the held piece would make a wall (RW), yellow wherever it captures (captures() of
// Capture.h). The edge hint (RE) is shown in Play and Opening, for the groups of both sides, on
// the board without held pieces. The RU colors are on the squares of pieces (both sides; not a
// held piece, nor a machine piece being dragged, which holds the reservation and is off the board
// for captures), so of the layers above them only the violation and the mismatch can meet them
// (blue, yellow, purple and the edge hint light empty squares, white a captured piece); they are
// shown while a game is on (every phase but Ready and Over; frozen with the game clock), with
// remaining = movedAt + untouchedMs - clock: none while remaining > LED_RU_WARN_MS, warning
// green at LED_RU_WARN_MIN + (255 - LED_RU_WARN_MIN) * (LED_RU_WARN_MS - remaining) /
// LED_RU_WARN_MS (integer math) while 0 < remaining <= LED_RU_WARN_MS, vulnerable cyan at
// remaining <= 0. They win over the cooldown red (a mismatch pause can cool a stale piece down).
// The violation blink starts lit at the violation time and alternates every 100 ms
// for 1 s; its dark half is black (it hides the other layers). White is lit while (now / 500) is
// even.
// mismatch (firmware, n*n entries or nullptr): squares where the sensors disagree with the game
// now. Shown only while the game is paused by a mismatch (Game::mismatch): a square where the
// game has a piece is white, one where it has none blinks red (lit while (now / 250) is even;
// the dark half is black). Held pieces' destinations stay blue during that pause.
void ledPlan(const Game& game, const MachineController& mc, uint32_t now, Rgb out[],
             const bool mismatch[] = nullptr);
