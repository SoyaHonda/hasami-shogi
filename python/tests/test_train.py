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
