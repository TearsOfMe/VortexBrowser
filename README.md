# VortexBrowser – High-Performance Headless Web Browser Streaming System
**(Linux PC / Raspberry Pi 4/5 Server $\rightarrow$ AmigaOS 4.1 PowerPC Client)**

VortexBrowser rendert moderne Webseiten über einen Headless Chromium (Playwright/CDP) auf dem **Linux-Server** (bzw. Raspberry Pi 4/5) und streamt die Bilddaten asymmetrisch komprimiert per **MJPEG/LZ4** an einen nativen **AmigaOS 4.1 PowerPC Reaction-Client**.

---

## Highlights & Features

1. **Multi-Tab-Browsing & Sitzungswiederherstellung:**
   - Echte Tabs über `clicktab.gadget` mit AmigaOS 4 Standard-Bedienung (`Amiga+T` für neuen Tab, `Amiga+W` zum Schließen).
   - **Tab-Persistenz:** Sämtliche geöffneten Tabs, Titel und der aktive Tab werden beim Beenden automatisch gespeichert und beim nächsten Start nahtlos wiederhergestellt.

2. **Download-Manager:**
   - Vollständiger nativer Download-Manager (`Amiga+D`) mit Live-Geschwindigkeitsanzeige, Fortschritt in Prozent, Restzeitschätzung (ETA), Datei- und Ordneröffner.
   - Saubere Umlaut- und Zeichensatz-Behandlung für Dateinamen und GUI-Buttons.

3. **Passwort-Manager & Auto-Login:**
   - Integrierter Passwort-Tresor (`Amiga+K`) mit starker Blowfish-CBC- und SHA-256-Verschlüsselung.
   - Ein-Klick Auto-Login für die aktuelle Website über `Amiga+L`.
   - Speicherort in `ENVARC:VortexBrowser/passwords.enc` und `ENV:VortexBrowser/passwords.enc`.

4. **Dateisystem-Stabilität (exFAT / SFS / FFS / RAM):**
   - Integrierter 512 KB Stack-Cookie (`$STACK: 524288` und `__stack = 524288`).
   - Deadlock-freie Konfiguration: Benutzereinstellungen werden atomar in `ENV:` und `ENVARC:` gespeichert, ohne Locks auf Dateisysteme wie `exFATFileSystem` zu halten.

5. **Audio-Streaming:**
   - Synchroner 16-Bit 48kHz Stereo-Audiostream über separaten Port 9757 direkt an AmigaOS 4 AHI (`ahi.device`).

6. **Radeon GPU-Skalierung:**
   - Hardware-Composite-Skalierung über `IGraphics->CompositeTags()` (z. B. 720p HD oder 540p qHD auf volle Bildschirmgröße) entlastet Amiga-CPUs (G4, SAM440, SAM460) um bis zu 70%.

---

## Verzeichnisstruktur des Gesamtpakets

- `VortexBrowser/`: Der vollständige AmigaOS 4 Client-Ordner
  - `VortexBrowser`: AmigaOS 4 Executable (Newlib)
  - `VortexBrowser.info`: AmigaOS 4 Dual-PNG-Icon
  - `VortexBrowser.guide`: Ausführliches AmigaGuide-Handbuch
  - `LIESMICH.txt`: Deutsche Kurzanleitung
  - `README.txt`: Englische Kurzanleitung
  - `icons/`: Symbolleisten- und Schnellwahl-Icons
- `Server-Linux/`: Rendering-Server für x86_64 Linux PCs
- `Server-RaspberryPi4/`: Rendering-Server für Raspberry Pi 4 / 5 (ARM64)
- `ANLEITUNG_DEUTSCH.txt`: Gesamtanleitung

---

## Build & Installation

### 1. Client kompilieren & Release-Pakete schnüren
```bash
# Kompiliert das SIMD-Modul und den AmigaOS 4 Client
./build.sh

# Erstellt alle Release-Archive (LHA, Complete-ZIP, Linux-ZIP, Pi-ZIP)
python3 make_release.py
```

### 2. Server starten (Linux PC oder Raspberry Pi 4/5)
```bash
cd Server-Linux        # bzw. Server-RaspberryPi4
./install_server.sh    # Einmalige Einrichtung
./run_server.sh        # Server starten
```

### 3. Client auf AmigaOS 4.1 starten
1. Ordner `VortexBrowser` auf den Amiga kopieren (z. B. nach `Work:Internet/VortexBrowser`).
2. Doppelklick auf das `VortexBrowser`-Icon.
3. IP-Adresse des Servers eingeben und auf **Verbinden** klicken!
