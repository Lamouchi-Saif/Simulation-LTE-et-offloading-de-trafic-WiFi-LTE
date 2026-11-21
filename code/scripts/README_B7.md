# TP3 — Question B7 (NS-3.48)

## Contenu
- `scripts/run_b7.sh` : compilation optionnelle et campagne de 10 simulations (distance/RSSI × 5 graines).
- `scripts/analyze_b7.py` : validation des CSV, statistiques moyenne/écart-type, comparaison appariée et figures.

## Paramètres par défaut
`pattern=border`, `apRadius=50 m`, `hyst=5 m`, `speed=5 m/s`, `simTime=80 s`, `borderAmp=10 m`, `offsetFrac=0.30`, `shadowSigma=0`, `distNoise=0`, `seed=1..5`, `run=1`.

Le décalage latéral aléatoire permet aux graines de produire cinq géométries différentes sans ajouter de shadowing au canal. Le même jeu de graines est appliqué aux deux critères.

## Installation dans ns-3.48
Copier le code C++ fourni dans `ns-3.48/script/tp3/tp3-offload.cc`, puis les deux scripts dans `ns-3.48/script/tp3/` :

```bash
cp tp3-offload.cc "$HOME/ns-3.48/script/tp3/tp3-offload.cc"
cp scripts/run_b7.sh scripts/analyze_b7.py "$HOME/ns-3.48/script/tp3/"
chmod +x "$HOME/ns-3.48/script/tp3/"{run_b7.sh,analyze_b7.py}
```

Lancer la campagne :

```bash
cd "$HOME/ns-3.48/script/tp3"
./run_b7.sh
```

Le script copie la source dans `scratch/tp3-offload.cc`, compile une fois, puis lance les simulations avec jusqu'à cinq processus NS-3 simultanés.

## Analyse

```bash
python3 "$HOME/ns-3.48/script/tp3/analyze_b7.py" \
  "$HOME/ns-3.48/results/tp3/B7_experiment/raw" \
  --out "$HOME/ns-3.48/results/tp3/B7_experiment/analysis"
```

## Paramètres modifiables depuis le terminal

```bash
WORKERS=3 RADIUS=50 HYST=5 SPEED=5 SIM_TIME=80 \
OFFSET_FRAC=0.30 SEEDS="11 12 13 14 15" BUILD=0 \
./run_b7.sh
```

Variables disponibles : `NS3_DIR`, `SOURCE_FILE`, `RADIUS`, `HYST`, `SPEED`, `SIM_TIME`, `BORDER_AMP`, `OFFSET_FRAC`, `SHADOW_SIGMA`, `DIST_NOISE`, `EDGE_RSSI`, `EWMA`, `CHECK_PERIOD`, `RSSI_TIMEOUT`, `SEEDS`, `WORKERS`, `BUILD`, `PCAP`, `FLOWMON`, `SAVE_BINS`, `OUT_DIR`.

## Résultats attendus
- `raw/distance/seed_N/` et `raw/rssi/seed_N/` : résumés et traces CSV de chaque run.
- `logs/` : journal de chaque simulation.
- `analysis/B7_statistics.csv` : moyenne et écart-type par critère.
- `analysis/B7_paired_by_seed.csv` : différence RSSI − distance pour chaque graine.
- `analysis/B7_analysis.md` et trois figures PNG.

L'écart-type est l'écart-type échantillonnal (`ddof=1`). Le script refuse de conclure si moins de cinq graines ou si les graines ne sont pas appariées.
