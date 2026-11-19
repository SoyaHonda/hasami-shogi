"""1 手を選ぶのにかかる時間を、学習した AI と今の手書きの読みで比べる（python/train/time_inference.py）。

    python python/train/time_inference.py runs/b1_s10/model.pt

B1 との実際の局の決定点を集め、同じ局面で、学習した AI（ネットの順伝播 1 回、1 局面ずつ）と今の読み
（read_actions、つよい）の 1 手あたりの時間を測る。どちらも CPU の 1 スレッド。
"""
import argparse
import time

import numpy as np
import torch

import hasami_env
from model import Net


def main(argv=None):
    p = argparse.ArgumentParser()
    p.add_argument("model")
    p.add_argument("--positions", type=int, default=2000)
    p.add_argument("--speed-scale", type=float, default=1.0)
    a = p.parse_args(argv)
    torch.set_num_threads(1)
    net = Net()
    net.load_state_dict(torch.load(a.model, map_location="cpu"))
    net.eval()
    params = sum(t.numel() for t in net.parameters())

    env = hasami_env.BatchEnv(n_envs=1, seed=1, opponent="b1", speed_scale=a.speed_scale, n_threads=1)
    obs, scal, mask = env.reset()
    net_ms, read_ms = [], []
    with torch.no_grad():
        for _ in range(20):  # 最初の数回は遅いので数えない
            net(*(torch.from_numpy(x) for x in (obs, scal, mask)))
        while len(net_ms) < a.positions:
            t0 = time.perf_counter()
            env.read_actions()
            t1 = time.perf_counter()
            logits, _ = net(*(torch.from_numpy(x) for x in (obs, scal, mask)))
            act = int(logits.argmax())
            t2 = time.perf_counter()
            read_ms.append((t1 - t0) * 1000)
            net_ms.append((t2 - t1) * 1000)
            obs, scal, mask, *_ = env.step(np.array([act], np.int32))

    print(f"model: {a.model}  parameters: {params:,}  positions: {len(net_ms)}")
    for name, ms in (("learned", net_ms), ("read", read_ms)):
        ms = np.array(ms)
        print(f"{name:8s} median {np.median(ms):.3f} ms  mean {ms.mean():.3f} ms  95% {np.percentile(ms, 95):.3f} ms  "
              f"max {ms.max():.3f} ms")


if __name__ == "__main__":
    main()
