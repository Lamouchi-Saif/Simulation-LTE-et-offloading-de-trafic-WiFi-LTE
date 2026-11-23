import glob, os
import numpy as np, pandas as pd

D, RATE, PKT = "results/tp3/q9", 2.0, 1200
NOM = RATE * 1e6 / 8 * 0.1          # nominal bytes per 100 ms bin

def interruptions(tag):
    """Per-switch interruption: contiguous bins with rx < 50% nominal near each switch."""
    b = pd.read_csv(f"{D}/{tag}_bins.csv")
    s = pd.read_csv(f"{D}/{tag}_switches.csv")
    rx = (b.rxWifi_B + b.rxLte_B).values
    tx = (b.txWifi_B + b.txLte_B).values
    low = rx < 0.5 * NOM
    out = []
    for t in s.t:
        i = int(round(t * 10))
        lo, hi = max(i - 2, 0), min(i + 30, len(b) - 1)
        start = next((k for k in range(lo, hi + 1) if low[k]), None)
        if start is None:
            out.append((0.0, 0, b.delayMax_ms[lo:hi + 1].max())); continue
        end = start
        while end + 1 < len(b) and low[end + 1]:
            end += 1
        lost = max(0, (tx[start:end + 1].sum() - rx[start:end + 1].sum()) / PKT)
        out.append(((end - start + 1) * 0.1, lost, b.delayMax_ms[start:end + 1].max()))
    return out

rows, intr = [], []
for f in sorted(glob.glob(f"{D}/*_summary.csv")):
    tag = os.path.basename(f).replace("_summary.csv", "")
    cfg = tag.rsplit("_r", 1)[0]
    r = pd.read_csv(f).iloc[0].to_dict(); r["cfg"] = cfg
    rows.append(r)
    for d, l, p in interruptions(tag):
        intr.append(dict(cfg=cfg, dur_s=d, lost=l, peak_ms=p))

df = pd.DataFrame(rows)
ms = lambda s: f"{s.mean():.2f} ± {s.std(ddof=1):.2f}"
cols = ["nSwitch", "nPingPong", "offloadRx", "lossPct", "delayMean_ms", "outage_s", "tputMean_Mbps"]
tab = df.groupby("cfg")[cols].agg(ms)

if intr:
    idf = pd.DataFrame(intr).groupby("cfg")[["dur_s", "lost", "peak_ms"]].agg(ms)
    idf.columns = ["interruption_s/switch", "lost_pkts/switch", "peak_delay_ms"]
    tab = tab.join(idf)

print(tab.to_string())
tab.to_csv(f"{D}/q9_table.csv")
