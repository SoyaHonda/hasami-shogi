"""hasami_env.BatchEnv against the array contract of plan 2026-11-12 section 4."""

import os

import numpy as np
import pytest

import hasami_env
from hasami_env import BatchEnv
from hasami_env.srchash import source_hash

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def random_legal(mask, rng):
    """One legal action per row of mask, uniformly (Gumbel-max over the legal ones)."""
    g = rng.gumbel(size=mask.shape)
    return np.where(mask, g, -np.inf).argmax(axis=1).astype(np.int32)


def test_build_matches_sources():
    # A prebuilt wheel (python/wheels) must come from these very sources; rebuild it otherwise.
    assert hasami_env.SOURCE_HASH == source_hash(REPO), (
        f"hasami_env was built from other sources ({hasami_env.SOURCE_HASH}, checkout {source_hash(REPO)}): "
        "rebuild it (python/README.md)"
    )


def test_constants():
    assert hasami_env.N_ACTIONS == 1177
    assert hasami_env.STAY == 1176
    assert (hasami_env.N_CHANNELS, hasami_env.BOARD, hasami_env.N_SCALARS) == (12, 7, 6)


def test_shapes_and_types():
    env = BatchEnv(n_envs=5, seed=1, opponent="b1", speed_scale=1.0)
    obs, scal, mask = env.reset()
    assert obs.shape == (5, 12, 7, 7) and obs.dtype == np.int16
    assert scal.shape == (5, 6) and scal.dtype == np.int16
    assert mask.shape == (5, 1177) and mask.dtype == np.bool_
    out = env.step(random_legal(mask, np.random.default_rng(0)))
    obs, scal, mask, reward, dt_ms, done, info = out
    assert obs.shape == (5, 12, 7, 7) and obs.dtype == np.int16
    assert reward.shape == (5,) and reward.dtype == np.float32
    assert dt_ms.shape == (5,) and dt_ms.dtype == np.int32
    assert done.shape == (5,) and done.dtype == np.bool_
    assert info["winner"].shape == (5,) and info["winner"].dtype == np.int8
    assert info["captures"].shape == (5, 2) and info["captures"].dtype == np.int16
    assert info["truncated"].dtype == np.bool_
    assert info["game_ms"].dtype == np.int32
    assert set(info["activity"]) == {
        "outside", "mean_row", "play_ms", "threats", "moves", "back_forth", "first_capture_ms", "lead_changes",
    }
    assert all(v.shape == (5,) for v in info["activity"].values())
    assert (dt_ms > 0).all()
    # Decisions come at reads, max(150, reactMs) = 250 ms apart at the least.
    assert (dt_ms >= 250).all() and (dt_ms % 10 == 0).all()


def test_opening_then_stay_is_legal():
    env = BatchEnv(n_envs=8, seed=3, opponent="race")
    obs, scal, mask = env.reset()
    # Every game starts in the opening, where the machine must make its one move.
    assert (scal[:, 3] == 1).all()
    assert not mask[:, hasami_env.STAY].any()
    assert (mask.sum(axis=1) == 35).all()
    assert (obs[:, 0, 0, :] == 1).all() and obs[:, 0].sum() == 8 * 7
    assert (scal[:, 2] == 6000).all() and (scal[:, 5] == 100).all()
    rng = np.random.default_rng(1)
    seen_play = False
    for _ in range(30):
        obs, scal, mask, reward, dt_ms, done, info = env.step(random_legal(mask, rng))
        # "Stay" is legal at every decision point outside the opening, and two actions at least.
        assert (mask[:, hasami_env.STAY] == (scal[:, 3] == 0)).all()
        assert (mask.sum(axis=1) >= 2).all()
        seen_play |= bool((scal[:, 3] == 0).any())
    assert seen_play


def test_illegal_actions_raise():
    env = BatchEnv(n_envs=3, seed=1, opponent="random")
    obs, scal, mask = env.reset()
    good = random_legal(mask, np.random.default_rng(0))
    bad = good.copy()
    bad[1] = hasami_env.STAY  # not legal in the opening
    with pytest.raises(ValueError, match="env 1"):
        env.step(bad)
    bad[1] = 1177
    with pytest.raises(ValueError):
        env.step(bad)
    with pytest.raises(ValueError):
        env.step(good[:2])
    # Nothing moved: the legal actions still work.
    env.step(good)


def test_bad_arguments_raise():
    with pytest.raises(ValueError):
        BatchEnv(n_envs=2, opponent="b2")
    with pytest.raises(ValueError):
        BatchEnv(n_envs=2, speed_scale=0.0)
    with pytest.raises(ValueError):
        BatchEnv(n_envs=0)
    with pytest.raises(ValueError):
        BatchEnv(n_envs=2).step(np.zeros(2, np.int32))  # before reset()


def run(env, steps, seed):
    rng = np.random.default_rng(seed)
    obs, scal, mask = env.reset()
    record = [(obs, scal, mask)]
    for _ in range(steps):
        out = env.step(random_legal(mask, rng))
        obs, scal, mask = out[:3]
        record.append(out)
    return record


def assert_same(a, b):
    if isinstance(a, dict):
        assert a.keys() == b.keys()
        for k in a:
            assert_same(a[k], b[k])
    elif isinstance(a, (tuple, list)):
        assert len(a) == len(b)
        for x, y in zip(a, b):
            assert_same(x, y)
    else:
        np.testing.assert_array_equal(a, b)


def test_same_seed_same_games_and_threads_do_not_matter():
    one = run(BatchEnv(n_envs=12, seed=5, opponent="b1", n_threads=1), 60, seed=9)
    many = run(BatchEnv(n_envs=12, seed=5, opponent="b1", n_threads=4), 60, seed=9)
    assert_same(one, many)
    other = run(BatchEnv(n_envs=12, seed=6, opponent="b1", n_threads=4), 60, seed=9)
    assert not all(np.array_equal(x[0], y[0]) for x, y in zip(one, other))


def test_automatic_reset():
    env = BatchEnv(n_envs=16, seed=1, opponent="random")
    obs, scal, mask = env.reset()
    rng = np.random.default_rng(2)
    totals = np.zeros(16)
    finished = 0
    for _ in range(400):
        obs, scal, mask, reward, dt_ms, done, info = env.step(random_legal(mask, rng))
        totals += reward
        for i in np.flatnonzero(done):
            finished += 1
            w = info["winner"][i]
            cm, ch = info["captures"][i]
            assert w in (0, 1, 2)
            # Four captures win, or more captures than the other side at the time limit.
            if info["game_ms"][i] < 600000:
                assert (w == 2) == (cm >= 4) and (w == 1) == (ch >= 4)
            else:
                assert w == (2 if cm > ch else 1 if ch > cm else 0)
            # The whole game's reward: +-1, 0.2 per piece of difference, less the draw penalty.
            result = {0: 0.0, 1: -1.0, 2: 1.0}[int(w)]
            expected = result + 0.2 * (int(cm) - int(ch))
            penalty_max = 0.5 / 600 * info["game_ms"][i] / 1000
            assert expected - penalty_max - 1e-4 <= totals[i] <= expected + 1e-4
            totals[i] = 0.0
            # obs, scal and mask are the next game's first decision point.
            assert scal[i, 0] == 0 and scal[i, 1] == 0
            assert scal[i, 2] == 6000 and scal[i, 3] == 1
            assert not mask[i, hasami_env.STAY]
            assert obs[i, 0].sum() == 7 and (obs[i, 0, 0] == 1).all()
            assert 0.0 <= info["activity"]["outside"][i] <= 1.0
        if finished >= 10:
            break
    assert finished >= 10


def test_read_actions_are_legal_and_playable():
    env = BatchEnv(n_envs=6, seed=1, opponent="b1", level="hard")
    obs, scal, mask = env.reset()
    for _ in range(40):
        a = env.read_actions()
        assert a.dtype == np.int32 and a.shape == (6,)
        assert mask[np.arange(6), a].all()
        np.testing.assert_array_equal(a, env.read_actions("hard"))
        obs, scal, mask, *_ = env.step(a)
    env.read_actions("normal")
    with pytest.raises(ValueError):
        env.read_actions("strong")


def test_speed_scale_and_keywords():
    env = BatchEnv(n_envs=2, seed=1, opponent="race", speed_scale=0.7, capture_reward=0.5, tick_ms=10)
    obs, scal, mask = env.reset()
    assert (scal[:, 5] == 70).all()
    assert env.speed_scale == pytest.approx(0.7)
    assert env.capture_reward == pytest.approx(0.5)
    assert (env.n_envs, env.opponent, env.level, env.tick_ms) == (2, "race", "hard", 10)


def test_capture_reward_scales_rewards():
    a = run(BatchEnv(n_envs=8, seed=2, opponent="random", capture_reward=0.2), 150, seed=4)
    b = run(BatchEnv(n_envs=8, seed=2, opponent="random", capture_reward=0.5), 150, seed=4)
    ra = np.array([x[3] for x in a[1:]])
    rb = np.array([x[3] for x in b[1:]])
    assert (ra != rb).any()  # some capture happened
    # The same games: the reward does not change the play.
    for x, y in zip(a[1:], b[1:]):
        np.testing.assert_array_equal(x[0], y[0])


# ---- DuelEnv: both sides from Python (plan 2026-11-12 7.3).


def random_legal(mask, rng):
    """One legal action per row of mask, uniformly."""
    return np.where(mask, rng.gumbel(size=mask.shape), -np.inf).argmax(axis=1).astype(np.int32)


def test_race3_opponent():
    env = BatchEnv(n_envs=4, seed=1, opponent="race3", n_threads=2)
    obs, scal, mask = env.reset()
    for _ in range(30):
        obs, scal, mask, *_ = env.step(env.read_actions())
    assert env.opponent == "race3"


def test_duel_shapes_sides_and_finished_games():
    duel = hasami_env.DuelEnv(n_envs=8, seed=3, n_threads=2)
    obs, scal, mask, side = duel.reset()
    assert obs.shape == (8, 12, 7, 7) and scal.shape == (8, 6) and mask.shape == (8, 1177)
    assert side.dtype == np.int8 and set(np.unique(side)) <= {0, 1}
    assert (duel.human_react_ms, duel.human_move_ms, duel.human_wait_ms) == (400, 500, 250)
    rng = np.random.default_rng(0)
    seen = set()
    done_games = 0
    for _ in range(4000):
        assert (mask.sum(axis=1) >= 2).all()
        seen.update(np.unique(side).tolist())
        obs, scal, mask, side, reward, dt, done, info = duel.step(random_legal(mask, rng))
        assert info["final_reward"].shape == (8, 2) and info["final_dt_ms"].shape == (8, 2)
        for i in np.flatnonzero(done):
            done_games += 1
            fr = info["final_reward"][i]
            if info["winner"][i] == 2:
                assert fr[0] > 0.9 and fr[1] < -0.9
            elif info["winner"][i] == 1:
                assert fr[1] > 0.9 and fr[0] < -0.9
        assert (info["final_reward"][~done] == 0).all()
        if done_games >= 8:
            break
    assert seen == {0, 1} and done_games >= 8


def test_duel_human_view_and_read_actions():
    duel = hasami_env.DuelEnv(n_envs=6, seed=5)
    obs, scal, mask, side = duel.reset()
    rng = np.random.default_rng(1)
    for _ in range(200):
        read = duel.read_actions()
        # The read's moves at the machine's decision points, STAY at the human's.
        assert mask[side == 0, read[side == 0]].all()
        assert (read[side == 1] == hasami_env.STAY).all()
        h = side == 1
        if h.any():
            # The human's view: its own pieces first, its captures first.
            assert (obs[h, 0].sum(axis=(1, 2)) == 7 - scal[h, 1]).all()
            assert (obs[h, 1].sum(axis=(1, 2)) <= 7 - scal[h, 0]).all()
        act = np.where(side == 0, read, random_legal(mask, rng))
        obs, scal, mask, side, *_ = duel.step(act)


def test_duel_same_seed_same_games_and_illegal_actions_raise():
    def play(threads):
        duel = hasami_env.DuelEnv(n_envs=5, seed=9, n_threads=threads)
        out = [duel.reset()]
        rng = np.random.default_rng(2)
        for _ in range(300):
            out.append(duel.step(random_legal(out[-1][2], rng)))
        return out

    a, b = play(1), play(3)
    for x, y in zip(a, b):
        for u, v in zip(x[:4], y[:4]):
            np.testing.assert_array_equal(u, v)
    duel = hasami_env.DuelEnv(n_envs=2, seed=1)
    obs, scal, mask, side = duel.reset()
    bad = np.array([np.flatnonzero(~mask[0])[0], np.flatnonzero(mask[1])[0]], dtype=np.int32)
    with pytest.raises(ValueError):
        duel.step(bad)
    with pytest.raises(ValueError):
        hasami_env.DuelEnv(n_envs=2, human_wait_ms=0)
