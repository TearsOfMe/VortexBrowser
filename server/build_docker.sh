#!/usr/bin/env bash
# ==============================================================================
# Build Docker images for:
#   1. Linux Server (linux/amd64)      -> vortexbrowser-server:linux-amd64
#   2. Raspberry Pi 4/5 (linux/arm64)  -> vortexbrowser-server:rpi-arm64
#   3. macOS Apple Silicon (arm64)     -> vortexbrowser-server:macos-arm64
#   4. macOS Intel (x86_64)            -> vortexbrowser-server:macos-amd64
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
    echo "=== [1/4] Building Linux x86_64 Docker Image (vortexbrowser-server:linux-amd64) ==="
    docker buildx build \
        --platform linux/amd64 \
        --build-arg VORTEX_TARGET=linux \
        --load \
        -t vortexbrowser-server:linux-amd64 \
        -t vortexbrowser-server:latest \
        .

    echo "=== [2/4] Building Raspberry Pi 4/5 ARM64 Docker Image (vortexbrowser-server:rpi-arm64) ==="
    docker buildx build \
        --platform linux/arm64 \
        --build-arg VORTEX_TARGET=linux \
        --load \
        -t vortexbrowser-server:rpi-arm64 \
        .
fi

echo "=== [3/4] Building macOS Apple Silicon ARM64 Docker Image (vortexbrowser-server:macos-arm64) ==="
docker buildx build \
    --platform linux/arm64 \
    --build-arg VORTEX_TARGET=macos \
    --load \
    -t vortexbrowser-server:macos-arm64 \
    .

echo "=== [4/4] Building macOS Intel x86_64 Docker Image (vortexbrowser-server:macos-amd64) ==="
docker buildx build \
    --platform linux/amd64 \
    --build-arg VORTEX_TARGET=macos \
    --load \
    -t vortexbrowser-server:macos-amd64 \
    .

if [ "$EXPORT_TAR" = true ]; then
    mkdir -p ../dist/docker
    if [ "$MACOS_ONLY" = false ]; then
        docker save vortexbrowser-server:linux-amd64 | gzip > ../dist/docker/vortexbrowser-server-linux-amd64.tar.gz
        docker save vortexbrowser-server:rpi-arm64 | gzip > ../dist/docker/vortexbrowser-server-rpi-arm64.tar.gz
    fi
    docker save vortexbrowser-server:macos-arm64 | gzip > ../dist/docker/vortexbrowser-server-macos-arm64.tar.gz
    docker save vortexbrowser-server:macos-amd64 | gzip > ../dist/docker/vortexbrowser-server-macos-amd64.tar.gz
    echo "[+] Exported compressed Docker images to ../dist/docker/"
    ls -lh ../dist/docker/
fi

echo ""
echo "[OK] Docker images built successfully!"
docker images | grep vortexbrowser-server
