#!/usr/bin/env bash
# ==============================================================================
# VortexBrowser Universal Linux Companion Server - Startup Script
# Automatically adapts to the host hardware (Intel, AMD, Raspberry Pi, ARM64)
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# If running interactively and a systemd service is active, stop it first to prevent port conflicts
if [ -z "${INVOCATION_ID:-}" ]; then
    for svc in vortexbrowser.service vortexbrowse-cachyos.service vortexbrowse.service; do
        if systemctl --user is-active --quiet "$svc" 2>/dev/null; then
            echo "[VortexServer] Stopping background user service $svc..."
            systemctl --user stop "$svc" 2>/dev/null || true
        fi
        if systemctl is-active --quiet "$svc" 2>/dev/null; then
            echo "[VortexServer] Stopping background system service $svc..."
            sudo -n systemctl stop "$svc" 2>/dev/null || systemctl stop "$svc" 2>/dev/null || true
        fi
    done
fi

if [ ! -d ".venv" ]; then
    echo "[VortexServer] Python virtual environment not found!"
    echo "[VortexServer] Please run ./install_server.sh first."
    exit 1
fi

# Ensure native SIMD pixel converter is built
if [ ! -f "fast_pixconv.so" ] || [ "fast_pixconv.c" -nt "fast_pixconv.so" ]; then
    echo "[VortexServer] Compiling native SIMD pixel converter..."
    gcc -O3 -march=native -mtune=native -fopenmp -DHAVE_TURBOJPEG_LZ4 -shared -fPIC \
        fast_pixconv.c -lturbojpeg -llz4 -o fast_pixconv.so 2>/dev/null || \
    gcc -O3 -march=native -shared -fPIC fast_pixconv.c -o fast_pixconv.so 2>/dev/null || true
fi

export OMP_NUM_THREADS="${OMP_NUM_THREADS:-4}"
ENGINE="${VORTEX_ENGINE:-chromium}"

BROWSER_ARGS=()
SAVED_BIN=""
if [ -f "browser_bin.txt" ]; then
    SAVED_BIN="$(head -n 1 browser_bin.txt | tr -d '\r\n')"
    if [ -n "$SAVED_BIN" ] && [ -x "$SAVED_BIN" ]; then
        BROWSER_ARGS=("--browser-bin" "$SAVED_BIN")
    fi
fi

# Detect if low-power system (Intel Atom, Celeron, Raspberry Pi) to set sensible default resolution
IS_LOW_POWER=false
if [ -f "/sys/firmware/devicetree/base/model" ] && grep -qi "raspberry" /sys/firmware/devicetree/base/model 2>/dev/null; then
    IS_LOW_POWER=true
elif grep -qi "atom\|x5-\|x7-\|celeron\|pentium\|bcm2711" /proc/cpuinfo 2>/dev/null; then
    IS_LOW_POWER=true
fi

if [ "$IS_LOW_POWER" = true ]; then
    DEFAULT_W=1280
    DEFAULT_H=720
    DEFAULT_BITRATE=4000
else
    DEFAULT_W=1920
    DEFAULT_H=1080
    DEFAULT_BITRATE=8000
fi

WIDTH="${VORTEX_WIDTH:-$DEFAULT_W}"
HEIGHT="${VORTEX_HEIGHT:-$DEFAULT_H}"
FPS="${VORTEX_FPS:-25}"
VIDEO_BITRATE="${VORTEX_VIDEO_BITRATE:-$DEFAULT_BITRATE}"
LOG_LEVEL="${VORTEX_LOG_LEVEL:-CRITICAL}"

echo "============================================================================"
echo " Starting VortexBrowser Universal Server"
echo " Resolution: ${WIDTH}x${HEIGHT} @ ${FPS} FPS | Format: YUV420P + 2D Dirty-Rect"
echo " Engine: ${ENGINE} | Browser: ${SAVED_BIN:-System-Default}"
echo " C-Native SIMD + TurboJPEG + LZ4 Pipeline Active"
echo "============================================================================"

if [ $# -eq 0 ]; then
    exec .venv/bin/python server.py \
        --host 0.0.0.0 \
        --port 7788 \
        --width "$WIDTH" \
        --height "$HEIGHT" \
        --fps "$FPS" \
        --format yuv420p \
        --video-bitrate "$VIDEO_BITRATE" \
        --log-level "$LOG_LEVEL" \
        --jpeg-quality 82 \
        --codec auto \
        --engine "$ENGINE" \
        "${BROWSER_ARGS[@]}"
else
    exec .venv/bin/python server.py "$@"
fi
