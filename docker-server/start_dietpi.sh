#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo "=================================================================="
echo " VortexBrowse - Docker Setup für DietPi & Raspberry Pi 4"
echo "=================================================================="

# Root-Rechte ermitteln falls nötig
SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    if command -v sudo >/dev/null 2>&1; then
        SUDO="sudo"
    fi
fi

# 1. Docker prüfen und bei Bedarf automatisch installieren
if ! command -v docker >/dev/null 2>&1; then
    echo "[INFO] Docker ist noch nicht installiert."
    if command -v dietpi-software >/dev/null 2>&1; then
        echo "[INFO] Installiere Docker über DietPi-Software (ID 134)..."
        $SUDO dietpi-software install 134 135
    else
        echo "[INFO] Installiere Docker über offizielles Docker-Install-Skript..."
        curl -fsSL https://get.docker.com -o /tmp/get-docker.sh
        $SUDO sh /tmp/get-docker.sh
        rm -f /tmp/get-docker.sh
    fi
    # Aktuellen Benutzer zur Docker-Gruppe hinzufügen
    $SUDO usermod -aG docker "$USER" 2>/dev/null || true
fi

# 2. Docker Compose Befehl ermitteln
COMPOSE_CMD=""
if docker compose version >/dev/null 2>&1; then
    COMPOSE_CMD="docker compose"
elif command -v docker-compose >/dev/null 2>&1; then
    COMPOSE_CMD="docker-compose"
else
    echo "[INFO] Installiere Docker Compose Plugin..."
    $SUDO apt-get update && $SUDO apt-get install -y docker-compose-plugin 2>/dev/null || true
    COMPOSE_CMD="docker compose"
fi

# 3. VortexBrowse Server Container bauen und im Hintergrund starten
echo "[INFO] Starte VortexBrowse Server via Docker Compose..."
$COMPOSE_CMD up -d --build

IP=$(hostname -I | awk '{print $1}')
echo ""
echo "=================================================================="
echo " [ERFOLG] VortexBrowse Server läuft im Docker-Container!"
echo " IP-Adresse: ${IP:-192.168.178.2}"
echo " Port:       7788"
echo ""
echo " AmigaOS 4 Verbindung:"
echo "   In VortexBrowse (ReAction oder MUI) einfach die IP auswählen/eingeben."
echo ""
echo " Nützliche Docker-Befehle:"
echo "   Status prüfen:   $COMPOSE_CMD ps"
echo "   Live-Logs sehen: $COMPOSE_CMD logs -f"
echo "   Neustart:        $COMPOSE_CMD restart"
echo "   Stoppen:         $COMPOSE_CMD down"
echo "=================================================================="
