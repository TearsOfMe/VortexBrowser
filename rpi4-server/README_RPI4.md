# VortexBrowse - Server für Raspberry Pi 4 (ARM64 / Cortex-A72)

Dieses Paket enthält den vollständigen VortexBrowse Streaming-Server, speziell angepasst und optimiert für den **Raspberry Pi 4 Model B** (Broadcom BCM2711 / 4x Cortex-A72).

---

## Highlights & Raspberry Pi 4 Optimierungen

1. **Native ARM64 NEON SIMD Beschleunigung (`fast_pixconv_aarch64.so`)**:
   - Die rechenintensiven Pixelkonvertierungen (Chromium RGB24 $\to$ AmigaOS 4 RGB565 / ARGB32) und die Erkennung veränderter Bildbereiche (Dirty Slices) sind in nativem C mit **ARMv8-A NEON 128-Bit Vektor-Befehlen** implementiert.
   - Minimale CPU-Last auf den Cortex-A72 Kernen bei flüssigen 25 FPS.

2. **Automatischer System-Chromium Support**:
   - Erkennt automatisch das native ARM64-Chromium von Raspberry Pi OS (`/usr/bin/chromium-browser` oder `/usr/bin/chromium`).
   - Volle Unterstützung moderner Webstandards (HTML5, JavaScript, CSS3, WebGL, YouTube, moderne Portale).

3. **PipeWire & PulseAudio Audio-Streaming**:
   - Erstellt automatisch einen virtuellen Null-Sink (`vortexbrowse_sink`), greift das Audiosignal von Chromium in Echtzeit ab und streamt 22050 Hz Stereo direkt an das AmigaOS 4 AHI-Subsystem.

4. **Integrierte Download-Verwaltung**:
   - Sofortige Weiterleitung von Downloads an den nativen AmigaOS 4 ASL-Dateirequester.

---

## Voraussetzungen auf dem Raspberry Pi 4

- **Betriebssystem**: Raspberry Pi OS 64-bit (Debian 12 Bookworm empfohlen, Bullseye ebenfalls kompatibel) oder Ubuntu ARM64.
- **Hardware**: Raspberry Pi 4 Model B (2 GB, 4 GB oder 8 GB RAM) oder Raspberry Pi 400.

---

## Installation & Start (in 3 Schritten)

### 1. Entpacken
Kopiere das Archiv auf deinen Raspberry Pi (z. B. per SCP, Samba oder USB-Stick) und entpacke es:
```bash
tar -xvf vortexbrowse-rpi4-server.tar.gz -C ~/vortexbrowse
cd ~/vortexbrowse
```

### 2. Automatische Installation ausführen
Das Installationsskript installiert alle benötigten Pakete (`chromium`, `pipewire`, `python3-venv`, etc.), richtet die Python-Umgebung ein und prüft die native ARM64-Bibliothek:
```bash
chmod +x install_rpi4.sh run_rpi4.sh
./install_rpi4.sh
```

### 3. Server starten
Starte den Server einfach mit:
```bash
./run_rpi4.sh
```

Der Server lauscht standardmäßig auf **Port 7788** (und Port 9757 für Abwärtskompatibilität).

---

## AmigaOS 4 Client verbinden

1. Starte `VortexBrowse` auf deinem AmigaOS 4 System (z. B. AmigaOne, Sam460, Pegasos II oder X5000).
2. Gib in den Verbindungseinstellungen oder der Tooltype-Konfiguration die IP-Adresse deines Raspberry Pi 4 ein, z. B.:
   ```text
   HOST=192.168.178.xx
   PORT=7788
   ```
3. Klicke auf Verbinden – der Odyssey-Skin und das Webstreaming erscheinen sofort auf deiner Workbench!

---

## Optional: Autostart beim Booten (systemd)

Um VortexBrowse automatisch im Hintergrund zu starten, wenn der Raspberry Pi 4 hochfährt:

```bash
# Pfad in vortexbrowse.service anpassen, falls nicht /home/pi/vortexbrowse:
sudo cp vortexbrowse.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now vortexbrowse
```

Status und Logs anzeigen:
```bash
sudo systemctl status vortexbrowse
journalctl -u vortexbrowse -f
```

---

## Dateien im Paket

- `server.py`: Hauptserver mit Playwright-, CDP-, Audio- und Download-Steuerung.
- `fast_pixconv_aarch64.so`: Vorkompilierte C-Bibliothek mit ARM NEON SIMD Optimierung für Cortex-A72.
- `fast_pixconv.c`: C-Quellcode (falls auf anderen ARM-CPUs wie Pi 5 / Cortex-A76 neu kompiliert werden soll).
- `install_rpi4.sh`: Vollautomatisches Installationsskript für Raspberry Pi OS / Debian Bookworm.
- `run_rpi4.sh`: Startskript mit automatischer Audio-Sink Konfiguration.
- `vortexbrowse.service`: Systemd Service Unit für Headless Autostart.
- `requirements.txt`: Python-Abhängigkeiten (`playwright`, `Pillow`, `lz4`).
