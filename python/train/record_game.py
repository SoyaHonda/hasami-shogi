"""学習した AI の 1 局を GIF にする（python/train/record_game.py）。

    python python/train/record_game.py runs/b1_s10/model.pt --opponent b1 --seed 7 --out runs/game.gif

赤が機械の駒、青が人間の駒、点線が持ち上げ中の駒、x が機械のヘッド。決定点ごとの盤なので動きは
飛び飛びで、最後に駒を取った瞬間の盤は入らない。
"""
import argparse

import matplotlib
import numpy as np
import torch
from torch.distributions import Categorical

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.animation import FuncAnimation, PillowWriter  # noqa: E402

import hasami_env  # noqa: E402
from model import Net  # noqa: E402


def main(argv=None):
    p = argparse.ArgumentParser()
    p.add_argument("model")
    p.add_argument("--opponent", default="b1")
    p.add_argument("--speed-scale", type=float, default=1.0)
    p.add_argument("--seed", type=int, default=7)
    p.add_argument("--out", default="runs/game.gif")
    a = p.parse_args(argv)

    net = Net()
    net.load_state_dict(torch.load(a.model, map_location="cpu"))
    net.eval()
    env = hasami_env.BatchEnv(n_envs=1, seed=a.seed, opponent=a.opponent, speed_scale=a.speed_scale)
    obs, scal, mask = env.reset()
    frames = [obs[0].copy()]
    torch.manual_seed(0)
    while True:
        with torch.no_grad():
            logits, _ = net(*(torch.from_numpy(x) for x in (obs, scal, mask)))
        act = Categorical(logits=logits).sample().numpy().astype(np.int32)
        obs, scal, mask, reward, dt_ms, done, info = env.step(act)
        if done[0]:
            break
        frames.append(obs[0].copy())
    print("winner", int(info["winner"][0]), "captures", info["captures"][0].tolist(), "frames", len(frames))

    fig, ax = plt.subplots(figsize=(4, 4))

    def draw(k):
        o = frames[k]
        ax.clear()
        ax.set_xlim(-0.5, 6.5)
        ax.set_ylim(6.5, -0.5)
        ax.set_xticks(range(7))
        ax.set_yticks(range(7))
        ax.grid(True)
        for r in range(7):
            for c in range(7):
                if o[0, r, c]:
                    ax.add_patch(plt.Circle((c, r), 0.4, color="tab:red"))  # 機械の駒
                if o[1, r, c]:
                    ax.add_patch(plt.Circle((c, r), 0.4, color="tab:blue"))  # 人間の駒
                if o[2, r, c]:
                    ax.add_patch(plt.Circle((c, r), 0.4, fill=False, color="tab:blue", ls="--"))  # 持ち上げ中
                if o[9, r, c]:
                    ax.plot(c, r, "kx", ms=12)  # 機械のヘッド
        ax.set_title(f"decision {k}")

    FuncAnimation(fig, draw, frames=len(frames)).save(a.out, writer=PillowWriter(fps=4))
    print("wrote", a.out)
    return a.out


if __name__ == "__main__":
    main()
