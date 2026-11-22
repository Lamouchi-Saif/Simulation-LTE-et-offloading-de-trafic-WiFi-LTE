#!/usr/bin/env bash
set -Eeuo pipefail

# Analyse uniquement les résultats CSV existants.
# Aucune simulation NS-3 n'est lancée.

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
NS3_DIR="${NS3_DIR:-$(cd "$SCRIPT_DIR/../.." && pwd)}"

PYTHON="${PYTHON:-python3}"

RAW_DIR="${RAW_DIR:-$NS3_DIR/results/tp3/B7_experiment/raw}"
OUT_DIR="${OUT_DIR:-$NS3_DIR/results/tp3/B7_experiment/analysis}"

MIN_SEEDS="${MIN_SEEDS:-5}"
TIMELINE_SEED="${TIMELINE_SEED:-1}"

ANALYZER="${ANALYZER:-$SCRIPT_DIR/analyze_q7.py}"

echo "======================================"
echo "       ANALYSE EXPERIMENTALE Q7"
echo "======================================"

echo "NS3_DIR       : $NS3_DIR"
echo "RAW_DIR       : $RAW_DIR"
echo "OUT_DIR       : $OUT_DIR"
echo "MIN_SEEDS     : $MIN_SEEDS"
echo "TIMELINE_SEED : $TIMELINE_SEED"

echo "======================================"

if [[ ! -f "$ANALYZER" ]]; then
    echo "ERREUR : script Python introuvable : $ANALYZER" >&2
    exit 1
fi

if [[ ! -d "$RAW_DIR" ]]; then
    echo "ERREUR : dossier raw introuvable : $RAW_DIR" >&2
    exit 1
fi

if ! [[ "$MIN_SEEDS" =~ ^[0-9]+$ ]]; then
    echo "ERREUR : MIN_SEEDS doit être un entier." >&2
    exit 1
fi

if ! [[ "$TIMELINE_SEED" =~ ^[0-9]+$ ]]; then
    echo "ERREUR : TIMELINE_SEED doit être un entier." >&2
    exit 1
fi

if ! command -v "$PYTHON" >/dev/null 2>&1; then
    echo "ERREUR : Python introuvable : $PYTHON" >&2
    exit 1
fi

mkdir -p "$OUT_DIR"

echo "[INFO] Vérification des dépendances Python..."

"$PYTHON" -c "import numpy, pandas, matplotlib" || {
    echo "ERREUR : installe les dépendances avec :" >&2
    echo "python3 -m pip install numpy pandas matplotlib" >&2
    exit 1
}

echo "[INFO] Lancement de l'analyse..."
echo

"$PYTHON" "$ANALYZER" \
    --raw "$RAW_DIR" \
    --out "$OUT_DIR" \
    --min-seeds "$MIN_SEEDS" \
    --timeline-seed "$TIMELINE_SEED"

echo
echo "======================================"
echo "ANALYSE TERMINEE"
echo "======================================"
echo "Résultats : $OUT_DIR"
echo "Graphiques : $OUT_DIR/figures"
