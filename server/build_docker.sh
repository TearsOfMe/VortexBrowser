#!/usr/bin/env bash
# ==============================================================================
# Build Docker images for:
#   1. Linux Server (linux/amd64)      -> vortexremotebrowser-server:linux-amd64
#   2. Raspberry Pi 4/5 (linux/arm64)  -> vortexremotebrowser-server:rpi-arm64
#   3. macOS Apple Silicon (arm64)     -> vortexremotebrowser-server:macos-arm64
#   4. macOS Intel (x86_64)            -> vortexremotebrowser-server:macos-amd64
# Flags:
#   --macos-only   Build only macOS-optimized images (macos-arm64 & macos-amd64)
#   --export       Export compressed .tar.gz archives into ../dist/docker/
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

EXPORT_TAR=false
MACOS_ONLY=false
for arg in "$@"; do
    case "$arg" in
        --export) EXPORT_TAR=true ;;
        --macos-only) MACOS_ONLY=true ;;
    esac
done

if [ "$MACOS_ONLY" = false ]; then
    echo "=== [1/4] Building Linux x86_64 Docker Image (vortexremotebrowser-server:linux-amd64) ==="
    docker buildx build \
        --platform linux/amd64 \
        --build-arg VORTEX_TARGET=linux \
        --load \
        -t vortexremotebrowser-server:linux-amd64 \
        -t vortexremotebrowser-server:latest \
        .

    echo "=== [2/4] Building Raspberry Pi 4/5 ARM64 Docker Image (vortexremotebrowser-server:rpi-arm64) ==="
    docker buildx build \
        --platform linux/arm64 \
        --build-arg VORTEX_TARGET=linux \
        --load \
        -t vortexremotebrowser-server:rpi-arm64 \
        .
fi

echo "=== [3/4] Building macOS Apple Silicon ARM64 Docker Image (vortexremotebrowser-server:macos-arm64) ==="
docker buildx build \
    --platform linux/arm64 \
    --build-arg VORTEX_TARGET=macos \
    --load \
    -t vortexremotebrowser-server:macos-arm64 \
    .

echo "=== [4/4] Building macOS Intel x86_64 Docker Image (vortexremotebrowser-server:macos-amd64) ==="
docker buildx build \
    --platform linux/amd64 \
    --build-arg VORTEX_TARGET=macos \
    --load \
    -t vortexremotebrowser-server:macos-amd64 \
    .

if [ "$EXPORT_TAR" = true ]; then
    mkdir -p ../dist/docker
    if [ "$MACOS_ONLY" = false ]; then
        docker save vortexremotebrowser-server:linux-amd64 | gzip > ../dist/docker/vortexremotebrowser-server-linux-amd64.tar.gz
        docker save vortexremotebrowser-server:rpi-arm64 | gzip > ../dist/docker/vortexremotebrowser-server-rpi-arm64.tar.gz
    fi
    docker save vortexremotebrowser-server:macos-arm64 | gzip > ../dist/docker/vortexremotebrowser-server-macos-arm64.tar.gz
    docker save vortexremotebrowser-server:macos-amd64 | gzip > ../dist/docker/vortexremotebrowser-server-macos-amd64.tar.gz
    echo "[+] Exported compressed Docker images to ../dist/docker/"
    ls -lh ../dist/docker/
fi

echo ""
echo "[OK] Docker images built successfully!"
docker images | grep vortexremotebrowser-server
