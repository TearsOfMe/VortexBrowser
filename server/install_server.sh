#!/usr/bin/env bash
# ==============================================================================
# VortexBrowser Universal Linux Companion Server - Universal Installer
# Compatible with: CachyOS, Arch Linux, Debian, Ubuntu, Raspberry Pi OS (Pi 4/5),
# Fedora, and all modern x86_64 and ARM64 / aarch64 Linux distributions.
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "============================================================================"
echo "      VortexBrowser Universal Linux Server - Self-Configuring Setup         "
echo "============================================================================"

ARCH="$(uname -m)"
echo "[i] Detected Architecture: ${ARCH}"

# ------------------------------------------------------------------------------
# 1. Hardware- & GPU-Erkennung
# ------------------------------------------------------------------------------
HAS_INTEL_GPU=false
HAS_AMD_GPU=false
IS_RPI=false

if [ -f "/sys/firmware/devicetree/base/model" ] && grep -qi "raspberry" /sys/firmware/devicetree/base/model 2>/dev/null; then
    IS_RPI=true
fi
if grep -qi "bcm2711\|bcm2712" /proc/cpuinfo 2>/dev/null; then
    IS_RPI=true
fi

for vpath in /sys/class/drm/card*/device/vendor; do
    if [ -f "$vpath" ]; then
        V_ID="$(cat "$vpath" 2>/dev/null | tr -d ' ')"
        if [ "$V_ID" = "0x8086" ]; then
            HAS_INTEL_GPU=true
        elif [ "$V_ID" = "0x1002" ] || [ "$V_ID" = "0x1022" ]; then
            HAS_AMD_GPU=true
        fi
    fi
done

if [ "$HAS_INTEL_GPU" = false ] && [ "$HAS_AMD_GPU" = false ]; then
    if grep -qi "intel" /proc/cpuinfo 2>/dev/null; then
        HAS_INTEL_GPU=true
    elif grep -qi "amd" /proc/cpuinfo 2>/dev/null; then
        HAS_AMD_GPU=true
    fi
fi

if [ "$IS_RPI" = true ]; then
    echo "[i] Hardware: Raspberry Pi (Broadcom VideoCore V3D)"
elif [ "$HAS_INTEL_GPU" = true ]; then
    echo "[i] Hardware: Intel CPU / GPU (QuickSync & VA-API acceleration)"
elif [ "$HAS_AMD_GPU" = true ]; then
    echo "[i] Hardware: AMD Ryzen / Radeon GPU (Vulkan & VA-API acceleration)"
else
    echo "[i] Hardware: Generic / Standard Linux Device"
fi

# ------------------------------------------------------------------------------
# 2. Systempakete pruefen & installieren
# ------------------------------------------------------------------------------
if command -v pacman &>/dev/null; then
    echo "[+] Arch Linux / CachyOS / Manjaro Paketmanager (pacman) erkannt."
    PKGS=("libjpeg-turbo" "lz4" "gcc" "mesa" "python" "python-pip")
    if [ "$HAS_AMD_GPU" = true ]; then
        PKGS+=("vulkan-radeon" "libva-mesa-driver")
    elif [ "$HAS_INTEL_GPU" = true ]; then
        PKGS+=("intel-media-driver" "libva-intel-driver" "vulkan-intel")
    fi
    if command -v pipewire &>/dev/null; then
        PKGS+=("pipewire-pulse")
    fi

    MISSING=()
    for p in "${PKGS[@]}"; do
        if ! pacman -Qi "$p" &>/dev/null; then
            MISSING+=("$p")
        fi
    done
    if [ ${#MISSING[@]} -gt 0 ]; then
        echo "[+] Installiere fehlende Pakete: ${MISSING[*]}..."
        if [ "$EUID" -eq 0 ]; then
            pacman -S --needed --noconfirm "${MISSING[@]}" || true
        else
            sudo pacman -S --needed --noconfirm "${MISSING[@]}" || true
        fi
    else
        echo "[OK] Alle Systempakete sind bereits vorhanden."
    fi

elif command -v apt-get &>/dev/null; then
    echo "[+] Debian / Ubuntu / Raspberry Pi OS Paketmanager (apt) erkannt."
    DEB_PKGS=("libturbojpeg0-dev" "liblz4-dev" "gcc" "build-essential" "python3" "python3-pip" "python3-venv")
    if [ "$HAS_INTEL_GPU" = true ]; then
        DEB_PKGS+=("intel-media-va-driver" "i965-va-driver")
    elif [ "$HAS_AMD_GPU" = true ]; then
        DEB_PKGS+=("mesa-va-drivers" "mesa-vulkan-drivers")
    fi

    if [ "$EUID" -eq 0 ]; then
        apt-get update -qq || true
        apt-get install -y --no-install-recommends "${DEB_PKGS[@]}" || true
    else
        sudo apt-get update -qq || true
        sudo apt-get install -y --no-install-recommends "${DEB_PKGS[@]}" || true
    fi

elif command -v dnf &>/dev/null; then
    echo "[+] Fedora / RHEL Paketmanager (dnf) erkannt."
    RPM_PKGS=("turbojpeg-devel" "lz4-devel" "gcc" "gcc-c++" "python3" "python3-pip" "python3-devel")
    if [ "$EUID" -eq 0 ]; then
        dnf install -y "${RPM_PKGS[@]}" || true
    else
        sudo dnf install -y "${RPM_PKGS[@]}" || true
    fi
fi

if ! command -v python3 &>/dev/null; then
    echo "[!] FEHLER: python3 wurde nicht gefunden. Bitte installieren Sie Python 3.10+."
    exit 1
fi

# ------------------------------------------------------------------------------
# 3. Python Virtual Environment & Abhaengigkeiten
# ------------------------------------------------------------------------------
DEPS_OK=false
if [ -d ".venv" ] && .venv/bin/python3 -c "import playwright, lz4, PIL, websockets" &>/dev/null; then
    DEPS_OK=true
fi

if [ "$DEPS_OK" = true ]; then
    echo "[OK] Python Virtual Environment & Abhaengigkeiten bereits vorhanden."
else
    if [ ! -d ".venv" ]; then
        echo "[+] Erstelle Python Virtual Environment (.venv)..."
        python3 -m venv .venv
    fi
    echo "[+] Installiere Python-Abhaengigkeiten aus requirements.txt..."
    .venv/bin/pip install --upgrade pip
    .venv/bin/pip install -r requirements.txt
fi

# ------------------------------------------------------------------------------
# 4. Browser-Erkennung & Interaktive Auswahl mit Empfehlung
# ------------------------------------------------------------------------------
echo ""
echo "============================================================================"
echo "  VortexBrowser - Browser-Engine Auswahl"
echo "============================================================================"

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

# 1. Ungoogled Chromium
for b in ~/.local/bin/ungoogled-chromium /usr/bin/ungoogled-chromium /usr/lib/ungoogled-chromium/ungoogled-chromium $(command -v ungoogled-chromium 2>/dev/null || true); do
    if [ -x "$b" ]; then
        add_candidate "$b" "Ungoogled Chromium" "Beste Privatsphaere & Performance, keine Google-Telemetrie" "[EMPFOHLEN]"
        break
    fi
done

# 2. System Chromium
for b in /usr/bin/chromium /usr/bin/chromium-browser /usr/lib/chromium/chromium $(command -v chromium 2>/dev/null || true) $(command -v chromium-browser 2>/dev/null || true); do
    if [ -x "$b" ]; then
        TAG="[SEHR GUT]"
        if [ ${#CANDIDATE_PATHS[@]} -eq 0 ]; then
            TAG="[EMPFOHLEN]"
        fi
        add_candidate "$b" "System Chromium" "Nativer System-Browser mit GPU-Hardwarebeschleunigung" "$TAG"
        break
    fi
done

# 3. Google Chrome
for b in /usr/bin/google-chrome /usr/bin/google-chrome-stable $(command -v google-chrome 2>/dev/null || true) $(command -v google-chrome-stable 2>/dev/null || true); do
    if [ -x "$b" ]; then
        add_candidate "$b" "Google Chrome" "Offizieller Google Chrome Browser" "[KOMPATIBEL]"
        break
    fi
done

# 4. Brave Browser
for b in /usr/bin/brave-browser /usr/bin/brave $(command -v brave-browser 2>/dev/null || true) $(command -v brave 2>/dev/null || true); do
    if [ -x "$b" ]; then
        add_candidate "$b" "Brave Browser" "Brave Browser mit Werbeblocker" "[KOMPATIBEL]"
        break
    fi
done

# 5. Playwright Chromium Cache
PW_CHROME=$(ls -d ~/.cache/ms-playwright/chromium-*/chrome-linux*/chrome 2>/dev/null | tail -n 1 || true)
if [ -n "$PW_CHROME" ] && [ -x "$PW_CHROME" ]; then
    TAG="[SEHR GUT]"
    if [ ${#CANDIDATE_PATHS[@]} -eq 0 ]; then
        TAG="[EMPFOHLEN]"
    fi
    add_candidate "$PW_CHROME" "Playwright Chromium" "Autonomer Chromium-Build in ~/.cache/ms-playwright" "$TAG"
fi

CHOSEN_BIN=""
if [ ${#CANDIDATE_PATHS[@]} -gt 0 ]; then
    echo "Gefundene Browser-Engines auf diesem System:"
    for i in "${!CANDIDATE_PATHS[@]}"; do
        NUM=$((i + 1))
        echo "  [${NUM}] ${CANDIDATE_NAMES[$i]} ${CANDIDATE_TAGS[$i]}"
        echo "      Pfad : ${CANDIDATE_PATHS[$i]}"
        echo "      Info : ${CANDIDATE_DESCS[$i]}"
    done
    OPT_DOWNLOAD=$(( ${#CANDIDATE_PATHS[@]} + 1 ))
    OPT_MANUAL=$(( ${#CANDIDATE_PATHS[@]} + 2 ))
    echo "  [${OPT_DOWNLOAD}] Playwright Chromium herunterladen & installieren (~180 MB)"
    echo "  [${OPT_MANUAL}] Anderen Browser-Pfad manuell eingeben"
    echo ""

    if [ -t 0 ]; then
        read -r -p "Welchen Browser moechten Sie verwenden? [1-${OPT_MANUAL}] (Standard: 1 - ${CANDIDATE_NAMES[0]}): " USER_SEL
        USER_SEL="${USER_SEL:-1}"
    else
        echo "[Info] Nicht-interaktiver Modus -> Verwende empfohlenen Browser (Option 1)."
        USER_SEL="1"
    fi

    if [ "$USER_SEL" = "$OPT_DOWNLOAD" ]; then
        echo "[+] Lade Playwright Chromium herunter..."
        .venv/bin/playwright install chromium
        CHOSEN_BIN=$(ls -d ~/.cache/ms-playwright/chromium-*/chrome-linux*/chrome 2>/dev/null | tail -n 1 || true)
    elif [ "$USER_SEL" = "$OPT_MANUAL" ]; then
        if [ -t 0 ]; then
            read -r -p "Bitte vollstaendigen Pfad zur ausfuehrbaren Browser-Datei eingeben: " MANUAL_PATH
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
        CHOSEN_BIN="${CANDIDATE_PATHS[0]}"
    fi
else
    echo "Kein vorinstallierter Chromium-Browser auf dem System gefunden."
    echo ""
    echo "  [1] Playwright Chromium jetzt herunterladen & installieren [EMPFOHLEN]"
    echo "  [2] Anderen Browser-Pfad manuell eingeben"
    echo ""
    if [ -t 0 ]; then
        read -r -p "Auswahl [1-2] (Standard: 1): " NO_BROWSER_SEL
        NO_BROWSER_SEL="${NO_BROWSER_SEL:-1}"
    else
        NO_BROWSER_SEL="1"
    fi

    if [ "$NO_BROWSER_SEL" = "2" ] && [ -t 0 ]; then
        read -r -p "Bitte vollstaendigen Pfad eingeben: " MANUAL_PATH
        if [ -x "$MANUAL_PATH" ]; then
            CHOSEN_BIN="$MANUAL_PATH"
        fi
    fi

    if [ -z "$CHOSEN_BIN" ]; then
        echo "[+] Lade Playwright Chromium herunter..."
        .venv/bin/playwright install chromium
        CHOSEN_BIN=$(ls -d ~/.cache/ms-playwright/chromium-*/chrome-linux*/chrome 2>/dev/null | tail -n 1 || true)
    fi
fi

if [ -n "$CHOSEN_BIN" ]; then
    echo "$CHOSEN_BIN" > browser_bin.txt
    echo "[OK] Konfigurierter Browser in browser_bin.txt gespeichert: $CHOSEN_BIN"
fi

# ------------------------------------------------------------------------------
# 5. Native Hardware-Optimierte C-SIMD Pixel-Pipeline kompilieren
# ------------------------------------------------------------------------------
echo ""
echo "[+] Kompiliere native SIMD-beschleunigte Pixelkonvertierungs-Pipeline (fast_pixconv.so)..."

COMPILED=false

# Versuch 1: Volle SIMD + OpenMP + TurboJPEG + LZ4 mit Host-CPU-Befehlssatz (-march=native)
if gcc -O3 -march=native -mtune=native -fopenmp -DHAVE_TURBOJPEG_LZ4 -shared -fPIC fast_pixconv.c -lturbojpeg -llz4 -o fast_pixconv.so 2>/dev/null; then
    echo "[OK] C-Pipeline erfolgreich mit nativer SIMD-Beschleunigung + TurboJPEG + LZ4 kompiliert!"
    COMPILED=true
fi

# Versuch 2: Ohne OpenMP, aber mit TurboJPEG + LZ4
if [ "$COMPILED" = false ] && gcc -O3 -march=native -mtune=native -DHAVE_TURBOJPEG_LZ4 -shared -fPIC fast_pixconv.c -lturbojpeg -llz4 -o fast_pixconv.so 2>/dev/null; then
    echo "[OK] C-Pipeline mit TurboJPEG + LZ4 kompiliert."
    COMPILED=true
fi

# Versuch 3: Universeller C-Build ohne externe Header
if [ "$COMPILED" = false ]; then
    gcc -O3 -march=native -shared -fPIC fast_pixconv.c -o fast_pixconv.so 2>/dev/null || \
    gcc -O3 -shared -fPIC fast_pixconv.c -o fast_pixconv.so
    echo "[OK] Standard C-Pipeline kompiliert."
fi

chmod +x run_server.sh sync_twitch_login.sh 2>/dev/null || true

# ------------------------------------------------------------------------------
# 6. systemd Service Vorlage generieren
# ------------------------------------------------------------------------------
CURRENT_USER="${SUDO_USER:-$(id -un)}"
cat <<EOF > vortexbrowser.service
[Unit]
Description=VortexBrowser Universal Server for AmigaOS 4
After=network-online.target sound.target pipewire.service pulseaudio.service
Wants=network-online.target

[Service]
Type=simple
User=$CURRENT_USER
WorkingDirectory=$SCRIPT_DIR
Environment=PYTHONUNBUFFERED=1
Environment=OMP_NUM_THREADS=4
Environment=XDG_RUNTIME_DIR=/run/user/\$(id -u "$CURRENT_USER" 2>/dev/null || echo 1000)
ExecStart=$SCRIPT_DIR/run_server.sh
Restart=always
RestartSec=2
Nice=-5

[Install]
WantedBy=multi-user.target
EOF

IP_ADDR=$(hostname -I 2>/dev/null | awk '{print $1}' || echo "127.0.0.1")

echo ""
echo "============================================================================"
echo "  VORTEXBROWSER UNIVERSAL SERVER ERFOLGREICH INSTALLIERT / AKTUALISIERT!    "
echo "============================================================================"
echo "  Server-IP fuer AmigaOS 4 Client : ${IP_ADDR}"
echo "  Server-Ports                    : 7788 (Stream) & 9757 (Audio)"
if [ -n "$CHOSEN_BIN" ]; then
    echo "  Browser-Engine                  : ${CHOSEN_BIN}"
fi
echo ""
echo "  Starten Sie den Server manuell mit:"
echo "    ./run_server.sh"
echo ""
echo "  Oder als systemd-Dienst aktivieren:"
echo "    sudo cp vortexbrowser.service /etc/systemd/system/"
echo "    sudo systemctl daemon-reload"
echo "    sudo systemctl enable --now vortexbrowser.service"
echo "============================================================================"
