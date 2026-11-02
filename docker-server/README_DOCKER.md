# VortexBrowse Server - Docker Edition für DietPi & Raspberry Pi 4

Ein vollständig autarker Docker-Container für den **VortexBrowse Remote-Browser-Streaming-Server**.

Enthält alles out-of-the-box:
- **Chromium Browser** (headless, mit Hardware-Rasterung & 4 Render-Threads)
- **uBlock Origin** Werbeblocker (vorinstalliert)
- **PulseAudio Virtual Sink** (verlustfreies 16-Bit AHI-Audio-Streaming zum Amiga)
- **Cortex-A72 NEON SIMD Pixel-Konverter** (`fast_pixconv`)
- **Automatischer Start & Restart bei Boot**

---

## 1. Schnellstart auf frischem DietPi (1 Befehl)

Auf dem Raspberry Pi 4 im DietPi-Terminal einfach ausführen:

```bash
cd vortexbrowse-docker
./start_dietpi.sh
```

Das Skript:
1. Prüft, ob Docker installiert ist (falls nicht, installiert es Docker über DietPi automatisch).
2. Baut das Image direkt nativ auf dem Raspberry Pi 4 (`linux/arm64`).
3. Startet den Container im Hintergrund und richtet den automatischen Neustart ein.

---

## 2. Manuelle Bedienung mit Docker Compose

Falls du Docker bereits installiert hast:

### Container starten:
```bash
docker compose up -d --build
```

### Status & Auslastung prüfen:
```bash
docker compose ps
docker stats vortexbrowse
```

### Live-Logs einsehen (Verbindungen vom Amiga, Audio, FPS):
```bash
docker compose logs -f
```

### Container stoppen / neu starten:
```bash
docker compose restart
docker compose down
```

---

## 3. Verbindung vom AmigaOS 4 Client

In **VortexBrowse** (ReAction oder MUI) auf dem Amiga:
- Server-Auswahl: **Raspberry Pi 4** (Standard-IP `192.168.178.2:7788`)
- Oder manuell eingeben: `<DietPi-IP>:7788`

---

## 4. Wichtige Konfigurationsdetails

- **Shared Memory (`shm_size: 512m`)**:
  Standard-Docker-Container haben nur 64 MB Shared Memory (`/dev/shm`). Chromium benötigt bei modernen, aufwändigen Webseiten (YouTube, Wikipedia mit vielen Medien) mehr IPC-Puffer. In der `docker-compose.yml` ist dies bereits optimal auf 512 MB vorkonfiguriert.
- **PulseAudio in Container**:
  Der Container erzeugt einen virtuellen Null-Sink (`vortexbrowse_sink`), sodass kein physisches Soundgerät auf dem Pi 4 vorhanden sein muss. Der Ton wird direkt in 44.1 kHz / 16-Bit Stereo an den Amiga gestreamt.
- **Port**:
  TCP-Port `7788` (Standard) und `9757` sind freigegeben.
