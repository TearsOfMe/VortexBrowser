#!/usr/bin/env bash
# ==============================================================================
# VortexBrowser Server - CachyOS & HP EliteDesk 805 G6 (AMD Ryzen PRO) Edition
# Full HD (1920x1080) @ 25 FPS Startup Script
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# If running interactively and systemd service is active, stop the service first so Chromium profile is not locked
if [ -z "${INVOCATION_ID:-}" ] && systemctl is-active --quiet vortexbrowser.service 2>/dev/null; then
    echo "[VortexServer] Stopping background vortexbrowser.service to avoid profile lock..."
    sudo -n systemctl stop vortexbrowser.service 2>/dev/null || systemctl stop vortexbrowser.service 2>/dev/null || true
fi

if [ ! -d ".venv" ]; then
    echo "[VortexServer] Python virtual environment not found!"
    echo "[VortexServer] Please run ./install_server.sh first."
    exit 1
fi

# Compile native AVX2 + OpenMP + libturbojpeg + liblz4 C helper if missing or outdated
if [ ! -f "fast_pixconv.so" ] || [ "fast_pixconv.c" -nt "fast_pixconv.so" ]; then
    echo "[VortexServer] Compiling native AMD Zen 2 (znver2) AVX2+OpenMP+TurboJPEG+LZ4 helper..."
    gcc -O3 -march=znver2 -mtune=znver2 -mavx2 -mfma -fopenmp -DHAVE_TURBOJPEG_LZ4 -shared -fPIC \
        fast_pixconv.c -lturbojpeg -llz4 -o fast_pixconv.so 2>/dev/null || \
    gcc -O3 -march=native -mavx2 -mfma -fopenmp -DHAVE_TURBOJPEG_LZ4 -shared -fPIC \
        fast_pixconv.c -lturbojpeg -llz4 -o fast_pixconv.so 2>/dev/null || \
    gcc -O3 -march=native -shared -fPIC fast_pixconv.c -o fast_pixconv.so
fi

export OMP_NUM_THREADS="${OMP_NUM_THREADS:-4}"
ENGINE="${VORTEX_ENGINE:-chromium}"

echo "============================================================================"
echo " Starting VortexBrowser Server - CachyOS HP EliteDesk 805 G6 (AMD Ryzen PRO)"
echo " Resolution: 1920x1080 (Full HD) @ 25 FPS | Format: YUV420P + 2D Dirty-Rect"
echo " Engine: ${ENGINE} | C-Native TurboJPEG + AVX2 + LZ4 Pipeline Active"
echo "============================================================================"

if [ $# -eq 0 ]; then
    exec .venv/bin/python server.py \
        --host 0.0.0.0 \
        --port 7788 \
        --width 1920 \
        --height 1080 \
        --fps 25 \
        --format yuv420p \
        --jpeg-quality 82 \
        --codec auto \
        --engine "$ENGINE"
else
    exec .venv/bin/python server.py "$@"
fi
