#!/usr/bin/env python3
"""
Run the NS-3 Q5 RandomWalk2d simulations in parallel:
every speed is simulated with several independent RNG runs, and the results
are aggregated (mean, standard deviation, 95% confidence interval).

Place this file in the NS-3 root directory, beside the ./ns3 script.
"""

import csv
import itertools
import re
import statistics
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

NS3_DIR = Path(__file__).resolve().parent
NS3 = NS3_DIR / "ns3"
PROGRAM = "tp3_q5_parallel"

SPEEDS = [5, 10, 20, 30, 50]
RUNS = [1, 2, 3, 4, 5]          # 5 independent replications per speed

# Total jobs = len(SPEEDS) * len(RUNS) = 25. Keep this <= (CPU cores - 1).
MAX_WORKERS = 6

RESULTS_DIR = NS3_DIR / "q5_parallel_results"

# Student t critical values (two-sided 95%) indexed by degrees of freedom.
T_95 = {1: 12.706, 2: 4.303, 3: 3.182, 4: 2.776, 5: 2.571,
        6: 2.447, 7: 2.365, 8: 2.306, 9: 2.262}

FLOW_PATTERN = re.compile(
    r"Flow:\s*(Downlink|Uplink).*?"
    r"Transmitted packets:\s*(\d+).*?"
    r"Received packets:\s*(\d+).*?"
    r"Lost packets:\s*(\d+).*?"
    r"Packet loss:\s*([0-9.eE+-]+)\s*%.*?"
    r"Average throughput:\s*([0-9.eE+-]+)\s*Mbps",
    re.DOTALL,
)


def build_program():
    if not NS3.exists():
        raise FileNotFoundError(
            f"Could not find {NS3}. Put this Python script in the NS-3 root directory."
        )
    print("Building the NS-3 program once...")
    subprocess.run([str(NS3), "build"], cwd=NS3_DIR, check=True)
    print("Build completed.\n")


def run_simulation(speed, run):
    """Run one (speed, run) experiment in its own output folder."""
    output_dir = RESULTS_DIR / f"speed_{speed}" / f"run_{run}"
    output_dir.mkdir(parents=True, exist_ok=True)

    command = [
        str(NS3),
        "run",
        "--no-build",
        f"{PROGRAM} --speed={speed} --run={run} --outputDir={output_dir}",
    ]

    print(f"[START] speed={speed} m/s, run={run}")

    completed = subprocess.run(command, cwd=NS3_DIR, capture_output=True, text=True)

    log_path = output_dir / "simulation.log"
    log_path.write_text(
        completed.stdout + "\n\n--- STDERR ---\n" + completed.stderr,
        encoding="utf-8",
    )

    base = {"speed": speed, "run": run}

    if completed.returncode != 0:
        return {**base, "ok": False,
                "error": f"NS-3 exited with code {completed.returncode}. Check {log_path}"}

    flows = {}
    for match in FLOW_PATTERN.finditer(completed.stdout):
        name, tx, rx, lost, loss_pct, throughput = match.groups()
        flows[name.lower()] = {
            "tx_packets": int(tx),
            "rx_packets": int(rx),
            "lost_packets": int(lost),
            "packet_loss_pct": float(loss_pct),
            "throughput_mbps": float(throughput),
        }

    if "downlink" not in flows or "uplink" not in flows:
        return {**base, "ok": False,
                "error": f"Could not parse both FlowMonitor flows. Check {log_path}"}

    print(f"[DONE]  speed={speed} m/s, run={run}")
    return {**base, "ok": True,
            "downlink": flows["downlink"], "uplink": flows["uplink"]}


def mean_std_ci(values):
    """Return (mean, sample std dev, 95% CI half-width)."""
    n = len(values)
    mean = statistics.mean(values)
    if n < 2:
        return mean, 0.0, 0.0
    std = statistics.stdev(values)  # sample std (n-1)
    t = T_95.get(n - 1, 1.96)
    return mean, std, t * std / (n ** 0.5)


def save_raw_csv(results):
    path = RESULTS_DIR / "q5_raw_results.csv"
    fields = ["speed_mps", "run",
              "dl_throughput_mbps", "ul_throughput_mbps",
              "dl_loss_pct", "ul_loss_pct",
              "dl_tx", "dl_rx", "ul_tx", "ul_rx"]
    with path.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        for r in sorted(results, key=lambda x: (x["speed"], x["run"])):
            if not r["ok"]:
                continue
            w.writerow({
                "speed_mps": r["speed"], "run": r["run"],
                "dl_throughput_mbps": r["downlink"]["throughput_mbps"],
                "ul_throughput_mbps": r["uplink"]["throughput_mbps"],
                "dl_loss_pct": r["downlink"]["packet_loss_pct"],
                "ul_loss_pct": r["uplink"]["packet_loss_pct"],
                "dl_tx": r["downlink"]["tx_packets"],
                "dl_rx": r["downlink"]["rx_packets"],
                "ul_tx": r["uplink"]["tx_packets"],
                "ul_rx": r["uplink"]["rx_packets"],
            })
    print(f"Raw CSV saved: {path}")


def aggregate(results):
    """Group successful runs by speed and compute statistics."""
    summary = []
    for speed in SPEEDS:
        ok = [r for r in results if r["ok"] and r["speed"] == speed]
        if not ok:
            continue
        row = {"speed": speed, "n": len(ok)}
        for key, direction, metric in [
            ("dl_thr", "downlink", "throughput_mbps"),
            ("ul_thr", "uplink", "throughput_mbps"),
            ("dl_loss", "downlink", "packet_loss_pct"),
            ("ul_loss", "uplink", "packet_loss_pct"),
        ]:
            values = [r[direction][metric] for r in ok]
            row[key] = mean_std_ci(values)
        summary.append(row)
    return summary


def save_summary_csv(summary):
    path = RESULTS_DIR / "q5_summary_mean_std.csv"
    fields = ["speed_mps", "n_runs"]
    for k in ["dl_throughput", "ul_throughput", "dl_loss", "ul_loss"]:
        fields += [f"{k}_mean", f"{k}_std", f"{k}_ci95"]
    with path.open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(fields)
        for row in summary:
            line = [row["speed"], row["n"]]
            for k in ["dl_thr", "ul_thr", "dl_loss", "ul_loss"]:
                line += [f"{v:.6f}" for v in row[k]]
            w.writerow(line)
    print(f"Summary CSV saved: {path}")


def save_graphs(summary):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("Matplotlib not installed: python -m pip install matplotlib")
        return
    if not summary:
        print("No successful runs; graphs were not created.")
        return

    speeds = [r["speed"] for r in summary]

    def plot(dl_key, ul_key, ylabel, title, filename):
        plt.figure(figsize=(9, 5))
        for key, label in [(dl_key, "Downlink"), (ul_key, "Uplink")]:
            means = [r[key][0] for r in summary]
            stds = [r[key][1] for r in summary]
            plt.errorbar(speeds, means, yerr=stds, marker="o",
                         capsize=4, label=label)
        plt.xlabel("UE speed (m/s)")
        plt.ylabel(ylabel)
        plt.title(title)
        plt.grid(True)
        plt.legend()
        plt.tight_layout()
        out = RESULTS_DIR / filename
        plt.savefig(out, dpi=150)
        plt.close()
        print(f"Graph saved: {out}")

    n = summary[0]["n"]
    plot("dl_thr", "ul_thr", "Average throughput (Mbps)",
         f"Q5: Throughput vs UE speed (mean ± std, {n} runs)",
         "q5_throughput_vs_speed.png")
    plot("dl_loss", "ul_loss", "Packet loss (%)",
         f"Q5: Packet loss vs UE speed (mean ± std, {n} runs)",
         "q5_packet_loss_vs_speed.png")


def main():
    RESULTS_DIR.mkdir(parents=True, exist_ok=True)

    try:
        build_program()
    except (subprocess.CalledProcessError, FileNotFoundError) as error:
        print(f"Build failed: {error}")
        sys.exit(1)

    jobs = list(itertools.product(SPEEDS, RUNS))
    print(f"Starting {len(jobs)} simulations "
          f"({len(SPEEDS)} speeds x {len(RUNS)} runs), "
          f"at most {MAX_WORKERS} at once.\n")

    results = []
    with ThreadPoolExecutor(max_workers=MAX_WORKERS) as executor:
        futures = {executor.submit(run_simulation, s, r): (s, r) for s, r in jobs}
        for future in as_completed(futures):
            s, r = futures[future]
            try:
                results.append(future.result())
            except Exception as error:
                results.append({"speed": s, "run": r, "ok": False, "error": str(error)})

    summary = aggregate(results)

    print("\n========== SUMMARY (mean ± std over runs) ==========")
    for row in summary:
        print(f"Speed {row['speed']:>2} m/s (n={row['n']}) | "
              f"DL: {row['dl_thr'][0]:.4f} ± {row['dl_thr'][1]:.4f} Mbps | "
              f"UL: {row['ul_thr'][0]:.4f} ± {row['ul_thr'][1]:.4f} Mbps")

    save_raw_csv(results)
    save_summary_csv(summary)
    save_graphs(summary)

    failed = [r for r in results if not r["ok"]]
    if failed:
        print(f"\n{len(failed)} simulation(s) failed:")
        for r in sorted(failed, key=lambda x: (x["speed"], x["run"])):
            print(f"  speed={r['speed']} run={r['run']}: {r['error']}")
        sys.exit(1)

    print("\nAll simulations completed successfully.")


if __name__ == "__main__":
    main()