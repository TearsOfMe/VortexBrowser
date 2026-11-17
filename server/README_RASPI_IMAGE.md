# VortexBrowser – Fertige SD-Karten-Images für Raspberry Pi 3, 4 und 5

Mit den fertigen Raspberry-Pi-Images benötigt dein Raspberry Pi **keine manuelle Installation** mehr: Image auf eine microSD-Karte schreiben, Karte einlegen, Raspberry Pi einschalten – der **VortexBrowser Server** startet automatisch und ist sofort für den AmigaOS-4-Client erreichbar.

---

## 1. Welches Image für welchen Raspberry Pi?

| Modell | Image-Datei | Optimierung & Standardwerte | mDNS-Hostname |
| :--- | :--- | :--- | :--- |
| **Raspberry Pi 3** (3B / 3B+ / 3A+) | `vortexbrowser-rpi3-arm64.img.xz` | Cortex-A53 (`fast_pixconv.so`), ZRAM (`zstd`), `1280x720 @ 20 FPS`, `3500` kbps | `vortexbrowser-rpi3.local` |
| **Raspberry Pi 4** (4B / 400 / CM4) | `vortexbrowser-rpi4-arm64.img.xz` | Cortex-A72 (`fast_pixconv.so`), `1280x720 @ 25 FPS`, `4000` kbps | `vortexbrowser.local` |
| **Raspberry Pi 5** (5 / 500) | `vortexbrowser-rpi5-arm64.img.xz` | Cortex-A76 (`fast_pixconv.so`), `1920x1080 @ 30 FPS`, `8000` kbps | `vortexbrowser-rpi5.local` |

> **Empfohlene SD-Karten-Größe:** mindestens **16 GB** (oder größer). Beim ersten Boot vergrößert sich das Dateisystem automatisch auf den gesamten freien Speicherplatz der SD-Karte.

---

## 2. Was ist im Image bereits fix & fertig eingerichtet?

* **System:** Offizielles **Raspberry Pi OS Lite 64-Bit (Debian Trixie ARM64)**
* **Standard-Browser:** **Ungoogled-Chromium `156.0.8078.79`** (`/usr/bin/ungoogled-chromium`)
* **Alternative Browser-Engine:** **Playwright Firefox** + **`firefox-esr 153.7.0esr`** komplett vorinstalliert
* **VortexBrowser Server (`/home/pi/vortexbrowse/server`):**
  * Python-`.venv` mit allen Abhängigkeiten (`playwright`, `websockets`, `lz4`, `Pillow`, `PyTurboJPEG`)
  * Nativ für das jeweilige Pi-Modell kompiliertes `fast_pixconv.so` (TurboJPEG + LZ4 + OpenMP)
  * `pulseaudio` / `pulseaudio-utils` für AHI-Audiostreaming an den Amiga
  * Aktivierte Systemdienste: `vortexbrowser.service`, `ssh.service`, `avahi-daemon.service`

---

## 3. Image auf die SD-Karte flashen

> **Wichtig für Raspberry Pi Imager:** Wähle **kein** nachträgliches „OS-Customizing“ (Einstellungen bearbeiten) im Imager aus – Benutzer, SSH und Autostart sind bereits fertig konfiguriert.

### Variante A: Mit Raspberry Pi Imager (Windows / macOS / Linux)
1. **Raspberry Pi Imager** starten.
2. **Betriebssystem wählen → „Eigenes Image“** (ganz unten) und die passende `.img.xz`-Datei auswählen (ein vorheriges Entpacken ist nicht nötig).
3. Ziel-SD-Karte auswählen, eventuelle Anpassungsabfragen mit **Nein** überspringen und **Schreiben** starten.

### Variante B: Mit balenaEtcher (Windows / macOS / Linux)
1. **Flash from file** → passende `.img.xz`-Datei wählen.
2. SD-Karte wählen → **Flash!**.

### Variante C: Direkt im Linux-Terminal (`dd`)
Zuerst optional die Prüfsumme kontrollieren:
```bash
sha256sum -c vortexbrowser-rpi4-arm64.img.xz.sha256
```
Dann das richtige Zielgerät (z. B. `/dev/sdX` oder `/dev/mmcblk0` – **unbedingt vorher mit `lsblk` prüfen!**) beschreiben:
```bash
xzcat vortexbrowser-rpi4-arm64.img.xz | sudo dd of=/dev/sdX bs=4M status=progress conv=fsync
sync
```

---

## 4. Starten & mit dem Amiga verbinden

1. Geflashte microSD-Karte in den Raspberry Pi einlegen.
2. **LAN-Kabel** anschließen (empfohlen für minimale Latenz) und Strom verbinden.
3. Beim **allerersten Start** ca. **60–90 Sekunden** warten (einmalige Dateisystem-Erweiterung auf die volle SD-Kartengröße + Start von `vortexbrowser.service`).
4. Auf dem **AmigaOS 4.1**-System in **VortexBrowser**:
   * Als Server-Adresse entweder den Hostnamen (`vortexbrowser.local`, `vortexbrowser-rpi3.local` oder `vortexbrowser-rpi5.local`) oder die IP-Adresse des Pi aus dem Router eintragen.
   * Standard-Ports: **7788** (Video/Steuerung) und **9757** (Audio).
   * Auf **Verbinden** klicken – fertig!

---

## 5. Zugangsdaten & Wartung per SSH

SSH ist bereits ab Werk aktiviert:
* **Benutzername:** `pi`
* **Passwort:** `vortex`

Verbindung herstellen (Beispiel für Pi 4):
```bash
ssh pi@vortexbrowser.local
```
*(Aus Sicherheitsgründen nach dem ersten Login empfehlenswert: `passwd` ausführen, um ein eigenes Passwort zu setzen.)*

---

## 6. Browser wechseln (Ungoogled-Chromium ↔ Firefox)

Standardmäßig nutzt der Server **Ungoogled-Chromium** (schnellster CDP-Screencast-Pfad). Falls eine Webseite im alternativen **Firefox** geöffnet werden soll, lässt sich die Engine mit einem einzigen Befehl umschalten:

```bash
# Auf Firefox umschalten (startet den Server automatisch neu):
sudo vortex-engine firefox

# Wieder zurück auf Ungoogled-Chromium:
sudo vortex-engine chromium
```

### Auflösung, FPS & Bitrate anpassen
Alle Startparameter stehen in `/etc/default/vortexbrowser`:
```bash
sudo nano /etc/default/vortexbrowser
```
Beispielinhalt:
```ini
VORTEX_ENGINE=chromium
VORTEX_WIDTH=1280
VORTEX_HEIGHT=720
VORTEX_FPS=25
VORTEX_VIDEO_BITRATE=4000
VORTEX_LOG_LEVEL=INFO
```
Nach einer Änderung den Dienst neu starten:
```bash
sudo systemctl restart vortexbrowser.service
```

---

## 7. Nützliche Diagnose-Befehle auf dem Pi

```bash
# Läuft der VortexBrowser Server?
systemctl status vortexbrowser.service

# Live-Log ansehen:
journalctl -u vortexbrowser.service -f

# Server neu starten / stoppen:
sudo systemctl restart vortexbrowser.service
sudo systemctl stop vortexbrowser.service
```

---

## 8. Images selbst neu bauen (optional)

Mit [build_rpi_image.sh](build_rpi_image.sh) lassen sich alle drei Images per Docker (`linux/arm64`) jederzeit aus dem aktuellen `server/`-Stand neu erzeugen:
```bash
cd server
./build_rpi_image.sh all      # baut rpi3, rpi4 und rpi5 nach ../dist/
# oder einzeln:
./build_rpi_image.sh rpi3
./build_rpi_image.sh rpi4
./build_rpi_image.sh rpi5
```
