#!/usr/bin/env bash
set -Eeuo pipefail

# ============================================================
# TP3 - B5 : comparaison seuil unique / hystérésis
# Aucun changement du fichier C++.
# ============================================================

NS3_DIR="${NS3_DIR:-$HOME/ns-3.48/}"
RESULTS_DIR="${RESULTS_DIR:-$NS3_DIR/results/tp3/B5_experiment}"

# Paramètres modifiables depuis le terminal
R="${R:-50}"                       # Rayon Wi-Fi (m)
AMP="${AMP:-10}"                   # Amplitude oscillation (m)
SPEED="${SPEED:-5}"                # Vitesse (m/s)
SIM_TIME="${SIM_TIME:-80}"         # Durée (s)
CHECK_PERIOD="${CHECK_PERIOD:-0.5}"
SEED="${SEED:-1}"
RUNS="${RUNS:-5}"
RATE="${RATE:-2}"                  # Débit CBR (Mbps)
PINGPONG_WIN="${PINGPONG_WIN:-15}"

# Vérifications
[[ -x "$NS3_DIR/ns3" ]] || {
    echo "ERREUR : NS-3 introuvable dans $NS3_DIR"
    exit 1
}

mkdir -p "$RESULTS_DIR"
cd "$NS3_DIR"

echo "=================================================="
echo " TP3 - B5 : protocole expérimental"
echo " NS-3       : $NS3_DIR"
echo " Résultats  : $RESULTS_DIR"
echo " Rayon      : $R m"
echo " Amplitude  : $AMP m"
echo " Vitesse    : $SPEED m/s"
echo " Durée      : $SIM_TIME s"
echo " Runs       : $RUNS"
echo "=================================================="

# Compilation unique
./ns3 build

for ((run=1; run<=RUNS; run++)); do

    for H in 0 5; do

        TAG="B5_H${H}_run${run}"

        echo
        echo "========== $TAG =========="

        ./ns3 run "scratch/tp3-offload \
            --criterion=distance \
            --pattern=border \
            --apRadius=$R \
            --borderAmp=$AMP \
            --hyst=$H \
            --speed=$SPEED \
            --simTime=$SIM_TIME \
            --checkPeriod=$CHECK_PERIOD \
            --pingPongWin=$PINGPONG_WIN \
            --offset=0 \
            --offsetFrac=0 \
            --shadowSigma=0 \
            --distNoise=0 \
            --rate=$RATE \
            --seed=$SEED \
            --run=$run \
            --outDir=$RESULTS_DIR \
            --tag=$TAG \
            --pcap=0 \
            --flowmon=1 \
            --saveBins=1"

    done
done

echo
echo "=================================================="
echo "Campagne B5 terminée."
echo "Résultats disponibles dans : $RESULTS_DIR"
echo "=================================================="
