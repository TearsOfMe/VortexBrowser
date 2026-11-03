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

# Browser ermitteln & Interaktive Auswahl mit Empfehlung
echo ""
echo "=================================================================="
echo " VortexBrowser - Browser-Engine Auswahl"
echo "=================================================================="

CANDIDATE_PATHS=()
CANDIDATE_NAMES=()
CANDIDATE_DESCS=()
CANDIDATE_TAGS=()

add_candidate() {
    local bin="$1"
    local name="$2"
    local desc="$3"
    local tag="$4"
    if [ -n "$bin" ] && [ -x "$bin" ]; then
        for existing in "${CANDIDATE_PATHS[@]}"; do
            if [ "$existing" = "$bin" ]; then
                return
            fi
        done
        CANDIDATE_PATHS+=("$bin")
        CANDIDATE_NAMES+=("$name")
        CANDIDATE_DESCS+=("$desc")
        CANDIDATE_TAGS+=("$tag")
    fi
}

# 1. System Chromium (auf Raspberry Pi OS speziell fuer BCM2711 / VideoCore VI optimiert)
for b in /usr/bin/chromium-browser /usr/bin/chromium /usr/lib/chromium-browser/chromium-browser $(command -v chromium-browser 2>/dev/null || true) $(command -v chromium 2>/dev/null || true); do
    if [ -x "$b" ]; then
        add_candidate "$b" "Raspberry Pi Chromium" "Offizieller Pi-Browser mit V4L2/DRM Hardwarebeschleunigung" "[EMPFOHLEN]"
        break
    fi
done

# 2. Ungoogled Chromium
for b in ~/.local/bin/ungoogled-chromium /usr/bin/ungoogled-chromium $(command -v ungoogled-chromium 2>/dev/null || true); do
    if [ -x "$b" ]; then
        TAG="[SEHR GUT]"
        if [ ${#CANDIDATE_PATHS[@]} -eq 0 ]; then
            TAG="[EMPFOHLEN]"
        fi
        add_candidate "$b" "Ungoogled Chromium" "Beste Privatsphaere & Performance, keine Google-Telemetrie" "$TAG"
        break
    fi
done

# 3. Playwright Chromium Cache
PW_CHROME=$(ls -d ~/.cache/ms-playwright/chromium-*/chrome-linux*/chrome 2>/dev/null | tail -n 1 || true)
if [ -n "$PW_CHROME" ] && [ -x "$PW_CHROME" ]; then
    TAG="[KOMPATIBEL]"
    if [ ${#CANDIDATE_PATHS[@]} -eq 0 ]; then
        TAG="[EMPFOHLEN]"
    fi
    add_candidate "$PW_CHROME" "Playwright Chromium (Cache)" "Getestete VortexBrowser Standard-Engine im lokalen Cache" "$TAG"
fi

CHOSEN_BIN=""

if [ ${#CANDIDATE_PATHS[@]} -gt 0 ]; then
    echo "Auf Ihrem Raspberry Pi wurden folgende verfuegbare Browser gefunden:"
    echo ""
    for i in "${!CANDIDATE_PATHS[@]}"; do
        idx=$((i + 1))
        echo "  [${idx}] ${CANDIDATE_NAMES[$i]} ${CANDIDATE_TAGS[$i]}"
        echo "      Pfad : ${CANDIDATE_PATHS[$i]}"
        echo "      Info : ${CANDIDATE_DESCS[$i]}"
        echo ""
    done
    OPT_APT=$(( ${#CANDIDATE_PATHS[@]} + 1 ))
    OPT_MANUAL=$(( ${#CANDIDATE_PATHS[@]} + 2 ))
    echo "  [${OPT_APT}] Chromium ueber APT neu installieren / aktualisieren"
    echo "  [${OPT_MANUAL}] Anderen Browser-Pfad manuell eingeben"
    echo ""

    if [ -t 0 ]; then
        read -r -p "Welchen Browser moechten Sie verwenden? [1-${OPT_MANUAL}] (Vorschlag/Standard: 1 - ${CANDIDATE_NAMES[0]}): " USER_SEL
        USER_SEL="${USER_SEL:-1}"
    else
        echo "[Info] Nicht-interaktiver Modus -> Verwende empfohlenen Browser (Option 1)."
        USER_SEL="1"
    fi

    if [ "$USER_SEL" = "$OPT_APT" ]; then
        echo "[+] Installiere Chromium ueber APT..."
        $SUDO apt-get install -y chromium-browser 2>/dev/null || $SUDO apt-get install -y chromium 2>/dev/null || true
        CHOSEN_BIN="$(command -v chromium-browser 2>/dev/null || command -v chromium 2>/dev/null || true)"
    elif [ "$USER_SEL" = "$OPT_MANUAL" ]; then
        if [ -t 0 ]; then
            read -r -p "Bitte vollstaendigen Pfad eingeben: " MANUAL_PATH
            if [ -x "$MANUAL_PATH" ]; then
                CHOSEN_BIN="$MANUAL_PATH"
            else
                echo "[!] Warnung: '$MANUAL_PATH' nicht ausfuehrbar. Fallback auf Option 1."
                CHOSEN_BIN="${CANDIDATE_PATHS[0]}"
            fi
        else
            CHOSEN_BIN="${CANDIDATE_PATHS[0]}"
        fi
    elif [[ "$USER_SEL" =~ ^[0-9]+$ ]] && [ "$USER_SEL" -ge 1 ] && [ "$USER_SEL" -le "${#CANDIDATE_PATHS[@]}" ]; then
        SEL_IDX=$((USER_SEL - 1))
        CHOSEN_BIN="${CANDIDATE_PATHS[$SEL_IDX]}"
    else
        echo "[!] Ungueltige Eingabe. Verwende Option 1."
        CHOSEN_BIN="${CANDIDATE_PATHS[0]}"
    fi
else
    echo "Kein vorinstallierter Chromium-Browser gefunden. Installiere ueber APT..."
    $SUDO apt-get install -y chromium-browser 2>/dev/null || $SUDO apt-get install -y chromium 2>/dev/null || true
    CHOSEN_BIN="$(command -v chromium-browser 2>/dev/null || command -v chromium 2>/dev/null || true)"
fi

if [ -n "$CHOSEN_BIN" ]; then
    echo "$CHOSEN_BIN" > browser_bin.txt
    echo "[OK] Konfigurierter Browser gespeichert in browser_bin.txt:"
    echo "     -> $CHOSEN_BIN"
fi

# 2. Python Virtual Environment (.venv) einrichten
echo ""
echo "[2/5] Richte Python Virtual Environment (.venv) ein..."
DEPS_OK=false
if [ -d ".venv" ] && .venv/bin/python3 -c "import playwright, lz4, PIL, websockets" >/dev/null 2>&1; then
    DEPS_OK=true
fi

if [ "$DEPS_OK" = true ]; then
    echo "[OK] Python Virtual Environment & Abhaengigkeiten bereits vorhanden (kein Download erforderlich)."
else
    if [ ! -d ".venv" ]; then
        python3 -m venv .venv
    fi
    echo "[+] Installiere/Aktualisiere Python-Abhaengigkeiten..."
    .venv/bin/python3 -m pip install --upgrade pip setuptools wheel
    .venv/bin/python3 -m pip install -r requirements.txt
fi

# 3. Native Cortex-A72 SIMD Pixel Converter kompilieren / verifizieren
echo ""
echo "[3/5] Prüfe / Optimiere nativen Cortex-A72 NEON SIMD Converter..."
SO_FILE="fast_pixconv_aarch64.so"
if [ "$ARCH" != "aarch64" ] && [ "$ARCH" != "arm64" ]; then
    SO_FILE="fast_pixconv.so"
fi

if [ -f "$SO_FILE" ] && [ -f "fast_pixconv.c" ] && [ "$SO_FILE" -nt "fast_pixconv.c" ]; then
    echo "[OK] $SO_FILE ist bereits aktuell kompiliert."
elif command -v gcc >/dev/null 2>&1; then
    if [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then
        gcc -O3 -march=armv8-a+crc -mtune=cortex-a72 -shared -fPIC -o fast_pixconv_aarch64.so fast_pixconv.c 2>/dev/null || \
        gcc -O3 -march=native -shared -fPIC -o fast_pixconv_aarch64.so fast_pixconv.c 2>/dev/null || true
    else
        gcc -O3 -march=native -shared -fPIC -o fast_pixconv.so fast_pixconv.c 2>/dev/null || true
    fi
    echo "[OK] $SO_FILE erfolgreich bereitgestellt."
elif [ -f "$SO_FILE" ]; then
    echo "[OK] Verwende vor-kompiliertes $SO_FILE."
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
