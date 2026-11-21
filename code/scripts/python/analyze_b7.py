#!/usr/bin/env python3
"""Analyse statistique B7 : nombre de bascules distance vs RSSI."""
from __future__ import annotations
import argparse
from pathlib import Path
import sys
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt


def load_runs(root: Path) -> pd.DataFrame:
    rows = []
    for criterion in ("distance", "rssi"):
        files = sorted((root / criterion).glob("seed_*/*_summary.csv"))
        for path in files:
            df = pd.read_csv(path)
            if len(df) != 1:
                raise ValueError(f"{path}: attendu une seule ligne de résumé, reçu {len(df)}")
            row = df.iloc[0].to_dict()
            row["summary_file"] = str(path)
            rows.append(row)
    if not rows:
        raise FileNotFoundError(f"Aucun *_summary.csv trouvé sous {root}")
    out = pd.DataFrame(rows)
    required = {"criterion", "seed", "run", "nIn", "nOut", "nSwitch", "rssiIn", "rssiOut", "thrIn", "thrOut"}
    missing = required - set(out.columns)
    if missing:
        raise ValueError(f"Colonnes manquantes : {sorted(missing)}")
    out["seed"] = out["seed"].astype(int)
    out["run"] = out["run"].astype(int)
    out["nSwitch_recalc"] = out["nIn"] + out["nOut"]
    if not (out["nSwitch"] == out["nSwitch_recalc"]).all():
        raise ValueError("Incohérence : nSwitch != nIn + nOut")
    return out.sort_values(["criterion", "seed"]).reset_index(drop=True)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("raw_dir", type=Path, help="Dossier raw de la campagne B7")
    ap.add_argument("--out", type=Path, default=Path("results/tp3/B7_experiment/analysis"))
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

    runs = load_runs(args.raw_dir)
    counts = runs.groupby("criterion")["seed"].nunique().to_dict()
    for criterion in ("distance", "rssi"):
        if counts.get(criterion, 0) < 5:
            raise ValueError(f"{criterion}: seulement {counts.get(criterion, 0)} graines distinctes (minimum 5)")

    # Comparaison appariée : chaque graine doit exister pour les deux critères.
    pivot = runs.pivot(index="seed", columns="criterion", values="nSwitch")
    if not {"distance", "rssi"}.issubset(pivot.columns) or pivot[["distance", "rssi"]].isna().any().any():
        raise ValueError("Les deux critères ne possèdent pas exactement les mêmes graines.")
    pivot["delta_rssi_moins_distance"] = pivot["rssi"] - pivot["distance"]
    pivot.to_csv(args.out / "B7_paired_by_seed.csv", index_label="seed")

    stats = (runs.groupby("criterion").agg(
        n_runs=("nSwitch", "count"), n_seeds=("seed", "nunique"),
        switches_mean=("nSwitch", "mean"), switches_std=("nSwitch", "std"),
        entries_mean=("nIn", "mean"), entries_std=("nIn", "std"),
        exits_mean=("nOut", "mean"), exits_std=("nOut", "std"),
        rssi_in_mean=("rssiIn", "mean"), rssi_out_mean=("rssiOut", "mean"),
    ).reset_index())
    stats.to_csv(args.out / "B7_statistics.csv", index=False)
    runs.to_csv(args.out / "B7_all_runs.csv", index=False)

    delta = pivot["delta_rssi_moins_distance"]
    report = [
        "# Analyse B7 — comparaison distance / RSSI", "",
        f"- Simulations analysées : {len(runs)} (attendu : 10 pour cinq graines).",
        f"- Graines communes : {', '.join(map(str, pivot.index.tolist()))}.",
        "- Écart-type calculé avec ddof=1 (écart-type échantillonnal).",
        "- Δ apparié = nombre de bascules RSSI − nombre de bascules distance.", "",
        "## Statistiques par critère", "",
        stats.to_markdown(index=False), "",
        "## Différence appariée par graine", "",
        pivot.to_markdown(), "",
        f"- Δ moyen : {delta.mean():.3f} bascule(s).",
        f"- Écart-type de Δ : {delta.std(ddof=1):.3f}.",
        f"- Graines où RSSI produit davantage de bascules : {(delta > 0).sum()}/{len(delta)}.",
        f"- Graines où RSSI produit moins de bascules : {(delta < 0).sum()}/{len(delta)}.",
        f"- Graines avec le même nombre de bascules : {(delta == 0).sum()}/{len(delta)}.", "",
        "## Précautions d'interprétation", "",
        "Un écart moyen faible au regard de la dispersion entre graines ne permet pas de conclure à une différence marquée. "
        "Dans ce scénario LogDistance sans shadowing, le RSSI suit essentiellement la distance ; une différence peut toutefois provenir "
        "de l'échantillonnage des trames, du lissage EWMA, du délai RSSI et de la période de vérification. "
        "En environnement indoor ou urbain dense, les trajets multiples, le masquage et les obstacles peuvent décorréler "
        "la puissance reçue de la seule distance géométrique, rendant le critère RSSI plus pertinent.",
        "",
    ]
    (args.out / "B7_analysis.md").write_text("\n".join(report), encoding="utf-8")

    # Figure 1 : moyennes et écarts-types.
    fig, ax = plt.subplots(figsize=(7, 4.5))
    ax.bar(stats["criterion"], stats["switches_mean"], yerr=stats["switches_std"].fillna(0), capsize=6)
    ax.set_ylabel("Nombre de bascules")
    ax.set_title("B7 — moyenne et écart-type sur cinq graines")
    ax.grid(axis="y", alpha=0.3)
    fig.tight_layout()
    fig.savefig(args.out / "B7_switches_mean_std.png", dpi=180)
    plt.close(fig)

    # Figure 2 : comparaison appariée graine par graine.
    fig, ax = plt.subplots(figsize=(8, 4.5))
    x = np.arange(len(pivot.index))
    ax.plot(x, pivot["distance"], marker="o", label="Distance")
    ax.plot(x, pivot["rssi"], marker="s", label="RSSI")
    ax.set_xticks(x, [str(s) for s in pivot.index])
    ax.set_xlabel("Graine aléatoire")
    ax.set_ylabel("Nombre de bascules")
    ax.set_title("B7 — nombre de bascules par graine")
    ax.legend()
    ax.grid(alpha=0.3)
    fig.tight_layout()
    fig.savefig(args.out / "B7_switches_by_seed.png", dpi=180)
    plt.close(fig)

    # Figure 3 : différence appariée.
    fig, ax = plt.subplots(figsize=(7, 4.2))
    ax.axhline(0, linewidth=1)
    ax.bar([str(s) for s in pivot.index], delta.values)
    ax.set_xlabel("Graine aléatoire")
    ax.set_ylabel("Δ bascules (RSSI − distance)")
    ax.set_title("B7 — différence appariée par graine")
    ax.grid(axis="y", alpha=0.3)
    fig.tight_layout()
    fig.savefig(args.out / "B7_paired_difference.png", dpi=180)
    plt.close(fig)

    print(stats.to_string(index=False))
    print(f"\nAnalyse terminée : {args.out}")
    return 0

if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERREUR: {exc}", file=sys.stderr)
        raise SystemExit(2)
