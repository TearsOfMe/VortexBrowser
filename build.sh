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
        cp -f VortexRemoteBrowser VortexRemoteBrowser.stripped VortexRemoteBrowser-AltiVec VortexRemoteBrowser-AltiVec.stripped /out_amiga/
        cp -rf catalogs/. /out_amiga/catalogs/
        chown -R ${HOST_UID}:${HOST_GID} /out_amiga/VortexRemoteBrowser /out_amiga/VortexRemoteBrowser.stripped /out_amiga/VortexRemoteBrowser-AltiVec /out_amiga/VortexRemoteBrowser-AltiVec.stripped /out_amiga/catalogs
    "

if [ -f "${SCRIPT_DIR}/amiga-client/VortexRemoteBrowser.info" ]; then
    [ -f "${SCRIPT_DIR}/amiga-client/VortexRemoteBrowser-AltiVec.info" ] || cp -f "${SCRIPT_DIR}/amiga-client/VortexRemoteBrowser.info" "${SCRIPT_DIR}/amiga-client/VortexRemoteBrowser-AltiVec.info"
fi

echo "=== [3/3] Deploying to QEMU Shared Folder ==="
KYVOS_DOWNLOAD_DIR="${KYVOS_DOWNLOAD_DIR:-${HOME}/kyvos-2.2.0-linux64/amigaos4/Download}"
if [ -d "${KYVOS_DOWNLOAD_DIR}" ]; then
    TARGET_DRAWER="${KYVOS_DOWNLOAD_DIR}/VortexRemoteBrowser"
    mkdir -p "${TARGET_DRAWER}"
    cp "${SCRIPT_DIR}/amiga-client/VortexRemoteBrowser" "${TARGET_DRAWER}/VortexRemoteBrowser"
    cp "${SCRIPT_DIR}/amiga-client/VortexRemoteBrowser.stripped" "${TARGET_DRAWER}/VortexRemoteBrowser.stripped"
    cp "${SCRIPT_DIR}/amiga-client/VortexRemoteBrowser-AltiVec" "${TARGET_DRAWER}/VortexRemoteBrowser-AltiVec"
    cp "${SCRIPT_DIR}/amiga-client/VortexRemoteBrowser-AltiVec.stripped" "${TARGET_DRAWER}/VortexRemoteBrowser-AltiVec.stripped"
    [ -f "${SCRIPT_DIR}/amiga-client/VortexRemoteBrowser.info" ] && cp "${SCRIPT_DIR}/amiga-client/VortexRemoteBrowser.info" "${TARGET_DRAWER}/VortexRemoteBrowser.info"
    [ -f "${SCRIPT_DIR}/amiga-client/VortexRemoteBrowser-AltiVec.info" ] && cp "${SCRIPT_DIR}/amiga-client/VortexRemoteBrowser-AltiVec.info" "${TARGET_DRAWER}/VortexRemoteBrowser-AltiVec.info"
    [ -f "${SCRIPT_DIR}/amiga-client/VortexRemoteBrowser_drawer.info" ] && cp "${SCRIPT_DIR}/amiga-client/VortexRemoteBrowser_drawer.info" "${KYVOS_DOWNLOAD_DIR}/VortexRemoteBrowser.info"
    if [ -d "${SCRIPT_DIR}/amiga-client/alt-icons" ]; then
        mkdir -p "${TARGET_DRAWER}/Alternative-Icons"
        cp -r "${SCRIPT_DIR}/amiga-client/alt-icons/." "${TARGET_DRAWER}/Alternative-Icons/"
        [ -f "${SCRIPT_DIR}/amiga-client/alt-icons/VortexRemoteBrowser_drawer.info" ] && cp "${SCRIPT_DIR}/amiga-client/alt-icons/VortexRemoteBrowser_drawer.info" "${TARGET_DRAWER}/Alternative-Icons.info"
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
    elif [ -f "${HOME}/.config/vortexremotebrowser/ftp_auth.env" ]; then
        # shellcheck disable=SC1090
        source "${HOME}/.config/vortexremotebrowser/ftp_auth.env"
    elif [ -f "${HOME}/.config/vortexbrowse/ftp_auth.env" ]; then
        # shellcheck disable=SC1090
        source "${HOME}/.config/vortexbrowse/ftp_auth.env"
    fi
fi

if [ -n "$AMIGA_FTP_AUTH" ] && [ -n "$AMIGA_FTP_HOST" ]; then
    FTP_HOST="$AMIGA_FTP_HOST"
    FTP_BASE_DIR="${AMIGA_FTP_DIR:-/Work2/VortexRemoteBrowser}"
    FTP_URL="ftp://${FTP_HOST}${FTP_BASE_DIR}"

    if curl -s --connect-timeout 2 --max-time 5 --insecure -u "${AMIGA_FTP_AUTH}" --ssl-reqd "ftp://${FTP_HOST}/Work2/" >/dev/null 2>&1; then
        echo "=== Auto-Uploading to Amiga FTP (${FTP_URL}) ==="
        curl -s --max-time 15 --insecure -u "${AMIGA_FTP_AUTH}" --ssl-reqd -T "${SCRIPT_DIR}/amiga-client/VortexRemoteBrowser.stripped" "${FTP_URL}/VortexRemoteBrowser" >/dev/null 2>&1 || true
        curl -s --max-time 15 --insecure -u "${AMIGA_FTP_AUTH}" --ssl-reqd -T "${SCRIPT_DIR}/amiga-client/VortexRemoteBrowser-AltiVec.stripped" "${FTP_URL}/VortexRemoteBrowser-AltiVec" >/dev/null 2>&1 || true
        [ -f "${SCRIPT_DIR}/amiga-client/VortexRemoteBrowser.info" ] && curl -s --max-time 10 --insecure -u "${AMIGA_FTP_AUTH}" --ssl-reqd -T "${SCRIPT_DIR}/amiga-client/VortexRemoteBrowser.info" "${FTP_URL}/VortexRemoteBrowser.info" >/dev/null 2>&1 || true
        [ -f "${SCRIPT_DIR}/amiga-client/VortexRemoteBrowser-AltiVec.info" ] && curl -s --max-time 10 --insecure -u "${AMIGA_FTP_AUTH}" --ssl-reqd -T "${SCRIPT_DIR}/amiga-client/VortexRemoteBrowser-AltiVec.info" "${FTP_URL}/VortexRemoteBrowser-AltiVec.info" >/dev/null 2>&1 || true
        [ -f "${SCRIPT_DIR}/amiga-client/VortexRemoteBrowser_drawer.info" ] && curl -s --max-time 10 --insecure -u "${AMIGA_FTP_AUTH}" --ssl-reqd -T "${SCRIPT_DIR}/amiga-client/VortexRemoteBrowser_drawer.info" "${FTP_URL}.info" >/dev/null 2>&1 || true
        if [ -d "${SCRIPT_DIR}/amiga-client/alt-icons" ]; then
            [ -f "${SCRIPT_DIR}/amiga-client/alt-icons/VortexRemoteBrowser_drawer.info" ] && curl -s --max-time 10 --insecure -u "${AMIGA_FTP_AUTH}" --ssl-reqd -T "${SCRIPT_DIR}/amiga-client/alt-icons/VortexRemoteBrowser_drawer.info" "${FTP_URL}/Alternative-Icons.info" >/dev/null 2>&1 || true
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
        LHA_FILE=$(ls -t "${SCRIPT_DIR}/dist/VortexRemoteBrowser-"*"-AmigaOS4.lha" 2>/dev/null | head -n1 || true)
        if [ -n "$LHA_FILE" ]; then
            LHA_NAME=$(basename "$LHA_FILE")
            curl -s --max-time 30 --insecure -u "${AMIGA_FTP_AUTH}" --ssl-reqd -T "${LHA_FILE}" "${FTP_URL}/${LHA_NAME}" >/dev/null 2>&1 || true
        fi
        echo "[+] Amiga FTP updated successfully!"
    fi
fi

echo "Build complete: VortexRemoteBrowser (Universal + AltiVec) created!"
