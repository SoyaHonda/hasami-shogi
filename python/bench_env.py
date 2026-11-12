"""Decision points per second of hasami_env.BatchEnv (plan 2026-11-12 section 6, 11/16).

    python python/bench_env.py [--n-envs 256] [--steps 300] [--threads 0,1] [--opponents b1,race,random]

The machine plays uniformly random legal actions (an untrained policy; numpy picks them, which is
counted in the time). One step = n_envs decision points. Prints one row per condition.
"""

import argparse
import os
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hasami_env import BatchEnv  # noqa: E402


def bench(opponent, n_envs, steps, threads, track_activity, seed=1):
    env = BatchEnv(n_envs=n_envs, seed=seed, opponent=opponent, n_threads=threads, track_activity=track_activity)
    rng = np.random.default_rng(seed)
    obs, scal, mask = env.reset()
    games = 0
    sim_ms = 0
    t0 = time.perf_counter()
    for _ in range(steps):
        a = np.where(mask, rng.gumbel(size=mask.shape), -np.inf).argmax(axis=1).astype(np.int32)
        obs, scal, mask, reward, dt_ms, done, info = env.step(a)
        games += int(done.sum())
        sim_ms += int(dt_ms.sum())
    elapsed = time.perf_counter() - t0
    return env.n_threads, n_envs * steps / elapsed, games, sim_ms / 1000 / elapsed


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--n-envs", type=int, default=256)
    p.add_argument("--steps", type=int, default=300)
    p.add_argument("--threads", default="0,1", help="0 = one per CPU")
    p.add_argument("--opponents", default="b1,race,random")
    p.add_argument("--no-activity", action="store_true", help="also measure with track_activity=False")
    args = p.parse_args()
    print(f"cpus={os.cpu_count()} n_envs={args.n_envs} steps={args.steps}")
    print("opponent  threads  activity  decisions/s  games  sim-s/s")
    for opp in args.opponents.split(","):
        for th in (int(x) for x in args.threads.split(",")):
            for act in (True, False) if args.no_activity else (True,):
                n, rate, games, sim = bench(opp, args.n_envs, args.steps, th, act)
                print(f"{opp:8s}  {n:7d}  {str(act):8s}  {rate:11.0f}  {games:5d}  {sim:7.0f}")


if __name__ == "__main__":
    main()
