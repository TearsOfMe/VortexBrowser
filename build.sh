#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONTAINER_ID="${1:-7f239cb96d8c259d2d5489d6df3a79b3ef6b736346acb46960f3f928c40f17fd}"

echo "=== [1/3] Building CachyOS Native SIMD Pixel Converter (fast_pixconv.so) ==="
gcc -O3 -march=native -shared -fPIC \
    "${SCRIPT_DIR}/pi-server/fast_pixconv.c" \
    -o "${SCRIPT_DIR}/pi-server/fast_pixconv.so"
gcc -O3 -march=native -shared -fPIC \
    "${SCRIPT_DIR}/rpi4-server/fast_pixconv.c" \
    -o "${SCRIPT_DIR}/rpi4-server/fast_pixconv.so" 2>/dev/null || true

echo "=== [2/3] Cross-Compiling AmigaOS 4.1 PowerPC Client (Newlib & clib4) via Docker ==="
docker exec "${CONTAINER_ID}" bash -c "mkdir -p /tmp/vb_build/common /tmp/vb_build/amiga-client"
docker cp "${SCRIPT_DIR}/common/." "${CONTAINER_ID}:/tmp/vb_build/common/"
docker cp "${SCRIPT_DIR}/amiga-client/." "${CONTAINER_ID}:/tmp/vb_build/amiga-client/"
echo "--- Compiling newlib build (VortexBrowse) ---"
docker exec "${CONTAINER_ID}" bash -c "cd /tmp/vb_build/amiga-client && make clean && make CRT=newlib"
echo "--- Compiling clib4 build (VortexBrowse.clib4) ---"
docker exec "${CONTAINER_ID}" bash -c "cd /tmp/vb_build/amiga-client && rm -f src/*.o && make CRT=clib4"

docker cp "${CONTAINER_ID}:/tmp/vb_build/amiga-client/VortexBrowse" "${SCRIPT_DIR}/amiga-client/VortexBrowse"
docker cp "${CONTAINER_ID}:/tmp/vb_build/amiga-client/VortexBrowse.stripped" "${SCRIPT_DIR}/amiga-client/VortexBrowse.stripped"
docker cp "${CONTAINER_ID}:/tmp/vb_build/amiga-client/VortexBrowse.clib4" "${SCRIPT_DIR}/amiga-client/VortexBrowse.clib4"
docker cp "${CONTAINER_ID}:/tmp/vb_build/amiga-client/VortexBrowse.clib4.stripped" "${SCRIPT_DIR}/amiga-client/VortexBrowse.clib4.stripped"

echo "=== [3/3] Deploying to QEMU Shared Folder (SHARED:Download/VortexBrowse) ==="
if [ -d "/home/tears/kyvos-2.2.0-linux64/amigaos4/Download" ]; then
    mkdir -p /home/tears/kyvos-2.2.0-linux64/amigaos4/Download/VortexBrowse
    cp "${SCRIPT_DIR}/amiga-client/VortexBrowse" /home/tears/kyvos-2.2.0-linux64/amigaos4/Download/VortexBrowse/VortexBrowse
    cp "${SCRIPT_DIR}/amiga-client/VortexBrowse.stripped" /home/tears/kyvos-2.2.0-linux64/amigaos4/Download/VortexBrowse/VortexBrowse.stripped
    cp "${SCRIPT_DIR}/amiga-client/VortexBrowse.clib4" /home/tears/kyvos-2.2.0-linux64/amigaos4/Download/VortexBrowse/VortexBrowse.clib4
    cp "${SCRIPT_DIR}/amiga-client/VortexBrowse.clib4.stripped" /home/tears/kyvos-2.2.0-linux64/amigaos4/Download/VortexBrowse/VortexBrowse.clib4.stripped
    if [ -f "${SCRIPT_DIR}/amiga-client/VortexBrowse.info" ]; then
        cp "${SCRIPT_DIR}/amiga-client/VortexBrowse.info" /home/tears/kyvos-2.2.0-linux64/amigaos4/Download/VortexBrowse/VortexBrowse.info
        cp "${SCRIPT_DIR}/amiga-client/VortexBrowse.info" /home/tears/kyvos-2.2.0-linux64/amigaos4/Download/VortexBrowse/VortexBrowse.clib4.info
    fi
    if [ -d "${SCRIPT_DIR}/amiga-client/icons" ]; then
        mkdir -p /home/tears/kyvos-2.2.0-linux64/amigaos4/Download/VortexBrowse/icons
        cp -r "${SCRIPT_DIR}/amiga-client/icons/." /home/tears/kyvos-2.2.0-linux64/amigaos4/Download/VortexBrowse/icons/
    fi
fi

echo "Build complete: Newlib and clib4 versions created!"
