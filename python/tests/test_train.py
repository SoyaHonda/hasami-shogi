"""The starter training scripts of python/train (model, PPO, evaluation, curve, recording).

They need PyTorch, which CI and the cloud dev environment do not have: there these tests are
skipped, and on the course VM (PyTorch 2.9.1) `python -m pytest python` runs them.
"""

import os
import sys

import numpy as np
import pytest

torch = pytest.importorskip("torch")

TRAIN = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "train")
if TRAIN not in sys.path:
    sys.path.insert(0, TRAIN)

import hasami_env  # noqa: E402
import evaluate  # noqa: E402
import ppo  # noqa: E402
from model import Net  # noqa: E402


def test_model_puts_each_move_at_its_action_number():
    net = Net()
    env = hasami_env.BatchEnv(n_envs=3, seed=1, opponent="random")
    obs, scal, mask = env.reset()
    o, s = torch.from_numpy(obs), torch.from_numpy(scal)
    with torch.no_grad():
        # Only channel k = d*6 + (dist-1) of the move head scores, so its 49 squares must come out at
        # the action numbers ((r*7 + c)*4 + d)*6 + (dist-1).
        net.move_head.weight.zero_()
        net.move_head.bias.zero_()
        d, dist = 2, 3
        net.move_head.bias[d * 6 + dist - 1] = 5.0
        logits, value = net(o, s, torch.ones(3, 1177, dtype=torch.bool))
    expected = sorted(((r * 7 + c) * 4 + d) * 6 + dist - 1 for r in range(7) for c in range(7))
    assert sorted(torch.nonzero(logits[0, :1176] == 5.0).flatten().tolist()) == expected
    assert logits.shape == (3, 1177) and value.shape == (3,)


def test_model_never_picks_an_illegal_action():
    net = Net()
    env = hasami_env.BatchEnv(n_envs=16, seed=2, opponent="b1")
    obs, scal, mask = env.reset()
    for _ in range(20):
        with torch.no_grad():
            logits, _ = net(*(torch.from_numpy(x) for x in (obs, scal, mask)))
        act = torch.distributions.Categorical(logits=logits).sample().numpy().astype(np.int32)
        assert mask[np.arange(16), act].all()
        obs, scal, mask, *_ = env.step(act)


def test_gae_discounts_by_time_and_stops_at_the_end_of_a_game():
    adv, ret = ppo.gae(torch.tensor([[0.0], [1.0]]), torch.tensor([[0.5], [0.5]]), torch.tensor([9.0]),
                       torch.tensor([[0.0], [0.0]]), torch.tensor([[0.0], [1.0]]))
    assert torch.allclose(adv.flatten(), torch.tensor([0.475, 0.5]))
    assert torch.allclose(ret.flatten(), torch.tensor([0.975, 1.0]))
    # 120 s later a value counts half.
    _, ret = ppo.gae(torch.zeros(1, 1), torch.zeros(1, 1), torch.tensor([1.0]), torch.tensor([[120000.0]]),
                     torch.zeros(1, 1), lam=1.0)
    assert ret.item() == pytest.approx(0.5)


def test_a_short_training_run_then_evaluation_curve_and_recording(tmp_path, capsys):
    out = str(tmp_path / "run")
    ppo.main(["--opponent", "random", "--envs", "8", "--rollout", "16", "--steps", str(8 * 16 * 2),
              "--threads", "2", "--out", out])
    with open(os.path.join(out, "log.csv")) as f:
        lines = f.read().splitlines()
    assert lines[0].startswith("update,steps,games,win_rate") and len(lines) == 3
    model = os.path.join(out, "model.pt")
    assert os.path.exists(model)

    evaluate.main(["--model", model, "--opponents", "random", "--games", "4"])
    printed = capsys.readouterr().out
    assert "read     random" in printed and "learned  random" in printed

    pytest.importorskip("matplotlib")
    import plot_curve
    import record_game

    assert os.path.exists(plot_curve.main([out]))
    gif = str(tmp_path / "game.gif")
    record_game.main([model, "--opponent", "random", "--seed", "3", "--out", gif])
    assert os.path.getsize(gif) > 0


def test_a_stopped_run_resumes_from_its_checkpoint(tmp_path, capsys):
    out = str(tmp_path / "run")
    small = ["--envs", "8", "--rollout", "16", "--threads", "2", "--out", out]
    ppo.main(["--opponent", "random", "--capture-reward", "0.5", "--steps", str(8 * 16 * 2)] + small)
    ckpt = torch.load(os.path.join(out, "checkpoint.pt"), weights_only=False)
    assert ckpt["update"] == 2 and ckpt["args"]["opponent"] == "random" and ckpt["args"]["capture_reward"] == 0.5
    # A row written after the checkpoint (the run stopped before the next save) is dropped and redone.
    with open(os.path.join(out, "log.csv"), "a") as f:
        f.write("3,384,0,0,0,0,0,0\n")

    capsys.readouterr()
    ppo.main(["--resume", "--steps", str(8 * 16 * 4), "--out", out, "--threads", "2"])
    printed = capsys.readouterr().out
    assert "resume:" in printed and "opponent=random" in printed and "update 3/4" in printed
    with open(os.path.join(out, "log.csv")) as f:
        rows = f.read().splitlines()[1:]
    assert [r.split(",")[0] for r in rows] == ["1", "2", "3", "4"]
    assert torch.load(os.path.join(out, "checkpoint.pt"), weights_only=False)["update"] == 4
    # The resumed network is the saved one, not a fresh one.
    from model import Net as _Net
    net = _Net()
    net.load_state_dict(torch.load(os.path.join(out, "model.pt")))

    # Nothing left to do -> a clear message instead of a silent run.
    with pytest.raises(SystemExit, match="--steps"):
        ppo.main(["--resume", "--out", out, "--threads", "2"])


def test_league_buffer_advantage_matches_ppo_gae():
    import league

    obs, scal, mask = np.zeros((12, 7, 7), np.int16), np.zeros(6, np.int16), np.ones(1177, bool)
    buf = league.Buffer()
    # Two interleaved sequences: "a" with a game end in the middle and a pending last point, "b" shorter.
    seq = {"a": dict(v=[0.3, -0.2, 0.5, 0.1, 0.4], r=[0.0, 1.0, 0.2, -0.2], dt=[250, 4000, 300, 900], d=[0, 1, 0, 0]),
           "b": dict(v=[0.7, 0.6, 0.2], r=[0.2, -0.5], dt=[1000, 60000], d=[0, 0])}
    for t in range(5):
        for key, s in seq.items():
            if t < len(s["v"]):
                buf.add(key, obs, scal, mask, 0, 0.0, s["v"][t])
                if t < len(s["r"]):
                    buf.complete(key, s["r"][t], s["dt"][t], s["d"][t])
    batch, rest = buf.take()
    adv = batch[5].numpy()
    got = {"a": adv[:4], "b": adv[4:]}  # take() orders the sequences by first appearance
    for key, s in seq.items():
        n = len(s["r"])
        ref, _ = ppo.gae(torch.tensor(s["r"]).view(n, 1), torch.tensor(s["v"][:n]).view(n, 1),
                         torch.tensor([s["v"][n]]), torch.tensor(s["dt"], dtype=torch.float32).view(n, 1),
                         torch.tensor(s["d"], dtype=torch.float32).view(n, 1))
        np.testing.assert_allclose(got[key], ref.flatten().numpy(), rtol=1e-5, atol=1e-6)
    np.testing.assert_allclose(batch[6].numpy(), adv + np.array(seq["a"]["v"][:4] + seq["b"]["v"][:2]), rtol=1e-5)
    # The pending last points stay for the next rollout.
    assert len(rest) == 0 and sorted(rest.pending) == ["a", "b"]


def test_a_short_league_resumes_and_an_exploiter_runs(tmp_path, capsys):
    import league

    out = str(tmp_path / "league")
    small = ["--b1-envs", "8", "--duel-envs", "8", "--rollout", "24", "--snapshot-every", "1", "--threads", "2",
             "--out", out]
    league.main(["--updates", "2"] + small)
    assert sorted(os.listdir(os.path.join(out, "pool"))) == ["human_00001.pt", "human_00002.pt"]
    capsys.readouterr()
    league.main(["--resume", "--updates", "3", "--out", out, "--threads", "2"])
    assert "resume:" in capsys.readouterr().out
    with open(os.path.join(out, "log.csv")) as f:
        rows = f.read().splitlines()
    assert rows[0].startswith("update,machine_steps,human_steps,b1_games") and [r.split(",")[0] for r in rows[1:]] == [
        "1", "2", "3"]
    # The league's machine is a model.pt for evaluate.py.
    evaluate.main(["--model", os.path.join(out, "machine.pt"), "--opponents", "race3", "--games", "2"])
    assert "learned  race3" in capsys.readouterr().out
    # A game against a learned human can be recorded, and the time per move measured.
    pytest.importorskip("matplotlib")
    import record_game
    import time_inference

    gif = str(tmp_path / "duel.gif")
    record_game.main([os.path.join(out, "machine.pt"), "--human", os.path.join(out, "human.pt"), "--speedup", "60",
                      "--out", gif])
    assert os.path.getsize(gif) > 0
    time_inference.main([os.path.join(out, "machine.pt"), "--positions", "20"])
    printed = capsys.readouterr().out
    assert "learned  median" in printed and "read     median" in printed

    for target in ("read", os.path.join(out, "machine.pt")):
        ex = str(tmp_path / ("ex_" + os.path.basename(target)))
        league.main(["--exploit", target, "--updates", "1", "--duel-envs", "8", "--rollout", "16", "--threads", "2",
                     "--out", ex])
        assert os.path.exists(os.path.join(ex, "human.pt"))


def test_fixed_humans_stay_in_the_pool_and_resume(tmp_path):
    import csv

    import league

    base = str(tmp_path / "base")
    small = ["--b1-envs", "8", "--duel-envs", "8", "--rollout", "24", "--threads", "2"]
    league.main(["--updates", "1", "--out", base] + small)
    out = str(tmp_path / "hard")
    fixed = os.path.join(base, "human.pt")
    league.main(["--updates", "3", "--snapshot-every", "1", "--pool-size", "1", "--main-share", "0",
                 "--fixed-human", fixed, "--fixed-human", fixed, "--pfsp-window", "50", "--out", out] + small)
    # The two fixed humans stay; of the snapshots only the newest (pool size 1) is left.
    ckpt = torch.load(os.path.join(out, "checkpoint.pt"), weights_only=False)
    assert sorted(ckpt["pfsp"]) == [3, 100001, 100002]
    with open(os.path.join(out, "log.csv")) as f:
        assert next(csv.reader(f))[-3:] == ["fixed_games", "fixed_win", "fixed_lost"]
    league.main(["--resume", "--updates", "4", "--out", out, "--threads", "2"])
    ckpt = torch.load(os.path.join(out, "checkpoint.pt"), weights_only=False)
    assert sorted(ckpt["pfsp"]) == [4, 100001, 100002] and ckpt["args"]["pfsp_window"] == 50


def test_pfsp_window_lets_old_results_fade():
    import league

    p = league.Pfsp(np.random.default_rng(0), window=10)
    p.add_version(1)
    for _ in range(100):
        p.record(1, 1)  # the machine loses
    for _ in range(30):
        p.record(1, 2)  # then wins
    assert p.win_rate(1) > 0.8  # about the last 10 games, all won
    q = league.Pfsp(np.random.default_rng(0))
    q.add_version(1)
    for w in [1] * 100 + [2] * 30:
        q.record(1, w)
    assert q.win_rate(1) < 0.3
