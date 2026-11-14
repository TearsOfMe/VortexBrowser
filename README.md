# VortexBrowser – High-Performance Headless Web Browser Streaming System
**(Universeller Linux / macOS Server [AMD/Intel/Apple Silicon/Raspberry Pi/Docker] $\rightarrow$ AmigaOS 4.1 PowerPC Client)**

[English version below / Zur englischen Version wechseln](#english-version)

---

## Deutsch

VortexBrowser rendert moderne Webseiten über einen Headless Chromium (Playwright / Rustwright + DirectCDP WebSocket) auf einem **universellen Linux- / macOS-Server oder im Docker-Container** (automatische Hardware-Erkennung für AMD Ryzen Vulkan, Intel QuickSync VA-API, **Apple VideoToolbox** und Raspberry Pi 4/5 V3D) und streamt die Bilddaten hocheffizient komprimiert per **Planar YUV420P + 2D Dirty-Rect + LZ4** an einen nativen **AmigaOS 4.1 PowerPC Reaction-Client**.

---

### Highlights & Features

1. **Hardware-beschleunigtes H.264 Video-Streaming & Dekodierung (va.library + VA-API):**
   - **Eingebettetes FFmpeg (`libavcodec`) auf AmigaOS 4:** Dekodiert H.264 Annex-B Bitstreams direkt in Hardware auf Radeon HD/RX Grafikkarten via `va.library` (`vaPutSurface`) – analog zu Maijestros MPlayer-Architektur.
   - **Hardware-Encoding auf dem Server:** H.264 GPU-Encoding in Echtzeit via `h264_vaapi` (AMD VCN auf Ryzen PRO bzw. Intel QuickSync) bei minimaler Server-CPU-Last (< 3%).
   - **Einstellbare Video-Qualität:**
     - Schieberegler in den Amiga-Einstellungen (`Amiga+1`): **Video-Bitrate von 1.000 bis 10.000 kbps (10 Mbit/s)**.
     - Server-Kommandozeilenoption: `--video-bitrate <kbps>` bzw. Umgebungsvariable `VORTEX_VIDEO_BITRATE`.
   - **Zero-Latency:** Exaktes 1-Sekunden-Keyframe-Intervall (`-g 25`) für sofortigen Sync bei Video-Start und 0 B-Frames (`-bf 0`) für latenzfreie Echtzeitübertragung.
   - **Planar YUV420p (12-Bit) & High-Speed JPEG:** Direkte hardwarenahe Übertragung ohne RGB-Overhead.
   - **Wählbare Framerate (15 / 25 / 30 / 50 / 60 FPS):** Vom Amiga aus flexibel einstellbar via Menü oder Einstellungen.
   - **2D Dirty-Rectangles (`CODEC_LZ4_RECT`):** Für sparsamen Datentransfer bei normalem Web-Browsing.
   - **AVX2 / ARM NEON SIMD-Mikroprädiktor:** Verdoppelt die LZ4-Kompressionsrate bei vollen 8-Bit Farbtiefe ohne CPU-Last auf dem Amiga.
   - **DirectCDP WebSocket Core:** Direkte WebSocket-Screencast-Anbindung ohne Treiber-Overhead.

2. **Native AmigaOS 4 ReAction Benutzeroberfläche:**
   - **Klassisches AmigaOS 4 Look-and-Feel (`VortexBrowser` & `VortexBrowser-AltiVec`):** Nahtlose Integration mit Tabs (`clicktab.gadget`), Font-Zoom, Screen-Cloning und GPU-Composite-Skalierung.
   - **Mehrsprachige Benutzeroberfläche (Locale):** Vollständige Lokalisierung über `locale.library` mit nativen AmigaOS-Katalogen für Deutsch (`german`/`deutsch`), Englisch (`english`), Französisch (`french`/`francais`), Italienisch (`italian`/`italiano`) und Spanisch (`spanish`/`espanol`) in `catalogs/` (optional per `ENV:VortexBrowser/language` überschreibbar).
   - **Dynamische Breitenanpassung der Schnellwahl-Buttons:** Die Schnellwahl-Leiste nutzt automatisch die gesamte verfügbare Fensterbreite.
   - **Favoriten:** In den Einstellungen (`Amiga+E`) können Lesezeichen und Schnellwahl-Seiten gepflegt werden.

3. **Multi-Tab-Browsing & Sitzungswiederherstellung:**
   - Echte Tabs über `clicktab.gadget` mit AmigaOS 4 Standard-Bedienung (`Amiga+T` für neuen Tab, `Amiga+W` zum Schließen).
   - **Tab-Persistenz:** Sämtliche geöffneten Tabs, Titel und der aktive Tab werden beim Beenden automatisch in `ENVARC:` gespeichert und beim nächsten Start nahtlos wiederhergestellt.

4. **Download-Manager:**
   - Vollständiger nativer Download-Manager (`Amiga+D`) mit Live-Geschwindigkeitsanzeige, Fortschritt in Prozent, Restzeitschätzung (ETA), Datei- und Ordneröffner.
   - Fensterposition und -größe des Download-Managers werden dauerhaft in `ENVARC:` gesichert.
   - Saubere Umlaut- und Zeichensatz-Behandlung für Dateinamen.

5. **Passwort-Manager:**
   - Integrierter Passwort-Tresor (`Amiga+K`) mit starker Blowfish-CBC- und SHA-256-Verschlüsselung.
   - Benutzername und Passwort per Knopfdruck ins Amiga-Clipboard kopieren.
   - Speicherort in `ENVARC:VortexBrowser/passwords.enc`.
   - **Twitch-Unterstützung:** Session-Token-Login (`twitch_token.txt`), um Bot-Schranken auf dem Amiga zu umgehen.

6. **Audio-Streaming:**
   - Synchroner 16-Bit Stereo-Audiostream über separaten Port 9757 direkt an AmigaOS 4 AHI (`ahi.device`).

---

### Workbench Icon-ToolTypes & Konfiguration

VortexBrowser unterstützt die vollständige Konfiguration über standardmäßige AmigaOS 4 **ToolTypes** in den `.info`-Dateien (`VortexBrowser.info`, `VortexBrowser-AltiVec.info`).

Um die ToolTypes zu bearbeiten:
1. Klicken Sie das Programm-Icon auf der Workbench an.
2. Wählen Sie im Workbench-Menü **Icons $\rightarrow$ Information...** (oder Tastenkombination **`Rechte Amiga + I`**).
3. Aktivieren Sie die gewünschten ToolTypes durch Entfernen der runden Klammern `(...)` und tragen Sie Ihre Werte ein.

| ToolType | Standard | Beschreibung |
| :--- | :--- | :--- |
| `SERVER=<IP/Host>` | `(SERVER=192.168.178.2)` | IP-Adresse oder Rechnername des Rendering-Servers. |
| `PORT=<Port>` | `(PORT=7788)` | TCP-Port des Rendering-Servers (Standard: `7788`). |
| `URL=<URL>` | `(URL=https://www.amiga-news.de)` | Startseite beim Starten über das Icon. |
| `STREAMMODE=<Modus>` | `(STREAMMODE=AUTO)` | Streaming-System: `AUTO`, `H264` oder `LZ4` / `DIRTYRECT`. |
| `BITRATE=<kbps>` | `(BITRATE=8000)` | Video-Bitrate für H.264 in kbps (`1000` bis `10000`). |
| `FPS=<Zahl>` | `(FPS=25)` | Ziel-Bildwiederholrate (`15`, `25`, `30`, `50`, `60`). |
| `DARKMODE=<0\|1>` | `(DARKMODE=0)` | Dunkles Erscheinungsbild beim Start erzwingen (`1`=Dark, `0`=Hell). |
| `DOWNLOADDIR=<Pfad>` | `(DOWNLOADDIR=RAM:)` | Standard-Download-Verzeichnis. |

---

### Build & Ausführung

#### 1. Nativ auf Linux-Host (PC, CachyOS, Debian/Ubuntu, Raspberry Pi)
```bash
cd server
./install_server.sh           # Automatische Einrichtung (Treiber & Dependencies)
./run_server.sh               # Server starten (oder ./run_linux.sh im Hauptverzeichnis)
```

#### 2. Nativ auf macOS (Apple Silicon M1–M4 & Intel mit Apple VideoToolbox)
```bash
cd server
chmod +x install_macos.sh run_macos.sh
./install_macos.sh            # Installiert Python 3.11, FFmpeg, TurboJPEG/LZ4 & baut fast_pixconv.dylib
./run_macos.sh                # Startet mit h264_videotoolbox Hardware-Encoding
```

#### 3. Mit Docker oder Docker Compose (Linux x86_64, Raspberry Pi 4/5 & macOS Docker Desktop)
```bash
cd server

# Multi-Arch Docker-Images bauen (inkl. macOS-Images):
./build_docker.sh

# Starten auf Linux x86_64:
docker compose up -d vortex-server

# Starten auf Raspberry Pi 4/5 (ARM64):
docker compose --profile rpi up -d vortex-rpi

# Starten auf macOS (Docker Desktop – Apple Silicon & Intel):
docker compose -f docker-compose.macos.yml up -d
```

#### 4. Client auf AmigaOS 4.1 starten
1. Archiv `VortexBrowser-0.99-RC3-AmigaOS4.lha` auf dem Amiga entpacken (z. B. nach `Work:Internet/VortexBrowser`).
2. Doppelklick auf das `VortexBrowser`-Icon.
3. Server-IP eintragen (oder `Amiga+1` drücken) und auf **Verbinden** klicken.

---
---

<a name="english-version"></a>
## English Version

VortexBrowser renders modern websites via headless Chromium (Playwright / Rustwright + DirectCDP WebSocket) on a **universal Linux / macOS server or Docker container** (with automatic hardware acceleration for AMD Ryzen Vulkan, Intel QuickSync VA-API, **Apple VideoToolbox**, and Raspberry Pi 4/5 V3D) and streams compressed video data using **Planar YUV420P + 2D Dirty-Rect + LZ4** to a native **AmigaOS 4.1 PowerPC ReAction client**.

---

### Highlights & Features

1. **Hardware-Accelerated H.264 Video Streaming & Decoding (va.library + VA-API):**
   - **Embedded FFmpeg (`libavcodec`) on AmigaOS 4:** Decodes H.264 Annex-B bitstreams directly in hardware on Radeon HD/RX graphics cards using `va.library` (`vaPutSurface`).
   - **Server-Side Hardware Encoding:** Real-time GPU encoding via `h264_vaapi` (AMD VCN on Ryzen PRO or Intel QuickSync) with low CPU usage (< 3%).
   - **Adjustable Video Quality:**
     - Bitrate slider in Amiga settings (`Amiga+1`): **1,000 to 10,000 kbps (10 Mbit/s)**.
     - Server CLI parameter: `--video-bitrate <kbps>` or environment variable `VORTEX_VIDEO_BITRATE`.
   - **Zero-Latency:** 1-second keyframe interval (`-g 25`) for quick video start and 0 B-frames (`-bf 0`) for real-time delivery.
   - **Planar YUV420p (12-bit) & High-Speed JPEG:** Direct transmission without RGB conversion overhead.
   - **Selectable Frame Rates (15 / 25 / 30 / 50 / 60 FPS):** Configurable from the Amiga menu or settings window.
   - **2D Dirty-Rectangles (`CODEC_LZ4_RECT`):** Efficient bandwidth usage during general web browsing.
   - **AVX2 / ARM NEON SIMD Predictor:** Improves LZ4 compression ratio at full 8-bit depth without extra Amiga CPU load.
   - **DirectCDP WebSocket Core:** Low-overhead direct screencast connection.

2. **Native AmigaOS 4 ReAction GUI:**
   - **Classic AmigaOS 4 Look-and-Feel (`VortexBrowser` & `VortexBrowser-AltiVec`):** ReAction tabs (`clicktab.gadget`), font zooming, screen cloning, and GPU composite scaling.
   - **Multilingual Support (Locale):** Complete localization via `locale.library` with native AmigaOS catalogs for English (`english`), German (`german`/`deutsch`), French (`french`/`francais`), Italian (`italian`/`italiano`), and Spanish (`spanish`/`espanol`) in `catalogs/` (optionally overridable via `ENV:VortexBrowser/language`).
   - **Dynamic Quick Link Layout:** Toolbar buttons scale to use available window width.
   - **Bookmarks & Quick Links:** Managed in preferences (`Amiga+E`).

3. **Multi-Tab Browsing & Session Persistence:**
   - Tabs via `clicktab.gadget` using standard shortcuts (`Amiga+T` for new tab, `Amiga+W` to close).
   - **Session Restore:** Open tabs, URLs, and active index are stored in `ENVARC:` on exit and restored upon restart.

4. **Download Manager:**
   - Native Download Manager (`Amiga+D`) displaying download speed, progress percentage, ETA, and buttons to open files or folders.
   - Window position and size are remembered across sessions in `ENVARC:`.
   - Proper character set / umlaut conversion for filenames.

5. **Password Manager:**
   - Secure credential vault (`Amiga+K`) with Blowfish-CBC and SHA-256 encryption.
   - Copy username and password to Amiga clipboard.
   - Saved securely in `ENVARC:VortexBrowser/passwords.enc`.
   - **Twitch Support:** Login token import (`twitch_token.txt`) to bypass bot verification challenges.

6. **Audio Streaming:**
   - Low-latency 16-bit 44.1 kHz stereo audio streamed via dedicated port 9757 directly to AmigaOS 4 AHI (`ahi.device`).

---

### Workbench Icon ToolTypes & Configuration

VortexBrowser supports configuration via standard AmigaOS 4 **ToolTypes** in `.info` files (`VortexBrowser.info`, `VortexBrowser-AltiVec.info`):

1. Click the application icon on Workbench.
2. Select **Icons $\rightarrow$ Information...** from Workbench menu (or press **`Right Amiga + I`**).
3. Remove parentheses `(...)` to activate a ToolType and save.

| ToolType | Default | Description |
| :--- | :--- | :--- |
| `SERVER=<IP/Host>` | `(SERVER=192.168.178.2)` | Server IP address or hostname. |
| `PORT=<Port>` | `(PORT=7788)` | Server TCP port (default: `7788`). |
| `URL=<URL>` | `(URL=https://www.amiga-news.de)` | Default homepage on startup. |
| `STREAMMODE=<Mode>` | `(STREAMMODE=AUTO)` | Stream format: `AUTO`, `H264`, or `LZ4` / `DIRTYRECT`. |
| `BITRATE=<kbps>` | `(BITRATE=8000)` | Video bitrate for H.264 in kbps (`1000` to `10000`). |
| `FPS=<Number>` | `(FPS=25)` | Target frame rate (`15`, `25`, `30`, `50`, `60`). |
| `DARKMODE=<0\|1>` | `(DARKMODE=0)` | Force dark appearance (`1`=Dark, `0`=Light). |
| `DOWNLOADDIR=<Path>` | `(DOWNLOADDIR=RAM:)` | Default download path. |

---

### Build & Run Instructions

#### 1. Native on Linux Host (PC, CachyOS, Debian/Ubuntu, Raspberry Pi)
```bash
cd server
./install_server.sh           # Run once: detects hardware & installs drivers
./run_server.sh               # Starts the server (or ./run_linux.sh from repo root)
```

#### 2. Native on macOS (Apple Silicon M1–M4 & Intel with Apple VideoToolbox)
```bash
cd server
chmod +x install_macos.sh run_macos.sh
./install_macos.sh            # Installs Python 3.11, FFmpeg, TurboJPEG/LZ4 & compiles fast_pixconv.dylib
./run_macos.sh                # Launches with h264_videotoolbox hardware encoding
```

#### 3. Using Docker or Docker Compose (Linux x86_64, Raspberry Pi 4/5 & macOS Docker Desktop)
```bash
cd server

# Build multi-architecture Docker images (incl. macOS targets):
./build_docker.sh

# Run on Linux x86_64 (Intel / AMD):
docker compose up -d vortex-server

# Run on Raspberry Pi 4/5 (ARM64):
docker compose --profile rpi up -d vortex-rpi

# Run on macOS (Docker Desktop – Apple Silicon & Intel):
docker compose -f docker-compose.macos.yml up -d
```

#### 4. Starting the Client on AmigaOS 4.1
1. Unpack `VortexBrowser-0.99-RC3-AmigaOS4.lha` on your Amiga (e.g. into `Work:Internet/VortexBrowser`).
2. Double-click the `VortexBrowser` icon.
3. Enter your Linux/Docker server IP (or press `Amiga+1`) and click **Connect**.
