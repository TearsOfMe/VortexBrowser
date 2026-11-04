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
if [ -d "$HOME/kyvos-2.2.0-linux64/amigaos4/Download" ]; then
    mkdir -p $HOME/kyvos-2.2.0-linux64/amigaos4/Download/VortexBrowser
    cp "${SCRIPT_DIR}/amiga-client/VortexBrowser" $HOME/kyvos-2.2.0-linux64/amigaos4/Download/VortexBrowser/VortexBrowser
    cp "${SCRIPT_DIR}/amiga-client/VortexBrowser.stripped" $HOME/kyvos-2.2.0-linux64/amigaos4/Download/VortexBrowser/VortexBrowser.stripped
    if [ -f "${SCRIPT_DIR}/amiga-client/VortexBrowser.info" ]; then
        cp "${SCRIPT_DIR}/amiga-client/VortexBrowser.info" $HOME/kyvos-2.2.0-linux64/amigaos4/Download/VortexBrowser/VortexBrowser.info
    fi
    if [ -d "${SCRIPT_DIR}/amiga-client/icons" ]; then
        mkdir -p $HOME/kyvos-2.2.0-linux64/amigaos4/Download/VortexBrowser/icons
        cp -r "${SCRIPT_DIR}/amiga-client/icons/." $HOME/kyvos-2.2.0-linux64/amigaos4/Download/VortexBrowser/icons/
    fi
fi

# Optional: Auto-Deploy to Amiga FTP (192.168.178.15) if available
if curl -s --connect-timeout 2 --insecure -u "${AMIGA_FTP_AUTH}" --ssl-reqd "ftp://192.168.178.15/Work2/" >/dev/null 2>&1; then
    echo "=== Auto-Uploading to Amiga FTP (192.168.178.15 /Work2/VortexBrowser) ==="
    curl -s --insecure -u "${AMIGA_FTP_AUTH}" --ssl-reqd -T "${SCRIPT_DIR}/amiga-client/VortexBrowser.stripped" "ftp://192.168.178.15/Work2/VortexBrowser/VortexBrowser" >/dev/null 2>&1 || true
    if [ -f "${SCRIPT_DIR}/dist/VortexBrowser-0.99-RC2-AmigaOS4.lha" ]; then
        curl -s --insecure -u "${AMIGA_FTP_AUTH}" --ssl-reqd -T "${SCRIPT_DIR}/dist/VortexBrowser-0.99-RC2-AmigaOS4.lha" "ftp://192.168.178.15/Work2/VortexBrowser/VortexBrowser-0.99-RC2-AmigaOS4.lha" >/dev/null 2>&1 || true
    fi
    echo "[+] Amiga FTP updated successfully!"
fi

echo "Build complete: VortexBrowser 0.99 RC2 (Newlib) created!"
