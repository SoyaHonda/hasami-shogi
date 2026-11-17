"""学習曲線を描く（python/train/plot_curve.py）。

    python python/train/plot_curve.py runs/b1_s10                          # runs/b1_s10/curve.png
    python python/train/plot_curve.py runs/b1_s07 runs/b1_s10 runs/b1_s13  # runs/curves.png（重ねて描く）
"""
import csv
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

# 色覚の違いがあっても隣どうしを見分けられる順に並べた色（青、橙、緑青、黄）。線の右端に名前も書く。
COLORS = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100"]
# 速さはどの図でも同じ色にする（b1_s07 と league_s07 が同じ青になる）。ほかの名前は残りの色を順に使う。
SPEED_COLORS = {"s07": COLORS[0], "s10": COLORS[1], "s13": COLORS[2]}


def colors_for(names):
    """フォルダーの名前の最後（_s07 など）で色を決める。"""
    spare = iter(COLORS[3:] + COLORS)
    return [SPEED_COLORS.get(n.rsplit("_", 1)[-1]) or next(spare) for n in names]


def finish_axes(axes):
    """目盛りの線を薄くし、線が 2 本以上ある図では凡例と線の右端の名前をつける。"""
    for ax in axes:
        ax.grid(True, color="#e4e3df", linewidth=0.8)
        ax.set_axisbelow(True)
        for side in ("top", "right"):
            ax.spines[side].set_visible(False)
        lines = ax.get_lines()
        if len(lines) < 2:
            continue
        ax.legend(frameon=False)
        ends = []
        for line in lines:
            xs, ys = line.get_xdata(), line.get_ydata()
            last = next((i for i in range(len(ys) - 1, -1, -1) if ys[i] is not None), None)
            if last is not None:
                ends.append([ys[last], xs[last], line.get_label()])
        # 右端が近い線どうしは、名前が重ならないように縦に離す（縦の幅の 6% ずつ）。
        lo, hi = ax.get_ylim()
        gap = (hi - lo) * 0.06
        ends.sort()
        for prev, cur in zip(ends, ends[1:]):
            cur[0] = max(cur[0], prev[0] + gap)
        for y, x, label in ends:
            ax.annotate(label, (x, y), xytext=(4, 0), textcoords="offset points", va="center", fontsize=8,
                        color="#3d3d3a", annotation_clip=False)
        ax.set_xlim(right=ax.get_xlim()[1] * 1.12)


def main(argv=None):
    runs = list(sys.argv[1:] if argv is None else argv) or ["runs/b1_s10"]
    fig, (win_ax, lost_ax) = plt.subplots(1, 2, figsize=(10, 4))
    for color, run in zip(colors_for(os.path.basename(os.path.normpath(r)) for r in runs), runs):
        with open(os.path.join(run, "log.csv")) as f:
            rows = list(csv.DictReader(f))
        x = [int(r["steps"]) / 1e6 for r in rows]
        name = os.path.basename(os.path.normpath(run))
        win_ax.plot(x, [float(r["win_rate"]) for r in rows], color=color, linewidth=1.5, label=name)
        lost_ax.plot(x, [float(r["lost"]) for r in rows], color=color, linewidth=1.5, label=name)
    win_ax.set_title("win rate")
    lost_ax.set_title("pieces lost per game")
    for ax in (win_ax, lost_ax):
        ax.set_xlabel("decisions (millions)")
    finish_axes((win_ax, lost_ax))
    out = os.path.join(runs[0], "curve.png") if len(runs) == 1 else os.path.join(os.path.dirname(os.path.normpath(runs[0])), "curves.png")
    fig.tight_layout()
    fig.savefig(out, dpi=120)
    print("wrote", out)
    return out


if __name__ == "__main__":
    main()
