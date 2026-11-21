
#!/usr/bin/env python3

import argparse
from pathlib import Path

import pandas as pd
import matplotlib.pyplot as plt


def main():
    parser = argparse.ArgumentParser(
        description="Analyse des résultats expérimentaux B5"
    )
    parser.add_argument(
        "--input",
        default=str(Path.home() / "ns-3.48/results/tp3/B5_experiment"),
        help="Répertoire contenant les CSV B5"
    )
    parser.add_argument(
        "--output",
        default=None,
        help="Répertoire de sortie des graphiques"
    )
    args = parser.parse_args()

    input_dir = Path(args.input)
    output_dir = Path(args.output) if args.output else input_dir / "analysis"
    output_dir.mkdir(parents=True, exist_ok=True)

    files = sorted(input_dir.glob("B5_H*_run*_summary.csv"))

    if not files:
        raise FileNotFoundError(
            f"Aucun fichier B5_H*_run*_summary.csv dans {input_dir}"
        )

    data = pd.concat(
        [pd.read_csv(file).assign(source=file.name) for file in files],
        ignore_index=True
    )

    required = [
        "hyst", "run", "nSwitch", "nPingPong",
        "offloadRx", "tputMean_Mbps", "tputCV",
        "lossPct", "delayMean_ms", "delayMax_ms", "outage_s"
    ]

    missing = [column for column in required if column not in data.columns]
    if missing:
        raise ValueError(f"Colonnes manquantes : {missing}")

    data["configuration"] = data["hyst"].map(
        lambda h: f"H={h:g} m"
    )

    metrics = [
        "nSwitch",
        "nPingPong",
        "offloadRx",
        "tputMean_Mbps",
        "tputCV",
        "lossPct",
        "delayMean_ms",
        "delayMax_ms",
        "outage_s"
    ]

    summary = data.groupby("configuration")[metrics].agg(
        ["mean", "std"]
    )

    print("\n========== DONNÉES PAR RUN ==========")
    print(data[["configuration", "run"] + metrics].to_string(index=False))

    print("\n========== MOYENNES ET ÉCARTS-TYPES ==========")
    print(summary.round(6).to_string())

    summary.to_csv(output_dir / "B5_statistics.csv")
    data.to_csv(output_dir / "B5_all_runs.csv", index=False)

    # Graphique 1 : nombre de bascules
    for metric, ylabel, filename in [
        ("nSwitch", "Nombre de bascules", "B5_switches.png"),
        ("nPingPong", "Nombre de ping-pong", "B5_pingpong.png"),
        ("offloadRx", "Taux d'offloading reçu", "B5_offloading.png"),
        ("tputCV", "Coefficient de variation du débit", "B5_throughput_cv.png"),
    ]:
        grouped = data.groupby("configuration")[metric]
        means = grouped.mean()
        stds = grouped.std().fillna(0)

        plt.figure(figsize=(7, 5))
        plt.bar(
            means.index,
            means.values,
            yerr=stds.values,
            capsize=5
        )
        plt.ylabel(ylabel)
        plt.title(f"B5 - Comparaison {ylabel.lower()}")
        plt.grid(axis="y", alpha=0.3)
        plt.tight_layout()
        plt.savefig(output_dir / filename, dpi=300)
        plt.close()

    print(f"\nAnalyse terminée : {output_dir}")


if __name__ == "__main__":
    main()
