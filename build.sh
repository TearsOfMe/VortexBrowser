#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONTAINER_ID="${1:-7f239cb96d8c259d2d5489d6df3a79b3ef6b736346acb46960f3f928c40f17fd}"

echo "=== [1/3] Building CachyOS Native SIMD Pixel Converter (fast_pixconv.so) ==="
gcc -O3 -march=native -mavx2 -mfma -fopenmp -DHAVE_TURBOJPEG_LZ4 -shared -fPIC \
    "${SCRIPT_DIR}/pi-server/fast_pixconv.c" \
    -lturbojpeg -llz4 \
    -o "${SCRIPT_DIR}/pi-server/fast_pixconv.so" 2>/dev/null || \
gcc -O3 -march=native -shared -fPIC \
    "${SCRIPT_DIR}/pi-server/fast_pixconv.c" \
    -o "${SCRIPT_DIR}/pi-server/fast_pixconv.so"

if [ -f "${SCRIPT_DIR}/cachyos-elitedesk-server/fast_pixconv.c" ]; then
    gcc -O3 -march=znver2 -mtune=znver2 -mavx2 -mfma -fopenmp -DHAVE_TURBOJPEG_LZ4 -shared -fPIC \
        "${SCRIPT_DIR}/cachyos-elitedesk-server/fast_pixconv.c" \
        -lturbojpeg -llz4 \
        -o "${SCRIPT_DIR}/cachyos-elitedesk-server/fast_pixconv.so" 2>/dev/null || \
    gcc -O3 -march=native -shared -fPIC \
        "${SCRIPT_DIR}/cachyos-elitedesk-server/fast_pixconv.c" \
        -o "${SCRIPT_DIR}/cachyos-elitedesk-server/fast_pixconv.so"
fi

gcc -O3 -march=native -shared -fPIC \
    "${SCRIPT_DIR}/rpi4-server/fast_pixconv.c" \
    -o "${SCRIPT_DIR}/rpi4-server/fast_pixconv.so" 2>/dev/null || true

echo "=== [2/3] Cross-Compiling AmigaOS 4.1 PowerPC Client (Newlib) via Docker ==="
docker exec "${CONTAINER_ID}" bash -c "mkdir -p /tmp/vb_build/common /tmp/vb_build/amiga-client"
docker cp "${SCRIPT_DIR}/common/." "${CONTAINER_ID}:/tmp/vb_build/common/"
docker cp "${SCRIPT_DIR}/amiga-client/." "${CONTAINER_ID}:/tmp/vb_build/amiga-client/"
echo "--- Compiling newlib build (VortexBrowser) ---"
docker exec "${CONTAINER_ID}" bash -c "cd /tmp/vb_build/amiga-client && make clean && make CRT=newlib"

docker cp "${CONTAINER_ID}:/tmp/vb_build/amiga-client/VortexBrowser" "${SCRIPT_DIR}/amiga-client/VortexBrowser"
docker cp "${CONTAINER_ID}:/tmp/vb_build/amiga-client/VortexBrowser.stripped" "${SCRIPT_DIR}/amiga-client/VortexBrowser.stripped"
cp -f "${SCRIPT_DIR}/amiga-client/VortexBrowser" "${SCRIPT_DIR}/amiga-client/VortexBrowse"
cp -f "${SCRIPT_DIR}/amiga-client/VortexBrowser.stripped" "${SCRIPT_DIR}/amiga-client/VortexBrowse.stripped"

echo "=== [3/3] Deploying to QEMU Shared Folder (SHARED:Download/VortexBrowser) ==="
if [ -d "/home/tears/kyvos-2.2.0-linux64/amigaos4/Download" ]; then
    mkdir -p /home/tears/kyvos-2.2.0-linux64/amigaos4/Download/VortexBrowser
    cp "${SCRIPT_DIR}/amiga-client/VortexBrowser" /home/tears/kyvos-2.2.0-linux64/amigaos4/Download/VortexBrowser/VortexBrowser
    cp "${SCRIPT_DIR}/amiga-client/VortexBrowser.stripped" /home/tears/kyvos-2.2.0-linux64/amigaos4/Download/VortexBrowser/VortexBrowser.stripped
    if [ -f "${SCRIPT_DIR}/amiga-client/VortexBrowser.info" ]; then
        cp "${SCRIPT_DIR}/amiga-client/VortexBrowser.info" /home/tears/kyvos-2.2.0-linux64/amigaos4/Download/VortexBrowser/VortexBrowser.info
    fi
    if [ -d "${SCRIPT_DIR}/amiga-client/icons" ]; then
        mkdir -p /home/tears/kyvos-2.2.0-linux64/amigaos4/Download/VortexBrowser/icons
        cp -r "${SCRIPT_DIR}/amiga-client/icons/." /home/tears/kyvos-2.2.0-linux64/amigaos4/Download/VortexBrowser/icons/
    fi
fi

echo "Build complete: VortexBrowser 0.99 RC1 (Newlib) created!"
