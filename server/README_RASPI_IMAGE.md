# VortexRemoteBrowser – Ready-to-Flash SD Card Images for Raspberry Pi 3, 4 & 5

[Zur deutschen Anleitung](#deutsch)

With these prebuilt Raspberry Pi images, **no manual setup is required**: flash the image onto a microSD card, insert it, power on your Raspberry Pi, and the **VortexRemoteBrowser Server** starts automatically and is immediately ready for your AmigaOS 4 client.

---

## English Guide

### 1. Which Image for Which Raspberry Pi?

| Model | Image File | Optimization & Defaults | mDNS Hostname |
| :--- | :--- | :--- | :--- |
| **Raspberry Pi 3** (3B / 3B+ / 3A+) | `vortexremotebrowser-rpi3-arm64.img.xz` | Cortex-A53 (`fast_pixconv.so`), ZRAM (`zstd`), `1280x720 @ 20 FPS`, `3500` kbps | `vortexremotebrowser-rpi3.local` |
| **Raspberry Pi 4** (4B / 400 / CM4) | `vortexremotebrowser-rpi4-arm64.img.xz` | Cortex-A72 (`fast_pixconv.so`), ZRAM (`zstd`), `1280x720 @ 25 FPS`, `4000` kbps | `vortexremotebrowser.local` |
| **Raspberry Pi 5** (5 / 500) | `vortexremotebrowser-rpi5-arm64.img.xz` | Cortex-A76 (`fast_pixconv.so`), `1920x1080 @ 30 FPS`, `8000` kbps | `vortexremotebrowser-rpi5.local` |

> **Recommended microSD size:** **16 GB** or larger. On first boot, the root filesystem automatically expands to fill the entire SD card.

---

### 2. What Is Already Pre-Installed & Configured?

* **OS Base:** Official **Raspberry Pi OS Lite 64-bit (Debian Trixie ARM64)**
* **Default Browser:** **Ungoogled-Chromium `156.0.8078.79`** (`/usr/bin/ungoogled-chromium`) with **Broadcom V3D ANGLE GLES/EGL** GPU acceleration (`libegl1`, `libegl-mesa0`, `libgles2`, `mesa-vulkan-drivers`)
* **Alternative Browser Engine:** **Playwright Firefox** + **`firefox-esr 153.7.0esr`** pre-installed
* **Automation Core:** **Rustwright** (`VORTEX_USE_RUSTWRIGHT=1`) for fast zero-overhead DirectCDP streaming
* **VortexRemoteBrowser Server (`/home/pi/vortexbrowse/server`):**
  * Python `.venv` with all dependencies (`rustwright`, `playwright`, `websockets`, `lz4`, `Pillow`, `PyTurboJPEG`)
  * Target-optimized native ARM64 `fast_pixconv.so` (TurboJPEG + LZ4 + OpenMP)
  * `pulseaudio` / `pulseaudio-utils` for AHI audio streaming to AmigaOS 4
  * Enabled systemd services: `vortexremotebrowser.service`, `ssh.service`, `avahi-daemon.service`

---

### 3. Flash the Image to a microSD Card

> **Important for Raspberry Pi Imager:** Do **not** apply "OS Customisation" settings in Raspberry Pi Imager — the user account, SSH, and auto-start service are already configured.

#### Option A: Raspberry Pi Imager (Windows / macOS / Linux)
1. Open **Raspberry Pi Imager**.
2. Click **Choose OS → Use custom** and select the matching `.img.xz` file (no manual decompression needed).
3. Select your microSD card, decline OS customisation (**No**), and click **Write**.

#### Option B: balenaEtcher (Windows / macOS / Linux)
1. Click **Flash from file** and select the `.img.xz` file.
2. Select your microSD card → **Flash!**.

#### Option C: Linux Terminal (`dd`)
Optionally verify the checksum first:
```bash
sha256sum -c vortexremotebrowser-rpi4-arm64.img.xz.sha256
```
Then write to the target block device (check `lsblk` carefully before running!):
```bash
xzcat vortexremotebrowser-rpi4-arm64.img.xz | sudo dd of=/dev/sdX bs=4M status=progress conv=fsync
sync
```

---

### 4. Boot & Connect From Your Amiga

1. Insert the flashed microSD card into your Raspberry Pi.
2. Connect an **Ethernet cable** (recommended for lowest latency) and power on the Pi.
3. On the **very first boot**, wait about **60–90 seconds** while the filesystem expands automatically and `vortexremotebrowser.service` starts.
4. In **VortexRemoteBrowser** on **AmigaOS 4.1**:
   * Enter the Pi's hostname (`vortexremotebrowser.local`, `vortexremotebrowser-rpi3.local`, or `vortexremotebrowser-rpi5.local`) or its IP address from your router.
   * Default ports: **7788** (Video / Control) and **9757** (Audio).
   * Click **Connect** — done!

---

### 5. SSH Credentials & Maintenance

SSH is enabled out of the box:
* **Username:** `pi`
* **Password:** `vortex`

Connect via SSH (Pi 4 example):
```bash
ssh pi@vortexremotebrowser.local
```
*(For security, run `passwd` after your first login to set your own password.)*

---

### 6. Switch Browser Engine (Ungoogled-Chromium ↔ Firefox)

By default, the server uses **Ungoogled-Chromium** (fastest DirectCDP screencast path via Rustwright). To switch to **Firefox** (or back):

```bash
# Switch to Firefox (automatically restarts the server):
sudo vortex-engine firefox

# Switch back to Ungoogled-Chromium:
sudo vortex-engine chromium
```

#### Adjust Resolution, FPS & Bitrate
All server defaults live in `/etc/default/vortexremotebrowser`:
```bash
sudo nano /etc/default/vortexremotebrowser
```
Example:
```ini
VORTEX_ENGINE=chromium
VORTEX_USE_RUSTWRIGHT=1
VORTEX_WIDTH=1280
VORTEX_HEIGHT=720
VORTEX_FPS=25
VORTEX_VIDEO_BITRATE=4000
VORTEX_LOG_LEVEL=INFO
```
Restart the service after editing:
```bash
sudo systemctl restart vortexremotebrowser.service
```

---

### 7. Useful Diagnostic Commands

```bash
# Check server status:
systemctl status vortexremotebrowser.service

# Follow live logs:
journalctl -u vortexremotebrowser.service -f

# Restart or stop the server:
sudo systemctl restart vortexremotebrowser.service
sudo systemctl stop vortexremotebrowser.service
```

---

### 8. Rebuild Images Yourself (Optional)

You can rebuild all three images anytime via Docker (`linux/arm64`) using [build_rpi_image.sh](build_rpi_image.sh):
```bash
cd server
./build_rpi_image.sh all      # builds rpi3, rpi4, and rpi5 into ../dist/
# or individually:
./build_rpi_image.sh rpi3
./build_rpi_image.sh rpi4
./build_rpi_image.sh rpi5
```

---

## Deutsch

Mit den fertigen Raspberry-Pi-Images benötigt dein Raspberry Pi **keine manuelle Installation** mehr: Image auf eine microSD-Karte schreiben, Karte einlegen, Raspberry Pi einschalten – der **VortexRemoteBrowser Server** startet automatisch und ist sofort für den AmigaOS-4-Client erreichbar.

---

### 1. Welches Image für welchen Raspberry Pi?

| Modell | Image-Datei | Optimierung & Standardwerte | mDNS-Hostname |
| :--- | :--- | :--- | :--- |
| **Raspberry Pi 3** (3B / 3B+ / 3A+) | `vortexremotebrowser-rpi3-arm64.img.xz` | Cortex-A53 (`fast_pixconv.so`), ZRAM (`zstd`), `1280x720 @ 20 FPS`, `3500` kbps | `vortexremotebrowser-rpi3.local` |
| **Raspberry Pi 4** (4B / 400 / CM4) | `vortexremotebrowser-rpi4-arm64.img.xz` | Cortex-A72 (`fast_pixconv.so`), ZRAM (`zstd`), `1280x720 @ 25 FPS`, `4000` kbps | `vortexremotebrowser.local` |
| **Raspberry Pi 5** (5 / 500) | `vortexremotebrowser-rpi5-arm64.img.xz` | Cortex-A76 (`fast_pixconv.so`), `1920x1080 @ 30 FPS`, `8000` kbps | `vortexremotebrowser-rpi5.local` |

> **Empfohlene SD-Karten-Größe:** mindestens **16 GB** (oder größer). Beim ersten Boot vergrößert sich das Dateisystem automatisch auf den gesamten freien Speicherplatz der SD-Karte.

---

### 2. Was ist im Image bereits fix & fertig eingerichtet?

* **System:** Offizielles **Raspberry Pi OS Lite 64-Bit (Debian Trixie ARM64)**
* **Standard-Browser:** **Ungoogled-Chromium `156.0.8078.79`** (`/usr/bin/ungoogled-chromium`) mit **Broadcom V3D ANGLE GLES/EGL** GPU-Beschleunigung (`libegl1`, `libegl-mesa0`, `libgles2`, `mesa-vulkan-drivers`)
* **Alternative Browser-Engine:** **Playwright Firefox** + **`firefox-esr 153.7.0esr`** komplett vorinstalliert
* **Automation-Core:** **Rustwright** (`VORTEX_USE_RUSTWRIGHT=1`) für schnelles DirectCDP-Streaming
* **VortexRemoteBrowser Server (`/home/pi/vortexbrowse/server`):**
  * Python-`.venv` mit allen Abhängigkeiten (`rustwright`, `playwright`, `websockets`, `lz4`, `Pillow`, `PyTurboJPEG`)
  * Nativ für das jeweilige Pi-Modell kompiliertes `fast_pixconv.so` (TurboJPEG + LZ4 + OpenMP)
  * `pulseaudio` / `pulseaudio-utils` für AHI-Audiostreaming an den Amiga
  * Aktivierte Systemdienste: `vortexremotebrowser.service`, `ssh.service`, `avahi-daemon.service`

---

### 3. Image auf die SD-Karte flashen

> **Wichtig für Raspberry Pi Imager:** Wähle **kein** nachträgliches „OS-Customizing“ (Einstellungen bearbeiten) im Imager aus – Benutzer, SSH und Autostart sind bereits fertig konfiguriert.

#### Variante A: Mit Raspberry Pi Imager (Windows / macOS / Linux)
1. **Raspberry Pi Imager** starten.
2. **Betriebssystem wählen → „Eigenes Image“** (ganz unten) und die passende `.img.xz`-Datei auswählen (ein vorheriges Entpacken ist nicht nötig).
3. Ziel-SD-Karte auswählen, eventuelle Anpassungsabfragen mit **Nein** überspringen und **Schreiben** starten.

#### Variante B: Mit balenaEtcher (Windows / macOS / Linux)
1. **Flash from file** → passende `.img.xz`-Datei wählen.
2. SD-Karte wählen → **Flash!**.

#### Variante C: Direkt im Linux-Terminal (`dd`)
Zuerst optional die Prüfsumme kontrollieren:
```bash
sha256sum -c vortexremotebrowser-rpi4-arm64.img.xz.sha256
```
Dann das richtige Zielgerät (z. B. `/dev/sdX` oder `/dev/mmcblk0` – **unbedingt vorher mit `lsblk` prüfen!**) beschreiben:
```bash
xzcat vortexremotebrowser-rpi4-arm64.img.xz | sudo dd of=/dev/sdX bs=4M status=progress conv=fsync
sync
```

---

### 4. Starten & mit dem Amiga verbinden

1. Geflashte microSD-Karte in den Raspberry Pi einlegen.
2. **LAN-Kabel** anschließen (empfohlen für minimale Latenz) und Strom verbinden.
3. Beim **allerersten Start** ca. **60–90 Sekunden** warten (einmalige Dateisystem-Erweiterung auf die volle SD-Kartengröße + Start von `vortexremotebrowser.service`).
4. Auf dem **AmigaOS 4.1**-System in **VortexRemoteBrowser**:
   * Als Server-Adresse entweder den Hostnamen (`vortexremotebrowser.local`, `vortexremotebrowser-rpi3.local` oder `vortexremotebrowser-rpi5.local`) oder die IP-Adresse des Pi aus dem Router eintragen.
   * Standard-Ports: **7788** (Video/Steuerung) und **9757** (Audio).
   * Auf **Verbinden** klicken – fertig!

---

### 5. Zugangsdaten & Wartung per SSH

SSH ist bereits ab Werk aktiviert:
* **Benutzername:** `pi`
* **Passwort:** `vortex`

Verbindung herstellen (Beispiel für Pi 4):
```bash
ssh pi@vortexremotebrowser.local
```
*(Aus Sicherheitsgründen nach dem ersten Login empfehlenswert: `passwd` ausführen, um ein eigenes Passwort zu setzen.)*

---

### 6. Browser wechseln (Ungoogled-Chromium ↔ Firefox)

Standardmäßig nutzt der Server **Ungoogled-Chromium** (schnellster DirectCDP-Screencast-Pfad per Rustwright). Falls eine Webseite im alternativen **Firefox** geöffnet werden soll, lässt sich die Engine mit einem einzigen Befehl umschalten:

```bash
# Auf Firefox umschalten (startet den Server automatisch neu):
sudo vortex-engine firefox

# Wieder zurück auf Ungoogled-Chromium:
sudo vortex-engine chromium
```

#### Auflösung, FPS & Bitrate anpassen
Alle Startparameter stehen in `/etc/default/vortexremotebrowser`:
```bash
sudo nano /etc/default/vortexremotebrowser
```
Beispielinhalt:
```ini
VORTEX_ENGINE=chromium
VORTEX_USE_RUSTWRIGHT=1
VORTEX_WIDTH=1280
VORTEX_HEIGHT=720
VORTEX_FPS=25
VORTEX_VIDEO_BITRATE=4000
VORTEX_LOG_LEVEL=INFO
```
Nach einer Änderung den Dienst neu starten:
```bash
sudo systemctl restart vortexremotebrowser.service
```

---

### 7. Nützliche Diagnose-Befehle auf dem Pi

```bash
# Läuft der VortexRemoteBrowser Server?
systemctl status vortexremotebrowser.service

# Live-Log ansehen:
journalctl -u vortexremotebrowser.service -f

# Server neu starten / stoppen:
sudo systemctl restart vortexremotebrowser.service
sudo systemctl stop vortexremotebrowser.service
```

---

### 8. Images selbst neu bauen (optional)

Mit [build_rpi_image.sh](build_rpi_image.sh) lassen sich alle drei Images per Docker (`linux/arm64`) jederzeit aus dem aktuellen `server/`-Stand neu erzeugen:
```bash
cd server
./build_rpi_image.sh all      # baut rpi3, rpi4 und rpi5 nach ../dist/
# oder einzeln:
./build_rpi_image.sh rpi3
./build_rpi_image.sh rpi4
./build_rpi_image.sh rpi5
```
