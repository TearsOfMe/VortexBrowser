#!/usr/bin/env bash
# ==============================================================================
# VortexBrowser Server - Intel Launcher
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Intel VA-API Treiber-Auswahl (sofern durch Installer ermittelt)
if [ -f ".va_driver" ]; then
    VA_DRV=$(cat .va_driver | tr -d '[:space:]')
    if [ -n "$VA_DRV" ] && [ -z "$LIBVA_DRIVER_NAME" ]; then
        export LIBVA_DRIVER_NAME="$VA_DRV"
        echo "[Intel Profile] VA-API Driver: ${LIBVA_DRIVER_NAME}"
    fi
fi

# CPU-Kerne & Threading anpassen
CPU_CORES=$(nproc 2>/dev/null || echo 4)
if [ "$CPU_CORES" -le 4 ]; then
    # Bei 4 Kernen (z. B. Atom x5-Z8350) 2 Threads fuer OpenMP/SIMD belassen,
    # damit Chromium und Python nicht blockiert werden
    export OMP_NUM_THREADS=2
else
    export OMP_NUM_THREADS=4
fi

if [ ! -d ".venv" ]; then
    echo "[!] .venv nicht gefunden. Bitte zuerst ./install_intel.sh ausfuehren!"
    exit 1
fi

exec .venv/bin/python server.py "$@"
