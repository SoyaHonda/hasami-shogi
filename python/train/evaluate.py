"""学習した方策と今の読みを、同じ 200 局で比べる（python/train/evaluate.py）。

    python python/train/evaluate.py --model runs/b1_s10/model.pt --speed-scale 1.0
"""
import argparse

import numpy as np
import torch
from torch.distributions import Categorical

import hasami_env
from model import Net


def evaluate(policy, opponent, speed_scale=1.0, games=200, seed=1):
    env = hasami_env.BatchEnv(n_envs=games, seed=seed, opponent=opponent, speed_scale=speed_scale,
                              track_activity=True)
    obs, scal, mask = env.reset()
    result = [None] * games
    while any(r is None for r in result):
        obs, scal, mask, reward, dt_ms, done, info = env.step(policy(env, obs, scal, mask))
        for i in np.flatnonzero(done):
            if result[i] is None:  # 環境 i の最初の局だけを使う（種は seed + i）
                result[i] = (int(info["winner"][i]), int(info["captures"][i, 0]), int(info["captures"][i, 1]),
                             float(info["activity"]["outside"][i]))
    return np.array(result, dtype=float)


def main(argv=None):
    p = argparse.ArgumentParser()
    p.add_argument("--model", help="学習した model.pt（省略すると今の読みだけ）")
    p.add_argument("--speed-scale", type=float, default=1.0)
    p.add_argument("--opponents", default="b1,race,random")
    p.add_argument("--games", type=int, default=200)
    a = p.parse_args(argv)
    dev = "cuda" if torch.cuda.is_available() else "cpu"

    players = {"read": lambda env, obs, scal, mask: env.read_actions()}  # 今の手書きの読み（つよい）
    if a.model:
        net = Net().to(dev)
        net.load_state_dict(torch.load(a.model, map_location=dev))
        net.eval()

        def learned(env, obs, scal, mask):
            with torch.no_grad():
                logits, _ = net(*(torch.from_numpy(x).to(dev) for x in (obs, scal, mask)))
            return Categorical(logits=logits).sample().cpu().numpy().astype(np.int32)

        players["learned"] = learned

    print("player   opponent  win     draw    taken  lost   outside")
    for opp in a.opponents.split(","):
        for name, policy in players.items():
            torch.manual_seed(0)
            r = evaluate(policy, opp, a.speed_scale, a.games)
            print(f"{name:8s} {opp:8s}  {(r[:, 0] == 2).mean():6.1%}  {(r[:, 0] == 0).mean():6.1%}  "
                  f"{r[:, 1].mean():5.2f}  {r[:, 2].mean():5.2f}  {r[:, 3].mean():6.1%}", flush=True)


if __name__ == "__main__":
    main()
