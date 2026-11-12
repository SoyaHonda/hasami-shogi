"""PPO で機械側を学習する（python/train/ppo.py）。

    python python/train/ppo.py --opponent b1 --speed-scale 1.0 --out runs/b1_s10

The settings default to those of plan 2026-11-06 3.9 (256 environments, 128 decision points per
rollout, 4 epochs, minibatch 4096, clip 0.2, learning rate 3e-4 decaying linearly, entropy 0.01,
value loss 0.5, gradient norm 0.5); the discount halves every 120 s of play (plan 2026-11-12 4).
"""
import argparse
import csv
import os
import time

import numpy as np
import torch
import torch.nn as nn
from torch.distributions import Categorical

import hasami_env
from model import Net


def gae(reward, value, last_value, dt_ms, done, lam=0.95, half_life_ms=120_000):
    """時間で割り引く GAE。reward, value, dt_ms, done は [T, N]、last_value は [N]。"""
    gamma = 0.5 ** (dt_ms / half_life_ms)  # 決定点ごとに違う割引（120 秒で半分）
    adv = torch.zeros_like(reward)
    last = torch.zeros_like(last_value)
    next_value = last_value
    for t in reversed(range(reward.shape[0])):
        nonterminal = 1.0 - done[t]  # 局が終わったら、次の局の価値は足さない
        delta = reward[t] + gamma[t] * next_value * nonterminal - value[t]
        last = delta + gamma[t] * lam * nonterminal * last
        adv[t] = last
        next_value = value[t]
    return adv, adv + value


def to_dev(dev, *arrays):
    return [torch.from_numpy(a).to(dev) for a in arrays]


def main(argv=None):
    p = argparse.ArgumentParser()
    p.add_argument("--opponent", default="b1")  # b1 | race | random
    p.add_argument("--speed-scale", type=float, default=1.0)
    p.add_argument("--capture-reward", type=float, default=0.2)
    p.add_argument("--steps", type=float, default=30e6)  # 学習に使う決定点の合計
    p.add_argument("--threads", type=int, default=3)
    p.add_argument("--out", default="runs/b1_s10")
    p.add_argument("--envs", type=int, default=256)  # 環境の数
    p.add_argument("--rollout", type=int, default=128)  # 1 回に集める長さ（決定点）
    p.add_argument("--epochs", type=int, default=4)
    p.add_argument("--minibatch", type=int, default=4096)
    p.add_argument("--seed", type=int, default=1)
    a = p.parse_args(argv)
    os.makedirs(a.out, exist_ok=True)
    dev = "cuda" if torch.cuda.is_available() else "cpu"
    torch.manual_seed(a.seed)
    N, T, EPOCHS = a.envs, a.rollout, a.epochs
    MB = min(a.minibatch, N * T)

    env = hasami_env.BatchEnv(n_envs=N, seed=a.seed, opponent=a.opponent, speed_scale=a.speed_scale,
                              capture_reward=a.capture_reward, n_threads=a.threads, track_activity=False)
    net = Net().to(dev)
    opt = torch.optim.Adam(net.parameters(), lr=3e-4)
    updates = max(1, int(a.steps // (N * T)))
    sched = torch.optim.lr_scheduler.LambdaLR(opt, lambda u: 1 - u / updates)  # 学習率を線形に下げる

    log_file = open(os.path.join(a.out, "log.csv"), "w", newline="")
    log = csv.writer(log_file)
    log.writerow(["update", "steps", "games", "win_rate", "taken", "lost", "game_min", "steps_per_s"])
    obs, scal, mask = env.reset()
    t0 = time.time()
    for u in range(updates):
        # 1) 集める: T 回 step して、学習に使う材料をためる
        B = {k: [] for k in ["obs", "scal", "mask", "act", "logp", "val", "rew", "dt", "done"]}
        games = wins = taken = lost = game_ms = 0
        for t in range(T):
            with torch.no_grad():
                logits, value = net(*to_dev(dev, obs, scal, mask))
                dist = Categorical(logits=logits)
                act = dist.sample()
            B["obs"].append(obs)
            B["scal"].append(scal)
            B["mask"].append(mask)
            B["act"].append(act.cpu())
            B["logp"].append(dist.log_prob(act).cpu())
            B["val"].append(value.cpu())
            obs, scal, mask, reward, dt_ms, done, info = env.step(act.cpu().numpy().astype(np.int32))
            B["rew"].append(torch.from_numpy(reward))
            B["dt"].append(torch.from_numpy(dt_ms).float())
            B["done"].append(torch.from_numpy(done).float())
            d = np.flatnonzero(done)  # この step で終わった局
            games += len(d)
            wins += int((info["winner"][d] == 2).sum())
            taken += int(info["captures"][d, 0].sum())
            lost += int(info["captures"][d, 1].sum())
            game_ms += int(info["game_ms"][d].sum())

        # 2) アドバンテージを計算する
        with torch.no_grad():
            _, last_value = net(*to_dev(dev, obs, scal, mask))
        rew, val, dt, dn = (torch.stack(B[k]) for k in ("rew", "val", "dt", "done"))
        adv, ret = gae(rew, val, last_value.cpu(), dt, dn)

        # 3) 更新する: [T, N] を [T*N] に並べ、ミニバッチに分けて EPOCHS 回
        O = torch.from_numpy(np.concatenate(B["obs"]))
        S = torch.from_numpy(np.concatenate(B["scal"]))
        M = torch.from_numpy(np.concatenate(B["mask"]))
        A, LP = torch.cat(B["act"]), torch.cat(B["logp"])
        ADV, RET = adv.reshape(-1), ret.reshape(-1)
        for _ in range(EPOCHS):
            for idx in torch.randperm(N * T).split(MB):
                logits, value = net(O[idx].to(dev), S[idx].to(dev), M[idx].to(dev))
                dist = Categorical(logits=logits)
                ratio = (dist.log_prob(A[idx].to(dev)) - LP[idx].to(dev)).exp()
                adv_b = ADV[idx].to(dev)
                adv_b = (adv_b - adv_b.mean()) / (adv_b.std() + 1e-8)
                pg = -torch.min(ratio * adv_b, ratio.clamp(0.8, 1.2) * adv_b).mean()  # クリップ 0.2
                vf = (value - RET[idx].to(dev)).pow(2).mean()
                loss = pg + 0.5 * vf - 0.01 * dist.entropy().mean()
                opt.zero_grad()
                loss.backward()
                nn.utils.clip_grad_norm_(net.parameters(), 0.5)
                opt.step()
        sched.step()

        # 4) 記録する
        steps = (u + 1) * N * T
        g = max(games, 1)
        row = [u + 1, steps, games, wins / g, taken / g, lost / g, game_ms / g / 60000, steps / (time.time() - t0)]
        log.writerow(row)
        log_file.flush()
        print(f"update {u + 1}/{updates}  steps {steps}  games {games}  win {wins / g:.3f}  "
              f"taken {taken / g:.2f}  lost {lost / g:.2f}  {row[-1]:.0f} steps/s", flush=True)
        if (u + 1) % 50 == 0 or u + 1 == updates:
            torch.save(net.state_dict(), os.path.join(a.out, "model.pt"))


if __name__ == "__main__":
    main()
