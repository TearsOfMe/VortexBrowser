#!/usr/bin/env bash
set -euo pipefail

# ==============================================================================
# VortexBrowser Native macOS Launcher (Apple VideoToolbox + SIMD)
# ==============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if [ -d "$SCRIPT_DIR/.venv" ]; then
    source "$SCRIPT_DIR/.venv/bin/activate"
fi

# Ensure Homebrew libraries/binaries are on PATH
if [ -x "/opt/homebrew/bin/brew" ]; then
    eval "$(/opt/homebrew/bin/brew shellenv)"
elif [ -x "/usr/local/bin/brew" ]; then
    eval "$(/usr/local/bin/brew shellenv)"
fi

ARCH="$(uname -m)"
CPU_CORES="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
export OMP_NUM_THREADS="${OMP_NUM_THREADS:-$((CPU_CORES > 6 ? 6 : CPU_CORES))}"

echo "============================================================================"
echo " VortexBrowser macOS Companion Server (${ARCH})"
echo " Video: Apple VideoToolbox (h264_videotoolbox) | SIMD: fast_pixconv.dylib"
echo " Ports: 7788 (Stream) & 9757 (Audio)"
echo "============================================================================"

if [ $# -eq 0 ]; then
    exec python3 "$SCRIPT_DIR/server.py" \
        --host 0.0.0.0 \
        --port "${VORTEX_PORT:-7788}" \
        --width "${VORTEX_WIDTH:-1920}" \
        --height "${VORTEX_HEIGHT:-1080}" \
        --fps "${VORTEX_FPS:-30}" \
        --format yuv420p \
        --video-bitrate "${VORTEX_VIDEO_BITRATE:-10000}" \
        --jpeg-quality "${VORTEX_JPEG_QUALITY:-85}" \
        --codec auto \
        --engine "${VORTEX_ENGINE:-chromium}" \
        --log-level "${VORTEX_LOG_LEVEL:-INFO}"
else
    exec python3 "$SCRIPT_DIR/server.py" "$@"
fi
