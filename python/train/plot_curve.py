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


def main(argv=None):
    runs = list(sys.argv[1:] if argv is None else argv) or ["runs/b1_s10"]
    fig, (win_ax, lost_ax) = plt.subplots(1, 2, figsize=(10, 4))
    for run in runs:
        with open(os.path.join(run, "log.csv")) as f:
            rows = list(csv.DictReader(f))
        x = [int(r["steps"]) / 1e6 for r in rows]
        name = os.path.basename(os.path.normpath(run))
        win_ax.plot(x, [float(r["win_rate"]) for r in rows], label=name)
        lost_ax.plot(x, [float(r["lost"]) for r in rows], label=name)
    win_ax.set_title("win rate")
    lost_ax.set_title("pieces lost per game")
    for ax in (win_ax, lost_ax):
        ax.set_xlabel("decisions (millions)")
        ax.grid(True)
        ax.legend()
    out = os.path.join(runs[0], "curve.png") if len(runs) == 1 else os.path.join(os.path.dirname(os.path.normpath(runs[0])), "curves.png")
    fig.tight_layout()
    fig.savefig(out, dpi=120)
    print("wrote", out)
    return out


if __name__ == "__main__":
    main()
