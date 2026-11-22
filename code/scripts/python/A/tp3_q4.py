#!/usr/bin/env python3
"""Automate TP3 Q4: run NS-3 at several distances and plot throughput/RSRP."""

from pathlib import Path
import csv
import math
import subprocess
import sys
import matplotlib.pyplot as plt

BASE_DIR = Path(__file__).resolve().parent
NS3 = BASE_DIR / "ns3"
PROGRAM = "tp3_q1"
DISTANCES = [100, 300, 500, 1000, 1500, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000]  # metres; change if needed

DL_FILE = BASE_DIR / "DlRlcStats.txt"
UL_FILE = BASE_DIR / "UlRlcStats.txt"
RSRP_FILE = BASE_DIR / "DlRsrpSinrStats.txt"


def read_rlc_throughput(filename):
    total_bytes = 0
    first_start = None
    last_end = None

    with filename.open(encoding="utf-8") as f:
        for line in f:
            cols = line.split()
            if not cols or cols[0].startswith("%") or len(cols) < 10:
                continue
            try:
                start, end = float(cols[0]), float(cols[1])
                rx_bytes = int(cols[9])  # RxBytes
            except ValueError:
                continue
            total_bytes += rx_bytes
            first_start = start if first_start is None else min(first_start, start)
            last_end = end if last_end is None else max(last_end, end)

    if first_start is None or last_end is None or last_end <= first_start:
        raise RuntimeError(f"No valid RLC data in {filename.name}")
    return total_bytes * 8 / (last_end - first_start) / 1_000_000


def read_average_rsrp_dbm(filename):
    rsrp_watts = []
    with filename.open(encoding="utf-8") as f:
        for line in f:
            cols = line.split()
            if not cols or cols[0].startswith("%") or len(cols) < 5:
                continue
            try:
                value = float(cols[4])  # RSRP is column 5
            except ValueError:
                continue
            if value > 0 and math.isfinite(value):
                rsrp_watts.append(value)

    if not rsrp_watts:
        raise RuntimeError(f"No valid RSRP data in {filename.name}")

    # Average linear power first, then convert the mean to dBm.
    mean_watts = sum(rsrp_watts) / len(rsrp_watts)
    return 10 * math.log10(mean_watts * 1000)


def run_one_distance(distance):
    command = [str(NS3), "run", f"{PROGRAM} --distance={distance}"]
    print(f"\nRunning at {distance} m: {' '.join(command)}")
    subprocess.run(command, cwd=BASE_DIR, check=True)

    for file in (DL_FILE, UL_FILE, RSRP_FILE):
        if not file.exists():
            raise RuntimeError(f"{file.name} was not generated.")

    # Read the files immediately; the next run overwrites them.
    return {
        "distance_m": distance,
        "dl_mbps": read_rlc_throughput(DL_FILE),
        "ul_mbps": read_rlc_throughput(UL_FILE),
        "rsrp_dbm": read_average_rsrp_dbm(RSRP_FILE),
    }


def main():
    if not NS3.exists():
        sys.exit("Put this Python file in the NS-3.48 root directory, beside ./ns3.")

    results = []
    try:
        for distance in DISTANCES:
            results.append(run_one_distance(distance))
    except (subprocess.CalledProcessError, OSError, RuntimeError) as error:
        sys.exit(f"\nStopped: {error}")

    csv_path = BASE_DIR / "q4_distance_results.csv"
    with csv_path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(
            f, fieldnames=["distance_m", "dl_mbps", "ul_mbps", "rsrp_dbm"]
        )
        writer.writeheader()
        writer.writerows(results)

    x = [r["distance_m"] for r in results]
    dl = [r["dl_mbps"] for r in results]
    ul = [r["ul_mbps"] for r in results]
    rsrp = [r["rsrp_dbm"] for r in results]

    fig, ax = plt.subplots(figsize=(9, 5))
    ax.plot(x, dl, marker="o", label="Downlink")
    ax.plot(x, ul, marker="s", label="Uplink")
    ax.set(title="LTE Throughput vs UE-eNodeB Distance",
           xlabel="Distance (m)", ylabel="Average throughput (Mbps)")
    ax.grid(True)
    ax.legend()
    fig.tight_layout()
    fig.savefig(BASE_DIR / "q4_throughput_vs_distance.png", dpi=150)

    fig, ax = plt.subplots(figsize=(9, 5))
    ax.plot(x, rsrp, marker="o")
    ax.set(title="LTE RSRP vs UE-eNodeB Distance",
           xlabel="Distance (m)", ylabel="Average RSRP (dBm)")
    ax.grid(True)
    fig.tight_layout()
    fig.savefig(BASE_DIR / "q4_rsrp_vs_distance.png", dpi=150)

    print("\nQ4 RESULTS")
    print("Distance (m) | DL (Mbps) | UL (Mbps) | Average RSRP (dBm)")
    print("-" * 65)
    for r in results:
        print(f'{r["distance_m"]:12.1f} | {r["dl_mbps"]:9.6f} | '
              f'{r["ul_mbps"]:9.6f} | {r["rsrp_dbm"]:18.3f}')

    print("\nCreated:")
    print("  q4_distance_results.csv")
    print("  q4_throughput_vs_distance.png")
    print("  q4_rsrp_vs_distance.png")
    plt.show()


if __name__ == "__main__":
    main()
