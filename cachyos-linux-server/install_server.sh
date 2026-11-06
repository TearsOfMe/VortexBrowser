#!/usr/bin/env bash
# ==============================================================================
# VortexBrowser Server - CachyOS-Linux Installer
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "============================================================================"
echo "  VortexBrowser Server - CachyOS Linux (1080p@25fps) Setup    "
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

# 3. Browser-Erkennung & Interaktive Auswahl mit Empfehlung
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

# 1. Ungoogled Chromium (Privacy & Top-Performance ohne Google-Telemetrie)
for b in ~/.local/bin/ungoogled-chromium /usr/bin/ungoogled-chromium /usr/lib/ungoogled-chromium/ungoogled-chromium $(command -v ungoogled-chromium 2>/dev/null || true); do
    if [ -x "$b" ]; then
        add_candidate "$b" "Ungoogled Chromium" "Beste Privatsphaere & Performance, keine Google-Telemetrie" "[EMPFOHLEN]"
        break
    fi
done

# 2. System Chromium (Arch / CachyOS / Debian native mit VA-API & Vulkan)
for b in /usr/bin/chromium /usr/bin/chromium-browser /usr/lib/chromium/chromium $(command -v chromium 2>/dev/null || true) $(command -v chromium-browser 2>/dev/null || true); do
    if [ -x "$b" ]; then
        TAG="[SEHR GUT]"
        if [ ${#CANDIDATE_PATHS[@]} -eq 0 ]; then
            TAG="[EMPFOHLEN]"
        fi
        add_candidate "$b" "System Chromium" "Optimiert fuer CachyOS/Arch (VA-API Hardwarebeschleunigung & Vulkan)" "$TAG"
        break
    fi
done

# 3. Google Chrome (Offiziell)
for b in /usr/bin/google-chrome /usr/bin/google-chrome-stable $(command -v google-chrome 2>/dev/null || true) $(command -v google-chrome-stable 2>/dev/null || true); do
    if [ -x "$b" ]; then
        add_candidate "$b" "Google Chrome" "Offizieller Browser mit hoher Medien- und Video-Kompatibilitaet" "[KOMPATIBEL]"
        break
    fi
done

# 4. Brave Browser
for b in /usr/bin/brave-browser /usr/bin/brave $(command -v brave-browser 2>/dev/null || true) $(command -v brave 2>/dev/null || true); do
    if [ -x "$b" ]; then
        add_candidate "$b" "Brave Browser" "Brave Browser mit integrierten Werbeblockern" "[KOMPATIBEL]"
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
    add_candidate "$PW_CHROME" "Playwright Chromium (Cache)" "Getestete VortexBrowser Standard-Engine im lokalen Cache" "$TAG"
fi

CHOSEN_BIN=""

if [ ${#CANDIDATE_PATHS[@]} -gt 0 ]; then
    echo "Auf Ihrem System wurden folgende verfuegbare Browser gefunden:"
    echo ""
    for i in "${!CANDIDATE_PATHS[@]}"; do
        idx=$((i + 1))
        echo "  [${idx}] ${CANDIDATE_NAMES[$i]} ${CANDIDATE_TAGS[$i]}"
        echo "      Pfad : ${CANDIDATE_PATHS[$i]}"
        echo "      Info : ${CANDIDATE_DESCS[$i]}"
        echo ""
    done
    OPT_DOWNLOAD=$(( ${#CANDIDATE_PATHS[@]} + 1 ))
    OPT_MANUAL=$(( ${#CANDIDATE_PATHS[@]} + 2 ))
    echo "  [${OPT_DOWNLOAD}] Playwright Chromium neu herunterladen & installieren (~180 MB)"
    echo "  [${OPT_MANUAL}] Anderen Browser-Pfad manuell eingeben"
    echo ""

    if [ -t 0 ]; then
        read -r -p "Welchen Browser moechten Sie verwenden? [1-${OPT_MANUAL}] (Vorschlag/Standard: 1 - ${CANDIDATE_NAMES[0]}): " USER_SEL
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
        echo "[!] Ungueltige Eingabe. Verwende empfohlenen Browser Option 1."
        CHOSEN_BIN="${CANDIDATE_PATHS[0]}"
    fi
else
    echo "Kein vorinstallierter Chromium-Browser auf dem System gefunden."
    echo ""
    echo "  [1] Playwright Chromium jetzt herunterladen & installieren [EMPFOHLEN]"
    echo "  [2] Anderen Browser-Pfad manuell eingeben"
    echo ""
    if [ -t 0 ]; then
        read -r -p "Auswahl [1-2] (Vorschlag/Standard: 1): " NO_BROWSER_SEL
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
    echo "[OK] Konfigurierter Browser gespeichert in browser_bin.txt:"
    echo "     -> $CHOSEN_BIN"
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
