"""学習した AI の 1 局を GIF にする（python/train/record_game.py）。

    python python/train/record_game.py runs/b1_s10/model.pt --opponent b1 --seed 7 --out runs/game.gif
    python python/train/record_game.py runs/hard_s10/machine.pt --human runs/exploit_hard_s10/human.pt --out runs/turtle.gif

黒が機械の駒、白が人間の駒、点線の丸が人間が持ち上げている駒の元のマス、x が機械のヘッド（駒を動かす磁石）。
黄色の輪は 40 秒動いていない駒で、相手の駒が上下左右の隣に来るだけで取られる（正式ルールの RU40）。
決定点の盤を並べるので、決定点のあいだに起きたこと（人間が置いてすぐ取られた駒など）は映らない。
文字は日本語なので、日本語のフォント（IPAGothic など）がないと四角に化ける（クラウドの開発環境にはある）。
--human を付けると、人間側をボットではなく学習した人間（リーグの human.pt や攻略役）にする。
盤は対局の時計で --speedup 倍の速さに並べ直す（既定は 4 倍。1 コマが 1 秒）。最後のコマは終わった局の結果。
"""
import argparse

import matplotlib
import numpy as np
import torch
from torch.distributions import Categorical

matplotlib.use("Agg")
import matplotlib.font_manager  # noqa: E402
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.animation import FuncAnimation, PillowWriter  # noqa: E402

import hasami_env  # noqa: E402
from model import Net  # noqa: E402

FPS = 4
LIMIT_MS = 600_000  # 10 分の時間切れ。scal[2] は残り時間（100 ミリ秒単位）
WINNER = {2: "機械の勝ち", 1: "人間の勝ち", 0: "引き分け"}
_HAVE = {f.name for f in matplotlib.font_manager.fontManager.ttflist}
plt.rcParams["font.family"] = [n for n in ("IPAPGothic", "IPAGothic", "Noto Sans CJK JP", "Yu Gothic", "Hiragino Sans") if n in _HAVE] + [
    "DejaVu Sans"]


def load(path):
    net = Net()
    net.load_state_dict(torch.load(path, map_location="cpu"))
    net.eval()
    return net


def pick(net, obs, scal, mask):
    with torch.no_grad():
        logits, _ = net(*(torch.from_numpy(x) for x in (obs, scal, mask)))
    return Categorical(logits=logits).sample().numpy().astype(np.int32)


def snapshot(obs, scal, side):
    """盤と取った枚数を、機械から見た並びにそろえて返す（人間の手番の obs は機械と人間の面が入れ替わっている）。"""
    o = obs[0]
    machine, human = (o[0], o[1]) if side == 0 else (o[1], o[0])
    lifted = o[2] if side == 0 else np.zeros_like(o[2])
    caps = (int(scal[0, 0]), int(scal[0, 1])) if side == 0 else (int(scal[0, 1]), int(scal[0, 0]))
    clock = LIMIT_MS - int(scal[0, 2]) * 100
    # 面 7・8 は機械と人間の駒が RU（40 秒動かさないと、隣に来られただけで取られる）になるまでの残り。どちらの見方でも同じ。
    stale = (machine > 0) & (o[7] == 0) | (human > 0) & (o[8] == 0)
    return clock, dict(machine=machine.copy(), human=human.copy(), lifted=lifted.copy(), head=o[9].copy(), caps=caps,
                       stale=stale)


def play(a):
    machine = load(a.model)
    torch.manual_seed(a.seed)
    shots = []
    if a.human:
        human = load(a.human)
        env = hasami_env.DuelEnv(n_envs=1, seed=a.seed, speed_scale=a.speed_scale, n_threads=1)
        obs, scal, mask, side = env.reset()
        while True:
            shots.append(snapshot(obs, scal, int(side[0])))
            act = pick(machine if side[0] == 0 else human, obs, scal, mask)
            obs, scal, mask, side, _, _, done, info = env.step(act)
            if done[0]:
                return shots, info
    env = hasami_env.BatchEnv(n_envs=1, seed=a.seed, opponent=a.opponent, speed_scale=a.speed_scale, n_threads=1)
    obs, scal, mask = env.reset()
    while True:
        shots.append(snapshot(obs, scal, 0))
        obs, scal, mask, _, _, done, info = env.step(pick(machine, obs, scal, mask))
        if done[0]:
            return shots, info


def main(argv=None):
    p = argparse.ArgumentParser()
    p.add_argument("model")
    p.add_argument("--opponent", default="b1")
    p.add_argument("--human", help="人間側に使う human.pt（付けると --opponent は使わない）")
    p.add_argument("--speed-scale", type=float, default=1.0)
    p.add_argument("--seed", type=int, default=7)
    p.add_argument("--speedup", type=float, default=4.0)
    p.add_argument("--title", default="")
    p.add_argument("--out", default="runs/game.gif")
    a = p.parse_args(argv)

    shots, info = play(a)
    winner, caps = int(info["winner"][0]), info["captures"][0].tolist()
    end_ms = shots[-1][0]
    # 対局の時計で等間隔に並べ直す。各コマはその時刻までの最後の決定点の盤。
    step_ms = 1000.0 * a.speedup / FPS
    frames, j = [], 0
    for k in range(int(end_ms // step_ms) + 1):
        while j + 1 < len(shots) and shots[j + 1][0] <= k * step_ms:
            j += 1
        frames.append((k * step_ms, shots[j][1]))
    last = dict(shots[-1][1], caps=tuple(caps), lifted=np.zeros_like(shots[-1][1]["lifted"]),
                stale=np.zeros_like(shots[-1][1]["stale"]))
    frames += [(end_ms, last)] * (2 * FPS)  # 結果を 2 秒見せる
    print(WINNER[winner], "captures", caps, f"{end_ms / 1000:.0f} s", "frames", len(frames))

    fig, ax = plt.subplots(figsize=(4.3, 4.8))
    fig.text(0.53, 0.015, "黄色の輪：40 秒動いていない駒（上下左右の隣に来られるだけで取られる）", ha="center", va="bottom",
             fontsize=7.5, color="#5f5e5a")

    def draw(k):
        t, s = frames[k]
        ax.clear()
        ax.set_xlim(-0.5, 6.5)
        ax.set_ylim(6.5, -0.5)
        ax.set_xticks([])
        ax.set_yticks([])
        ax.set_facecolor("#e9d9b6")
        for i in range(8):
            ax.plot([i - 0.5, i - 0.5], [-0.5, 6.5], color="#8a7350", lw=1)
            ax.plot([-0.5, 6.5], [i - 0.5, i - 0.5], color="#8a7350", lw=1)
        for r in range(7):
            for c in range(7):
                if s["machine"][r, c]:
                    ax.add_patch(plt.Circle((c, r), 0.38, color="#26251f"))
                if s["human"][r, c]:
                    ax.add_patch(plt.Circle((c, r), 0.38, facecolor="#fbf8f0", edgecolor="#26251f", lw=1.2))
                if s["stale"][r, c]:
                    ax.add_patch(plt.Circle((c, r), 0.45, fill=False, edgecolor="#eda100", lw=3))
                if s["lifted"][r, c]:
                    ax.add_patch(plt.Circle((c, r), 0.38, fill=False, edgecolor="#26251f", ls="--", lw=1.2))
                if s["head"][r, c]:
                    ax.plot(c, r, "x", color="#d6402b", ms=14, mew=3)
        m, h = s["caps"]
        head = f"{a.title}\n" if a.title else ""
        if k >= len(frames) - 2 * FPS:
            ax.set_title(f"{head}{WINNER[winner]}　機械 {m} − {h} 人間", fontsize=11)
        else:
            ax.set_title(f"{head}{int(t // 60000)}:{int(t % 60000 // 1000):02d}　機械 {m} − {h} 人間（{a.speedup:g} 倍速）",
                         fontsize=11)
        ax.text(-0.75, 0, "機械の側", ha="right", va="top", rotation=90, fontsize=9, color="#5f5e5a")
        ax.text(-0.75, 6, "人間の側", ha="right", va="bottom", rotation=90, fontsize=9, color="#5f5e5a")

    fig.subplots_adjust(left=0.09, right=0.97, bottom=0.06, top=0.86)
    FuncAnimation(fig, draw, frames=len(frames)).save(a.out, writer=PillowWriter(fps=FPS))
    print("wrote", a.out)
    return a.out


if __name__ == "__main__":
    main()
