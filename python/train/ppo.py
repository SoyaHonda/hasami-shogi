"""PPO で機械側を学習する（python/train/ppo.py）。

    python python/train/ppo.py --opponent b1 --speed-scale 1.0 --out runs/b1_s10
    python python/train/ppo.py --out runs/b1_s10 --resume     # 止まった学習を続きから

Every 50 updates and at the end it saves model.pt (the network alone, what evaluate.py and
record_game.py read) and checkpoint.pt (network, optimizer, learning-rate schedule, update count and
settings). --resume picks up from checkpoint.pt with the run's own settings; only --threads, and
--steps to lengthen the run, may be given anew. The games restart from fresh seeds, and the rows of
log.csv after the checkpoint are dropped, since those updates are done again.

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


# --resume で引き継ぐ設定（--threads と --out は引き継がず、--steps は指定すれば変えられる）
RUN_SETTINGS = ["opponent", "speed_scale", "capture_reward", "steps", "envs", "rollout", "epochs", "minibatch", "seed"]


def save_atomic(obj, path):
    """書いている途中で VM が止まっても、前のファイルが壊れないように一度別名で書いてから置き換える。"""
    torch.save(obj, path + ".tmp")
    os.replace(path + ".tmp", path)


def main(argv=None):
    p = argparse.ArgumentParser()
    p.add_argument("--opponent", default="b1")  # b1 | race | random
    p.add_argument("--speed-scale", type=float, default=1.0)
    p.add_argument("--capture-reward", type=float, default=0.2)
    p.add_argument("--steps", type=float, default=None)  # 学習に使う決定点の合計（既定 3,000 万）
    p.add_argument("--threads", type=int, default=3)
    p.add_argument("--out", default="runs/b1_s10")
    p.add_argument("--envs", type=int, default=256)  # 環境の数
    p.add_argument("--rollout", type=int, default=128)  # 1 回に集める長さ（決定点）
    p.add_argument("--epochs", type=int, default=4)
    p.add_argument("--minibatch", type=int, default=4096)
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--resume", action="store_true", help="--out の checkpoint.pt から続ける")
    a = p.parse_args(argv)
    dev = "cuda" if torch.cuda.is_available() else "cpu"
    ckpt_path = os.path.join(a.out, "checkpoint.pt")
    ckpt = None
    if a.resume:
        if not os.path.exists(ckpt_path):
            raise SystemExit(f"{ckpt_path} がありません（50 回目の更新より前に止まった学習は、最初からやり直してください）")
        ckpt = torch.load(ckpt_path, map_location=dev, weights_only=False)
        steps = a.steps
        for k in RUN_SETTINGS:
            setattr(a, k, ckpt["args"][k])
        if steps is not None:
            a.steps = steps
        print(f"resume: {ckpt_path} の {ckpt['update']} 回目の更新から（" +
              ", ".join(f"{k}={getattr(a, k)}" for k in RUN_SETTINGS) + "）", flush=True)
    if a.steps is None:
        a.steps = 30e6
    os.makedirs(a.out, exist_ok=True)
    start = ckpt["update"] if ckpt else 0
    torch.manual_seed(a.seed + start)
    N, T, EPOCHS = a.envs, a.rollout, a.epochs
    MB = min(a.minibatch, N * T)

    # 続きからのときは、前と同じ局をくり返さないように種をずらす
    env = hasami_env.BatchEnv(n_envs=N, seed=a.seed + start * N, opponent=a.opponent, speed_scale=a.speed_scale,
                              capture_reward=a.capture_reward, n_threads=a.threads, track_activity=False)
    net = Net().to(dev)
    opt = torch.optim.Adam(net.parameters(), lr=3e-4)
    updates = max(1, int(a.steps // (N * T)))
    sched = torch.optim.lr_scheduler.LambdaLR(opt, lambda u: max(0.0, 1 - u / updates))  # 学習率を線形に下げる
    if ckpt:
        net.load_state_dict(ckpt["net"])
        opt.load_state_dict(ckpt["opt"])
        sched.load_state_dict(ckpt["sched"])
        if start >= updates:
            raise SystemExit(f"もう {start} 回の更新が済んでいます（--steps {a.steps:.0f} は {updates} 回）。長くするなら --steps を大きくしてください")

    log_path = os.path.join(a.out, "log.csv")
    header = ["update", "steps", "games", "win_rate", "taken", "lost", "game_min", "steps_per_s"]
    if ckpt:
        # checkpoint より後の行は、これからやり直す更新なので捨てる
        with open(log_path, newline="") as f:
            rows = [r for r in csv.reader(f)][1:]
        rows = [r for r in rows if r and int(r[0]) <= start]
        log_file = open(log_path, "w", newline="")
        log = csv.writer(log_file)
        log.writerows([header] + rows)
    else:
        log_file = open(log_path, "w", newline="")
        log = csv.writer(log_file)
        log.writerow(header)
    obs, scal, mask = env.reset()
    t0 = time.time()
    for u in range(start, updates):
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
        rate = (u + 1 - start) * N * T / (time.time() - t0)  # この実行で進んだ分の速さ
        row = [u + 1, steps, games, wins / g, taken / g, lost / g, game_ms / g / 60000, rate]
        log.writerow(row)
        log_file.flush()
        print(f"update {u + 1}/{updates}  steps {steps}  games {games}  win {wins / g:.3f}  "
              f"taken {taken / g:.2f}  lost {lost / g:.2f}  {row[-1]:.0f} steps/s", flush=True)
        if (u + 1) % 50 == 0 or u + 1 == updates:
            save_atomic(net.state_dict(), os.path.join(a.out, "model.pt"))
            save_atomic({"update": u + 1, "net": net.state_dict(), "opt": opt.state_dict(),
                         "sched": sched.state_dict(), "args": {k: getattr(a, k) for k in RUN_SETTINGS}}, ckpt_path)


if __name__ == "__main__":
    main()
