#!/usr/bin/env bash
# ==============================================================================
# VortexBrowser Server - CachyOS & HP EliteDesk 805 G6 (AMD Ryzen PRO) Installer
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "============================================================================"
echo "  VortexBrowser Server - CachyOS HP EliteDesk 805 G6 (1080p@25fps) Setup    "
echo "============================================================================"

if command -v pacman &>/dev/null; then
    echo "[+] Pruefe CachyOS / Arch Systempakete (libjpeg-turbo, lz4, mesa, vulkan-radeon, power-profiles-daemon, gcc)..."
    sudo pacman -S --needed --noconfirm libjpeg-turbo lz4 gcc mesa vulkan-radeon libva-mesa-driver pipewire-pulse power-profiles-daemon 2>/dev/null || true
fi

if ! command -v python3 &>/dev/null; then
    echo "[!] FEHLER: python3 wurde nicht gefunden. Bitte installieren Sie Python 3.10+."
    exit 1
fi

if [ ! -d ".venv" ]; then
    echo "[+] Erstelle Python Virtual Environment (.venv)..."
    python3 -m venv .venv
fi

echo "[+] Installiere Python-Abhaengigkeiten (playwright, lz4, Pillow, PyTurboJPEG)..."
.venv/bin/pip install --upgrade pip
.venv/bin/pip install -r requirements.txt

echo "[+] Installiere Playwright Chromium Browser-Engine..."
.venv/bin/playwright install chromium

if command -v gcc &>/dev/null && [ -f "fast_pixconv.c" ]; then
    echo "[+] Kompiliere C-native AMD Zen 2 (znver2) AVX2 + OpenMP + TurboJPEG + LZ4 Pipeline..."
    gcc -O3 -march=znver2 -mtune=znver2 -mavx2 -mfma -fopenmp -DHAVE_TURBOJPEG_LZ4 -shared -fPIC \
        fast_pixconv.c -lturbojpeg -llz4 -o fast_pixconv.so 2>/dev/null || \
    gcc -O3 -march=native -mavx2 -mfma -fopenmp -DHAVE_TURBOJPEG_LZ4 -shared -fPIC \
        fast_pixconv.c -lturbojpeg -llz4 -o fast_pixconv.so 2>/dev/null || \
    gcc -O3 -march=native -shared -fPIC fast_pixconv.c -o fast_pixconv.so
fi

chmod +x run_server.sh

IP_ADDR=$(hostname -I 2>/dev/null | awk '{print $1}' || echo "192.168.178.2")

echo ""
echo "============================================================================"
echo "  CACHYOS HP ELITEDESK 805 G6 SERVER ERFOLGREICH INSTALLIERT!"
echo "============================================================================"
echo "  Server-IP fuer AmigaOS 4 Client : ${IP_ADDR}"
echo "  Server-Ports                    : 7788 & 9757"
echo "  Standard-Aufloesung             : 1920x1080 (Full HD) @ 25 FPS"
echo ""
echo "  Starten Sie den Server jetzt mit:"
echo "    ./run_server.sh"
echo "============================================================================"
