#include "LedPlan.h"
#include "Capture.h"

void ledPlan(const Game& game, const MachineController& mc, uint32_t now, Rgb out[], const bool mismatch[]) {
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

  const Rules& rules = game.rules();
  const Phase ph = game.phase();
  // RU: pieces of both sides close to (warning) or past (vulnerable) untouchedMs without a move.
  if (rules.untouchedMs > 0 && ph != Phase::Ready && ph != Phase::Over) {
    for (int i = 0; i < game.pieceCount(); i++) {
      const PieceView& p = pieces[i];
      if (!p.alive || p.held || p.id == game.reservedBy()) continue;  // off the board
      const uint32_t age = clock > p.movedAt ? clock - p.movedAt : 0;
      if (age >= rules.untouchedMs) {
        out[p.r * n + p.c] = LED_RU_VULNERABLE;
        continue;
      }
      const uint32_t rem = rules.untouchedMs - age;
      if (rem > LED_RU_WARN_MS) continue;
      // Brightness from LED_RU_WARN_MIN (10 s to go) rising to 255 (integer math).
      const uint32_t v = LED_RU_WARN_MIN + ((255u - LED_RU_WARN_MIN) * (LED_RU_WARN_MS - rem)) / LED_RU_WARN_MS;
      const auto scale = [v](uint8_t ch) { return static_cast<uint8_t>((ch * v) / 255u); };
      out[p.r * n + p.c] = Rgb{scale(LED_RU_WARN.r), scale(LED_RU_WARN.g), scale(LED_RU_WARN.b)};
    }
  }

  // Edge hint (RE): the last empty neighbour of an edge group, either side's.
  if (rules.edgeCapture && (ph == Phase::Play || ph == Phase::Opening)) {
    const Board plain = game.boardWithoutHeld();
    SquareSet seen;
    seen.clear();
    for (int i = 0; i < n * n; i++) {
      if (plain.cells[i] == EMPTY || seen.has(i)) continue;
      EdgeGroup g;
      if (!edgeGroupAt(plain, i / n, i % n, g)) {
        seen.set(i);
        continue;
      }
      for (int w = 0; w < 3; w++) seen.w[w] |= g.cells.w[w];
      if (g.libCount == 1) out[g.libs[0]] = LED_EDGE_HINT;
    }
  }

  // Blue, then yellow on top: destinations of held pieces.
  if (game.heldCount() > 0 &&
      (ph == Phase::Play || ph == Phase::Opening || ph == Phase::Removing || game.mismatch())) {
    const Board withReserve = game.boardWithReserve();
    Board plain = game.boardWithoutHeld();
    const bool readLift = mc.params().readLift;
    const SquareSet stale = game.stale();
    for (int h = 0; h < game.heldCount(); h++) {
      const PieceView& p = pieces[game.heldIds()[h] - 1];
      int d[MAX_N * MAX_N];
      int k = dests(withReserve, p.r, p.c, d);
      for (int j = 0; j < k; j++)
        if (!makesWall(withReserve, rules, HUMAN, p.r, p.c, d[j] / n, d[j] % n)) out[d[j]] = LED_BLUE;
      if (!readLift) continue;
      // Same as `threat` in the original simulator.
      k = dests(plain, p.r, p.c, d);
      for (int j = 0; j < k; j++) {
        if (makesWall(plain, rules, HUMAN, p.r, p.c, d[j] / n, d[j] % n)) continue;
        int caps[MAX_N * MAX_N];
        plain.cells[d[j]] = HUMAN;
        const int nc = captures(plain, rules, HUMAN, d[j] / n, d[j] % n, caps, &stale);
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

  // Mismatch pause: where pieces should be (white) and should not be (red blink).
  if (mismatch && game.mismatch()) {
    const Board b = game.boardWithoutHeld();
    const bool lit = (now / LED_MISMATCH_BLINK_MS) % 2 == 0;
    for (int i = 0; i < n * n; i++) {
      if (!mismatch[i]) continue;
      out[i] = b.cells[i] != EMPTY ? LED_WHITE : lit ? LED_RED : LED_OFF;
    }
  }
}
