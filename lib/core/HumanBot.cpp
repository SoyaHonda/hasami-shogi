#include "HumanBot.h"

HumanBot::HumanBot(const BotParams& p, uint32_t seed) : params_(p), rng_(seed) { reset(); }

void HumanBot::reset() {
  holding_ = false;
  heldId_ = -1;
  armed_ = false;
  nextAt_ = 0;
  failures_ = 0;
}

BotAction HumanBot::decide(const Game& game) {
  const BotAction none{BotAction::None, -1, -1};
  if (game.phase() != Phase::Play) return none;
  const uint32_t clock = game.clockMs();
  if (holding_) {
    // The piece may have been released by something else (e.g. a new game).
    bool stillHeld = false;
    for (int i = 0; i < game.heldCount(); i++)
      if (game.heldIds()[i] == heldId_) stillHeld = true;
    if (!stillHeld) {
      holding_ = false;
      armed_ = true;
      nextAt_ = clock + params_.reactMs;
      return none;
    }
  } else if (!armed_) {
    armed_ = true;
    nextAt_ = clock + params_.reactMs;
  }
  if (clock < nextAt_) return none;

  const Board b = game.boardWithReserve();
  const int n = b.n;
  const PieceView* pieces = game.pieces();

  if (!holding_) {
    // Movable human pieces: cooled down with at least one legal destination.
    int ids[2 * MAX_N];
    int k = 0;
    for (int i = 0; i < game.pieceCount(); i++) {
      const PieceView& p = pieces[i];
      if (!p.alive || p.held || p.side != HUMAN || clock < p.cooldownUntil) continue;
      int d[MAX_N * MAX_N];
      if (dests(b, p.r, p.c, d) == 0) continue;
      ids[k++] = p.id;
    }
    if (k == 0) return none;
    const PieceView& p = pieces[ids[rng_.below(k)] - 1];
    return BotAction{BotAction::Lift, p.r, p.c};
  }

  const PieceView& p = pieces[heldId_ - 1];
  int d[MAX_N * MAX_N];
  const int k = dests(b, p.r, p.c, d);
  if (k == 0) {
    // Put it back when the origin is free; otherwise any empty square (escape rule).
    if (b.at(p.r, p.c) == EMPTY) return BotAction{BotAction::Place, p.r, p.c};
    int empty[MAX_N * MAX_N];
    int ne = 0;
    for (int i = 0; i < n * n; i++)
      if (b.cells[i] == EMPTY) empty[ne++] = i;
    if (ne == 0) return none;
    const int e = empty[rng_.below(ne)];
    return BotAction{BotAction::Place, e / n, e % n};
  }
  int capt[MAX_N * MAX_N];
  int nc = 0;
  // Capture check on the real board (the reserved square is not a machine piece yet).
  Board bb = game.boardWithoutHeld();
  for (int i = 0; i < k; i++) {
    int caps[MAX_N * MAX_N];
    bb.cells[d[i]] = HUMAN;
    if (capturesAt(bb, HUMAN, d[i] / n, d[i] % n, caps) > 0) capt[nc++] = d[i];
    bb.cells[d[i]] = EMPTY;
  }
  int to;
  if (nc > 0 && failures_ == 0 && rng_.unit() < params_.greed) {
    to = capt[rng_.below(nc)];
  } else {
    to = d[rng_.below(k)];
  }
  return BotAction{BotAction::Place, to / n, to % n};
}

void HumanBot::onLifted(const Game& game, bool ok, int r, int c) {
  if (!ok) return;
  (void)r;
  (void)c;
  // The lifted piece is the last one in the held list.
  heldId_ = game.heldIds()[game.heldCount() - 1];
  holding_ = true;
  failures_ = 0;
  nextAt_ = game.clockMs() + params_.holdMs;
}

void HumanBot::onPlaced(const Game& game, bool ok) {
  if (!ok) {
    failures_++;  // retry next tick with the fallback
    return;
  }
  holding_ = false;
  heldId_ = -1;
  failures_ = 0;
  armed_ = true;
  nextAt_ = game.clockMs() + params_.reactMs;
}
