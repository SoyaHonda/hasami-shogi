// embind bindings of lib/core for the browser simulator (web/) and the Node.js tools (tools/).
// Settings cross the boundary as plain JavaScript objects; a missing field takes the C++
// default, so JavaScript never has its own copy of the defaults (it reads them from defaults()).
#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <charconv>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include "AiCase.h"
#include "HumanBot.h"
#include "Params.h"
#include "Rules.h"
#include "SimDriver.h"

using emscripten::val;

namespace {

// A float setting as the shortest decimal that reads back as the same float (0.8f -> 0.8, not
// 0.800000011920929), so JavaScript shows and compares the value as written in C++.
double shortest(float f) {
  char buf[32];
  const std::to_chars_result res = std::to_chars(buf, buf + sizeof(buf) - 1, f);
  *res.ptr = '\0';
  return std::strtod(buf, nullptr);
}

bool present(const val& v) { return !v.isUndefined() && !v.isNull(); }

[[noreturn]] void throwType(const std::string& msg) {
  val::global("TypeError").new_(msg).throw_();
  __builtin_unreachable();
}

// Throws a TypeError for a key of `o` that is not in `known` (catches typos in settings).
void checkKeys(const val& o, const char* what, std::initializer_list<const char*> known) {
  const val keys = val::global("Object").call<val>("keys", o);
  const int len = keys["length"].as<int>();
  for (int i = 0; i < len; i++) {
    const std::string k = keys[i].as<std::string>();
    bool ok = false;
    for (const char* kn : known) ok = ok || k == kn;
    if (!ok) throwType(std::string("unknown ") + what + " field: " + k);
  }
}

void readObject(const val& o, const char* what) {
  if (o.typeOf().as<std::string>() != "object") throwType(std::string(what) + " must be an object");
}

template <class T>
void take(const val& o, const char* key, T& dst) {
  const val f = o[key];
  if (present(f)) dst = f.as<T>();
}

Level levelFrom(const val& v) {
  if (v.typeOf().as<std::string>() == "string") {
    const std::string s = v.as<std::string>();
    if (s == "easy" || s == "Easy" || s == "E") return Level::Easy;
    if (s == "normal" || s == "Normal" || s == "N") return Level::Normal;
    if (s == "hard" || s == "Hard" || s == "H") return Level::Hard;
    throwType("unknown level: " + s);
  }
  return v.as<Level>();
}

Rules rulesFrom(const val& o) {
  Rules r;
  if (!present(o)) return r;
  readObject(o, "rules");
  checkKeys(o, "rules",
            {"n", "target", "diffOn", "diff", "humanCooldownMs", "machineCooldownMs", "countdownMs", "resumeCountdownMs"});
  take(o, "n", r.n);
  take(o, "target", r.target);
  take(o, "diffOn", r.diffOn);
  take(o, "diff", r.diff);
  take(o, "humanCooldownMs", r.humanCooldownMs);
  take(o, "machineCooldownMs", r.machineCooldownMs);
  take(o, "countdownMs", r.countdownMs);
  take(o, "resumeCountdownMs", r.resumeCountdownMs);
  if (r.n < 3 || r.n > MAX_N) throwType("rules.n must be 3.." + std::to_string(MAX_N));
  return r;
}

Params paramsFrom(const val& o) {
  Params p;
  if (!present(o)) return p;
  readObject(o, "params");
  checkKeys(o, "params", {"freeSpeed", "dragSpeed", "engageMs", "reactMs", "level", "aggression", "readLift"});
  take(o, "freeSpeed", p.freeSpeed);
  take(o, "dragSpeed", p.dragSpeed);
  take(o, "engageMs", p.engageMs);
  take(o, "reactMs", p.reactMs);
  if (present(o["level"])) p.level = levelFrom(o["level"]);
  take(o, "aggression", p.aggression);
  take(o, "readLift", p.readLift);
  return p;
}

BotParams botFrom(const val& o) {
  BotParams b;
  if (!present(o)) return b;
  readObject(o, "bot");
  checkKeys(o, "bot", {"reactMs", "holdMs", "greed"});
  take(o, "reactMs", b.reactMs);
  take(o, "holdMs", b.holdMs);
  take(o, "greed", b.greed);
  return b;
}

val rulesToVal(const Rules& r) {
  val o = val::object();
  o.set("n", r.n);
  o.set("target", r.target);
  o.set("diffOn", r.diffOn);
  o.set("diff", r.diff);
  o.set("humanCooldownMs", r.humanCooldownMs);
  o.set("machineCooldownMs", r.machineCooldownMs);
  o.set("countdownMs", r.countdownMs);
  o.set("resumeCountdownMs", r.resumeCountdownMs);
  return o;
}

val paramsToVal(const Params& p) {
  val o = val::object();
  o.set("freeSpeed", shortest(p.freeSpeed));
  o.set("dragSpeed", shortest(p.dragSpeed));
  o.set("engageMs", p.engageMs);
  o.set("reactMs", p.reactMs);
  o.set("level", p.level);
  o.set("aggression", shortest(p.aggression));
  o.set("readLift", p.readLift);
  return o;
}

val botToVal(const BotParams& b) {
  val o = val::object();
  o.set("reactMs", b.reactMs);
  o.set("holdMs", b.holdMs);
  o.set("greed", shortest(b.greed));
  return o;
}

// {rules, params, bot}: the C++ defaults.
val defaults() {
  val o = val::object();
  o.set("rules", rulesToVal(Rules()));
  o.set("params", paramsToVal(Params()));
  o.set("bot", botToVal(BotParams()));
  return o;
}

val stateToVal(const State& s) {
  val o = val::object();
  o.set("phase", s.phase);
  o.set("winner", s.winner);
  o.set("n", s.n);
  const int nn = s.n * s.n;
  val cells = val::array();
  val leds = val::array();
  for (int i = 0; i < nn; i++) {
    cells.set(i, static_cast<int>(s.cells[i]));
    // 0xRRGGBB
    leds.set(i, (static_cast<int>(s.leds[i].r) << 16) | (static_cast<int>(s.leds[i].g) << 8) | s.leds[i].b);
  }
  o.set("cells", cells);
  o.set("leds", leds);
  val pieces = val::array();
  for (int i = 0; i < s.pieceCount; i++) {
    const PieceState& p = s.pieces[i];
    val q = val::object();
    q.set("id", p.id);
    q.set("side", static_cast<int>(p.side));
    q.set("r", p.r);
    q.set("c", p.c);
    q.set("alive", p.alive);
    q.set("held", p.held);
    q.set("cooldownLeft", p.cooldownLeft);
    q.set("x", p.x);
    q.set("y", p.y);
    pieces.set(i, q);
  }
  o.set("pieces", pieces);
  o.set("headR", s.headR);
  o.set("headC", s.headC);
  val job = val::object();
  job.set("active", s.job.active);
  job.set("pieceId", s.job.pieceId);
  job.set("toR", s.job.toR);
  job.set("toC", s.job.toC);
  job.set("stage", s.job.stage);
  job.set("removing", s.job.removing);
  o.set("job", job);
  o.set("clockMs", s.clockMs);
  o.set("capturedHuman", s.capturedHuman);
  o.set("capturedMachine", s.capturedMachine);
  o.set("saves", s.saves);
  o.set("machineMoves", s.machineMoves);
  o.set("stuckId", s.stuckId);
  val events = val::array();
  for (int i = 0; i < s.eventCount; i++) {
    const SimEvent& e = s.events[i];
    val ev = val::object();
    ev.set("seq", e.seq);
    ev.set("kind", e.kind);
    ev.set("side", static_cast<int>(e.side));
    ev.set("count", e.count);
    ev.set("r", e.r);
    ev.set("c", e.c);
    ev.set("clockMs", e.clockMs);
    events.set(i, ev);
  }
  o.set("events", events);  // newest first
  return o;
}

// One line of test/golden/ai_cases.txt: {parsed, match, none, fromR, fromC, toR, toC, score}.
// match is checkAiCase (the move equals the line's expectation); from/to are the move think()
// picked (-1 when none).
val thinkCase(const std::string& line) {
  val o = val::object();
  AiCase c;
  const bool parsed = parseAiCase(line.c_str(), c);
  o.set("parsed", parsed);
  if (!parsed) return o;
  AiMove mv;
  const bool match = checkAiCase(c, &mv);
  const bool valid = !mv.none && mv.pieceId >= 1 && mv.pieceId <= c.snap.count;
  o.set("match", match);
  o.set("none", mv.none);
  o.set("fromR", valid ? c.snap.pieces[mv.pieceId - 1].r : -1);
  o.set("fromC", valid ? c.snap.pieces[mv.pieceId - 1].c : -1);
  o.set("toR", valid ? mv.toR : -1);
  o.set("toC", valid ? mv.toC : -1);
  o.set("score", shortest(mv.score));
  return o;
}

// SimDriver as seen from JavaScript: settings as plain objects, state() as an object.
class JsSim {
 public:
  JsSim() : d_() {}
  JsSim(const val& rules, const val& params, uint32_t seed) : d_(rulesFrom(rules), paramsFrom(params), seed) {}

  void setParams(const val& p) { d_.setParams(paramsFrom(p)); }
  void setRules(const val& r) { d_.setRules(rulesFrom(r)); }
  void reset() { d_.reset(); }
  void start() { d_.start(); }
  void togglePause() { d_.togglePause(); }
  void advance(uint32_t dtMs) { d_.advance(dtMs); }
  LiftResult lift(int r, int c) { return d_.lift(r, c); }
  PlaceResult place(int r, int c) { return d_.place(r, c); }
  void enableBot(const val& b) { d_.enableBot(botFrom(b)); }
  void disableBot() { d_.disableBot(); }
  val state() const { return stateToVal(d_.state()); }
  // Cheap reads for loops that would otherwise build a whole state() each step.
  Phase phase() const { return d_.game().phase(); }
  uint32_t clockMs() const { return d_.game().clockMs(); }

 private:
  SimDriver d_;
};

}  // namespace

EMSCRIPTEN_BINDINGS(hasami) {
  using namespace emscripten;

  enum_<LiftResult>("LiftResult")
      .value("Ok", LiftResult::Ok)
      .value("InCooldown", LiftResult::InCooldown)
      .value("NotHumanPiece", LiftResult::NotHumanPiece)
      .value("NotPlaying", LiftResult::NotPlaying)
      .value("TooMany", LiftResult::TooMany);
  enum_<PlaceResult>("PlaceResult")
      .value("Ok", PlaceResult::Ok)
      .value("Cancelled", PlaceResult::Cancelled)
      .value("Illegal", PlaceResult::Illegal)
      .value("NoHeldPiece", PlaceResult::NoHeldPiece)
      .value("NotPlaying", PlaceResult::NotPlaying);
  enum_<Phase>("Phase")
      .value("Ready", Phase::Ready)
      .value("Countdown", Phase::Countdown)
      .value("Play", Phase::Play)
      .value("Paused", Phase::Paused)
      .value("Removing", Phase::Removing)
      .value("Over", Phase::Over);
  enum_<Winner>("Winner").value("None", Winner::None).value("Human", Winner::Human).value("Machine", Winner::Machine);
  enum_<Level>("Level").value("Easy", Level::Easy).value("Normal", Level::Normal).value("Hard", Level::Hard);
  enum_<Stage>("Stage").value("Travel", Stage::Travel).value("Engage", Stage::Engage).value("Drag", Stage::Drag);
  enum_<EventKind>("EventKind").value("Capture", EventKind::Capture).value("Save", EventKind::Save);

  class_<JsSim>("SimDriver")
      .constructor<>()
      .constructor<const val&, const val&, uint32_t>()
      .function("setParams", &JsSim::setParams)
      .function("setRules", &JsSim::setRules)
      .function("reset", &JsSim::reset)
      .function("start", &JsSim::start)
      .function("togglePause", &JsSim::togglePause)
      .function("advance", &JsSim::advance)
      .function("lift", &JsSim::lift)
      .function("place", &JsSim::place)
      .function("enableBot", &JsSim::enableBot)
      .function("disableBot", &JsSim::disableBot)
      .function("state", &JsSim::state)
      .function("phase", &JsSim::phase)
      .function("clockMs", &JsSim::clockMs);

  function("defaults", &defaults);
  function("thinkCase", &thinkCase);
}
