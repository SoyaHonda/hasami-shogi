"""第2章: 機械にランダムな合法手を指させて、B1 相手の 500 局ほどを回す（docs/training-manual.md）。

リポジトリの根で python python/train/try_random.py として実行する。
"""
import time
import numpy as np
import hasami_env

env = hasami_env.BatchEnv(n_envs=256, seed=1, opponent="b1")
obs, scal, mask = env.reset()
print("obs", obs.shape, "scal", scal.shape, "mask", mask.shape)
print("最初に選べる手の数:", mask[0].sum(), " 動かない(1176)を選べるか:", mask[0, 1176])

rng = np.random.default_rng(0)
games = wins = taken = lost = steps = 0
t0 = time.time()
while games < 500:
    actions = np.where(mask, rng.gumbel(size=mask.shape), -np.inf).argmax(axis=1).astype(np.int32)
    obs, scal, mask, reward, dt_ms, done, info = env.step(actions)
    steps += 1
    for i in np.flatnonzero(done):
        games += 1
        wins += int(info["winner"][i] == 2)
        taken += int(info["captures"][i, 0])
        lost += int(info["captures"][i, 1])
print(f"{games} 局: 機械の勝ち {wins}、取った枚数/局 {taken / games:.2f}、失った枚数/局 {lost / games:.2f}")
print(f"決定点/秒 {steps * 256 / (time.time() - t0):.0f}")
