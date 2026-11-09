#!/usr/bin/env bash
# ==============================================================================
# VortexBrowser Server - Intel (Atom / Celeron / Core) 1-Click Installer
# Optimized for Intel HD / UHD Graphics (Cherry Trail, Braswell, Skylake+)
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "============================================================================"
echo "  VortexBrowser Server - Intel CPU & GPU Setup (VA-API / EGL / SSE SIMD)   "
echo "============================================================================"

# 1. Hardware-Erkennung
CPU_MODEL=$(grep -m1 "model name" /proc/cpuinfo 2>/dev/null | cut -d: -f2 | xargs || uname -m)
CPU_CORES=$(nproc 2>/dev/null || echo 4)
echo "[+] Erkannte CPU: ${CPU_MODEL} (${CPU_CORES} Kerne)"

IS_ATOM=false
if grep -qiE "atom|x5-|x7-|celeron|pentium|z8350|z8300|n3450|n4000" /proc/cpuinfo 2>/dev/null; then
    IS_ATOM=true
    echo "[i] Low-Power Intel CPU erkannt (Atom/Celeron). Optimiertes 2-Thread-Rastering aktiv."
fi

# 2. Systempakete & Intel VA-API Treiber installieren
echo "[+] Pruefe und installiere Intel Grafik-Treiber und Systembibliotheken..."
if command -v apt-get &>/dev/null; then
    sudo apt-get update -qq || true
    # libva-intel-driver (i965) ist ideal fuer Gen8 (Atom x5-Z8350), intel-media-va-driver (iHD) fuer Gen9+
    sudo apt-get install -y --no-install-recommends \
        python3 python3-venv python3-pip gcc \
        libva-intel-driver intel-media-va-driver vainfo \
        libturbojpeg0-dev liblz4-dev libjpeg-dev \
        pipewire-pulse pulseaudio-utils || true
elif command -v pacman &>/dev/null; then
    sudo pacman -S --needed --noconfirm \
        python python-pip gcc \
        libva-intel-driver intel-media-driver libva-utils \
        libjpeg-turbo lz4 pipewire-pulse || true
elif command -v dnf &>/dev/null; then
    sudo dnf install -y \
        python3 python3-pip gcc \
        libva-intel-driver intel-media-driver libva-utils \
        turbojpeg-devel lz4-devel || true
fi

# 3. Python Virtual Environment (.venv)
if [ ! -d ".venv" ]; then
    echo "[+] Erstelle Python Virtual Environment (.venv)..."
    python3 -m venv .venv
fi

echo "[+] Installiere/Aktualisiere Python-Abhaengigkeiten..."
.venv/bin/pip install --upgrade pip
.venv/bin/pip install -r requirements.txt

# 4. Playwright Chromium installieren
echo "[+] Installiere Playwright Chromium..."
.venv/bin/playwright install chromium
if [ -f /etc/debian_version ] || [ -f /etc/os-release ]; then
    sudo .venv/bin/playwright install-deps chromium 2>/dev/null || true
fi

# 5. Native C-SIMD Pixelbeschleunigung kompilieren
echo "[+] Kompiliere native x86 SIMD Pixelbeschleunigung (fast_pixconv.so)..."
if command -v gcc &>/dev/null && [ -f "fast_pixconv.c" ]; then
    gcc -O3 -march=native -msse4.2 -DHAVE_TURBOJPEG_LZ4 -shared -fPIC \
        fast_pixconv.c -lturbojpeg -llz4 -o fast_pixconv.so 2>/dev/null || \
    gcc -O3 -march=native -msse4.2 -shared -fPIC fast_pixconv.c -o fast_pixconv.so 2>/dev/null || \
    gcc -O3 -march=native -shared -fPIC fast_pixconv.c -o fast_pixconv.so
    echo "[OK] fast_pixconv.so erfolgreich kompiliert."
fi

# 6. VA-API Hardwarebeschleunigung testen
echo ""
echo "--- Intel VA-API Hardware-Status ---"
if command -v vainfo &>/dev/null; then
    # Teste zuerst i965 (ideal fuer Atom x5 Cherryview)
    if LIBVA_DRIVER_NAME=i965 vainfo 2>&1 | grep -q "VAProfileH264"; then
        echo "[OK] Intel VA-API verfuegbar ueber i965 Treiber (Hardware H.264 aktiv)!"
        echo "i965" > .va_driver
    elif LIBVA_DRIVER_NAME=iHD vainfo 2>&1 | grep -q "VAProfileH264"; then
        echo "[OK] Intel VA-API verfuegbar ueber iHD Treiber (Hardware H.264 aktiv)!"
        echo "iHD" > .va_driver
    elif vainfo 2>&1 | grep -q "VAProfileH264"; then
        echo "[OK] Intel VA-API verfuegbar (Standard-Treiber)!"
    else
        echo "[!] Hinweis: vainfo meldet keinen direkten H.264 Hardware-Dekoder. EGL-Rasterisierung bleibt aktiv."
    fi
else
    echo "[i] vainfo nicht installiert, Erkennung erfolgt dynamisch im Server."
fi

# 7. Systemd Service Datei erstellen
CURRENT_USER="${SUDO_USER:-$(id -un)}"
cat <<EOF > vortexbrowser.service
[Unit]
Description=VortexBrowser Server for AmigaOS 4 (Intel Profile)
After=network-online.target sound.target
Wants=network-online.target

[Service]
Type=simple
User=$CURRENT_USER
WorkingDirectory=$SCRIPT_DIR
Environment=PYTHONUNBUFFERED=1
Environment=XDG_RUNTIME_DIR=/run/user/\$(id -u "$CURRENT_USER" 2>/dev/null || echo 1000)
ExecStart=$SCRIPT_DIR/run_intel.sh
Restart=always
RestartSec=2
Nice=-5

[Install]
WantedBy=multi-user.target
EOF

chmod +x run_intel.sh 2>/dev/null || true

IP_ADDR=$(hostname -I 2>/dev/null | awk '{print $1}' || echo "127.0.0.1")

echo ""
echo "============================================================================"
echo "  INTEL VORTEXBROWSER SERVER ERFOLGREICH INSTALLIERT!"
echo "============================================================================"
echo "  Server-IP fuer AmigaOS 4 Client : ${IP_ADDR}"
echo "  Server-Ports                    : 7788 & 9757"
echo "  Empfohlene Amiga-Aufloesung     : 1024x768 oder 1280x720 @ 15-20 FPS"
echo ""
echo "  Starten Sie den Server jetzt mit:"
echo "    ./run_intel.sh"
echo "============================================================================"
