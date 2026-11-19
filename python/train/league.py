"""小さなリーグ: 機械の主役と人間の主役を同時に PPO で学習する（python/train/league.py）。

    python python/train/league.py --init-machine runs/b1_s10/model.pt --out runs/league
    python python/train/league.py --out runs/league --resume                         # 続きから
    python python/train/league.py --exploit runs/league/machine.pt --out runs/exploit_league
    python python/train/league.py --exploit read --out runs/exploit_read           # 手書きの読みを狙う

Plan 2026-11-12 7.2-7.3. The machine plays two environments at once: a BatchEnv against the fixed
human B1 (the anchor to a realistic human) and a DuelEnv whose human side is played from here. In
each DuelEnv game the human is either the human main (learning, the current version, share
--main-share) or a past version of it picked by PFSP (prioritized fictitious self-play: a version
the machine beats x of the time is picked in proportion to (1 - x)^2, draws half a win). The
machine learns from all its games; the human main learns from its games against the current
machine. A snapshot of the human main joins the pool every --snapshot-every updates.

--fixed-human adds frozen humans (e.g. the human.pt of an exploiter) to the pool for good: PFSP
picks them like past versions, and they never leave the pool. With --pfsp-window N, PFSP weighs the
last N or so games against each version (older results fade), so a version the machine has learned
to beat stops being picked; without it, all games count alike.

--exploit trains only a human, from scratch, against a fixed machine: a model.pt / machine.pt
(acting as evaluate.py does, sampling its policy) or "read" (the hand-written read). It measures
how much a learner aimed at that machine alone can take from it.

Both learners use the PPO of ppo.py (clip 0.2, 4 epochs, minibatch 4096, learning rate 3e-4 decaying
linearly, entropy 0.01, value loss 0.5, gradient norm 0.5) and the advantage of ppo.py's gae
(lambda 0.95, the discount halving every 120 s of play), here over each side's own sequence of
decision points in each game slot.

Every 50 updates and at the end: machine.pt and human.pt (state_dicts; machine.pt is what
evaluate.py --model reads), checkpoint.pt for --resume, and log.csv one row per update.
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

MAIN = -1  # the opponent id of the human main in a DuelEnv slot
FIXED = 100_000  # versions above this are the --fixed-human ones (pool/human_100001.pt and so on)
HALF_LIFE_MS = 120_000
LAM = 0.95


def save_atomic(obj, path):
    torch.save(obj, path + ".tmp")
    os.replace(path + ".tmp", path)


class Buffer:
    """One learner's decision points, kept as a sequence per (env, slot, side) key.

    add() records a decision point (it stays pending); complete() gives the pending one of that key
    its reward, elapsed time and whether the game ended there. take() returns the completed ones
    with their advantages and returns, and keeps the pending ones (the bootstrap of their sequence)
    for the next rollout.
    """

    def __init__(self):
        self.rows = []  # [obs, scal, mask, act, logp, val, rew, dt, done]
        self.seqs = {}  # key -> row indices in order
        self.pending = {}  # key -> row index awaiting complete()

    def add(self, key, obs, scal, mask, act, logp, val):
        assert key not in self.pending, key
        self.rows.append([obs, scal, mask, int(act), float(logp), float(val), 0.0, 0.0, False])
        idx = len(self.rows) - 1
        self.seqs.setdefault(key, []).append(idx)
        self.pending[key] = idx

    def complete(self, key, rew, dt, done):
        idx = self.pending.pop(key, None)
        if idx is None:
            return
        row = self.rows[idx]
        row[6], row[7], row[8] = float(rew), float(dt), bool(done)

    def drop_pending(self, key):
        """Forget the pending decision point of key (a slot leaving this learner)."""
        idx = self.pending.pop(key, None)
        if idx is not None:
            self.seqs[key].remove(idx)

    def __len__(self):
        return len(self.rows) - len(self.pending)

    def take(self):
        """(obs, scal, mask, act, logp, adv, ret) of the completed decision points, and the rest."""
        seqs = []
        pend_set = set(self.pending.values())
        for key, idxs in self.seqs.items():
            done_idxs = [i for i in idxs if i not in pend_set]
            if not done_idxs:
                continue
            pend = self.pending.get(key)
            boot = self.rows[pend][5] if pend is not None else 0.0
            seqs.append((done_idxs, boot))
        if not seqs:
            return None, self._carry()
        L, S = max(len(s[0]) for s in seqs), len(seqs)
        R, V, DT, D = (np.zeros((L, S), np.float32) for _ in range(4))
        valid = np.zeros((L, S), bool)
        boot = np.zeros(S, np.float32)
        for j, (idxs, b) in enumerate(seqs):
            n = len(idxs)
            valid[:n, j] = True
            boot[j] = b
            for t, i in enumerate(idxs):
                row = self.rows[i]
                V[t, j], R[t, j], DT[t, j], D[t, j] = row[5], row[6], row[7], float(row[8])
        gamma = 0.5 ** (DT / HALF_LIFE_MS)
        adv = np.zeros((L, S), np.float32)
        nxt_adv = np.zeros(S, np.float32)
        valid_next = np.vstack([valid[1:], np.zeros((1, S), bool)])
        for t in reversed(range(L)):
            last = valid[t] & ~valid_next[t]  # the sequence's last completed decision point
            nxt_val = np.where(last, boot, V[t + 1] if t + 1 < L else 0.0)
            nxt_a = np.where(last, 0.0, nxt_adv)
            nonterm = 1.0 - D[t]
            delta = R[t] + gamma[t] * nxt_val * nonterm - V[t]
            a = delta + gamma[t] * LAM * nonterm * nxt_a
            adv[t] = np.where(valid[t], a, 0.0)
            nxt_adv = adv[t]
        order = [i for idxs, _ in seqs for i in idxs]
        flat_adv = np.concatenate([adv[: len(idxs), j] for j, (idxs, _) in enumerate(seqs)])
        rows = [self.rows[i] for i in order]
        batch = (
            torch.from_numpy(np.stack([r[0] for r in rows])),
            torch.from_numpy(np.stack([r[1] for r in rows])),
            torch.from_numpy(np.stack([r[2] for r in rows])),
            torch.tensor([r[3] for r in rows]),
            torch.tensor([r[4] for r in rows]),
            torch.from_numpy(flat_adv),
            torch.from_numpy(flat_adv + np.array([r[5] for r in rows], np.float32)),
        )
        return batch, self._carry()

    def _carry(self):
        nb = Buffer()
        for key, idx in self.pending.items():
            r = self.rows[idx]
            nb.add(key, r[0], r[1], r[2], r[3], r[4], r[5])
        return nb


def act(net, dev, obs, scal, mask):
    """Sampled actions, their log-probabilities and the values (numpy)."""
    with torch.no_grad():
        logits, value = net(*(torch.from_numpy(x).to(dev) for x in (obs, scal, mask)))
        dist = Categorical(logits=logits)
        a = dist.sample()
        return a.cpu().numpy().astype(np.int32), dist.log_prob(a).cpu().numpy(), value.cpu().numpy()


def ppo_update(net, opt, batch, dev, epochs, minibatch):
    O, S, M, A, LP, ADV, RET = batch
    n = len(A)
    for _ in range(epochs):
        for idx in torch.randperm(n).split(min(minibatch, n)):
            logits, value = net(O[idx].to(dev), S[idx].to(dev), M[idx].to(dev))
            dist = Categorical(logits=logits)
            ratio = (dist.log_prob(A[idx].to(dev)) - LP[idx].to(dev)).exp()
            adv = ADV[idx].to(dev)
            adv = (adv - adv.mean()) / (adv.std() + 1e-8) if len(idx) > 1 else adv
            pg = -torch.min(ratio * adv, ratio.clamp(0.8, 1.2) * adv).mean()
            vf = (value - RET[idx].to(dev)).pow(2).mean()
            loss = pg + 0.5 * vf - 0.01 * dist.entropy().mean()
            opt.zero_grad()
            loss.backward()
            nn.utils.clip_grad_norm_(net.parameters(), 0.5)
            opt.step()


class Pfsp:
    """The machine's results against each past human version, and the PFSP choice."""

    def __init__(self, rng, window=0):
        self.rng = rng
        self.fade = 1.0 - 1.0 / window if window else 1.0  # each new game shrinks the old ones by this
        self.score = {}  # version -> [machine points (win 1, draw 0.5), games]

    def add_version(self, k):
        self.score.setdefault(k, [0.0, 0])

    def drop_version(self, k):
        self.score.pop(k, None)

    def record(self, k, winner):
        if k in self.score:
            s = self.score[k]
            s[0] = s[0] * self.fade + (1.0 if winner == 2 else 0.5 if winner == 0 else 0.0)
            s[1] = s[1] * self.fade + 1

    def win_rate(self, k):
        pts, g = self.score[k]
        return (pts + 1.0) / (g + 2.0)  # a prior of one win and one loss

    def pick(self):
        ks = sorted(self.score)
        w = np.array([(1.0 - self.win_rate(k)) ** 2 for k in ks])
        return ks[self.rng.choice(len(ks), p=w / w.sum())]


def main(argv=None):
    p = argparse.ArgumentParser()
    p.add_argument("--out", default="runs/league")
    p.add_argument("--updates", type=int, default=None)  # default 2000
    p.add_argument("--speed-scale", type=float, default=1.0)
    p.add_argument("--capture-reward", type=float, default=0.2)
    p.add_argument("--b1-envs", type=int, default=128)  # BatchEnv against B1 (machine only)
    p.add_argument("--duel-envs", type=int, default=128)  # DuelEnv (machine and human)
    p.add_argument("--rollout", type=int, default=128)  # env steps of each per update
    p.add_argument("--epochs", type=int, default=4)
    p.add_argument("--minibatch", type=int, default=4096)
    p.add_argument("--lr", type=float, default=3e-4)
    p.add_argument("--main-share", type=float, default=0.5)  # DuelEnv games against the human main
    p.add_argument("--snapshot-every", type=int, default=50)
    p.add_argument("--pool-size", type=int, default=30)
    p.add_argument("--threads", type=int, default=3)
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--init-machine", help="model.pt to start the machine from (e.g. runs/b1_s10/model.pt)")
    p.add_argument("--init-human", help="human.pt to start the human from")
    p.add_argument("--fixed-human", action="append", default=[],
                   help="a human.pt (an exploiter's, say) to keep in the pool for good; repeat for more")
    p.add_argument("--pfsp-window", type=int, default=0, help="PFSP weighs about the last N games of each version")
    p.add_argument("--exploit", help='train only a human against this machine: a model.pt, or "read"')
    p.add_argument("--resume", action="store_true")
    a = p.parse_args(argv)
    dev = "cuda" if torch.cuda.is_available() else "cpu"
    ckpt_path = os.path.join(a.out, "checkpoint.pt")
    ckpt = None
    if a.resume:
        if not os.path.exists(ckpt_path):
            raise SystemExit(f"{ckpt_path} がありません（50 回目の更新より前に止まったときは最初から）")
        ckpt = torch.load(ckpt_path, map_location=dev, weights_only=False)
        # The run's own settings; only --threads, and --updates to lengthen it, are taken anew.
        updates = a.updates
        for k, v in ckpt["args"].items():
            if k not in ("out", "threads", "resume", "updates"):
                setattr(a, k, v)
        a.updates = updates if updates is not None else ckpt["args"]["updates"]
        print(f"resume: {ckpt_path} の {ckpt['update']} 回目の更新から", flush=True)
    if a.updates is None:
        a.updates = 2000
    os.makedirs(os.path.join(a.out, "pool"), exist_ok=True)
    start = ckpt["update"] if ckpt else 0
    if start >= a.updates:
        raise SystemExit(f"もう {start} 回の更新が済んでいます。長くするなら --updates を大きくしてください")
    rng = np.random.default_rng(a.seed + start)
    torch.manual_seed(a.seed + start)
    exploit = a.exploit is not None

    machine, human = Net().to(dev), Net(human=True).to(dev)
    fixed_read = exploit and a.exploit == "read"
    if exploit and not fixed_read:
        machine.load_state_dict(torch.load(a.exploit, map_location=dev))
    elif a.init_machine and not ckpt:
        machine.load_state_dict(torch.load(a.init_machine, map_location=dev))
    if a.init_human and not ckpt:
        human.load_state_dict(torch.load(a.init_human, map_location=dev))
    opt_m = torch.optim.Adam(machine.parameters(), lr=a.lr)
    opt_h = torch.optim.Adam(human.parameters(), lr=a.lr)
    decay = lambda u: max(0.0, 1 - u / a.updates)  # noqa: E731
    sched_m = torch.optim.lr_scheduler.LambdaLR(opt_m, decay)
    sched_h = torch.optim.lr_scheduler.LambdaLR(opt_h, decay)
    pfsp = Pfsp(rng, a.pfsp_window)
    pool = {}  # version (update number) -> frozen human Net
    if ckpt:
        machine.load_state_dict(ckpt["machine"])
        human.load_state_dict(ckpt["human"])
        opt_m.load_state_dict(ckpt["opt_m"])
        opt_h.load_state_dict(ckpt["opt_h"])
        sched_m.load_state_dict(ckpt["sched_m"])
        sched_h.load_state_dict(ckpt["sched_h"])
        pfsp.score = {int(k): list(v) for k, v in ckpt["pfsp"].items()}
    elif not exploit:
        for i, path in enumerate(a.fixed_human):
            k = FIXED + i + 1
            save_atomic(torch.load(path, map_location=dev), os.path.join(a.out, "pool", f"human_{k:05d}.pt"))
            pfsp.add_version(k)
    for k in sorted(pfsp.score):
        net = Net(human=True).to(dev)
        net.load_state_dict(torch.load(os.path.join(a.out, "pool", f"human_{k:05d}.pt"), map_location=dev))
        net.eval()
        pool[k] = net

    seed_off = a.seed + start * (a.b1_envs + a.duel_envs)
    env_b = None
    if not exploit and a.b1_envs > 0:
        env_b = hasami_env.BatchEnv(n_envs=a.b1_envs, seed=seed_off, opponent="b1", speed_scale=a.speed_scale,
                                    capture_reward=a.capture_reward, n_threads=a.threads, track_activity=False)
    env_d = hasami_env.DuelEnv(n_envs=a.duel_envs, seed=seed_off + 100_000, speed_scale=a.speed_scale,
                               capture_reward=a.capture_reward, n_threads=a.threads, track_activity=False)
    opp = np.full(a.duel_envs, MAIN)
    buf_m, buf_h = Buffer(), Buffer()

    log_path = os.path.join(a.out, "log.csv")
    header = ["update", "machine_steps", "human_steps", "b1_games", "b1_win", "b1_lost", "main_games", "main_win",
              "main_taken", "main_lost", "past_games", "past_win", "pool", "steps_per_s",
              "fixed_games", "fixed_win", "fixed_lost"]  # fixed_*: the --fixed-human games, also counted in past_*
    rows = []
    if ckpt and os.path.exists(log_path):
        with open(log_path, newline="") as f:
            rows = [r for r in list(csv.reader(f))[1:] if r and int(r[0]) <= start]
    log_file = open(log_path, "w", newline="")
    log = csv.writer(log_file)
    log.writerows([header] + rows)
    log_file.flush()

    if env_b is not None:
        ob, sb, mb = env_b.reset()
    od, sd, md, side = env_d.reset()
    if pool and not exploit:  # with fixed humans from the start, the first games are drawn as later ones
        opp[:] = [MAIN if rng.random() < a.main_share else pfsp.pick() for _ in range(a.duel_envs)]
    for u in range(start, a.updates):
        t0 = time.time()
        st = dict(b1_g=0, b1_w=0, b1_l=0, main_g=0, main_w=0, main_t=0, main_l=0, past_g=0, past_w=0,
                  fix_g=0, fix_w=0, fix_l=0, m=0, h=0)
        for _ in range(a.rollout):
            # 1) Against B1: every slot is at a machine decision point.
            if env_b is not None:
                act_b, lp_b, v_b = act(machine, dev, ob, sb, mb)
                for i in range(a.b1_envs):
                    buf_m.add(("b", i), ob[i], sb[i], mb[i], act_b[i], lp_b[i], v_b[i])
                ob, sb, mb, rew, dt, done, info = env_b.step(act_b)
                st["m"] += a.b1_envs
                for i in range(a.b1_envs):
                    buf_m.complete(("b", i), rew[i], dt[i], done[i])
                d = np.flatnonzero(done)
                st["b1_g"] += len(d)
                st["b1_w"] += int((info["winner"][d] == 2).sum())
                st["b1_l"] += int(info["captures"][d, 1].sum())

            # 2) The duel: each slot at one side's decision point.
            actions = np.zeros(a.duel_envs, np.int32)
            ms = np.flatnonzero(side == 0)
            if len(ms):
                if fixed_read:
                    actions[ms] = env_d.read_actions()[ms]
                else:
                    am, lpm, vm = act(machine, dev, od[ms], sd[ms], md[ms])
                    actions[ms] = am
                    if not exploit:
                        for j, i in enumerate(ms):
                            buf_m.add(("d", i), od[i], sd[i], md[i], am[j], lpm[j], vm[j])
                        st["m"] += len(ms)
            hs = np.flatnonzero(side == 1)
            for k in np.unique(opp[hs]):
                sel = hs[opp[hs] == k]
                net = human if k == MAIN else pool[int(k)]
                ah, lph, vh = act(net, dev, od[sel], sd[sel], md[sel])
                actions[sel] = ah
                if k == MAIN:
                    for j, i in enumerate(sel):
                        buf_h.add(("d", i), od[i], sd[i], md[i], ah[j], lph[j], vh[j])
                    st["h"] += len(sel)
            od, sd, md, new_side, rew, dt, done, info = env_d.step(actions)
            for i in range(a.duel_envs):
                if done[i]:
                    fr, fd = info["final_reward"][i], info["final_dt_ms"][i]
                    buf_m.complete(("d", i), fr[0], fd[0], True)
                    buf_h.complete(("d", i), fr[1], fd[1], True)
                    w = int(info["winner"][i])
                    if opp[i] == MAIN:
                        st["main_g"] += 1
                        st["main_w"] += int(w == 2)
                        st["main_t"] += int(info["captures"][i, 0])
                        st["main_l"] += int(info["captures"][i, 1])
                    else:
                        st["past_g"] += 1
                        st["past_w"] += int(w == 2)
                        if opp[i] > FIXED:
                            st["fix_g"] += 1
                            st["fix_w"] += int(w == 2)
                            st["fix_l"] += int(info["captures"][i, 1])
                        pfsp.record(int(opp[i]), w)
                    if not exploit:
                        opp[i] = MAIN if (not pool or rng.random() < a.main_share) else pfsp.pick()
                elif new_side[i] == 0:
                    buf_m.complete(("d", i), rew[i], dt[i], False)
                else:
                    buf_h.complete(("d", i), rew[i], dt[i], False)
            side = new_side

        # 3) Update both learners.
        if not exploit:
            batch_m, buf_m = buf_m.take()
            if batch_m is not None:
                ppo_update(machine, opt_m, batch_m, dev, a.epochs, a.minibatch)
            sched_m.step()
        batch_h, buf_h = buf_h.take()
        if batch_h is not None:
            ppo_update(human, opt_h, batch_h, dev, a.epochs, a.minibatch)
        sched_h.step()

        # 4) A snapshot of the human main for the pool.
        if not exploit and (u + 1) % a.snapshot_every == 0:
            k = u + 1
            save_atomic(human.state_dict(), os.path.join(a.out, "pool", f"human_{k:05d}.pt"))
            net = Net(human=True).to(dev)
            net.load_state_dict(human.state_dict())
            net.eval()
            pool[k] = net
            pfsp.add_version(k)
            while len([v for v in pool if v < FIXED]) > a.pool_size:
                old = min(pool)  # the oldest snapshot (the fixed humans sort after every snapshot)
                del pool[old]
                pfsp.drop_version(old)
                opp[opp == old] = MAIN  # its running games finish against the main

        # 5) Log and save.
        g = lambda x: max(x, 1)  # noqa: E731
        rate = 1.0 / (time.time() - t0)  # this update's decision points per second
        row = [u + 1, st["m"], st["h"], st["b1_g"], st["b1_w"] / g(st["b1_g"]), st["b1_l"] / g(st["b1_g"]),
               st["main_g"], st["main_w"] / g(st["main_g"]), st["main_t"] / g(st["main_g"]),
               st["main_l"] / g(st["main_g"]), st["past_g"], st["past_w"] / g(st["past_g"]), len(pool),
               (st["m"] + st["h"]) * rate, st["fix_g"], st["fix_w"] / g(st["fix_g"]), st["fix_l"] / g(st["fix_g"])]
        log.writerow(row)
        log_file.flush()
        print(f"update {u + 1}/{a.updates}  b1 win {row[4]:.3f} ({st['b1_g']})  main win {row[7]:.3f} "
              f"taken {row[8]:.2f} lost {row[9]:.2f} ({st['main_g']})  past win {row[11]:.3f} ({st['past_g']})  "
              f"pool {len(pool)}  {row[13]:.0f} steps/s", flush=True)
        if (u + 1) % 50 == 0 or u + 1 == a.updates:
            save_atomic(machine.state_dict(), os.path.join(a.out, "machine.pt"))
            save_atomic(human.state_dict(), os.path.join(a.out, "human.pt"))
            save_atomic({"update": u + 1, "machine": machine.state_dict(), "human": human.state_dict(),
                         "opt_m": opt_m.state_dict(), "opt_h": opt_h.state_dict(), "sched_m": sched_m.state_dict(),
                         "sched_h": sched_h.state_dict(), "pfsp": pfsp.score, "args": vars(a)}, ckpt_path)


if __name__ == "__main__":
    main()
