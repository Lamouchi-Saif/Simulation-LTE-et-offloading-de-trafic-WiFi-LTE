#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Analyse B5 : effet de l'hysteresis (moyenne, ecart-type, IC95, figures)."""
import argparse
from pathlib import Path
import numpy as np
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

T95 = {4: 2.776, 5: 2.571, 6: 2.447, 7: 2.365, 8: 2.306, 9: 2.262,
       10: 2.228, 15: 2.131, 20: 2.093, 30: 2.045}

def tcrit(n):
    df = n - 1
    if df <= 0:
        return 0.0
    keys = sorted(T95)
    for k in keys:
        if df <= k:
            return T95[k]
    return 1.96

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", default=str(Path.home() / "ns-3.48/results/tp3/B5"))
    ap.add_argument("--out", default=None)
    a = ap.parse_args()
    inp = Path(a.input)
    out = Path(a.out) if a.out else inp / "analysis"
    out.mkdir(parents=True, exist_ok=True)

    files = sorted((inp / "raw").glob("B5_H*_run*_summary.csv"))
    if not files:
        raise SystemExit("Aucun summary trouve")
    df = pd.concat([pd.read_csv(f).assign(src=f.name) for f in files], ignore_index=True)
    df.to_csv(out / "B5_all_runs.csv", index=False)

    metrics = ["nSwitch", "nPingPong", "offloadTx", "tputCV", "lossPct",
               "delayMean_ms", "delayMax_ms"]
    rows = []
    for H, g in df.groupby("hyst"):
        r = {"H": H, "n": len(g)}
        for m in metrics:
            sd = g[m].std(ddof=1) if len(g) > 1 else 0.0
            r[m + "_mean"] = g[m].mean()
            r[m + "_std"] = sd
            r[m + "_ci95"] = tcrit(len(g)) * sd / np.sqrt(len(g))
        r["nSwitch_min"] = g["nSwitch"].min()
        r["nSwitch_max"] = g["nSwitch"].max()
        rows.append(r)
    st = pd.DataFrame(rows).sort_values("H")
    st.to_csv(out / "B5_statistics.csv", index=False, float_format="%.5f")

    print("\n=== B5 : moyenne +- ecart-type (n runs) ===")
    for _, r in st.iterrows():
        print(f"H={r.H:>3.0f} m (n={int(r.n)}) | bascules {r.nSwitch_mean:6.2f} +- {r.nSwitch_std:5.2f}"
              f" [{r.nSwitch_min:.0f},{r.nSwitch_max:.0f}] | pingpong {r.nPingPong_mean:6.2f}"
              f" | offloading {100*r.offloadTx_mean:5.2f} % +- {100*r.offloadTx_std:4.2f}"
              f" | CV {r.tputCV_mean:.5f} | perte {r.lossPct_mean:.3f} %")

    H = st["H"].values
    def bar(col, ylabel, fname, scale=1.0, title=None):
        fig, ax = plt.subplots(figsize=(7, 4.6))
        ax.bar([f"{h:g}" for h in H], st[col + "_mean"] * scale,
               yerr=st[col + "_ci95"] * scale, capsize=5, color="#2f6fa8")
        ax.set_xlabel("Demi-hysteresis H (m)")
        ax.set_ylabel(ylabel)
        ax.set_title(title or ylabel)
        ax.grid(axis="y", alpha=.3)
        fig.tight_layout()
        fig.savefig(out / fname, dpi=200)
        plt.close(fig)

    bar("nSwitch", "Nombre de bascules (moyenne, IC 95 %)", "B5_switches.png")
    bar("nPingPong", "Nombre de ping-pong (moyenne, IC 95 %)", "B5_pingpong.png")
    bar("offloadTx", "Taux d'offloading (%)", "B5_offloading.png", scale=100)
    bar("tputCV", "Coefficient de variation du debit", "B5_cv.png")

    # Distribution des bascules par run
    fig, ax = plt.subplots(figsize=(7, 4.6))
    data = [df[df.hyst == h]["nSwitch"].values for h in H]
    ax.boxplot(data, tick_labels=[f"{h:g}" for h in H])
    ax.set_xlabel("Demi-hysteresis H (m)")
    ax.set_ylabel("Nombre de bascules par run")
    ax.set_title("Dispersion inter-runs")
    ax.grid(axis="y", alpha=.3)
    fig.tight_layout()
    fig.savefig(out / "B5_boxplot.png", dpi=200)
    plt.close(fig)

    # Courbe temporelle du run 1 (distance + interface active)
    fig, axes = plt.subplots(len(H), 1, figsize=(9, 2.2 * len(H)), sharex=True)
    axes = np.atleast_1d(axes)
    for ax, h in zip(axes, H):
        f = inp / "raw" / f"B5_H{h:g}_run1_bins.csv"
        if not f.exists():
            continue
        b = pd.read_csv(f)
        b = b[(b.t >= 1) & (b.t <= 79)]
        ax.plot(b.t, b.dist_m, color="gray", lw=1)
        ax.fill_between(b.t, 0, 1, where=b.onWifi == 1, transform=ax.get_xaxis_transform(),
                        color="#2f6fa8", alpha=.25, label="Wi-Fi actif")
        ax.axhline(50 - h, color="g", ls="--", lw=.8)
        ax.axhline(50 + h, color="r", ls="--", lw=.8)
        ax.set_ylabel("d (m)")
        ax.set_title(f"H = {h:g} m (run 1) : seuils entree (vert) / sortie (rouge)", fontsize=9)
    axes[-1].set_xlabel("Temps (s)")
    fig.tight_layout()
    fig.savefig(out / "B5_timeline.png", dpi=200)
    plt.close(fig)
    print(f"\nFigures et CSV dans : {out}")

if __name__ == "__main__":
    main()