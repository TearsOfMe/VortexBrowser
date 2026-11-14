# VortexBrowser Universal Linux Companion Server

Der offizielle, universelle Hochleistungs-Server für den **VortexBrowser (AmigaOS 4.1)**.

[English guide below / Zur englischen Anleitung](#english-guide)

---

## Deutsch

Dieser Server läuft auf jedem modernen Linux-System (x86_64 und ARM64) oder im **Docker-Container** und streamt moderne Webseiten mit voller Audio- und Videobeschleunigung an Ihren Amiga:
* **CachyOS / Arch Linux** (AMD Ryzen, Intel Core)
* **Debian / Ubuntu / Linux Mint**
* **Raspberry Pi OS** (Raspberry Pi 4 und Raspberry Pi 5 64-Bit)
* **Fedora / RHEL**
* **Docker & Docker Compose** (Multi-Arch: `linux/amd64` und `linux/arm64`)

---

### 🚀 Schnellstart & Installation (Nativ)

1. Wechseln Sie in den Ordner `server/`:
2. Führen Sie das universelle Installationsskript aus:
   ```bash
   ./install_server.sh
   ```
   *Das Skript erkennt automatisch Ihre Hardware (Intel, AMD, Raspberry Pi, ARM64) sowie Ihren Paketmanager und konfiguriert alle Treiber optimal.*

3. Starten Sie den Server:
   ```bash
   ./run_server.sh
   ```

4. Tragen Sie die IP-Adresse des Servers im VortexBrowser auf Ihrem Amiga ein (Standard-Port: **7788**) und klicken Sie auf **Verbinden**.

---

### 🐳 Docker & Docker Compose (Empfohlen für Server / NAS / Raspberry Pi)

Im Verzeichnis `server/` stehen ein Multi-Arch `Dockerfile` (`linux/amd64` und `linux/arm64`), eine `docker-compose.yml` sowie das Build-Skript `build_docker.sh` bereit.

#### Beide Docker-Images bauen:
```bash
./build_docker.sh
# Optional als .tar.gz nach dist/docker/ exportieren:
./build_docker.sh --export
```

#### Starten auf Linux x86_64 (Intel / AMD mit VA-API Beschleunigung):
```bash
docker compose up -d vortex-server
```
*(Oder direkt via: `docker run -d --name vortexbrowser-server --shm-size=2g -p 7788:7788 -p 9757:9757 --device /dev/dri:/dev/dri vortexbrowser-server:linux-amd64`)*

#### Starten auf Raspberry Pi 4 / 5 (ARM64):
```bash
docker compose --profile rpi up -d vortex-rpi
```
*(Oder direkt via: `docker run -d --name vortexbrowser-rpi --shm-size=1g -p 7788:7788 -p 9757:9757 vortexbrowser-server:rpi-arm64`)*

#### Starten auf macOS (Docker Desktop – Apple Silicon & Intel):
```bash
docker compose -f docker-compose.macos.yml up -d
```
*(Oder direkt via: `docker run -d --name vortexbrowser-macos --shm-size=2g -e VORTEX_PLATFORM=macos -p 7788:7788 -p 9757:9757 vortexbrowser-server:macos-arm64`)*

### 🍎 Schnellstart & Installation (macOS Nativ mit Apple VideoToolbox)

1. Wechseln Sie in den Ordner `server/` und installieren Sie die macOS-Abhängigkeiten:
   ```bash
   chmod +x install_macos.sh run_macos.sh
   ./install_macos.sh
   ```
   *Installiert über Homebrew `python@3.11`, `ffmpeg`, `jpeg-turbo`, `lz4`, optional `blackhole-2ch` (für Audio via AVFoundation) und kompiliert `fast_pixconv.dylib` mit Apple Clang (`-mcpu=apple-m1` / `-march=native`).*
2. Starten Sie den Server nativ mit Hardware-Encoding (`h264_videotoolbox`):
   ```bash
   ./run_macos.sh
   ```

---

### ⚙️ Features der Universal-Version

* **Vollautomatische Hardware-Erkennung**:
  * **Intel QuickSync & VA-API** (`iHD` / `i965`) für Intel CPUs
  * **AMD Radeon Vulkan & VA-API** für AMD Ryzen APUs / Grafikkarten
  * **Raspberry Pi V3D EGL** Hardware-Rendering
* **Native C-SIMD Pixel-Pipeline**:
  * Nutzt automatisch **AVX2 / FMA / SSE4.1** auf x86_64 bzw. **ARM NEON** auf 64-Bit ARM
  * Extrem verlustarme 2D Dirty-Rect Erkennung mit Turbokompression (LZ4 & TurboJPEG)
* **Audio-Streaming**:
  * Kristallklares Stereo-Audio (44.1 kHz 16-Bit) über PipeWire / PulseAudio auf Port **9757**
* **Werbeblocker & Media-Optimierung**:
  * Integriertes uBlock Origin & h264ify Erweiterungen
  * DarkReader für automatischen Dark Mode

---
---

<a name="english-guide"></a>
## English Guide

The official universal companion server for **VortexBrowser (AmigaOS 4.1)**.

Runs on any modern Linux distribution (x86_64 and ARM64) or inside **Docker containers**, streaming modern web pages with audio and hardware video acceleration to your AmigaOS 4 machine.

---

### 🚀 Quickstart & Native Installation

1. Open a terminal in the `server/` directory:
2. Run the universal installer:
   ```bash
   ./install_server.sh
   ```
   *Auto-detects CPU/GPU hardware (Intel, AMD, Raspberry Pi, ARM64) and system package managers, configuring drivers and dependencies automatically.*

3. Start the server:
   ```bash
   ./run_server.sh
   ```

4. Configure the server IP address in the Amiga client (default port: **7788**) and click **Connect**.

---

### 🐳 Docker & Docker Compose Setup

A multi-architecture `Dockerfile` (`linux/amd64` and `linux/arm64`), `docker-compose.yml`, and `build_docker.sh` are provided in `server/`.

#### Build Docker Images:
```bash
./build_docker.sh
# Optionally export compressed image archives to dist/docker/:
./build_docker.sh --export
```

#### Run on Linux x86_64 (Intel / AMD with VA-API GPU pass-through):
```bash
docker compose up -d vortex-server
```
*(Or via docker run: `docker run -d --name vortexbrowser-server --shm-size=2g -p 7788:7788 -p 9757:9757 --device /dev/dri:/dev/dri vortexbrowser-server:linux-amd64`)*

#### Run on Raspberry Pi 4 / 5 (ARM64):
```bash
docker compose --profile rpi up -d vortex-rpi
```
*(Or via docker run: `docker run -d --name vortexbrowser-rpi --shm-size=1g -p 7788:7788 -p 9757:9757 vortexbrowser-server:rpi-arm64`)*

#### Run on macOS (Docker Desktop – Apple Silicon & Intel):
```bash
docker compose -f docker-compose.macos.yml up -d
```
*(Or via docker run: `docker run -d --name vortexbrowser-macos --shm-size=2g -e VORTEX_PLATFORM=macos -p 7788:7788 -p 9757:9757 vortexbrowser-server:macos-arm64`)*

### 🍎 Quickstart & Native Installation (macOS with Apple VideoToolbox)

1. Open a terminal in `server/` and run the macOS installer:
   ```bash
   chmod +x install_macos.sh run_macos.sh
   ./install_macos.sh
   ```
   *Installs `python@3.11`, `ffmpeg`, `jpeg-turbo`, `lz4`, and optionally `blackhole-2ch` via Homebrew, and compiles `fast_pixconv.dylib` with Apple Clang (`-mcpu=apple-m1` / `-march=native`).*
2. Launch the native server with hardware encoding (`h264_videotoolbox`):
   ```bash
   ./run_macos.sh
   ```

---

### ⚙️ Server Highlights

* **Hardware Video Acceleration**:
  * Intel QuickSync & VA-API (`iHD` / `i965`)
  * AMD Radeon Vulkan & VA-API
  * Raspberry Pi V3D EGL acceleration
* **Native C-SIMD Pixel Engine**:
  * Utilizes AVX2, SSE4.1, and ARMv8 NEON
  * 2D dirty-rectangle detection and fast LZ4 / TurboJPEG compression
* **Low-Latency Audio Streaming**:
  * 16-bit 44.1 kHz stereo audio via PulseAudio/PipeWire streamed on port **9757**
* **Integrated Ad-Blocking & Content Enhancements**:
  * Built-in uBlock Origin & h264ify extensions
  * DarkReader dark mode engine
