#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Ensure native SIMD converter is compiled with native CPU optimizations (AVX2/AVX-512)
if [ ! -f "fast_pixconv.so" ] || [ "fast_pixconv.c" -nt "fast_pixconv.so" ]; then
    echo "[VortexBrowse] Compiling native CachyOS SIMD helper (fast_pixconv.so)..."
    gcc -O3 -march=native -shared -fPIC fast_pixconv.c -o fast_pixconv.so
fi

# Ensure Python virtual environment exists
if [ ! -d ".venv" ]; then
    echo "[VortexBrowse] Creating Python virtual environment..."
    python3 -m venv .venv
    .venv/bin/pip install --upgrade pip -r requirements.txt
    .venv/bin/playwright install chromium firefox
fi

ENGINE="${VORTEX_ENGINE:-chromium}"
BROWSER_ARGS=()
if [ -f "browser_bin.txt" ]; then
    SAVED_BIN="$(head -n 1 browser_bin.txt | tr -d '\r\n')"
    if [ -n "$SAVED_BIN" ] && [ -x "$SAVED_BIN" ]; then
        BROWSER_ARGS=("--browser-bin" "$SAVED_BIN")
    fi
fi

# Default arguments if none provided:
# Binds to 0.0.0.0:7788 (and server.py auto-binds secondary port 9757 for LAN)
if [ $# -eq 0 ]; then
    echo "[VortexBrowse] Starting server on CachyOS (Ports 7788 & 9757, Engine: ${ENGINE}, Browser: ${SAVED_BIN:-System-Default}, 30fps, Auto MJPEG/LZ4, Quality 100, TurboJPEG, Extensions)..."
    exec .venv/bin/python server.py --host 0.0.0.0 --port 7788 --width 1024 --height 768 --fps 30 --format yuv420p --jpeg-quality 100 --codec auto --engine "$ENGINE" "${BROWSER_ARGS[@]}"
else
    exec .venv/bin/python server.py "$@"
fi
