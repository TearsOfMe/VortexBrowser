#!/bin/bash
set -e

# ==============================================================================
# VortexBrowse - Raspberry Pi 4 Start Script
# ==============================================================================

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

if [ ! -d ".venv" ]; then
    echo "[VortexBrowse] Virtual Environment nicht gefunden. Führe zuerst ./install_rpi4.sh aus."
    exit 1
fi

# Beende eventuell im Hintergrund laufende alte Instanzen, um blockierte Ports zu vermeiden
pkill -f "python3.*server.py" 2>/dev/null || true
sleep 1

# Cortex-A72 NEON SIMD Converter aktualisieren/kompilieren falls nötig
if [ -f "fast_pixconv.c" ]; then
    if [ ! -f "fast_pixconv_aarch64.so" ] || [ "fast_pixconv.c" -nt "fast_pixconv_aarch64.so" ]; then
        if command -v gcc >/dev/null 2>&1; then
            echo "[VortexBrowse] Kompiliere optimierten Cortex-A72 Pixel-Konverter..."
            gcc -O3 -march=armv8-a+crc+simd -mtune=cortex-a72 -shared -fPIC -o fast_pixconv_aarch64.so fast_pixconv.c 2>/dev/null || \
            gcc -O3 -march=native -shared -fPIC -o fast_pixconv_aarch64.so fast_pixconv.c 2>/dev/null || true
        fi
    fi
fi

# 1. XDG_RUNTIME_DIR setzen (essentiell für PulseAudio/PipeWire in Headless-/SSH-Umgebungen wie DietPi)
if [ -z "$XDG_RUNTIME_DIR" ] || [ ! -d "$XDG_RUNTIME_DIR" ]; then
    UID_NUM="$(id -u)"
    if [ -d "/run/user/$UID_NUM" ]; then
        export XDG_RUNTIME_DIR="/run/user/$UID_NUM"
    else
        export XDG_RUNTIME_DIR="/tmp/xdg_runtime_$UID_NUM"
        mkdir -p "$XDG_RUNTIME_DIR" 2>/dev/null || true
        chmod 700 "$XDG_RUNTIME_DIR" 2>/dev/null || true
    fi
fi

# 2. PulseAudio Sound-Daemon starten, falls noch nicht aktiv (für AHI Audio-Streaming)
if command -v pulseaudio >/dev/null 2>&1; then
    if ! pactl info >/dev/null 2>&1; then
        echo "[VortexBrowse] Starte PulseAudio Sound-Daemon..."
        pulseaudio --start --exit-idle-time=-1 --disallow-exit 2>/dev/null || true
        sleep 1
    fi
fi

# 3. Virtuellen Null-Sink für Amiga-Audio aktivieren
if command -v pactl >/dev/null 2>&1; then
    if pactl info >/dev/null 2>&1; then
        if ! pactl list short sinks 2>/dev/null | grep -q "vortexbrowse_sink"; then
            pactl load-module module-null-sink sink_name=vortexbrowse_sink sink_properties=device.description=VortexBrowseAudio 2>/dev/null || true
        fi
        pactl set-sink-mute vortexbrowse_sink 0 2>/dev/null || true
        pactl set-sink-volume vortexbrowse_sink 100% 2>/dev/null || true
    fi
fi

echo "[VortexBrowse] Starte Server auf Raspberry Pi 4 (Port 7788 / 9757, YUV420P TurboJPEG, uBlock, h264ify)..."
exec .venv/bin/python3 server.py --host 0.0.0.0 --port 7788 --fps 25 --format yuv420p --jpeg-quality 75 "$@"
