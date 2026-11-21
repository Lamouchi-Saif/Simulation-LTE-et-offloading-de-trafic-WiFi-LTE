#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Analyse et valide la campagne B6 du TP3 NS-3.48.

Entrées :
  - archive ZIP de campagne contenant raw/*_summary.csv ;
  - ou répertoire contenant directement les CSV *_summary.csv ;
  - ou répertoire de campagne contenant raw/.

Sorties :
  B6_all_runs.csv
  B6_statistics.csv
  B6_validation.txt
  B6_analysis.md
  figures PNG

Dépendances : pandas, numpy, matplotlib
Installation : python3 -m pip install pandas numpy matplotlib
"""
from __future__ import annotations

import argparse
import math
import re
import sys
import zipfile
from pathlib import Path

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt


EXPECTED_RADII = [40.0, 50.0, 60.0]
EXPECTED_SPEEDS = [3.0, 5.0, 7.0]
EXPECTED_SEEDS = [1, 2, 3, 4, 5]
REQUIRED_COLUMNS = {
    "criterion", "pattern", "apRadius", "hyst", "speed", "offset_b",
    "shadowSigma", "distNoise", "seed", "run",
    "txBytesWifi", "txBytesLte", "offloadTx", "offloadTheory",
    "nSwitch", "lossPct", "tputMean_Mbps", "tputCV"
}


def locate_csv_directory(source: Path, work_dir: Path) -> Path:
    """Résout une archive ZIP, un dossier raw ou un dossier de campagne."""
    if source.is_file() and source.suffix.lower() == ".zip":
        extract_dir = work_dir / "extracted"
        extract_dir.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(source) as zf:
            bad = zf.testzip()
            if bad:
                raise RuntimeError(f"Archive ZIP corrompue : {bad}")
            zf.extractall(extract_dir)
        candidates = list(extract_dir.rglob("*_summary.csv"))
        if not candidates:
            raise FileNotFoundError("Aucun fichier *_summary.csv dans l'archive.")
        return candidates[0].parent

    if source.is_dir():
        if list(source.glob("*_summary.csv")):
            return source
        raw = source / "raw"
        if raw.is_dir() and list(raw.glob("*_summary.csv")):
            return raw
        candidates = list(source.rglob("*_summary.csv"))
        if candidates:
            return candidates[0].parent

    raise FileNotFoundError(
        f"Source introuvable ou aucun *_summary.csv : {source}"
    )


def load_runs(csv_dir: Path) -> tuple[pd.DataFrame, list[str]]:
    files = sorted(csv_dir.glob("*_summary.csv"))
    if not files:
        raise FileNotFoundError(f"Aucun *_summary.csv dans {csv_dir}")

    frames = []
    errors = []
    for file in files:
        try:
            df = pd.read_csv(file)
            if len(df) != 1:
                errors.append(f"{file.name}: attendu 1 ligne, trouvé {len(df)}")
                continue
            df["source_file"] = file.name
            frames.append(df)
        except Exception as exc:
            errors.append(f"{file.name}: lecture impossible ({exc})")

    if not frames:
        raise RuntimeError("Aucun CSV de synthèse exploitable.")

    data = pd.concat(frames, ignore_index=True)
    missing = REQUIRED_COLUMNS - set(data.columns)
    if missing:
        raise RuntimeError("Colonnes absentes des summary.csv : " + ", ".join(sorted(missing)))

    return data, errors


def validate_and_derive(data: pd.DataFrame, read_errors: list[str]) -> tuple[pd.DataFrame, list[str]]:
    issues = list(read_errors)
    df = data.copy()

    numeric = [
        "apRadius", "hyst", "speed", "offset_b", "shadowSigma", "distNoise",
        "seed", "run", "txBytesWifi", "txBytesLte", "offloadTx",
        "offloadTheory", "nSwitch", "lossPct", "tputMean_Mbps", "tputCV"
    ]
    for col in numeric:
        df[col] = pd.to_numeric(df[col], errors="coerce")
        if df[col].isna().any():
            issues.append(f"Valeurs non numériques ou manquantes dans {col}.")

    if len(df) != 45:
        issues.append(f"Nombre de simulations : {len(df)} au lieu de 45.")

    if df["source_file"].duplicated().any():
        issues.append("Noms de fichiers summary dupliqués.")

    expected = {
        "criterion": {"distance"},
        "pattern": {"line"},
        "hyst": {5.0},
        "shadowSigma": {0.0},
        "distNoise": {0.0},
        "run": {1.0},
    }
    for col, values in expected.items():
        observed = set(df[col].dropna().unique())
        if observed != values:
            issues.append(f"{col}: valeurs observées {sorted(observed)} ; attendu {sorted(values)}.")

    if set(df["seed"].dropna().astype(int)) != set(EXPECTED_SEEDS):
        issues.append(f"Graines observées {sorted(df['seed'].dropna().unique())}; attendu {EXPECTED_SEEDS}.")

    if set(df["apRadius"].dropna().unique()) != set(EXPECTED_RADII):
        issues.append("Les rayons ne correspondent pas à 40, 50 et 60 m.")
    if set(df["speed"].dropna().unique()) != set(EXPECTED_SPEEDS):
        issues.append("Les vitesses ne correspondent pas à 3, 5 et 7 m/s.")

    duplicated = df.duplicated(["apRadius", "speed", "seed", "run"], keep=False)
    if duplicated.any():
        issues.append("Combinaisons rayon/vitesse/graine/run dupliquées.")

    expected_keys = {
        (r, v, s, 1)
        for r in EXPECTED_RADII
        for v in EXPECTED_SPEEDS
        for s in EXPECTED_SEEDS
    }
    observed_keys = set(
        (float(row.apRadius), float(row.speed), int(row.seed), int(row.run))
        for row in df.itertuples()
    )
    missing_keys = sorted(expected_keys - observed_keys)
    extra_keys = sorted(observed_keys - expected_keys)
    if missing_keys:
        issues.append(f"Combinaisons manquantes : {missing_keys}")
    if extra_keys:
        issues.append(f"Combinaisons inattendues : {extra_keys}")

    denominator = df["txBytesWifi"] + df["txBytesLte"]
    df["offloadTx_recomputed"] = np.where(
        denominator > 0, df["txBytesWifi"] / denominator, np.nan
    )
    df["offloadTx_percent"] = 100.0 * df["offloadTx_recomputed"]
    df["offloadTx_reported_percent"] = 100.0 * df["offloadTx"]
    df["offloadTx_abs_error"] = (
        df["offloadTx_recomputed"] - df["offloadTx"]
    ).abs()

    max_error = df["offloadTx_abs_error"].max()
    if pd.isna(max_error) or max_error > 1e-4:
        issues.append(
            "offloadTx du summary ne correspond pas à txBytesWifi/"
            "(txBytesWifi+txBytesLte), tolérance 1e-4."
        )

    if (denominator <= 0).any():
        issues.append("Au moins une simulation possède un volume total transmis nul.")
    if (df[["txBytesWifi", "txBytesLte"]] < 0).any().any():
        issues.append("Compteurs d'octets négatifs détectés.")

    # La statistique officielle est recalculée depuis les compteurs bruts,
    # et non depuis le champ pré-calculé offloadTx.
    return df, issues


def make_statistics(df: pd.DataFrame) -> pd.DataFrame:
    stats = (
        df.groupby(["apRadius", "speed"], as_index=False)
        .agg(
            n=("offloadTx_recomputed", "count"),
            offload_mean=("offloadTx_recomputed", "mean"),
            offload_std=("offloadTx_recomputed", "std"),
            offload_min=("offloadTx_recomputed", "min"),
            offload_max=("offloadTx_recomputed", "max"),
            mean_tx_wifi_B=("txBytesWifi", "mean"),
            mean_tx_lte_B=("txBytesLte", "mean"),
            mean_offset_b_m=("offset_b", "mean"),
            std_offset_b_m=("offset_b", "std"),
            mean_switches=("nSwitch", "mean"),
            mean_loss_pct=("lossPct", "mean"),
            mean_throughput_Mbps=("tputMean_Mbps", "mean"),
            mean_tput_cv=("tputCV", "mean"),
        )
    )
    stats["offload_mean_pct"] = 100 * stats["offload_mean"]
    stats["offload_std_pp"] = 100 * stats["offload_std"]
    stats["offload_min_pct"] = 100 * stats["offload_min"]
    stats["offload_max_pct"] = 100 * stats["offload_max"]
    return stats


def save_figures(df: pd.DataFrame, stats: pd.DataFrame, out: Path) -> list[Path]:
    out.mkdir(parents=True, exist_ok=True)
    outputs = []

    # 1. Taux moyen en fonction du rayon, une courbe par vitesse.
    fig, ax = plt.subplots(figsize=(8, 5))
    for speed in EXPECTED_SPEEDS:
        sub = stats[stats["speed"] == speed].sort_values("apRadius")
        ax.errorbar(
            sub["apRadius"], sub["offload_mean_pct"],
            yerr=sub["offload_std_pp"], marker="o", capsize=4,
            linewidth=1.8, label=f"{speed:g} m/s"
        )
    ax.set(title="B6 — Taux d'offloading selon le rayon Wi-Fi",
           xlabel="Rayon de couverture Wi-Fi (m)",
           ylabel="Offloading moyen (%)")
    ax.set_xticks(EXPECTED_RADII)
    ax.grid(True, alpha=0.3)
    ax.legend(title="Vitesse UE")
    fig.tight_layout()
    p = out / "B6_offloading_vs_radius.png"
    fig.savefig(p, dpi=180)
    plt.close(fig)
    outputs.append(p)

    # 2. Taux moyen en fonction de la vitesse, une courbe par rayon.
    fig, ax = plt.subplots(figsize=(8, 5))
    for radius in EXPECTED_RADII:
        sub = stats[stats["apRadius"] == radius].sort_values("speed")
        ax.errorbar(
            sub["speed"], sub["offload_mean_pct"],
            yerr=sub["offload_std_pp"], marker="o", capsize=4,
            linewidth=1.8, label=f"R = {radius:g} m"
        )
    ax.set(title="B6 — Taux d'offloading selon la vitesse de l'UE",
           xlabel="Vitesse de l'UE (m/s)",
           ylabel="Offloading moyen (%)")
    ax.set_xticks(EXPECTED_SPEEDS)
    ax.grid(True, alpha=0.3)
    ax.legend(title="Rayon Wi-Fi")
    fig.tight_layout()
    p = out / "B6_offloading_vs_speed.png"
    fig.savefig(p, dpi=180)
    plt.close(fig)
    outputs.append(p)

    # 3. Matrice des moyennes.
    mean_matrix = stats.pivot(index="speed", columns="apRadius", values="offload_mean_pct")
    fig, ax = plt.subplots(figsize=(7, 4.8))
    im = ax.imshow(mean_matrix.values, aspect="auto")
    ax.set_xticks(range(len(mean_matrix.columns)), [f"{x:g} m" for x in mean_matrix.columns])
    ax.set_yticks(range(len(mean_matrix.index)), [f"{x:g} m/s" for x in mean_matrix.index])
    ax.set_xlabel("Rayon Wi-Fi")
    ax.set_ylabel("Vitesse UE")
    ax.set_title("B6 — Offloading moyen (%)")
    for i in range(mean_matrix.shape[0]):
        for j in range(mean_matrix.shape[1]):
            ax.text(j, i, f"{mean_matrix.iloc[i, j]:.2f}%",
                    ha="center", va="center")
    fig.colorbar(im, ax=ax, label="Offloading moyen (%)")
    fig.tight_layout()
    p = out / "B6_heatmap_mean.png"
    fig.savefig(p, dpi=180)
    plt.close(fig)
    outputs.append(p)

    # 4. Variabilité inter-graines (écart-type en points de pourcentage).
    std_matrix = stats.pivot(index="speed", columns="apRadius", values="offload_std_pp")
    fig, ax = plt.subplots(figsize=(7, 4.8))
    im = ax.imshow(std_matrix.values, aspect="auto")
    ax.set_xticks(range(len(std_matrix.columns)), [f"{x:g} m" for x in std_matrix.columns])
    ax.set_yticks(range(len(std_matrix.index)), [f"{x:g} m/s" for x in std_matrix.index])
    ax.set_xlabel("Rayon Wi-Fi")
    ax.set_ylabel("Vitesse UE")
    ax.set_title("B6 — Écart-type inter-graines (points de %)")
    for i in range(std_matrix.shape[0]):
        for j in range(std_matrix.shape[1]):
            ax.text(j, i, f"{std_matrix.iloc[i, j]:.3f}",
                    ha="center", va="center")
    fig.colorbar(im, ax=ax, label="Écart-type (points de %)")
    fig.tight_layout()
    p = out / "B6_heatmap_std.png"
    fig.savefig(p, dpi=180)
    plt.close(fig)
    outputs.append(p)

    # 5. Valeurs individuelles pour visualiser la dispersion entre graines.
    fig, ax = plt.subplots(figsize=(9, 5))
    for radius in EXPECTED_RADII:
        for speed in EXPECTED_SPEEDS:
            sub = df[(df.apRadius == radius) & (df.speed == speed)]
            x = np.full(len(sub), speed + (radius - 50) * 0.001)
            ax.scatter(x, sub["offloadTx_percent"], s=35,
                       label=f"R={radius:g} m" if speed == EXPECTED_SPEEDS[0] else None)
    ax.set(title="B6 — Résultats individuels par graine",
           xlabel="Vitesse UE (m/s) — léger décalage selon le rayon",
           ylabel="Offloading transmis (%)")
    ax.set_xticks(EXPECTED_SPEEDS)
    ax.grid(True, alpha=0.3)
    ax.legend(title="Rayon Wi-Fi")
    fig.tight_layout()
    p = out / "B6_seed_dispersion.png"
    fig.savefig(p, dpi=180)
    plt.close(fig)
    outputs.append(p)

    return outputs


def write_report(df: pd.DataFrame, stats: pd.DataFrame, issues: list[str], out: Path) -> None:
    lines = [
        "# Analyse de la campagne B6",
        "",
        "## Protocole observé dans les CSV",
        f"- Nombre de simulations : {len(df)}",
        f"- Rayons : {', '.join(f'{x:g} m' for x in sorted(df.apRadius.unique()))}",
        f"- Vitesses : {', '.join(f'{x:g} m/s' for x in sorted(df.speed.unique()))}",
        f"- Graines : {', '.join(str(int(x)) for x in sorted(df.seed.unique()))}",
        f"- Critère : {', '.join(map(str, sorted(df.criterion.unique())))}",
        f"- Trajectoire : {', '.join(map(str, sorted(df.pattern.unique())))}",
        "",
        "## Tableau des résultats",
        "",
        "| Rayon (m) | Vitesse (m/s) | n | Offloading moyen (%) | Écart-type (points de %) |",
        "|---:|---:|---:|---:|---:|",
    ]
    for r in stats.itertuples():
        sd = "NA" if pd.isna(r.offload_std_pp) else f"{r.offload_std_pp:.4f}"
        lines.append(
            f"| {r.apRadius:.0f} | {r.speed:.0f} | {r.n} | "
            f"{r.offload_mean_pct:.4f} | {sd} |"
        )

    lines += ["", "## Contrôles de validation", ""]
    if issues:
        lines += ["**Des anomalies ou avertissements ont été détectés :**", ""]
        lines += [f"- {x}" for x in issues]
    else:
        lines += ["Aucune anomalie bloquante détectée par les contrôles automatisés."]

    lines += [
        "",
        "## Interprétation à conduire",
        "",
        "- Comparer les moyennes entre rayons à vitesse constante.",
        "- Comparer les moyennes entre vitesses à rayon constant.",
        "- Comparer les écarts-types inter-graines à l'amplitude des variations de moyenne.",
        "- Ne pas interpréter un écart-type nul comme une absence générale d'aléa : "
        "il signifie seulement que les cinq résultats mesurés sont identiques à la précision des compteurs.",
        "",
        "## Limites du protocole",
        "",
        "Les CSV indiquent shadowSigma=0 et distNoise=0. La variabilité entre graines provient "
        "donc principalement du décalage latéral aléatoire offset_b. Elle ne représente pas "
        "une variabilité de shadowing radio. Les résultats doivent être discutés dans ce cadre.",
        "",
        "Le champ offloadTx est recalculé à partir des compteurs txBytesWifi et txBytesLte. "
        "La formule utilisée est : txBytesWifi / (txBytesWifi + txBytesLte).",
        "",
    ]
    (out / "B6_analysis.md").write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Validation et analyse de la campagne B6.")
    parser.add_argument(
        "source", nargs="?",
        default="B6_campaign_20261121(1).zip",
        help="Archive ZIP ou dossier contenant les summary.csv."
    )
    parser.add_argument(
        "--out", default="results/tp3/B6_experiment/analysis",
        help="Répertoire de sortie."
    )
    args = parser.parse_args()

    source = Path(args.source).expanduser().resolve()
    out = Path(args.out).expanduser().resolve()
    work = out / "_work"
    work.mkdir(parents=True, exist_ok=True)

    try:
        csv_dir = locate_csv_directory(source, work)
        df, read_errors = load_runs(csv_dir)
        df, issues = validate_and_derive(df, read_errors)
        stats = make_statistics(df)

        out.mkdir(parents=True, exist_ok=True)
        df.to_csv(out / "B6_all_runs.csv", index=False, float_format="%.8f")
        stats.to_csv(out / "B6_statistics.csv", index=False, float_format="%.8f")
        figures = save_figures(df, stats, out)
        write_report(df, stats, issues, out)

        validation = [
            f"Source CSV : {csv_dir}",
            f"Nombre de runs : {len(df)}",
            f"Combinaisons : {df.groupby(['apRadius','speed']).ngroups}",
            f"Graines : {sorted(df.seed.astype(int).unique().tolist())}",
            f"Erreur maximale entre offloadTx déclaré et recalculé : {df.offloadTx_abs_error.max():.8g}",
            "",
            "Anomalies :",
            *(issues if issues else ["Aucune anomalie bloquante détectée."]),
            "",
            "Figures :",
            *[str(p) for p in figures],
        ]
        (out / "B6_validation.txt").write_text("\n".join(validation), encoding="utf-8")

        print(f"[OK] {len(df)} simulations chargées.")
        print(f"[OK] Résultats : {out}")
        print("\nMoyennes et écarts-types :")
        print(stats[["apRadius", "speed", "n", "offload_mean_pct", "offload_std_pp"]].to_string(index=False))
        if issues:
            print("\n[ATTENTION] Contrôles à examiner :")
            for issue in issues:
                print(" -", issue)
            return 2
        print("\n[OK] Aucun problème détecté par les contrôles automatisés.")
        return 0

    except Exception as exc:
        print(f"[ERREUR] {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
