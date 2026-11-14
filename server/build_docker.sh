#!/usr/bin/env bash
# ==============================================================================
# Build Docker images for:
#   1. Linux Server (linux/amd64) -> vortexbrowser-server:linux-amd64
#   2. Raspberry Pi 4/5 (linux/arm64) -> vortexbrowser-server:rpi-arm64
# Optionally exports tar.gz archives into ../dist/docker/ when --export is passed.
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

EXPORT_TAR=false
if [ "${1:-}" = "--export" ]; then
    EXPORT_TAR=true
fi

echo "=== [1/2] Building Linux x86_64 Docker Image (vortexbrowser-server:linux-amd64) ==="
docker buildx build \
    --platform linux/amd64 \
    --load \
    -t vortexbrowser-server:linux-amd64 \
    -t vortexbrowser-server:latest \
    .

echo "=== [2/2] Building Raspberry Pi 4/5 ARM64 Docker Image (vortexbrowser-server:rpi-arm64) ==="
if [ "$EXPORT_TAR" = true ]; then
    mkdir -p ../dist/docker
    docker buildx build \
        --platform linux/amd64 \
        -t vortexbrowser-server:linux-amd64 \
        -o type=docker,dest=- . | gzip > ../dist/docker/vortexbrowser-server-linux-amd64.tar.gz
    docker buildx build \
        --platform linux/arm64 \
        -t vortexbrowser-server:rpi-arm64 \
        -o type=docker,dest=- . | gzip > ../dist/docker/vortexbrowser-server-rpi-arm64.tar.gz
    echo "[+] Exported compressed Docker images to ../dist/docker/"
    ls -lh ../dist/docker/
else
    docker buildx build \
        --platform linux/arm64 \
        --load \
        -t vortexbrowser-server:rpi-arm64 \
        .
fi

echo ""
echo "[OK] Docker images built successfully!"
docker images | grep vortexbrowser-server
