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

if [ -f "${SCRIPT_DIR}/cachyos-linux-server/fast_pixconv.c" ]; then
    gcc -O3 -march=znver2 -mtune=znver2 -mavx2 -mfma -fopenmp -DHAVE_TURBOJPEG_LZ4 -shared -fPIC \
        "${SCRIPT_DIR}/cachyos-linux-server/fast_pixconv.c" \
        -lturbojpeg -llz4 \
        -o "${SCRIPT_DIR}/cachyos-linux-server/fast_pixconv.so" 2>/dev/null || \
    gcc -O3 -march=native -shared -fPIC \
        "${SCRIPT_DIR}/cachyos-linux-server/fast_pixconv.c" \
        -o "${SCRIPT_DIR}/cachyos-linux-server/fast_pixconv.so"
fi

gcc -O3 -march=native -shared -fPIC \
    "${SCRIPT_DIR}/rpi4-server/fast_pixconv.c" \
    -o "${SCRIPT_DIR}/rpi4-server/fast_pixconv.so" 2>/dev/null || true

echo "=== [2/3] Cross-Compiling AmigaOS 4.1 PowerPC Client (Universal + AltiVec) via Docker ==="
docker exec "${CONTAINER_ID}" bash -c "mkdir -p /tmp/vb_build/common /tmp/vb_build/amiga-client /tmp/vb_build/sdk_va"
docker cp "${SCRIPT_DIR}/common/." "${CONTAINER_ID}:/tmp/vb_build/common/"
docker cp "${SCRIPT_DIR}/amiga-client/." "${CONTAINER_ID}:/tmp/vb_build/amiga-client/"
docker cp "${SCRIPT_DIR}/sdk_va/." "${CONTAINER_ID}:/tmp/vb_build/sdk_va/"
echo "--- Compiling Universal (Dynamic CPU Detection) and Pure AltiVec builds ---"
docker exec "${CONTAINER_ID}" bash -c "cd /tmp/vb_build/amiga-client && make clean && make"

docker cp "${CONTAINER_ID}:/tmp/vb_build/amiga-client/VortexBrowser" "${SCRIPT_DIR}/amiga-client/VortexBrowser"
docker cp "${CONTAINER_ID}:/tmp/vb_build/amiga-client/VortexBrowser.stripped" "${SCRIPT_DIR}/amiga-client/VortexBrowser.stripped"
docker cp "${CONTAINER_ID}:/tmp/vb_build/amiga-client/VortexBrowser-AltiVec" "${SCRIPT_DIR}/amiga-client/VortexBrowser-AltiVec"
docker cp "${CONTAINER_ID}:/tmp/vb_build/amiga-client/VortexBrowser-AltiVec.stripped" "${SCRIPT_DIR}/amiga-client/VortexBrowser-AltiVec.stripped"
docker cp "${CONTAINER_ID}:/tmp/vb_build/amiga-client/VortexBrowser-MUI" "${SCRIPT_DIR}/amiga-client/VortexBrowser-MUI"
docker cp "${CONTAINER_ID}:/tmp/vb_build/amiga-client/VortexBrowser-MUI.stripped" "${SCRIPT_DIR}/amiga-client/VortexBrowser-MUI.stripped"
cp -f "${SCRIPT_DIR}/amiga-client/VortexBrowser" "${SCRIPT_DIR}/amiga-client/VortexBrowse"
cp -f "${SCRIPT_DIR}/amiga-client/VortexBrowser.stripped" "${SCRIPT_DIR}/amiga-client/VortexBrowse.stripped"

if [ -f "${SCRIPT_DIR}/amiga-client/VortexBrowser.info" ]; then
    [ -f "${SCRIPT_DIR}/amiga-client/VortexBrowser-AltiVec.info" ] || cp -f "${SCRIPT_DIR}/amiga-client/VortexBrowser.info" "${SCRIPT_DIR}/amiga-client/VortexBrowser-AltiVec.info"
    [ -f "${SCRIPT_DIR}/amiga-client/VortexBrowser-MUI.info" ] || cp -f "${SCRIPT_DIR}/amiga-client/VortexBrowser.info" "${SCRIPT_DIR}/amiga-client/VortexBrowser-MUI.info"
fi

echo "=== [3/3] Deploying to QEMU Shared Folder ==="
KYVOS_DOWNLOAD_DIR="${KYVOS_DOWNLOAD_DIR:-${HOME}/kyvos-2.2.0-linux64/amigaos4/Download}"
if [ -d "${KYVOS_DOWNLOAD_DIR}" ]; then
    TARGET_DRAWER="${KYVOS_DOWNLOAD_DIR}/VortexBrowser"
    mkdir -p "${TARGET_DRAWER}"
    cp "${SCRIPT_DIR}/amiga-client/VortexBrowser" "${TARGET_DRAWER}/VortexBrowser"
    cp "${SCRIPT_DIR}/amiga-client/VortexBrowser.stripped" "${TARGET_DRAWER}/VortexBrowser.stripped"
    cp "${SCRIPT_DIR}/amiga-client/VortexBrowser-AltiVec" "${TARGET_DRAWER}/VortexBrowser-AltiVec"
    cp "${SCRIPT_DIR}/amiga-client/VortexBrowser-AltiVec.stripped" "${TARGET_DRAWER}/VortexBrowser-AltiVec.stripped"
    cp "${SCRIPT_DIR}/amiga-client/VortexBrowser-MUI" "${TARGET_DRAWER}/VortexBrowser-MUI"
    cp "${SCRIPT_DIR}/amiga-client/VortexBrowser-MUI.stripped" "${TARGET_DRAWER}/VortexBrowser-MUI.stripped"
    [ -f "${SCRIPT_DIR}/amiga-client/VortexBrowser.info" ] && cp "${SCRIPT_DIR}/amiga-client/VortexBrowser.info" "${TARGET_DRAWER}/VortexBrowser.info"
    [ -f "${SCRIPT_DIR}/amiga-client/VortexBrowser-AltiVec.info" ] && cp "${SCRIPT_DIR}/amiga-client/VortexBrowser-AltiVec.info" "${TARGET_DRAWER}/VortexBrowser-AltiVec.info"
    [ -f "${SCRIPT_DIR}/amiga-client/VortexBrowser-MUI.info" ] && cp "${SCRIPT_DIR}/amiga-client/VortexBrowser-MUI.info" "${TARGET_DRAWER}/VortexBrowser-MUI.info"
    [ -f "${SCRIPT_DIR}/amiga-client/VortexBrowser_drawer.info" ] && cp "${SCRIPT_DIR}/amiga-client/VortexBrowser_drawer.info" "${KYVOS_DOWNLOAD_DIR}/VortexBrowser.info"
    if [ -d "${SCRIPT_DIR}/amiga-client/alt-icons" ]; then
        mkdir -p "${TARGET_DRAWER}/Alternative-Icons"
        cp -r "${SCRIPT_DIR}/amiga-client/alt-icons/." "${TARGET_DRAWER}/Alternative-Icons/"
        [ -f "${SCRIPT_DIR}/amiga-client/alt-icons/VortexBrowser_drawer.info" ] && cp "${SCRIPT_DIR}/amiga-client/alt-icons/VortexBrowser_drawer.info" "${TARGET_DRAWER}/Alternative-Icons.info"
    fi
    if [ -d "${SCRIPT_DIR}/amiga-client/icons" ]; then
        mkdir -p "${TARGET_DRAWER}/icons"
        cp -r "${SCRIPT_DIR}/amiga-client/icons/." "${TARGET_DRAWER}/icons/"
    fi
    echo "[+] QEMU Shared Folder updated: ${TARGET_DRAWER}"
fi

# Optional: Auto-Deploy to Amiga FTPS server if configured
# Credentials can be set via environment ($AMIGA_FTP_AUTH) or local uncommitted config file
if [ -z "$AMIGA_FTP_AUTH" ]; then
    if [ -f "${SCRIPT_DIR}/.ftp_auth" ]; then
        # shellcheck disable=SC1091
        source "${SCRIPT_DIR}/.ftp_auth"
    elif [ -f "${HOME}/.config/vortexbrowse/ftp_auth.env" ]; then
        # shellcheck disable=SC1090
        source "${HOME}/.config/vortexbrowse/ftp_auth.env"
    fi
fi

if [ -n "$AMIGA_FTP_AUTH" ] && [ -n "$AMIGA_FTP_HOST" ]; then
    FTP_HOST="$AMIGA_FTP_HOST"
    FTP_BASE_DIR="${AMIGA_FTP_DIR:-/Work2/VortexBrowser}"
    FTP_URL="ftp://${FTP_HOST}${FTP_BASE_DIR}"

    if curl -s --connect-timeout 2 --max-time 5 --insecure -u "${AMIGA_FTP_AUTH}" --ssl-reqd "ftp://${FTP_HOST}/Work2/" >/dev/null 2>&1; then
        echo "=== Auto-Uploading to Amiga FTP (${FTP_URL}) ==="
        curl -s --max-time 15 --insecure -u "${AMIGA_FTP_AUTH}" --ssl-reqd -T "${SCRIPT_DIR}/amiga-client/VortexBrowser.stripped" "${FTP_URL}/VortexBrowser" >/dev/null 2>&1 || true
        curl -s --max-time 15 --insecure -u "${AMIGA_FTP_AUTH}" --ssl-reqd -T "${SCRIPT_DIR}/amiga-client/VortexBrowser-AltiVec.stripped" "${FTP_URL}/VortexBrowser-AltiVec" >/dev/null 2>&1 || true
        curl -s --max-time 15 --insecure -u "${AMIGA_FTP_AUTH}" --ssl-reqd -T "${SCRIPT_DIR}/amiga-client/VortexBrowser-MUI.stripped" "${FTP_URL}/VortexBrowser-MUI" >/dev/null 2>&1 || true
        [ -f "${SCRIPT_DIR}/amiga-client/VortexBrowser.info" ] && curl -s --max-time 10 --insecure -u "${AMIGA_FTP_AUTH}" --ssl-reqd -T "${SCRIPT_DIR}/amiga-client/VortexBrowser.info" "${FTP_URL}/VortexBrowser.info" >/dev/null 2>&1 || true
        [ -f "${SCRIPT_DIR}/amiga-client/VortexBrowser-MUI.info" ] && curl -s --max-time 10 --insecure -u "${AMIGA_FTP_AUTH}" --ssl-reqd -T "${SCRIPT_DIR}/amiga-client/VortexBrowser-MUI.info" "${FTP_URL}/VortexBrowser-MUI.info" >/dev/null 2>&1 || true
        [ -f "${SCRIPT_DIR}/amiga-client/VortexBrowser-AltiVec.info" ] && curl -s --max-time 10 --insecure -u "${AMIGA_FTP_AUTH}" --ssl-reqd -T "${SCRIPT_DIR}/amiga-client/VortexBrowser-AltiVec.info" "${FTP_URL}/VortexBrowser-AltiVec.info" >/dev/null 2>&1 || true
        [ -f "${SCRIPT_DIR}/amiga-client/VortexBrowser_drawer.info" ] && curl -s --max-time 10 --insecure -u "${AMIGA_FTP_AUTH}" --ssl-reqd -T "${SCRIPT_DIR}/amiga-client/VortexBrowser_drawer.info" "${FTP_URL}.info" >/dev/null 2>&1 || true
        if [ -d "${SCRIPT_DIR}/amiga-client/alt-icons" ]; then
            [ -f "${SCRIPT_DIR}/amiga-client/alt-icons/VortexBrowser_drawer.info" ] && curl -s --max-time 10 --insecure -u "${AMIGA_FTP_AUTH}" --ssl-reqd -T "${SCRIPT_DIR}/amiga-client/alt-icons/VortexBrowser_drawer.info" "${FTP_URL}/Alternative-Icons.info" >/dev/null 2>&1 || true
            for af in "${SCRIPT_DIR}/amiga-client/alt-icons/"*.info; do
                [ -f "$af" ] && curl -s --max-time 10 --insecure -u "${AMIGA_FTP_AUTH}" --ssl-reqd --ftp-create-dirs -T "$af" "${FTP_URL}/Alternative-Icons/$(basename "$af")" >/dev/null 2>&1 || true
            done
        fi
        LHA_FILE=$(ls -t "${SCRIPT_DIR}/dist/VortexBrowser-"*"-AmigaOS4.lha" 2>/dev/null | head -n1 || true)
        if [ -n "$LHA_FILE" ]; then
            LHA_NAME=$(basename "$LHA_FILE")
            curl -s --max-time 30 --insecure -u "${AMIGA_FTP_AUTH}" --ssl-reqd -T "${LHA_FILE}" "${FTP_URL}/${LHA_NAME}" >/dev/null 2>&1 || true
        fi
        echo "[+] Amiga FTP updated successfully!"
    fi
fi

echo "Build complete: VortexBrowser 0.99 RC3 (Universal + AltiVec + MUI) created!"
