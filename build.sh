#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOCKER_IMAGE="${VORTEX_OS4_DOCKER_IMAGE:-walkero/amigagccondocker:os4-gcc15-3.7.0}"
HOST_UID="$(id -u)"
HOST_GID="$(id -g)"

echo "=== [1/3] Building Universal Native SIMD Pixel Converter (fast_pixconv.so) ==="
if [ -f "${SCRIPT_DIR}/server/fast_pixconv.c" ]; then
    gcc -O3 -march=native -mtune=native -fopenmp -DHAVE_TURBOJPEG_LZ4 -shared -fPIC \
        "${SCRIPT_DIR}/server/fast_pixconv.c" \
        -lturbojpeg -llz4 \
        -o "${SCRIPT_DIR}/server/fast_pixconv.so" 2>/dev/null || \
    gcc -O3 -march=native -shared -fPIC \
        "${SCRIPT_DIR}/server/fast_pixconv.c" \
        -o "${SCRIPT_DIR}/server/fast_pixconv.so" 2>/dev/null || true
fi

echo "=== [2/3] Cross-Compiling AmigaOS 4.1 PowerPC Client (Universal + AltiVec) via Docker (${DOCKER_IMAGE}) ==="
echo "--- Compiling Universal (Dynamic CPU Detection) and Pure AltiVec builds ---"
docker run --rm \
    -v "${SCRIPT_DIR}:/src:ro" \
    -v "${SCRIPT_DIR}/amiga-client:/out_amiga" \
    "${DOCKER_IMAGE}" bash -c "
        set -e
        mkdir -p /tmp/vb_build
        cp -r /src/common /src/amiga-client /src/sdk_va /tmp/vb_build/
        cd /tmp/vb_build/amiga-client
        make clean && make
        cp -f VortexBrowser VortexBrowser.stripped VortexBrowser-AltiVec VortexBrowser-AltiVec.stripped /out_amiga/
        cp -rf catalogs/. /out_amiga/catalogs/
        chown -R ${HOST_UID}:${HOST_GID} /out_amiga/VortexBrowser /out_amiga/VortexBrowser.stripped /out_amiga/VortexBrowser-AltiVec /out_amiga/VortexBrowser-AltiVec.stripped /out_amiga/catalogs
    "
cp -f "${SCRIPT_DIR}/amiga-client/VortexBrowser" "${SCRIPT_DIR}/amiga-client/VortexBrowse"
cp -f "${SCRIPT_DIR}/amiga-client/VortexBrowser.stripped" "${SCRIPT_DIR}/amiga-client/VortexBrowse.stripped"

if [ -f "${SCRIPT_DIR}/amiga-client/VortexBrowser.info" ]; then
    [ -f "${SCRIPT_DIR}/amiga-client/VortexBrowser-AltiVec.info" ] || cp -f "${SCRIPT_DIR}/amiga-client/VortexBrowser.info" "${SCRIPT_DIR}/amiga-client/VortexBrowser-AltiVec.info"
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
    [ -f "${SCRIPT_DIR}/amiga-client/VortexBrowser.info" ] && cp "${SCRIPT_DIR}/amiga-client/VortexBrowser.info" "${TARGET_DRAWER}/VortexBrowser.info"
    [ -f "${SCRIPT_DIR}/amiga-client/VortexBrowser-AltiVec.info" ] && cp "${SCRIPT_DIR}/amiga-client/VortexBrowser-AltiVec.info" "${TARGET_DRAWER}/VortexBrowser-AltiVec.info"
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
    if [ -d "${SCRIPT_DIR}/amiga-client/catalogs" ]; then
        mkdir -p "${TARGET_DRAWER}/catalogs"
        cp -r "${SCRIPT_DIR}/amiga-client/catalogs/." "${TARGET_DRAWER}/catalogs/"
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
        [ -f "${SCRIPT_DIR}/amiga-client/VortexBrowser.info" ] && curl -s --max-time 10 --insecure -u "${AMIGA_FTP_AUTH}" --ssl-reqd -T "${SCRIPT_DIR}/amiga-client/VortexBrowser.info" "${FTP_URL}/VortexBrowser.info" >/dev/null 2>&1 || true
        [ -f "${SCRIPT_DIR}/amiga-client/VortexBrowser-AltiVec.info" ] && curl -s --max-time 10 --insecure -u "${AMIGA_FTP_AUTH}" --ssl-reqd -T "${SCRIPT_DIR}/amiga-client/VortexBrowser-AltiVec.info" "${FTP_URL}/VortexBrowser-AltiVec.info" >/dev/null 2>&1 || true
        [ -f "${SCRIPT_DIR}/amiga-client/VortexBrowser_drawer.info" ] && curl -s --max-time 10 --insecure -u "${AMIGA_FTP_AUTH}" --ssl-reqd -T "${SCRIPT_DIR}/amiga-client/VortexBrowser_drawer.info" "${FTP_URL}.info" >/dev/null 2>&1 || true
        if [ -d "${SCRIPT_DIR}/amiga-client/alt-icons" ]; then
            [ -f "${SCRIPT_DIR}/amiga-client/alt-icons/VortexBrowser_drawer.info" ] && curl -s --max-time 10 --insecure -u "${AMIGA_FTP_AUTH}" --ssl-reqd -T "${SCRIPT_DIR}/amiga-client/alt-icons/VortexBrowser_drawer.info" "${FTP_URL}/Alternative-Icons.info" >/dev/null 2>&1 || true
            for af in "${SCRIPT_DIR}/amiga-client/alt-icons/"*.info; do
                [ -f "$af" ] && curl -s --max-time 10 --insecure -u "${AMIGA_FTP_AUTH}" --ssl-reqd --ftp-create-dirs -T "$af" "${FTP_URL}/Alternative-Icons/$(basename "$af")" >/dev/null 2>&1 || true
            done
        fi
        if [ -d "${SCRIPT_DIR}/amiga-client/catalogs" ]; then
            for cfile in "${SCRIPT_DIR}/amiga-client/catalogs"/*/*.catalog; do
                if [ -f "$cfile" ]; then
                    rel_path=$(echo "$cfile" | sed "s|${SCRIPT_DIR}/amiga-client/||")
                    curl -s --max-time 15 --insecure -u "${AMIGA_FTP_AUTH}" --ssl-reqd --ftp-create-dirs -T "$cfile" "${FTP_URL}/${rel_path}" >/dev/null 2>&1 || true
                fi
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

echo "Build complete: VortexBrowser 1.0 (Universal + AltiVec) created!"
