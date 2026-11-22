#!/usr/bin/env bash
set -Eeuo pipefail

# Campagne B7 : comparaison distance vs RSSI, 5 graines appariées.
# À placer dans ns-3.48/script/tp3/ ou lancer avec NS3_DIR et SOURCE_FILE.
# Les paramètres sont surchargeables par variables d'environnement.

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
NS3_DIR="${NS3_DIR:-$HOME/ns-3.48}"
SOURCE_FILE="${SOURCE_FILE:-$NS3_DIR/scratch/tp3-offload.cc}"
NS3_BIN="$NS3_DIR/ns3"

RADIUS="${RADIUS:-50}"
HYST="${HYST:-5}"
SPEED="${SPEED:-5}"
SIM_TIME="${SIM_TIME:-80}"
BORDER_AMP="${BORDER_AMP:-10}"
OFFSET_FRAC="${OFFSET_FRAC:-0.30}"
SHADOW_SIGMA="${SHADOW_SIGMA:-0}"
DIST_NOISE="${DIST_NOISE:-0}"
EDGE_RSSI="${EDGE_RSSI:--75}"
EWMA="${EWMA:-0.3}"
CHECK_PERIOD="${CHECK_PERIOD:-0.5}"
RSSI_TIMEOUT="${RSSI_TIMEOUT:-1}"
SEEDS="${SEEDS:-1 2 3 4 5}"
WORKERS="${WORKERS:-5}"
BUILD="${BUILD:-1}"
PCAP="${PCAP:-0}"
FLOWMON="${FLOWMON:-1}"
SAVE_BINS="${SAVE_BINS:-1}"
OUT_DIR="${OUT_DIR:-$NS3_DIR/results/tp3/B7_experiment}"

[[ -x "$NS3_BIN" ]] || { echo "ERREUR: ns-3 introuvable: $NS3_BIN" >&2; exit 1; }
[[ -f "$SOURCE_FILE" ]] || { echo "ERREUR: source introuvable: $SOURCE_FILE" >&2; exit 1; }
[[ "$WORKERS" =~ ^[1-9][0-9]*$ ]] || { echo "ERREUR: WORKERS doit être un entier positif." >&2; exit 1; }
[[ "$BUILD" == 0 || "$BUILD" == 1 ]] || { echo "ERREUR: BUILD doit valoir 0 ou 1." >&2; exit 1; }

mkdir -p "$NS3_DIR/scratch" "$OUT_DIR/logs"
cd "$NS3_DIR"

if [[ "$BUILD" == 1 ]]; then
  echo "=== Compilation NS-3.48 ==="
  ./ns3 build
fi

read -r -a SEED_LIST <<< "$SEEDS"
((${#SEED_LIST[@]} >= 5)) || { echo "ERREUR: au moins cinq graines sont requises." >&2; exit 1; }

# Tous les paramètres physiques sont identiques entre les deux critères.
COMMON=(
  "--pattern=border" "--apRadius=$RADIUS" "--hyst=$HYST"
  "--speed=$SPEED" "--simTime=$SIM_TIME" "--borderAmp=$BORDER_AMP"
  "--offsetFrac=$OFFSET_FRAC" "--shadowSigma=$SHADOW_SIGMA"
  "--distNoise=$DIST_NOISE" "--edgeRssi=$EDGE_RSSI"
  "--ewma=$EWMA" "--checkPeriod=$CHECK_PERIOD"
  "--rssiTimeout=$RSSI_TIMEOUT" "--pcap=$PCAP"
  "--flowmon=$FLOWMON" "--saveBins=$SAVE_BINS"
)

pids=()
launch_job() {
  local criterion="$1" seed="$2"
  local tag="${criterion}_seed${seed}"
  local dir="$OUT_DIR/raw/${criterion}/seed_${seed}"
  mkdir -p "$dir"
  echo "Lancement: critère=$criterion seed=$seed"
  ./ns3 run "scratch/tp3-offload --criterion=$criterion --seed=$seed --run=1 --outDir=$dir --tag=$tag ${COMMON[*]}" \
    >"$OUT_DIR/logs/${tag}.log" 2>&1 &
  pids+=("$!")
}
wait_one() {
  local pid="${pids[0]}"
  if ! wait "$pid"; then
    echo "ERREUR: une simulation a échoué (PID=$pid). Consulte $OUT_DIR/logs/." >&2
    exit 1
  fi
  pids=("${pids[@]:1}")
}

for criterion in distance rssi; do
  for seed in "${SEED_LIST[@]}"; do
    [[ "$seed" =~ ^[1-9][0-9]*$ ]] || { echo "Graine invalide: $seed" >&2; exit 1; }
    launch_job "$criterion" "$seed"
    if ((${#pids[@]} >= WORKERS)); then wait_one; fi
  done
done
while ((${#pids[@]})); do wait_one; done

echo "Campagne terminée. Résultats bruts : $OUT_DIR/raw"
echo "Journaux : $OUT_DIR/logs"
echo "Analyse : python3 $NS3_DIR/script/tp3/analyze_b7.py $OUT_DIR/raw --out $OUT_DIR/analysis"
