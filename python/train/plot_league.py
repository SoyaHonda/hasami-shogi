"""リーグの勝率を速さごとに並べて描く（python/train/plot_league.py）。

    python python/train/plot_league.py runs/league_s07 runs/league_s10 runs/league_s13  # runs/league_curves.png

左から、B1・人間の主役・人間の過去の版を相手にした機械の勝率。どれも log.csv の各更新の値を、
前後 WINDOW 回の更新で局数の重みをつけて平均した線（1 回の更新は 40〜80 局なので、そのままだと揺れが大きい）。
"""
import csv
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

from plot_curve import colors_for, finish_axes  # noqa: E402

WINDOW = 25
PANELS = [("b1", "vs B1"), ("main", "vs human main"), ("past", "vs past humans")]


def smoothed(rows, kind):
    """更新ごとの勝率を、前後 WINDOW 回の局数で重みづけして平均する。局がない区間は None。"""
    games = [float(r[f"{kind}_games"]) for r in rows]
    wins = [g * float(r[f"{kind}_win"]) for g, r in zip(games, rows)]
    out = []
    for i in range(len(rows)):
        lo, hi = max(0, i - WINDOW // 2), min(len(rows), i + WINDOW // 2 + 1)
        n = sum(games[lo:hi])
        out.append(sum(wins[lo:hi]) / n if n else None)
    return out


def main(argv=None):
    runs = list(sys.argv[1:] if argv is None else argv) or ["runs/league_s10"]
    fig, axes = plt.subplots(1, len(PANELS), figsize=(13, 4), sharey=True)
    for color, run in zip(colors_for(os.path.basename(os.path.normpath(r)) for r in runs), runs):
        with open(os.path.join(run, "log.csv")) as f:
            rows = list(csv.DictReader(f))
        x = [int(r["update"]) for r in rows]
        name = os.path.basename(os.path.normpath(run))
        for ax, (kind, _) in zip(axes, PANELS):
            ax.plot(x, smoothed(rows, kind), color=color, linewidth=2, label=name)
    for ax, (_, title) in zip(axes, PANELS):
        ax.set_title(f"machine win rate {title}")
        ax.set_xlabel("update")
        ax.set_ylim(0, 1.02)
    finish_axes(axes)
    out = os.path.join(os.path.dirname(os.path.normpath(runs[0])), "league_curves.png")
    fig.tight_layout()
    fig.savefig(out, dpi=120)
    print("wrote", out)
    return out


if __name__ == "__main__":
    main()
