
import pandas as pd
import matplotlib.pyplot as plt

# Read distance data
df = pd.read_csv("UeDistances.txt")

# Plot distances
plt.figure(figsize=(10, 6))

plt.plot(
    df["Time"],
    df["DistanceToEnb1"],
    label="Distance to eNodeB 1"
)

plt.plot(
    df["Time"],
    df["DistanceToEnb2"],
    label="Distance to eNodeB 2"
)

# Mark the handover
plt.axvline(
    x=35.84,
    color="red",
    linestyle="--",
    label="Handover (35.84 s)"
)

plt.xlabel("Time (seconds)")
plt.ylabel("Distance (meters)")
plt.title("UE Distance from Both eNodeBs Over Time")

plt.legend()
plt.grid(True)

plt.tight_layout()
plt.savefig("q6_distances.png", dpi=300)
plt.show()
