#include "LedPlan.h"

void ledPlan(const Game& game, const MachineController& mc, uint32_t now, Rgb out[]) {
  const int n = game.rules().n;
  for (int i = 0; i < n * n; i++) out[i] = LED_OFF;
  const PieceView* pieces = game.pieces();
  const uint32_t clock = game.clockMs();
  const JobView job = mc.job();

  // Red: cooldown, proportional to the remaining time (integer math, same on every build).
  for (int i = 0; i < game.pieceCount(); i++) {
    const PieceView& p = pieces[i];
    if (!p.alive || p.held || clock >= p.cooldownUntil) continue;
    if (job.active && job.pieceId == p.id) continue;  // being moved
    const uint32_t len = p.side == HUMAN ? game.rules().humanCooldownMs : game.rules().machineCooldownMs;
    if (len == 0) continue;
    uint32_t rem = p.cooldownUntil - clock;
    if (rem > len) rem = len;
    const uint8_t v = static_cast<uint8_t>((255u * rem) / len);
    out[p.r * n + p.c] = Rgb{v, 0, 0};
  }

  // Blue, then yellow on top: destinations of held pieces.
  const Phase ph = game.phase();
  if (game.heldCount() > 0 && (ph == Phase::Play || ph == Phase::Removing)) {
    const Board withReserve = game.boardWithReserve();
    Board plain = game.boardWithoutHeld();
    const bool readLift = mc.params().readLift;
    for (int h = 0; h < game.heldCount(); h++) {
      const PieceView& p = pieces[game.heldIds()[h] - 1];
      int d[MAX_N * MAX_N];
      int k = dests(withReserve, p.r, p.c, d);
      for (int j = 0; j < k; j++) out[d[j]] = LED_BLUE;
      if (!readLift) continue;
      // Same as `threat` in the original simulator.
      k = dests(plain, p.r, p.c, d);
      for (int j = 0; j < k; j++) {
        int caps[MAX_N * MAX_N];
        plain.cells[d[j]] = HUMAN;
        const int nc = capturesAt(plain, HUMAN, d[j] / n, d[j] % n, caps);
        plain.cells[d[j]] = EMPTY;
        if (nc > 0) out[d[j]] = LED_YELLOW;
      }
    }
  }

  // Purple: destination of the move job.
  if (job.active && !job.removing && job.toR >= 0 && job.toR < n && job.toC >= 0 && job.toC < n)
    out[job.toR * n + job.toC] = LED_PURPLE;

  // White blink: captured pieces still on the board.
  if ((now / LED_WHITE_BLINK_MS) % 2 == 0) {
    Removal rm[2 * MAX_N];
    const int k = game.removals(rm);
    for (int i = 0; i < k; i++) {
      // The piece being carried may already be off the board (reserve row).
      if (rm[i].from.r >= 0 && rm[i].from.r < n && rm[i].from.c >= 0 && rm[i].from.c < n)
        out[rm[i].from.r * n + rm[i].from.c] = LED_WHITE;
    }
  }

  // Violation: fast red blink for 1 s.
  const Violation v = game.lastViolation();
  if (v.valid && v.r >= 0 && v.r < n && v.c >= 0 && v.c < n) {
    const uint32_t age = now - v.at;
    if (age < LED_VIOLATION_MS) out[v.r * n + v.c] = (age / LED_VIOLATION_BLINK_MS) % 2 == 0 ? LED_RED : LED_OFF;
  }
}
