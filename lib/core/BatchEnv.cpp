#include "BatchEnv.h"
#include <cmath>
#include <cstring>

EnvConfig envConfig(EnvOpponent opponent, float speedScale, Level level) {
  EnvConfig c;
  c.rules.edgeCapture = true;
  c.rules.wallLimit = true;
  c.rules.untouchedMs = 40000;
  c.params.level = level;
  c.params.freeSpeed *= speedScale;
  c.params.dragSpeed *= speedScale;
  c.params.engageMs = static_cast<uint32_t>(std::lround(static_cast<double>(c.params.engageMs) / speedScale));
  c.speedScale100 = static_cast<int>(std::lround(speedScale * 100.0));
  switch (opponent) {
    case EnvOpponent::B1:
      c.bot.style = BotStyle::Race;
      c.bot.reactMs = 1000;
      c.bot.thinkMaxMs = 3000;
      c.bot.threatMiss = 0.3f;
      break;
    case EnvOpponent::Race:
      c.bot.style = BotStyle::Race;
      break;
    case EnvOpponent::Random:
      c.bot.style = BotStyle::Random;
      break;
  }
  return c;
}

EnvSlot::EnvSlot(const EnvConfig& cfg)
    : cfg_(cfg),
      game_(cfg.rules),
      gantry_(cfg.params),
      ctl_(game_, gantry_, cfg.params),
      bot_(cfg.bot, 1),
      now_(0),
      seed_(0),
      stride_(1),
      truncated_(false),
      snap_{},
      maskCount_(0),
      decisionAt_(0),
      reward_(0.0f),
      lastDiff_(0) {
  std::memset(mask_, 0, sizeof(mask_));
}

void EnvSlot::newGame(uint32_t seed) {
  seed_ = seed;
  // Fresh objects, as tools/autoplay.mjs makes a new SimDriver for every game.
  game_ = Game(cfg_.rules);
  gantry_ = VirtualGantry(cfg_.params);
  ctl_ = MachineController(game_, gantry_, cfg_.params);
  bot_ = HumanBot(cfg_.bot, seed);
  bot_.setMachineParams(cfg_.params);
  activity_.reset();
  now_ = 0;
  truncated_ = false;
  reward_ = 0.0f;
  lastDiff_ = 0;
  decisionAt_ = 0;
  // SimDriver's constructor (resetMachine) and start().
  ctl_.update(now_);
  gantry_.magnet(MagnetMode::Off);
  gantry_.setHead(0.0f, static_cast<float>(cfg_.rules.n / 2));
  gantry_.update(now_);
  game_.start(now_);
}

void EnvSlot::reset(uint32_t seed, uint32_t seedStride) {
  stride_ = seedStride;
  newGame(seed);
  while (!runToDecision()) newGame(seed_ + stride_);
  decisionAt_ = now_;
}

float EnvSlot::captureDelta() {
  const int d = diff();
  const float r = cfg_.captureReward * static_cast<float>(d - lastDiff_);
  lastDiff_ = d;
  return r;
}

bool EnvSlot::runToDecision() {
  for (;;) {
    if (game_.phase() == Phase::Over) return false;
    if (now_ >= cfg_.maxSimMs) {
      truncated_ = true;
      return false;
    }
    // SimDriver::advance, with the read replaced by the decision point.
    now_ += cfg_.tickMs;
    const uint32_t clockBefore = game_.clockMs();
    const int diffBefore = diff();
    game_.tick(now_);
    // The game clock advances only here (in Play), before anything in the tick can capture.
    if (diffBefore == 0)
      reward_ -= cfg_.drawPenaltyPerSec * static_cast<float>(game_.clockMs() - clockBefore) / 1000.0f;
    ctl_.update(now_);
    gantry_.update(now_);
    if (game_.phase() == Phase::Paused && game_.stuckId() >= 0) game_.onManualRemoval(game_.stuckId());
    if (ctl_.wantsThink(now_)) {
      snap_ = ctl_.takeSnapshot(now_);
      maskCount_ = buildMask(game_, mask_);
      if (maskCount_ >= 2) {
        reward_ += captureDelta();
        return true;
      }
      // A single choice (or none, which cannot normally happen) is taken without asking.
      int only = ACTION_STAY;
      for (int a = 0; a < ACTION_COUNT; a++)
        if (mask_[a]) {
          only = a;
          break;
        }
      applyRead(only);
    }
    tailOfTick();
  }
}

void EnvSlot::applyRead(int action) {
  const AiMove m = moveOfAction(game_, action);
  const bool wasActive = ctl_.job().active;
  ctl_.onThinkResult(m, now_);  // always delivered: it clears the awaiting mark
  const JobView j = ctl_.job();
  if (cfg_.trackActivity && !wasActive && j.active && !j.removing) {
    const PieceView& mp = game_.pieces()[j.pieceId - 1];
    activity_.machineMove(j.pieceId, mp.r, mp.c, j.toR, j.toC);
  }
}

void EnvSlot::tailOfTick() {
  // SimDriver::runBot, lift and place.
  const BotAction a = bot_.decide(game_, gantry_.headR(), gantry_.headC(), now_);
  if (a.kind == BotAction::Lift) {
    const LiftResult res = game_.onHumanLift(a.r, a.c, now_);
    if (res == LiftResult::Ok) ctl_.onHumanLift(now_);
    bot_.onLifted(game_, res == LiftResult::Ok, a.r, a.c);
  } else if (a.kind == BotAction::Place) {
    const PlaceResult res = game_.onHumanPlace(a.r, a.c, now_);
    bot_.onPlaced(game_, res);
  }
  if (cfg_.trackActivity) activity_.observe(game_, gantry_.headR(), gantry_.headC(), cfg_.params);
}

EnvStep EnvSlot::step(int action) {
  EnvStep out{};
  applyRead(action);
  tailOfTick();
  if (runToDecision()) {
    out.reward = reward_;
    out.dtMs = now_ - decisionAt_;
    out.done = false;
    out.info = info();
    reward_ = 0.0f;
    decisionAt_ = now_;
    return out;
  }
  // The game is over (or cut off).
  reward_ += captureDelta();
  if (!truncated_) {
    const Winner w = game_.winner();
    if (w == Winner::Machine) reward_ += cfg_.winReward;
    if (w == Winner::Human) reward_ -= cfg_.winReward;
  }
  out.reward = reward_;
  out.dtMs = now_ - decisionAt_;
  out.done = true;
  out.info = info();
  // The next game, up to its first decision point. Reward accrued there before that point (none
  // in practice: no capture can happen in the opening) is part of the next step.
  do {
    newGame(seed_ + stride_);
  } while (!runToDecision());
  decisionAt_ = now_;
  return out;
}

void EnvSlot::features(int16_t planes[], int16_t scal[]) const {
  buildFeatures(game_, gantry_.headR(), gantry_.headC(), cfg_.speedScale100, planes, scal);
}

int EnvSlot::readAction(Level level) const {
  Params p = cfg_.params;
  p.level = level;
  return actionOfAiMove(game_, think(snap_, p));
}

EnvGameInfo EnvSlot::info() const {
  EnvGameInfo g{};
  const Winner w = game_.phase() == Phase::Over ? game_.winner() : Winner::None;
  g.winner = w == Winner::Human ? 1 : w == Winner::Machine ? 2 : 0;
  g.capturedMachine = game_.captured(MACHINE);
  g.capturedHuman = game_.captured(HUMAN);
  g.clockMs = game_.clockMs();
  g.truncated = truncated_;
  g.activity = activity_.stats();
  return g;
}

BatchEnv::BatchEnv(int nEnvs, uint32_t seed, const EnvConfig& cfg) : cfg_(cfg), seed_(seed) {
  slots_.reserve(nEnvs > 0 ? nEnvs : 0);
  for (int i = 0; i < nEnvs; i++) slots_.emplace_back(new EnvSlot(cfg));
}

void BatchEnv::resetRange(int begin, int end) {
  const uint32_t n = static_cast<uint32_t>(slots_.size());
  for (int i = begin; i < end; i++) slots_[i]->reset(seed_ + static_cast<uint32_t>(i), n);
}

void BatchEnv::stepRange(int begin, int end, const int32_t actions[], EnvStep out[]) {
  for (int i = begin; i < end; i++) out[i] = slots_[i]->step(actions[i]);
}

void BatchEnv::readRange(int begin, int end, Level level, int32_t out[]) const {
  for (int i = begin; i < end; i++) out[i] = slots_[i]->readAction(level);
}

void BatchEnv::observeRange(int begin, int end, int16_t planes[], int16_t scal[], uint8_t mask[]) const {
  for (int i = begin; i < end; i++) {
    slots_[i]->features(planes + static_cast<size_t>(i) * FEAT_PLANES, scal + static_cast<size_t>(i) * FEAT_SCALARS);
    std::memcpy(mask + static_cast<size_t>(i) * ACTION_COUNT, slots_[i]->mask(), ACTION_COUNT);
  }
}
