#!/bin/bash
set -e

# ==============================================================================
# VortexBrowse - Raspberry Pi 4 Installation & Setup Script
# Kompatibel mit: bash, dash, sh, sudo
# OS: Raspberry Pi OS 64-bit (Bookworm / Bullseye), Debian ARM64, Ubuntu ARM64
# Hardware: Raspberry Pi 4 / 400 / CM4 (BCM2711 / 4x Cortex-A72)
# ==============================================================================

# Verzeichnis ermitteln (funktioniert in bash, dash und sh)
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo "=================================================================="
echo " VortexBrowse - Raspberry Pi 4 Setup"
echo " Target Architecture: $(uname -m)"
echo " OS: $(grep -s PRETTY_NAME /etc/os-release | cut -d= -f2- | tr -d '\"' || uname -s)"
echo "=================================================================="

ARCH="$(uname -m)"
if [ "$ARCH" != "aarch64" ] && [ "$ARCH" != "arm64" ]; then
    echo "[WARNUNG] Erkanntes System ist '$ARCH' (empfohlen: aarch64/arm64 für Pi 4)."
fi

# 1. Systempakete installieren (APT)
echo ""
echo "[1/5] Installiere erforderliche Systempakete (APT)..."
SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    if command -v sudo >/dev/null 2>&1; then
        SUDO="sudo"
    else
        echo "[FEHLER] Bitte führe das Skript mit Root-Rechten oder installiertem 'sudo' aus."
        exit 1
    fi
fi

$SUDO apt-get update
$SUDO apt-get install -y \
    python3 \
    python3-pip \
    python3-venv \
    python3-dev \
    build-essential \
    gcc \
    pulseaudio \
    pulseaudio-utils \
    pipewire \
    pipewire-pulse \
    liblz4-dev \
    mesa-vulkan-drivers \
    libgl1-mesa-dri \
    fonts-dejavu-core \
    fonts-freefont-ttf

CURRENT_USER="${SUDO_USER:-$(id -un)}"
$SUDO usermod -a -G audio,video,render "$CURRENT_USER" 2>/dev/null || true

# Browser installieren (Ungoogled-Chromium, Chromium-Browser oder Chromium)
if ! command -v ungoogled-chromium >/dev/null 2>&1 && ! command -v chromium-browser >/dev/null 2>&1 && ! command -v chromium >/dev/null 2>&1; then
    echo "Installiere Browser (Ungoogled-Chromium oder Chromium)..."
    $SUDO apt-get install -y ungoogled-chromium 2>/dev/null || \
    $SUDO apt-get install -y chromium-browser 2>/dev/null || \
    $SUDO apt-get install -y chromium 2>/dev/null || true
fi

# 2. Python Virtual Environment (.venv) einrichten
echo ""
echo "[2/5] Richte Python Virtual Environment (.venv) ein..."
if [ ! -d ".venv" ]; then
    python3 -m venv .venv
fi

# Direkter Aufruf der venv-Binaries - benötigt KEIN 'source' (funktioniert in bash, dash, sh):
.venv/bin/python3 -m pip install --upgrade pip setuptools wheel
.venv/bin/python3 -m pip install -r requirements.txt

# 3. Native Cortex-A72 SIMD Pixel Converter kompilieren / verifizieren
echo ""
echo "[3/5] Prüfe / Optimiere nativen Cortex-A72 NEON SIMD Converter..."
if command -v gcc >/dev/null 2>&1; then
    if [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then
        gcc -O3 -march=armv8-a+crc -mtune=cortex-a72 -shared -fPIC -o fast_pixconv_aarch64.so fast_pixconv.c 2>/dev/null || \
        gcc -O3 -march=native -shared -fPIC -o fast_pixconv_aarch64.so fast_pixconv.c 2>/dev/null || true
    else
        gcc -O3 -march=native -shared -fPIC -o fast_pixconv.so fast_pixconv.c 2>/dev/null || true
    fi
    echo "[OK] fast_pixconv_aarch64.so erfolgreich bereitgestellt."
elif [ -f "fast_pixconv_aarch64.so" ]; then
    echo "[OK] Verwende vor-kompiliertes fast_pixconv_aarch64.so."
else
    echo "[WARNUNG] Kein gcc gefunden und keine .so vorhanden. Fallback auf Pillow aktiv."
fi

# 4. PipeWire / PulseAudio Virtual Sink vorbereiten
echo ""
echo "[4/5] Prüfe PipeWire / PulseAudio Audio-Streaming..."
if command -v pactl >/dev/null 2>&1; then
    pactl load-module module-null-sink sink_name=vortexbrowse_sink sink_properties=device.description=VortexBrowseAudio 2>/dev/null || true
    pactl set-sink-mute vortexbrowse_sink 0 2>/dev/null || true
    pactl set-sink-volume vortexbrowse_sink 100% 2>/dev/null || true
    echo "[OK] Virtueller Audio-Sink 'vortexbrowse_sink' geprüft."
else
    echo "[INFO] pactl wird beim Start über run_rpi4.sh initialisiert."
fi

# 5. Skripte ausführbar machen & Systemd Service anpassen
echo ""
echo "[5/5] Setze Dateiberechtigungen & passe Systemd-Service an..."
chmod +x server.py run_rpi4.sh install_rpi4.sh 2>/dev/null || true

CURRENT_USER="${SUDO_USER:-$(id -un)}"
cat <<EOF > vortexbrowse.service
[Unit]
Description=VortexBrowse Remote Web Rendering Server for AmigaOS 4
After=network.target sound.target pipewire.service pulseaudio.service

[Service]
Type=simple
User=$CURRENT_USER
WorkingDirectory=$SCRIPT_DIR
ExecStartPre=+/bin/sh -c 'for g in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do echo performance > "\$g" 2>/dev/null || true; done'
ExecStart=$SCRIPT_DIR/run_rpi4.sh
ExecStopPost=+/bin/sh -c 'for g in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do echo schedutil > "\$g" 2>/dev/null || true; done'
Restart=always
RestartSec=3
Environment=PYTHONUNBUFFERED=1

[Install]
WantedBy=multi-user.target
EOF

# CPU-Governor Zugriff für Benutzer ohne Root-Passwort erlauben
$SUDO chmod 666 /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor 2>/dev/null || true

# Falls das Skript via sudo aufgerufen wurde, Rechte für den ursprünglichen User wiederherstellen
if [ -n "$SUDO_USER" ]; then
    chown -R "$SUDO_USER":"$SUDO_USER" "$SCRIPT_DIR" 2>/dev/null || true
fi

echo ""
echo "=================================================================="
echo " [FERTIG] VortexBrowse ist einsatzbereit auf dem Raspberry Pi 4!"
echo ""
echo " Server starten mit:"
echo "   ./run_rpi4.sh"
echo ""
echo " Oder als Hintergrunddienst (systemd) einrichten mit:"
echo "   sudo cp vortexbrowse.service /etc/systemd/system/"
echo "   sudo systemctl daemon-reload"
echo "   sudo systemctl enable --now vortexbrowse"
echo "=================================================================="
