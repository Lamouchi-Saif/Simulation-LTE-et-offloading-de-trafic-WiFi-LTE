#!/usr/bin/env bash
set -Eeuo pipefail

# ============================================================
# TP3 - B5 : effet de l'hysteresis (parallele, parametrable)
# Code C++ : scratch/tp3-offload.cc (inchange)
# ============================================================

NS3_ROOT="${NS3_DIR:-$HOME/ns-3.48}"
OUT="$NS3_ROOT/results/tp3/B5"

HYSTS="0 2 5 8"       # demi-largeurs d'hysteresis H (m)
RUNS=10               # nombre de runs independants
SEED=1                # graine fixe, c'est --run qui varie
RADIUS=50             # rayon Wi-Fi R (m)
AMP=3                 # amplitude d'oscillation (m)
SPEED=2               # vitesse (m/s)
SIM_TIME=80
NOISE=2               # bruit de mesure de distance (m)
SHADOW=0              # shadowing (dB)
CHECK=0.5             # periode de verification (s)
PPWIN=5               # fenetre ping-pong (s)
RATE=2                # debit CBR (Mb/s)
JOBS=$(nproc)         # parallelisme
BUILD=1

usage() {
  cat <<EOF
Usage: $0 [options]
  --hysts "0 2 5 8"   --runs N      --seed N     --radius M
  --amp M             --speed V     --sim-time S --noise M
  --shadow DB         --check S     --ppwin S    --rate MBPS
  --jobs N            --out DIR     --no-build   -h
EOF
}

while (($#)); do
  case "$1" in
    --hysts) HYSTS="$2"; shift 2;;
    --runs) RUNS="$2"; shift 2;;
    --seed) SEED="$2"; shift 2;;
    --radius) RADIUS="$2"; shift 2;;
    --amp) AMP="$2"; shift 2;;
    --speed) SPEED="$2"; shift 2;;
    --sim-time) SIM_TIME="$2"; shift 2;;
    --noise) NOISE="$2"; shift 2;;
    --shadow) SHADOW="$2"; shift 2;;
    --check) CHECK="$2"; shift 2;;
    --ppwin) PPWIN="$2"; shift 2;;
    --rate) RATE="$2"; shift 2;;
    --jobs) JOBS="$2"; shift 2;;
    --out) OUT="$2"; shift 2;;
    --no-build) BUILD=0; shift;;
    -h|--help) usage; exit 0;;
    *) echo "Option inconnue: $1" >&2; usage; exit 1;;
  esac
done

NS3="$NS3_ROOT/ns3"
[[ -x "$NS3" ]] || { echo "ERREUR: $NS3 introuvable" >&2; exit 1; }
[[ -f "$NS3_ROOT/scratch/tp3-offload.cc" ]] || { echo "ERREUR: scratch/tp3-offload.cc absent" >&2; exit 1; }

mkdir -p "$OUT/raw" "$OUT/logs"
cd "$NS3_ROOT"

{
  echo "Question=B5"; echo "hysts=$HYSTS"; echo "runs=$RUNS"; echo "seed=$SEED"
  echo "radius=$RADIUS"; echo "amp=$AMP"; echo "speed=$SPEED"; echo "simTime=$SIM_TIME"
  echo "noise=$NOISE"; echo "shadow=$SHADOW"; echo "check=$CHECK"; echo "ppwin=$PPWIN"
  echo "rate=$RATE"
} > "$OUT/config.txt"

(( BUILD )) && ./ns3 build

run_one() {
  local H="${1%%|*}" run="${1##*|}"
  local tag="B5_H${H}_run${run}"
  local bins=0
  [[ "$run" == "1" ]] && bins=1     # CSV 100 ms seulement pour le run 1 (courbes)
  "$NS3" run --no-build "scratch/tp3-offload \
    --criterion=distance --pattern=border \
    --apRadius=$RADIUS --borderAmp=$AMP --hyst=$H \
    --speed=$SPEED --simTime=$SIM_TIME \
    --checkPeriod=$CHECK --pingPongWin=$PPWIN \
    --offset=0 --offsetFrac=0 \
    --shadowSigma=$SHADOW --distNoise=$NOISE \
    --rate=$RATE --seed=$SEED --run=$run \
    --saveBins=$bins --flowmon=true --pcap=false \
    --outDir=$OUT/raw --tag=$tag" > "$OUT/logs/$tag.log" 2>&1 \
    && echo "OK  $tag" || echo "ECHEC $tag"
}
export -f run_one
export NS3 OUT RADIUS AMP SPEED SIM_TIME CHECK PPWIN NOISE SHADOW RATE SEED

echo "Simulations : $(( $(wc -w <<< "$HYSTS") * RUNS )) | jobs : $JOBS"

for H in $HYSTS; do
  for ((r=1; r<=RUNS; r++)); do echo "$H|$r"; done
done | xargs -P "$JOBS" -I{} bash -c 'run_one "$1"' _ {}

N=$(ls "$OUT"/raw/*_summary.csv 2>/dev/null | wc -l)
echo "Summary produits : $N"