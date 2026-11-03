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

# 1. Systempakete pruefen und nur bei Bedarf installieren
if command -v pacman &>/dev/null; then
    MISSING_PKGS=()
    for pkg in libjpeg-turbo lz4 gcc mesa vulkan-radeon libva-mesa-driver pipewire-pulse power-profiles-daemon; do
        if ! pacman -Qi "$pkg" &>/dev/null; then
            MISSING_PKGS+=("$pkg")
        fi
    done
    if [ ${#MISSING_PKGS[@]} -gt 0 ]; then
        echo "[+] Installiere fehlende Systempakete: ${MISSING_PKGS[*]}..."
        sudo pacman -S --needed --noconfirm "${MISSING_PKGS[@]}" 2>/dev/null || true
    else
        echo "[OK] Alle erforderlichen Systempakete sind bereits vorhanden."
    fi
fi

if ! command -v python3 &>/dev/null; then
    echo "[!] FEHLER: python3 wurde nicht gefunden. Bitte installieren Sie Python 3.10+."
    exit 1
fi

# 2. Python Virtual Environment & Abhaengigkeiten pruefen (kein unnoetiger Download)
DEPS_OK=false
if [ -d ".venv" ] && .venv/bin/python3 -c "import playwright, lz4, PIL, websockets" &>/dev/null; then
    DEPS_OK=true
fi

if [ "$DEPS_OK" = true ]; then
    echo "[OK] Python Virtual Environment & Abhaengigkeiten bereits vorhanden (kein Download erforderlich)."
else
    if [ ! -d ".venv" ]; then
        echo "[+] Erstelle Python Virtual Environment (.venv)..."
        python3 -m venv .venv
    fi
    echo "[+] Installiere/Aktualisiere Python-Abhaengigkeiten..."
    .venv/bin/pip install --upgrade pip
    .venv/bin/pip install -r requirements.txt
fi

# 3. Vorhandene Browser-Engine pruefen (Chromium / Ungoogled-Chromium / Chrome / Brave / Playwright Cache)
EXISTING_BROWSER=""
for b in ungoogled-chromium chromium-browser chromium google-chrome google-chrome-stable brave-browser brave /usr/lib/chromium/chromium; do
    if command -v "$b" &>/dev/null; then
        EXISTING_BROWSER="$(command -v "$b" 2>/dev/null)"
        break
    elif [ -x "$b" ]; then
        EXISTING_BROWSER="$b"
        break
    fi
done

# Falls systemweit nicht gefunden, Playwright-Cache pruefen
if [ -z "$EXISTING_BROWSER" ]; then
    PW_CHROME=$(ls -d ~/.cache/ms-playwright/chromium-*/chrome-linux*/chrome 2>/dev/null | tail -n 1 || true)
    if [ -n "$PW_CHROME" ] && [ -x "$PW_CHROME" ]; then
        EXISTING_BROWSER="$PW_CHROME"
    fi
fi

if [ -n "$EXISTING_BROWSER" ]; then
    echo "[OK] Installierter Browser gefunden: $EXISTING_BROWSER"
    echo "     -> Verwende vorhandenen Browser (kein erneuter Download von Playwright Chromium erforderlich)!"
else
    echo "[+] Kein bestehender Browser gefunden. Installiere Playwright Chromium..."
    .venv/bin/playwright install chromium
fi

# 4. Native AMD Zen 2 AVX2 C-Pipeline pruefen
if [ -f "fast_pixconv.so" ] && [ -f "fast_pixconv.c" ] && [ "fast_pixconv.so" -nt "fast_pixconv.c" ]; then
    echo "[OK] fast_pixconv.so ist bereits aktuell kompiliert."
elif command -v gcc &>/dev/null && [ -f "fast_pixconv.c" ]; then
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
echo "  CACHYOS HP ELITEDESK 805 G6 SERVER ERFOLGREICH INSTALLIERT / AKTUALISIERT!"
echo "============================================================================"
echo "  Server-IP fuer AmigaOS 4 Client : ${IP_ADDR}"
echo "  Server-Ports                    : 7788 & 9757"
echo "  Standard-Aufloesung             : 1920x1080 (Full HD) @ 25 FPS"
if [ -n "$EXISTING_BROWSER" ]; then
    echo "  Browser-Engine                  : ${EXISTING_BROWSER}"
fi
echo ""
echo "  Starten Sie den Server jetzt mit:"
echo "    ./run_server.sh"
echo "============================================================================"
