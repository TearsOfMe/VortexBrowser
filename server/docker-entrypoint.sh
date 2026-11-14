#!/usr/bin/env bash
set -e

cd /app

# Ensure runtime directory for PulseAudio / D-Bus inside container
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/runtime-vortex}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"

# Start D-Bus session if not running (helps Chromium & PulseAudio)
if [ -z "${DBUS_SESSION_BUS_ADDRESS:-}" ] && command -v dbus-daemon &>/dev/null; then
    mkdir -p /tmp/dbus
    DBUS_SESSION_BUS_ADDRESS="$(dbus-daemon --session --fork --print-address 2>/dev/null || true)"
    export DBUS_SESSION_BUS_ADDRESS
fi

# Start PulseAudio daemon in background for AHI audio streaming (port 9757)
if command -v pulseaudio &>/dev/null; then
    if ! pactl info >/dev/null 2>&1; then
        pulseaudio --start --exit-idle-time=-1 --disallow-exit >/dev/null 2>&1 || true
        sleep 0.5
    fi
fi

# Optional: rebuild fast_pixconv.so with -march=native on first boot if requested
if [ "${VORTEX_REBUILD_NATIVE:-0}" = "1" ] && [ -f "fast_pixconv.c" ]; then
    echo "[VortexDocker] Recompiling fast_pixconv.so with -march=native..."
    gcc -O3 -march=native -mtune=native -fopenmp -DHAVE_TURBOJPEG_LZ4 -shared -fPIC \
        fast_pixconv.c -lturbojpeg -llz4 -o fast_pixconv.so 2>/dev/null || true
fi

# Auto-detect platform / low-power defaults unless overridden via ENV
ARCH="$(uname -m)"
PLATFORM="${VORTEX_PLATFORM:-auto}"
IS_RPI=false

if [ "$PLATFORM" = "rpi" ]; then
    IS_RPI=true
elif [ "$PLATFORM" != "macos" ]; then
    # Only treat aarch64/arm64 as Raspberry Pi if devicetree confirms Broadcom/RPi
    if [ -f "/sys/firmware/devicetree/base/model" ] && grep -qi "raspberry" /sys/firmware/devicetree/base/model 2>/dev/null; then
        IS_RPI=true
    elif [ -f "/proc/device-tree/compatible" ] && grep -qiE "raspberrypi|brcm" /proc/device-tree/compatible 2>/dev/null; then
        IS_RPI=true
    fi
fi

if [ "$IS_RPI" = true ]; then
    DEFAULT_W="1280"
    DEFAULT_H="720"
    DEFAULT_BITRATE="4000"
    DEFAULT_FPS="25"
    DEFAULT_OMP="4"
elif [ "$PLATFORM" = "macos" ]; then
    # Optimized defaults for Docker Desktop on macOS (Apple Silicon / Intel)
    DEFAULT_W="1920"
    DEFAULT_H="1080"
    DEFAULT_BITRATE="10000"
    DEFAULT_FPS="30"
    DEFAULT_OMP="6"
else
    DEFAULT_W="1920"
    DEFAULT_H="1080"
    DEFAULT_BITRATE="8000"
    DEFAULT_FPS="25"
    DEFAULT_OMP="4"
fi

WIDTH="${VORTEX_WIDTH:-$DEFAULT_W}"
HEIGHT="${VORTEX_HEIGHT:-$DEFAULT_H}"
FPS="${VORTEX_FPS:-$DEFAULT_FPS}"
VIDEO_BITRATE="${VORTEX_VIDEO_BITRATE:-$DEFAULT_BITRATE}"
JPEG_QUALITY="${VORTEX_JPEG_QUALITY:-82}"
LOG_LEVEL="${VORTEX_LOG_LEVEL:-INFO}"
ENGINE="${VORTEX_ENGINE:-chromium}"
PORT="${VORTEX_PORT:-7788}"

export OMP_NUM_THREADS="${OMP_NUM_THREADS:-$DEFAULT_OMP}"

echo "============================================================================"
echo " VortexBrowser Universal Docker Server (${ARCH})"
echo " Resolution: ${WIDTH}x${HEIGHT} @ ${FPS} FPS | Bitrate: ${VIDEO_BITRATE} kbps"
echo " Engine: ${ENGINE} | Ports: ${PORT} (Stream) & 9757 (Audio)"
echo "============================================================================"

if [ $# -eq 0 ]; then
    exec python3 /app/server.py \
        --host 0.0.0.0 \
        --port "$PORT" \
        --width "$WIDTH" \
        --height "$HEIGHT" \
        --fps "$FPS" \
        --format yuv420p \
        --video-bitrate "$VIDEO_BITRATE" \
        --jpeg-quality "$JPEG_QUALITY" \
        --codec auto \
        --engine "$ENGINE" \
        --log-level "$LOG_LEVEL"
else
    exec python3 /app/server.py "$@"
fi
