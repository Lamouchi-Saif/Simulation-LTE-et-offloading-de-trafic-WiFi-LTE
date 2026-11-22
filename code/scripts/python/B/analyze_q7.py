
#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Q7 - Analyse comparative Distance vs RSSI.

Arborescence attendue :
raw/
  distance/
    seed_1/*_summary.csv
    seed_1/*_bins.csv
  rssi/
    seed_1/*_summary.csv
    seed_1/*_bins.csv

Sorties :
  Q7_all_runs.csv
  Q7_statistics.csv
  Q7_paired_by_seed.csv
  Q7_statistical_tests.csv
  Q7_analysis.md
  figures/*.png

Aucune simulation NS-3 n'est lancée.
"""

import argparse
import sys
from pathlib import Path

import numpy as np
import pandas as pd

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


CRITERIA = ["distance", "rssi"]

INTEGER_COLUMNS = [
    "seed", "run", "nIn", "nOut", "nSwitch"
]

NUMERIC_COLUMNS = [
    "apRadius", "hyst", "thrIn", "thrOut",
    "rssiIn", "rssiOut", "speed",
    "shadowSigma", "distNoise",
    "nPingPong",
    "txBytesWifi", "txBytesLte",
    "rxBytesWifi", "rxBytesLte",
    "offloadTx", "offloadRx", "offloadTheory",
    "tputMean_Mbps", "tputStd_Mbps",
    "tputCV", "lossPct",
    "delayMean_ms", "delayMax_ms",
    "outage_s", "tcpTput_Mbps"
]

METRICS = [
    "nSwitch", "nIn", "nOut", "nPingPong",
    "offloadTx", "offloadRx",
    "tputMean_Mbps", "tputStd_Mbps",
    "tputCV", "lossPct",
    "delayMean_ms", "delayMax_ms",
    "outage_s", "tcpTput_Mbps"
]

# pattern est volontairement exclu : c'est une colonne textuelle.
COMMON_NUMERIC_PARAMETERS = [
    "apRadius", "hyst", "thrIn", "thrOut",
    "speed", "shadowSigma", "distNoise"
]


def read_csv(path):
    try:
        return pd.read_csv(path)
    except Exception as exc:
        raise RuntimeError(
            f"Impossible de lire {path}: {exc}"
        ) from exc


def discover_summaries(raw_dir):
    frames = []

    for criterion in CRITERIA:
        folder = raw_dir / criterion

        if not folder.is_dir():
            raise FileNotFoundError(
                f"Dossier manquant : {folder}"
            )

        files = sorted(
            folder.glob("seed_*/*_summary.csv")
        )

        if not files:
            raise FileNotFoundError(
                f"Aucun *_summary.csv dans {folder}"
            )

        for file in files:
            df = read_csv(file)

            if len(df) != 1:
                raise ValueError(
                    f"{file}: une seule ligne est attendue, "
                    f"{len(df)} trouvée(s)."
                )

            df["source_file"] = str(file)
            frames.append(df)

    return pd.concat(frames, ignore_index=True)


def validate_data(df, minimum_seeds):
    required = [
        "criterion", "seed", "run",
        "nIn", "nOut", "nSwitch",
        "apRadius", "hyst",
        "offloadTx", "offloadRx",
        "tputMean_Mbps", "tputStd_Mbps",
        "tputCV", "lossPct",
        "delayMean_ms", "outage_s"
    ]

    missing = sorted(
        set(required) - set(df.columns)
    )

    if missing:
        raise ValueError(
            f"Colonnes obligatoires manquantes : {missing}"
        )

    df = df.copy()

    # Colonnes catégorielles : aucune conversion numérique.
    df["criterion"] = (
        df["criterion"].astype(str).str.strip().str.lower()
    )

    if "pattern" in df.columns:
        df["pattern"] = (
            df["pattern"].astype(str).str.strip().str.lower()
        )

    if set(df["criterion"]) != set(CRITERIA):
        raise ValueError(
            "Les deux critères distance et rssi doivent être présents."
        )

    # Conversion des entiers.
    for col in INTEGER_COLUMNS:
        if col not in df.columns:
            continue

        df[col] = pd.to_numeric(
            df[col], errors="raise"
        )

        if df[col].isna().any():
            raise ValueError(
                f"Valeur manquante dans {col}"
            )

        if not np.isfinite(df[col]).all():
            raise ValueError(
                f"Valeur non finie dans {col}"
            )

        if not np.allclose(
            df[col], df[col].round()
        ):
            raise ValueError(
                f"Valeur non entière dans {col}"
            )

        df[col] = df[col].astype(int)

    # Conversion des colonnes numériques uniquement.
    # IMPORTANT : pattern et criterion ne sont pas convertis.
    for col in NUMERIC_COLUMNS:
        if col not in df.columns:
            continue

        df[col] = pd.to_numeric(
            df[col], errors="raise"
        )

        values = df[col].dropna().to_numpy(dtype=float)

        if not np.isfinite(values).all():
            raise ValueError(
                f"Valeur non finie dans {col}"
            )

    if (df[["nIn", "nOut", "nSwitch"]] < 0).any().any():
        raise ValueError("Compteur négatif détecté.")

    if not (
        df["nSwitch"] == df["nIn"] + df["nOut"]
    ).all():
        raise ValueError(
            "nSwitch doit être égal à nIn + nOut."
        )

    if df.duplicated(
        ["criterion", "seed", "run"]
    ).any():
        raise ValueError(
            "Simulations dupliquées détectées."
        )

    if (
        df.groupby(["criterion", "seed"]).size() > 1
    ).any():
        raise ValueError(
            "Plusieurs runs détectés pour une même graine."
        )

    # Vérification de l'appariement des graines.
    seed_sets = {
        c: set(df.loc[df.criterion == c, "seed"])
        for c in CRITERIA
    }

    if seed_sets["distance"] != seed_sets["rssi"]:
        raise ValueError(
            "Les graines ne sont pas appariées : "
            f"distance={sorted(seed_sets['distance'])}, "
            f"rssi={sorted(seed_sets['rssi'])}"
        )

    if len(seed_sets["distance"]) < minimum_seeds:
        raise ValueError(
            f"Nombre insuffisant de graines : "
            f"{len(seed_sets['distance'])}, "
            f"minimum demandé : {minimum_seeds}."
        )

    # Vérification des paramètres communs.
    common_columns = (
        COMMON_NUMERIC_PARAMETERS +
        (["pattern"] if "pattern" in df.columns else [])
    )

    for col in common_columns:
        if col not in df.columns:
            continue

        values = {
            c: set(
                df.loc[df.criterion == c, col].dropna()
            )
            for c in CRITERIA
        }

        if values["distance"] != values["rssi"]:
            raise ValueError(
                f"Paramètre non identique entre critères : {col}"
            )

    return df.sort_values(
        ["criterion", "seed"]
    ).reset_index(drop=True)


def calculate_statistics(df):
    available_metrics = [
        m for m in METRICS if m in df.columns
    ]

    rows = []

    for criterion in CRITERIA:
        part = df[df["criterion"] == criterion]

        row = {
            "criterion": criterion,
            "n_runs": len(part),
            "n_seeds": part["seed"].nunique()
        }

        for metric in available_metrics:
            row[f"{metric}_mean"] = part[metric].mean()
            row[f"{metric}_std"] = part[metric].std(ddof=1)

        rows.append(row)

    return pd.DataFrame(rows)


def calculate_paired(df):
    available_metrics = [
        m for m in METRICS if m in df.columns
    ]

    wide = df.pivot(
        index="seed",
        columns="criterion",
        values=available_metrics
    )

    result = pd.DataFrame(index=wide.index)

    for metric in available_metrics:
        result[f"distance_{metric}"] = (
            wide[(metric, "distance")]
        )

        result[f"rssi_{metric}"] = (
            wide[(metric, "rssi")]
        )

        result[f"delta_{metric}"] = (
            wide[(metric, "rssi")] -
            wide[(metric, "distance")]
        )

    result.index.name = "seed"

    return result.reset_index()


def statistical_tests(paired):
    rows = []

    try:
        from scipy.stats import wilcoxon
    except ImportError:
        wilcoxon = None

    tested_metrics = [
        "nSwitch",
        "offloadTx",
        "delayMean_ms",
        "tputMean_Mbps"
    ]

    for metric in tested_metrics:
        col = f"delta_{metric}"

        if col not in paired.columns:
            continue

        delta = paired[col].dropna().astype(float)

        if wilcoxon is None:
            statistic = np.nan
            pvalue = np.nan
            note = "SciPy non installé"

        elif np.allclose(delta, 0):
            statistic = 0.0
            pvalue = 1.0
            note = "Toutes les différences sont nulles"

        else:
            try:
                result = wilcoxon(
                    delta,
                    alternative="two-sided",
                    zero_method="wilcox"
                )

                statistic = result.statistic
                pvalue = result.pvalue
                note = ""

            except Exception as exc:
                statistic = np.nan
                pvalue = np.nan
                note = str(exc)

        rows.append({
            "metric": metric,
            "n_pairs": len(delta),
            "mean_delta": delta.mean(),
            "std_delta": delta.std(ddof=1),
            "wilcoxon_statistic": statistic,
            "p_value": pvalue,
            "note": note
        })

    return pd.DataFrame(rows)


def save_plot(fig, path):
    fig.tight_layout()
    fig.savefig(
        path,
        dpi=180,
        bbox_inches="tight"
    )
    plt.close(fig)


def plot_switches(stats, paired, figures_dir):
    ordered = stats.set_index("criterion").loc[CRITERIA]

    # Moyenne et écart-type.
    fig, ax = plt.subplots(figsize=(8, 5))

    ax.bar(
        ["Distance", "RSSI"],
        ordered["nSwitch_mean"],
        yerr=ordered["nSwitch_std"].fillna(0),
        capsize=6
    )

    ax.set_ylabel("Nombre moyen de bascules")
    ax.set_title("Comparaison du nombre de bascules")
    ax.grid(axis="y", alpha=0.3)

    save_plot(
        fig,
        figures_dir / "Q7_switches_mean_std.png"
    )

    # Comparaison par graine.
    fig, ax = plt.subplots(figsize=(8, 5))

    ax.plot(
        paired["seed"],
        paired["distance_nSwitch"],
        marker="o",
        label="Distance"
    )

    ax.plot(
        paired["seed"],
        paired["rssi_nSwitch"],
        marker="s",
        label="RSSI"
    )

    ax.set_xlabel("Graine")
    ax.set_ylabel("Nombre de bascules")
    ax.set_title("Bascules par graine")
    ax.legend()
    ax.grid(alpha=0.3)

    save_plot(
        fig,
        figures_dir / "Q7_switches_by_seed.png"
    )

    # Différences appariées.
    fig, ax = plt.subplots(figsize=(8, 5))

    ax.axhline(0, linestyle="--")

    ax.bar(
        paired["seed"].astype(str),
        paired["delta_nSwitch"]
    )

    ax.set_xlabel("Graine")
    ax.set_ylabel("RSSI - Distance")
    ax.set_title("Différences appariées")
    ax.grid(axis="y", alpha=0.3)

    save_plot(
        fig,
        figures_dir / "Q7_switches_paired_delta.png"
    )


def plot_metric(
    stats,
    metric,
    ylabel,
    title,
    filename,
    figures_dir,
    scale=1.0
):
    mean_col = f"{metric}_mean"
    std_col = f"{metric}_std"

    if mean_col not in stats.columns:
        return

    ordered = stats.set_index("criterion").loc[CRITERIA]

    fig, ax = plt.subplots(figsize=(8, 5))

    ax.bar(
        ["Distance", "RSSI"],
        ordered[mean_col] * scale,
        yerr=ordered[std_col].fillna(0) * scale,
        capsize=6
    )

    ax.set_ylabel(ylabel)
    ax.set_title(title)
    ax.grid(axis="y", alpha=0.3)

    save_plot(
        fig,
        figures_dir / filename
    )


def plot_timeline(raw_dir, seed, figures_dir):
    frames = {}

    for criterion in CRITERIA:
        folder = raw_dir / criterion / f"seed_{seed}"
        files = sorted(folder.glob("*_bins.csv"))

        if not files:
            print(
                f"[INFO] bins.csv absent pour {criterion}, "
                f"seed {seed}. Timeline ignorée."
            )
            return

        frame = read_csv(files[0])

        required = {
            "t", "dist_m", "rssi_dBm", "onWifi"
        }

        if not required.issubset(frame.columns):
            print(
                f"[INFO] Colonnes temporelles manquantes dans {files[0]}"
            )
            return

        for col in required:
            frame[col] = pd.to_numeric(
                frame[col], errors="coerce"
            )

        frames[criterion] = frame.sort_values("t")

    fig, axes = plt.subplots(
        3, 1,
        figsize=(11, 9),
        sharex=True
    )

    for criterion, frame in frames.items():
        label = criterion.capitalize()

        axes[0].plot(
            frame["t"], frame["dist_m"], label=label
        )

        axes[1].plot(
            frame["t"], frame["rssi_dBm"], label=label
        )

        axes[2].step(
            frame["t"],
            frame["onWifi"],
            where="post",
            label=label
        )

    axes[0].set_ylabel("Distance (m)")
    axes[1].set_ylabel("RSSI (dBm)")
    axes[2].set_ylabel("Wi-Fi actif")
    axes[2].set_xlabel("Temps (s)")
    axes[2].set_ylim(-0.1, 1.1)

    for ax in axes:
        ax.grid(alpha=0.3)
        ax.legend()

    fig.suptitle(
        f"Évolution temporelle — graine {seed}"
    )

    save_plot(
        fig,
        figures_dir / f"Q7_timeline_seed_{seed}.png"
    )


def write_report(stats, paired, tests, out_dir):
    delta = paired["delta_nSwitch"]

    def fmt(value):
        if pd.isna(value):
            return "N/A"
        return f"{value:.4f}"

    lines = [
        "# Analyse Q7 — Distance vs RSSI",
        "",
        "## 1. Validation expérimentale",
        "",
        f"- Nombre de graines appariées : {len(paired)}.",
        f"- Nombre total de simulations : {2 * len(paired)}.",
        "- Paramètres communs vérifiés.",
        "- Absence de doublons.",
        "- Vérification de nSwitch = nIn + nOut.",
        "- Écart-type calculé avec ddof=1.",
        "",
        "## 2. Statistiques descriptives",
        "",
        stats.to_markdown(index=False, floatfmt=".4f"),
        "",
        "## 3. Différences appariées",
        "",
        "Convention : différence = RSSI - Distance.",
        "",
        paired.to_markdown(index=False, floatfmt=".4f"),
        "",
        "### Synthèse des bascules",
        "",
        f"- Différence moyenne : {fmt(delta.mean())}.",
        f"- Écart-type des différences : {fmt(delta.std(ddof=1))}.",
        f"- RSSI avec moins de bascules : {(delta < 0).sum()}/{len(delta)}.",
        f"- Même nombre de bascules : {(delta == 0).sum()}/{len(delta)}.",
        f"- RSSI avec plus de bascules : {(delta > 0).sum()}/{len(delta)}.",
        "",
        "## 4. Tests statistiques",
        "",
        tests.to_markdown(index=False, floatfmt=".4f")
        if not tests.empty else "Aucun test disponible.",
        "",
        "## 5. Interprétation",
        "",
        "Dans un scénario sans obstacles et sans shadowing, le RSSI est "
        "fortement lié à la distance géométrique. Les différences éventuelles "
        "entre les critères peuvent provenir de la réception des trames, "
        "du lissage EWMA, du délai d'expiration RSSI et de la période de contrôle.",
        "",
        "Dans un environnement urbain dense ou indoor, les obstacles, "
        "l'atténuation et les trajets multiples peuvent rendre la distance "
        "géométrique moins représentative de la qualité réelle du lien radio. "
        "Le RSSI peut alors fournir une indication plus pertinente de l'état "
        "du canal, sous réserve de filtrer ses variations rapides.",
        "",
        "Les résultats doivent être interprétés dans les limites du scénario "
        "simulé et du nombre de graines étudiées.",
        ""
    ]

    (out_dir / "Q7_analysis.md").write_text(
        "\n".join(lines),
        encoding="utf-8"
    )


def main():
    parser = argparse.ArgumentParser(
        description="Analyse expérimentale Q7 Distance vs RSSI"
    )

    script_dir = Path(__file__).resolve().parent
    ns3_dir = script_dir.parent.parent

    parser.add_argument(
        "--raw",
        type=Path,
        default=ns3_dir / "results/tp3/B7_experiment/raw"
    )

    parser.add_argument(
        "--out",
        type=Path,
        default=ns3_dir / "results/tp3/B7_experiment/analysis"
    )

    parser.add_argument(
        "--min-seeds",
        type=int,
        default=5
    )

    parser.add_argument(
        "--timeline-seed",
        type=int,
        default=1
    )

    args = parser.parse_args()

    if args.min_seeds < 2:
        parser.error("--min-seeds doit être supérieur ou égal à 2.")

    args.out.mkdir(parents=True, exist_ok=True)

    figures_dir = args.out / "figures"
    figures_dir.mkdir(parents=True, exist_ok=True)

    print("[1/7] Lecture des fichiers summary...")
    runs = discover_summaries(args.raw)

    print("[2/7] Validation des résultats...")
    runs = validate_data(runs, args.min_seeds)

    print("[3/7] Calcul des statistiques...")
    stats = calculate_statistics(runs)
    paired = calculate_paired(runs)
    tests = statistical_tests(paired)

    print("[4/7] Export CSV...")
    runs.to_csv(args.out / "Q7_all_runs.csv", index=False)
    stats.to_csv(args.out / "Q7_statistics.csv", index=False)
    paired.to_csv(args.out / "Q7_paired_by_seed.csv", index=False)
    tests.to_csv(
        args.out / "Q7_statistical_tests.csv",
        index=False
    )

    print("[5/7] Génération des graphiques...")
    plot_switches(stats, paired, figures_dir)

    plot_metric(
        stats,
        "offloadTx",
        "Offloading TX (%)",
        "Taux moyen d'offloading TX",
        "Q7_offloading_tx.png",
        figures_dir,
        scale=100
    )

    plot_metric(
        stats,
        "offloadRx",
        "Offloading RX (%)",
        "Taux moyen d'offloading RX",
        "Q7_offloading_rx.png",
        figures_dir,
        scale=100
    )

    plot_metric(
        stats,
        "delayMean_ms",
        "Délai moyen (ms)",
        "Délai moyen",
        "Q7_delay_mean.png",
        figures_dir
    )

    plot_metric(
        stats,
        "tputMean_Mbps",
        "Débit moyen (Mbps)",
        "Débit moyen",
        "Q7_throughput_mean.png",
        figures_dir
    )

    print("[6/7] Génération de la timeline...")
    plot_timeline(
        args.raw,
        args.timeline_seed,
        figures_dir
    )

    print("[7/7] Génération du rapport...")
    write_report(stats, paired, tests, args.out)

    print("\nAnalyse Q7 terminée avec succès.")
    print(f"Résultats : {args.out}")
    print(f"Graphiques : {figures_dir}")

    print("\nRésumé des bascules :")
    print(
        stats[
            [
                "criterion",
                "n_runs",
                "n_seeds",
                "nSwitch_mean",
                "nSwitch_std"
            ]
        ].to_string(index=False)
    )


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"\nERREUR : {exc}", file=sys.stderr)
        sys.exit(2)
