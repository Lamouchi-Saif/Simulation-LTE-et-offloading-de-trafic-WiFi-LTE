import pandas as pd
import matplotlib.pyplot as plt
import numpy as np

# Read the RSRP trace
df = pd.read_csv(
    "DlRsrpSinrStats.txt",
    sep=r"\s+",
    comment="%",
    header=None,
    names=["Time", "CellId", "IMSI", "RNTI",
           "RSRP", "SINR", "ComponentCarrierId"]
)

# Convert RSRP from Watts to dBm
df["RSRP_dBm"] = 10 * np.log10(df["RSRP"] * 1000)

# Keep only the UE's measurements
df = df[df["IMSI"] == 1]

# Plot RSRP for each cell
plt.figure(figsize=(12, 6))

for cell_id in sorted(df["CellId"].unique()):
    cell_data = df[df["CellId"] == cell_id]
    plt.plot(
        cell_data["Time"],
        cell_data["RSRP_dBm"],
        label=f"eNodeB {cell_id}"
    )

# Mark the handover
plt.axvline(
    x=35.84,
    color="red",
    linestyle="--",
    label="Handover (35.84 s)"
)

plt.xlabel("Time (seconds)")
plt.ylabel("RSRP (dBm)")
plt.title("UE RSRP from Both eNodeBs During Handover")
plt.legend()
plt.grid(True)
plt.tight_layout()

plt.savefig("q6_rsrp.png", dpi=300)
plt.show()