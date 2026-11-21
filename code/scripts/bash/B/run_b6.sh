#!/usr/bin/env bash
set -Eeuo pipefail

# ============================================================
# TP3 - B6 : campagne de simulations NS-3
# 3 rayons x 3 vitesses x 5 graines = 45 simulations
# 5 workers paralleles, soit 9 simulations par worker.
#
# Code C++ : scratch/tp3-offload.cc (INCHANGE)
# Script    : script/tp3/run_b6.sh
# Resultats : results/tp3/B6_experiment/
# ============================================================

# ---------- Configuration generale ----------

NS3_ROOT="${NS3_DIR:-$HOME/ns-3.48}"
WORKERS=5
BUILD=1

RADII=(40 50 60)
SPEEDS=(3 5 7)
SEEDS=(1 2 3 4 5)

HYST=5
SIM_TIME=80
OFFSET_FRAC=0.30
SHADOW_SIGMA=0
DIST_NOISE=0
RATE=2
PKT_SIZE=1200

# ---------- Options ----------

usage() {
    cat <<'EOF'
Usage:
  ./script/tp3/run_b6.sh [options]

Options:
  --workers N          Nombre de workers (defaut: 5)
  --radii "40 50 60"   Rayons Wi-Fi en metres
  --speeds "3 5 7"     Vitesses UE en m/s
  --seeds "1 2 3 4 5"  Graines aleatoires
  --hyst N             Hysteresis en metres
  --sim-time N         Duree de simulation
  --no-build           Ne pas compiler NS-3
  -h, --help           Afficher cette aide

Exemples:
  ./script/tp3/run_b6.sh
  ./script/tp3/run_b6.sh --workers 3
  ./script/tp3/run_b6.sh --no-build
EOF
}

while (($#)); do
    case "$1" in
        --workers)
            WORKERS="$2"; shift 2 ;;
        --radii)
            read -r -a RADII <<< "$2"; shift 2 ;;
        --speeds)
            read -r -a SPEEDS <<< "$2"; shift 2 ;;
        --seeds)
            read -r -a SEEDS <<< "$2"; shift 2 ;;
        --hyst)
            HYST="$2"; shift 2 ;;
        --sim-time)
            SIM_TIME="$2"; shift 2 ;;
        --no-build)
            BUILD=0; shift ;;
        -h|--help)
            usage; exit 0 ;;
        *)
            echo "Option inconnue : $1" >&2
            usage
            exit 1 ;;
    esac
done

# ---------- Detection NS-3 ----------

[[ -d "$NS3_ROOT" ]] || {
    echo "ERREUR : NS3_ROOT introuvable : $NS3_ROOT" >&2
    exit 1
}

NS3_ROOT="$(cd "$NS3_ROOT" && pwd)"
NS3="$NS3_ROOT/ns3"
SOURCE="$NS3_ROOT/scratch/tp3-offload.cc"

[[ -x "$NS3" ]] || {
    echo "ERREUR : executable ns3 introuvable." >&2
    exit 1
}

[[ -f "$SOURCE" ]] || {
    echo "ERREUR : fichier C++ absent : $SOURCE" >&2
    echo "Le fichier doit rester dans scratch/." >&2
    exit 1
}

[[ "$WORKERS" =~ ^[1-9][0-9]*$ ]] || {
    echo "ERREUR : workers doit etre positif." >&2
    exit 1
}

[[ "$BUILD" == 0 || "$BUILD" == 1 ]] || exit 1

# ---------- Validation des graines ----------

declare -A SEEN=()

for seed in "${SEEDS[@]}"; do
    [[ "$seed" =~ ^[1-9][0-9]*$ ]] || {
        echo "ERREUR : graine invalide : $seed" >&2
        exit 1
    }

    [[ -z "${SEEN[$seed]+x}" ]] || {
        echo "ERREUR : graine dupliquee : $seed" >&2
        exit 1
    }

    SEEN[$seed]=1
done

for radius in "${RADII[@]}"; do
    awk -v r="$radius" -v h="$HYST" \
        'BEGIN { exit !(r > h && h >= 0) }' || {
        echo "ERREUR : rayon $radius incompatible avec hyst=$HYST" >&2
        exit 1
    }
done

TOTAL=$((
    ${#RADII[@]} *
    ${#SPEEDS[@]} *
    ${#SEEDS[@]}
))

(( WORKERS <= TOTAL )) || WORKERS="$TOTAL"

# ---------- Repertoire de campagne ----------

TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
CAMPAIGN="$NS3_ROOT/results/tp3/B6_experiment/campaign_$TIMESTAMP"

RAW="$CAMPAIGN/raw"
LOGS="$CAMPAIGN/logs"
META="$CAMPAIGN/metadata"

mkdir -p "$RAW" "$LOGS" "$META"

# Sauvegarde de la configuration experimentale.

{
    echo "Question=B6"
    echo "criterion=distance"
    echo "pattern=line"
    echo "radii=${RADII[*]}"
    echo "speeds=${SPEEDS[*]}"
    echo "seeds=${SEEDS[*]}"
    echo "workers=$WORKERS"
    echo "hyst=$HYST"
    echo "simTime=$SIM_TIME"
    echo "offsetFrac=$OFFSET_FRAC"
    echo "shadowSigma=$SHADOW_SIGMA"
    echo "distNoise=$DIST_NOISE"
    echo "rate=$RATE"
    echo "pktSize=$PKT_SIZE"
    echo "saveBins=false"
    echo "flowmon=true"
    echo "pcap=false"
} > "$META/config.txt"

# ---------- Construction des 45 simulations ----------

declare -a JOBS=()

for radius in "${RADII[@]}"; do
    for speed in "${SPEEDS[@]}"; do
        for seed in "${SEEDS[@]}"; do

            tag="B6_R${radius}_V${speed}_S${seed}"

            JOBS+=("$radius|$speed|$seed|$tag")
        done
    done
done

# ---------- Compilation unique ----------

cd "$NS3_ROOT"

if (( BUILD == 1 )); then
    echo "Compilation NS-3..."
    ./ns3 build
fi

# ---------- Fonction Worker ----------

worker() {
    local worker_id="$1"
    local index job
    local radius speed seed tag
    local logfile summary
    local failed=0

    logfile="$LOGS/worker_${worker_id}.log"

    echo "Worker $worker_id demarre : $(date)" > "$logfile"

    # Repartition cyclique des simulations.
    # Avec 45 simulations et 5 workers :
    # exactement 9 simulations par worker.

    for ((index=worker_id-1; index<TOTAL; index+=WORKERS)); do

        job="${JOBS[$index]}"
        IFS='|' read -r radius speed seed tag <<< "$job"

        summary="$RAW/${tag}_summary.csv"

        {
            echo
            echo "========================================"
            echo "Simulation : $tag"
            echo "Rayon      : $radius m"
            echo "Vitesse    : $speed m/s"
            echo "Seed       : $seed"
            echo "Debut      : $(date)"
        } >> "$logfile"

        if "$NS3" run --no-build \
            "scratch/tp3-offload \
            --criterion=distance \
            --pattern=line \
            --apRadius=$radius \
            --hyst=$HYST \
            --speed=$speed \
            --simTime=$SIM_TIME \
            --offsetFrac=$OFFSET_FRAC \
            --shadowSigma=$SHADOW_SIGMA \
            --distNoise=$DIST_NOISE \
            --rate=$RATE \
            --pktSize=$PKT_SIZE \
            --seed=$seed \
            --run=1 \
            --saveBins=false \
            --flowmon=true \
            --pcap=false \
            --anim=false \
            --lteTraces=false \
            --outDir=$RAW \
            --tag=$tag" >> "$logfile" 2>&1
        then
            if [[ -s "$summary" ]] &&
               [[ "$(wc -l < "$summary")" -ge 2 ]]; then

                echo "RESULTAT : OK" >> "$logfile"
            else
                echo "RESULTAT : SUMMARY ABSENT OU INVALIDE" >> "$logfile"
                failed=$((failed + 1))
            fi
        else
            echo "RESULTAT : ECHEC NS-3" >> "$logfile"
            failed=$((failed + 1))
        fi

        echo "Fin : $(date)" >> "$logfile"
    done

    echo "Worker $worker_id termine. Echecs=$failed" >> "$logfile"
}

# ---------- Lancement parallele ----------

echo
echo "=============================================="
echo "CAMPAGNE B6"
echo "=============================================="
echo "Code C++    : $SOURCE"
echo "Simulations : $TOTAL"
echo "Workers     : $WORKERS"
echo "Sortie      : $CAMPAIGN"
echo "=============================================="

PIDS=()

for ((w=1; w<=WORKERS; w++)); do
    worker "$w" &
    PIDS+=("$!")
    echo "Worker $w lance : PID ${PIDS[-1]}"
done

# Attendre tous les workers.

for pid in "${PIDS[@]}"; do
    wait "$pid"
done

# ---------- Verification finale ----------

FOUND=0
MISSING=0

for job in "${JOBS[@]}"; do

    IFS='|' read -r radius speed seed tag <<< "$job"
    summary="$RAW/${tag}_summary.csv"

    if [[ -s "$summary" ]] &&
       [[ "$(wc -l < "$summary")" -ge 2 ]]; then
        FOUND=$((FOUND + 1))
    else
        MISSING=$((MISSING + 1))
        echo "$tag : resultat absent ou invalide" \
            >> "$META/missing_results.txt"
    fi
done

echo
echo "=============================================="
echo "BILAN FINAL B6"
echo "=============================================="
echo "Simulations attendues : $TOTAL"
echo "Summary valides       : $FOUND"
echo "Resultats manquants   : $MISSING"
echo "Repertoire            : $CAMPAIGN"
echo "=============================================="

if (( MISSING > 0 )); then
    echo "ERREUR : campagne incomplete."
    exit 1
fi

echo "SUCCES : campagne B6 terminee."
