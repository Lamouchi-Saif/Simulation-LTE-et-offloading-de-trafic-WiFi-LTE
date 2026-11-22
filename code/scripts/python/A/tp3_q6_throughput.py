import pandas as pd
import matplotlib.pyplot as plt

df = pd.read_csv("Q6_Throughput.txt")

plt.figure(figsize=(12, 6))

plt.plot(df["Time"], df["DownlinkMbps"], label="Downlink")
plt.plot(df["Time"], df["UplinkMbps"], label="Uplink")

plt.axvline(
    x=35.84,
    color="red",
    linestyle="--",
    label="Handover (35.84 s)"
)

plt.xlim(34, 38)
plt.xlabel("Time (seconds)")
plt.ylabel("Throughput (Mbps)")
plt.title("Throughput Around the LTE Handover")
plt.legend()
plt.grid(True)
plt.tight_layout()

plt.savefig("q6_throughput.png", dpi=300)
plt.show()