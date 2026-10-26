#!/bin/bash
set -e

# Setup user runtime directory for PulseAudio / PipeWire
export XDG_RUNTIME_DIR="/tmp/xdg_runtime"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"

# Clean up any stale pid files or lock files from previous runs
rm -f "$XDG_RUNTIME_DIR/pulse/pid" 2>/dev/null || true

# Start PulseAudio sound daemon in user mode
echo "[VortexBrowse-Docker] Starting PulseAudio sound daemon..."
pulseaudio --start --exit-idle-time=-1 --disallow-exit 2>/dev/null || true
sleep 1

# Configure virtual null-sink for audio streaming to Amiga AHI
if command -v pactl >/dev/null 2>&1; then
    if ! pactl list short sinks 2>/dev/null | grep -q "vortexbrowse_sink"; then
        echo "[VortexBrowse-Docker] Creating virtual null-sink 'vortexbrowse_sink'..."
        pactl load-module module-null-sink sink_name=vortexbrowse_sink sink_properties=device.description=VortexBrowseAudio 2>/dev/null || true
    fi
    pactl set-sink-mute vortexbrowse_sink 0 2>/dev/null || true
    pactl set-sink-volume vortexbrowse_sink 100% 2>/dev/null || true
    echo "[VortexBrowse-Docker] Virtual audio sink ready."
fi

echo "[VortexBrowse-Docker] Starting VortexBrowse Server..."
exec python3 /app/server.py "$@"
