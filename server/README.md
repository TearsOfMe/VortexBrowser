# VortexBrowser Universal Linux Companion Server

Der offizielle, universelle Hochleistungs-Server fuer den **VortexBrowser (AmigaOS 4.1)**.

Dieser Server laeuft auf jedem modernen Linux-System (x86_64 und ARM64) und streamt moderne Webseiten mit voller Audio- und Videobeschleunigung an Ihren Amiga:
* **CachyOS / Arch Linux** (AMD Ryzen, Intel Core)
* **Debian / Ubuntu / Linux Mint**
* **Raspberry Pi OS** (Raspberry Pi 4 und Raspberry Pi 5)
* **Fedora / RHEL**
* **Intel NUC, Atom, Celeron & Mini-PCs**

---

## 🚀 Schnellstart & Installation

1. Kopieren Sie den Ordner `Server-Linux` auf Ihren Linux-PC oder Raspberry Pi.
2. Fuehren Sie das universelle Installationsskript aus:
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

## ⚙️ Features der Universal-Version

* **Vollautomatische Hardware-Erkennung**:
  * **Intel QuickSync & VA-API** (`iHD` / `i965`) fuer Intel CPUs
  * **AMD Radeon Vulkan & VA-API** fuer AMD Ryzen APUs / Grafikkarten
  * **Raspberry Pi V3D EGL** Hardware-Rendering
* **Native C-SIMD Pixel-Pipeline**:
  * Nutzt automatisch **AVX2 / FMA / SSE4.1** auf x86_64 bzw. **ARM NEON** auf 64-Bit ARM
  * Extrem verlustarme 2D Dirty-Rect Erkennung mit Turbokompression (LZ4 & TurboJPEG)
* **Audio-Streaming**:
  * Kristallklares Stereo-Audio (44.1 kHz 16-Bit) ueber PipeWire / PulseAudio auf Port **9757**
* **Werbeblocker & Media-Optimierung**:
  * Integriertes uBlock Origin & h264ify Erweiterungen
  * DarkReader fuer automatischen Dark Mode
