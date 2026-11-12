// pybind11 binding of lib/core/BatchEnv (plan 2026-11-12 section 4). The slots are stepped on
// several threads with the GIL released; the arrays are numpy arrays made here.
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <atomic>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "BatchEnv.h"

namespace py = pybind11;

// Hash of the C++ sources (hasami_env/srchash.py), set by setup.py.
#ifndef HASAMI_SOURCE_HASH
#define HASAMI_SOURCE_HASH "unknown"
#endif

namespace {

EnvOpponent opponentOf(const std::string& s) {
  if (s == "b1") return EnvOpponent::B1;
  if (s == "race") return EnvOpponent::Race;
  if (s == "random") return EnvOpponent::Random;
  throw py::value_error("opponent must be \"b1\", \"race\" or \"random\": " + s);
}

Level levelOf(const std::string& s) {
  if (s == "easy") return Level::Easy;
  if (s == "normal") return Level::Normal;
  if (s == "hard") return Level::Hard;
  throw py::value_error("level must be \"easy\", \"normal\" or \"hard\": " + s);
}

template <typename T>
py::array_t<T> makeArray(std::vector<py::ssize_t> shape) {
  return py::array_t<T>(shape);
}

class PyBatchEnv {
 public:
  PyBatchEnv(int nEnvs, uint32_t seed, const std::string& opponent, double speedScale, const std::string& level,
             double captureReward, int nThreads, bool trackActivity, int tickMs)
      : opponent_(opponent), level_(level), speedScale_(speedScale), env_(nEnvs, seed, configOf(opponent, speedScale, level, captureReward, trackActivity, tickMs)) {
    if (nEnvs < 1) throw py::value_error("n_envs must be at least 1");
    const unsigned hw = std::thread::hardware_concurrency();
    threads_ = nThreads > 0 ? nThreads : static_cast<int>(hw ? hw : 1);
    if (threads_ > nEnvs) threads_ = nEnvs;
    steps_.resize(nEnvs);
  }

  int nEnvs() const { return env_.size(); }

  py::tuple reset() {
    {
      py::gil_scoped_release nogil;
      parallel([this](int b, int e) { env_.resetRange(b, e); });
    }
    reset_ = true;
    return observe();
  }

  py::tuple step(const py::array& actionsIn) {
    if (!reset_) throw py::value_error("call reset() before step()");
    const int n = env_.size();
    auto actions = py::array_t<int32_t, py::array::c_style | py::array::forcecast>::ensure(actionsIn);
    if (!actions || actions.ndim() != 1 || actions.shape(0) != n)
      throw py::value_error("actions must be an integer array of shape (" + std::to_string(n) + ",)");
    const int32_t* act = actions.data();
    for (int i = 0; i < n; i++) {
      const int32_t a = act[i];
      if (a < 0 || a >= ACTION_COUNT || !env_.slot(i).mask()[a])
        throw py::value_error("action " + std::to_string(a) + " of env " + std::to_string(i) + " is not in its mask");
    }
    {
      py::gil_scoped_release nogil;
      EnvStep* out = steps_.data();
      parallel([this, act, out](int b, int e) { env_.stepRange(b, e, act, out); });
    }
    auto reward = makeArray<float>({n});
    auto dt = makeArray<int32_t>({n});
    auto done = makeArray<bool>({n});
    auto winner = makeArray<int8_t>({n});
    auto captures = makeArray<int16_t>({n, 2});
    auto truncated = makeArray<bool>({n});
    auto gameMs = makeArray<int32_t>({n});
    auto outside = makeArray<float>({n});
    auto meanRow = makeArray<float>({n});
    auto playMs = makeArray<int32_t>({n});
    auto threats = makeArray<int32_t>({n});
    auto moves = makeArray<int32_t>({n});
    auto backForth = makeArray<int32_t>({n});
    auto firstCapture = makeArray<int32_t>({n});
    auto leadChanges = makeArray<int32_t>({n});
    float* rw = reward.mutable_data();
    int32_t* dtp = dt.mutable_data();
    bool* dn = done.mutable_data();
    int8_t* wn = winner.mutable_data();
    int16_t* cp = captures.mutable_data();
    bool* tr = truncated.mutable_data();
    int32_t* gm = gameMs.mutable_data();
    float* os = outside.mutable_data();
    float* mr = meanRow.mutable_data();
    int32_t* pm = playMs.mutable_data();
    int32_t* th = threats.mutable_data();
    int32_t* mv = moves.mutable_data();
    int32_t* bf = backForth.mutable_data();
    int32_t* fc = firstCapture.mutable_data();
    int32_t* lc = leadChanges.mutable_data();
    for (int i = 0; i < n; i++) {
      const EnvStep& s = steps_[i];
      const EnvGameInfo& g = s.info;
      rw[i] = s.reward;
      dtp[i] = static_cast<int32_t>(s.dtMs);
      dn[i] = s.done;
      wn[i] = static_cast<int8_t>(g.winner);
      cp[2 * i] = static_cast<int16_t>(g.capturedMachine);
      cp[2 * i + 1] = static_cast<int16_t>(g.capturedHuman);
      tr[i] = g.truncated;
      gm[i] = static_cast<int32_t>(g.clockMs);
      os[i] = static_cast<float>(ActivityMeter::outsideShare(g.activity));
      mr[i] = static_cast<float>(ActivityMeter::meanRow(g.activity));
      pm[i] = static_cast<int32_t>(g.activity.playMs);
      th[i] = g.activity.threats;
      mv[i] = g.activity.moves;
      bf[i] = g.activity.backForth;
      fc[i] = g.activity.firstCaptureMs == ACTIVITY_NONE ? -1 : static_cast<int32_t>(g.activity.firstCaptureMs);
      lc[i] = g.activity.leadChanges;
    }
    py::dict activity;
    activity["outside"] = outside;
    activity["mean_row"] = meanRow;
    activity["play_ms"] = playMs;
    activity["threats"] = threats;
    activity["moves"] = moves;
    activity["back_forth"] = backForth;
    activity["first_capture_ms"] = firstCapture;
    activity["lead_changes"] = leadChanges;
    py::dict info;
    info["winner"] = winner;
    info["captures"] = captures;
    info["truncated"] = truncated;
    info["game_ms"] = gameMs;
    info["activity"] = activity;
    const py::tuple obs = observe();
    return py::make_tuple(obs[0], obs[1], obs[2], reward, dt, done, info);
  }

  py::array_t<int32_t> readActions(const py::object& level) {
    if (!reset_) throw py::value_error("call reset() before read_actions()");
    const Level lv = levelOf(level.is_none() ? level_ : level.cast<std::string>());
    const int n = env_.size();
    auto out = makeArray<int32_t>({n});
    int32_t* o = out.mutable_data();
    {
      py::gil_scoped_release nogil;
      parallel([this, lv, o](int b, int e) { env_.readRange(b, e, lv, o); });
    }
    return out;
  }

  const std::string& opponent() const { return opponent_; }
  const std::string& level() const { return level_; }
  double speedScale() const { return speedScale_; }
  int threads() const { return threads_; }
  int tickMs() const { return static_cast<int>(env_.config().tickMs); }
  double captureReward() const { return env_.config().captureReward; }

 private:
  static EnvConfig configOf(const std::string& opponent, double speedScale, const std::string& level,
                            double captureReward, bool trackActivity, int tickMs) {
    if (!(speedScale > 0.0)) throw py::value_error("speed_scale must be positive");
    if (tickMs < 1) throw py::value_error("tick_ms must be at least 1");
    EnvConfig c = envConfig(opponentOf(opponent), static_cast<float>(speedScale), levelOf(level));
    c.captureReward = static_cast<float>(captureReward);
    c.trackActivity = trackActivity;
    c.tickMs = static_cast<uint32_t>(tickMs);
    return c;
  }

  py::tuple observe() const {
    const int n = env_.size();
    auto obs = makeArray<int16_t>({n, FEAT_CHANNELS, FEAT_N, FEAT_N});
    auto scal = makeArray<int16_t>({n, FEAT_SCALARS});
    auto mask = makeArray<bool>({n, ACTION_COUNT});
    static_assert(sizeof(bool) == 1, "numpy bool is one byte");
    env_.observeRange(0, n, obs.mutable_data(), scal.mutable_data(), reinterpret_cast<uint8_t*>(mask.mutable_data()));
    return py::make_tuple(obs, scal, mask);
  }

  // f(begin, end) over chunks of the slots, on threads_ threads (this one included).
  template <typename F>
  void parallel(F f) const {
    const int n = env_.size();
    if (threads_ <= 1) {
      f(0, n);
      return;
    }
    // Small chunks taken in turn: games differ a lot in cost (the bot reads at some ticks only).
    const int chunk = n / (threads_ * 8) > 0 ? n / (threads_ * 8) : 1;
    std::atomic<int> next(0);
    auto work = [&]() {
      for (;;) {
        const int b = next.fetch_add(chunk);
        if (b >= n) return;
        f(b, b + chunk < n ? b + chunk : n);
      }
    };
    std::vector<std::thread> pool;
    pool.reserve(threads_ - 1);
    for (int t = 1; t < threads_; t++) pool.emplace_back(work);
    work();
    for (auto& t : pool) t.join();
  }

  std::string opponent_, level_;
  double speedScale_;
  BatchEnv env_;
  int threads_ = 1;
  bool reset_ = false;
  std::vector<EnvStep> steps_;
};

}  // namespace

PYBIND11_MODULE(_core, m) {
  m.doc() = "Hasami shogi learning environment (lib/core/BatchEnv, plan 2026-11-12 section 4)";
  m.attr("N_ACTIONS") = ACTION_COUNT;
  m.attr("STAY") = ACTION_STAY;
  m.attr("N_CHANNELS") = FEAT_CHANNELS;
  m.attr("BOARD") = FEAT_N;
  m.attr("N_SCALARS") = FEAT_SCALARS;
  m.attr("SOURCE_HASH") = HASAMI_SOURCE_HASH;
  py::class_<PyBatchEnv>(m, "BatchEnv")
      .def(py::init<int, uint32_t, const std::string&, double, const std::string&, double, int, bool, int>(),
           py::arg("n_envs") = 256, py::arg("seed") = 1, py::arg("opponent") = "b1", py::arg("speed_scale") = 1.0,
           py::arg("level") = "hard", py::arg("capture_reward") = 0.2, py::arg("n_threads") = 0,
           py::arg("track_activity") = true, py::arg("tick_ms") = 10)
      .def("reset", &PyBatchEnv::reset)
      .def("step", &PyBatchEnv::step, py::arg("actions"))
      .def("read_actions", &PyBatchEnv::readActions, py::arg("level") = py::none())
      .def_property_readonly("n_envs", &PyBatchEnv::nEnvs)
      .def_property_readonly("opponent", &PyBatchEnv::opponent)
      .def_property_readonly("level", &PyBatchEnv::level)
      .def_property_readonly("speed_scale", &PyBatchEnv::speedScale)
      .def_property_readonly("n_threads", &PyBatchEnv::threads)
      .def_property_readonly("tick_ms", &PyBatchEnv::tickMs)
      .def_property_readonly("capture_reward", &PyBatchEnv::captureReward);
}
