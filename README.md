# VortexBrowser – High-Performance Headless Web Browser Streaming System
**(Linux PC / CachyOS-Linux / Raspberry Pi 4/5 Server $\rightarrow$ AmigaOS 4.1 PowerPC Client)**

VortexBrowser rendert moderne Webseiten über einen Headless Chromium (Playwright / Rustwright + DirectCDP WebSocket) auf einem **Linux-Server** (Standard-PC, CachyOS-Linux PC oder Raspberry Pi 4/5) und streamt die Bilddaten hocheffizient komprimiert per **Planar YUV420P + 2D Dirty-Rect + LZ4** an einen nativen **AmigaOS 4.1 PowerPC Reaction-Client**.

---

## Highlights & Features

1. **Hardware-beschleunigtes H.264 Video-Streaming & Dekodierung (va.library + VA-API):**
   - **Eingebettetes FFmpeg (`libavcodec`) auf AmigaOS 4:** Dekodiert H.264 Annex-B Bitstreams direkt in Hardware auf Radeon HD/RX Grafikkarten via `va.library` (`vaPutSurface`) – analog zu Maijestros MPlayer-Architektur.
   - **Hardware-Encoding auf dem Server:** H.264 GPU-Encoding in Echtzeit via `h264_vaapi` (AMD VCN auf Ryzen PRO bzw. Intel QuickSync) bei minimaler Server-CPU-Last (< 3%).
   - **Einstellbare Video-Qualität:**
     - Neuer Schieberegler in den Amiga-Einstellungen (`Amiga+1`): **Video-Bitrate von 1.000 bis 10.000 kbps (10 Mbit/s)**.
     - Server-Kommandozeilenoption: `--video-bitrate <kbps>` bzw. Umgebungsvariable `VORTEX_VIDEO_BITRATE`.
   - **Zero-Latency:** Exaktes 1-Sekunden-Keyframe-Intervall (`-g 25`) für sofortigen Sync bei Video-Start und 0 B-Frames (`-bf 0`) für latenzfreie Echtzeitübertragung.
   - **Planar YUV420p (12-Bit) & High-Speed JPEG:** Direkte hardwarenahe Übertragung ohne RGB-Overhead.
   - **Wählbare Framerate (15 / 25 / 30 / 50 / 60 FPS):** Vom Amiga aus flexibel einstellbar via Menü oder Einstellungen.
   - **2D Dirty-Rectangles (`CODEC_LZ4_RECT`):** Für sparsamen Datentransfer bei normalem Web-Browsing.
   - **AVX2-Mikroprädiktor:** Verdoppelt die LZ4-Kompressionsrate bei vollen 8-Bit Farbtiefe (PSNR > 44 dB, verlustfreie Wahrnehmung) ohne CPU-Last auf dem Amiga.
   - **Rustwright + DirectCDP Core:** Direkte WebSocket-Screencast-Anbindung ohne Node.js-Treiber-Overhead.

2. **Zwei native GUI-Engines zur Auswahl (ReAction & MUI im Odyssey-Look):**
   - **Standard ReAction (`VortexBrowser` & `VortexBrowser-AltiVec`):** Klassisches AmigaOS 4 System-Look-and-Feel mit Tabs (`clicktab.gadget`), Font-Zoom, Screen-Cloning und GPU-Composite-Skalierung.
   - **MUI Edition (`VortexBrowser-MUI` im Odyssey Web Browser Look):**
     - Exakt nachempfundene Benutzeroberfläche des beliebten AmigaOS 4 Odyssey Web Browsers:
       - **Navigationsleiste (`navigationgroup`):** Schnellzugriff auf `<` (Zurück), `>` (Vorwärts), `Neu`, `Stopp`, `Start`, `Lesezeichen`, `Downloads`, `Passwörter`, `+ Tab`, `- Tab` und animierter Lade-Throbber.
       - **Adress- & Suchzeile (`urlgroup`):** Sicherheits-Indikator (`[🔒 https]` / `[🌐 http]`), URL-Eingabezeile, `[X]`, `[Los]`, interaktiver `BalanceObject`-Teiler und Suchleiste mit Suchmaschinen-Wahl (`DuckDuckGo`, `Google`, `Wikipedia`, `Aminet`, `OS4Depot`, `YouTube`).
       - **Schnellwahlleiste (`fastlinkparentgroup`):** Lesezeichen-Buttons mit Favicons, `+Lesez.`, Server-Schnellauswahl und Verbinden-Button.
       - **Integrierte Tableiste:** Direkt über dem Web-Viewport mit dynamischen Tab-Titeln, Hervorhebung des aktiven Tabs, Tab-Schließen-Buttons `[x]` und neuem Tab `[+]`.
       - **Segmentierte Statusleiste (`statusgroup`):** Aufgeteilt in Sicherheits-Zone (`[🔒 Sicher]`), vertikale Trennbalken, Statusmeldung / Hover-URLs, Fortschrittsbalken und Hardware-Stream-Status (`25 FPS | 4000 kbps | VA-API HW`).
       - **Menüstruktur:** Entspricht der bekannten Odyssey-Menüführung inklusive Video-Bitrateneinstellungen bis 10.000 kbps.

3. **Multi-Tab-Browsing & Sitzungswiederherstellung:**
   - Echte Tabs über `clicktab.gadget` mit AmigaOS 4 Standard-Bedienung (`Amiga+T` für neuen Tab, `Amiga+W` zum Schließen).
   - **Tab-Persistenz:** Sämtliche geöffneten Tabs, Titel und der aktive Tab werden beim Beenden automatisch in `ENVARC:` gespeichert und beim nächsten Start nahtlos wiederhergestellt.

3. **Download-Manager:**
   - Vollständiger nativer Download-Manager (`Amiga+D`) mit Live-Geschwindigkeitsanzeige, Fortschritt in Prozent, Restzeitschätzung (ETA), Datei- und Ordneröffner.
   - Saubere Umlaut- und Zeichensatz-Behandlung für Dateinamen und Reaction-Buttons.

4. **Passwort-Manager & Auto-Login:**
   - Integrierter Passwort-Tresor (`Amiga+K`) mit starker Blowfish-CBC- und SHA-256-Verschlüsselung.
   - Ein-Klick Auto-Login für die aktuelle Website über `Amiga+L`.
   - Speicherort in `ENVARC:VortexBrowser/passwords.enc`.
   - **Twitch-Unterstützung:** Nahtloses Session-Token-Login (`twitch_token.txt`), um Bot-Schranken auf dem Amiga zu umgehen.

5. **Dateisystem-Stabilität (exFAT / SFS / FFS / RAM):**
   - Integrierter 512 KB Stack-Cookie (`$STACK: 524288` und `__stack = 524288`).
   - Deadlock-freie Konfiguration: Benutzereinstellungen werden atomar in `ENV:` und `ENVARC:` gespeichert, ohne Locks auf Dateisysteme wie `exFATFileSystem` zu halten.

6. **Audio-Streaming:**
   - Synchroner 16-Bit Stereo-Audiostream über separaten Port 9757 direkt an AmigaOS 4 AHI (`ahi.device`).

7. **Radeon GPU-Skalierung & FPS-Tuning:**
   - Hardware-Composite-Skalierung über `IGraphics->CompositeTags()` (z. B. 720p HD oder 540p qHD auf volle Bildschirmgröße) entlastet Amiga-CPUs (G4, SAM440, SAM460, X1000, X5000) um bis zu 70%.
   - Einstellbare Ziel-Framerate direkt in den Client-Einstellungen und im Menü `Ansicht` (15, 25, 30, 50, 60 FPS).

---

## Verzeichnisstruktur des Gesamtpakets

- `amiga-client/`: Quelltext und Assets des AmigaOS 4 Clients
- `cachyos-linux-server/`: High-End Rendering-Server für CachyOS / Arch / Fedora (Ryzen PRO, AVX2, powerprofilesctl)
- `Linux server/`: Standard-Linux-Server (x86_64 / ARM64)
- `rpi4-server/`: Optimierter Server für Raspberry Pi 4 / 5
- `make_release.py`: Erstellt automatisch alle fertigen Release-Archive (`.lha`, `.zip`)

---

## Build & Installation

### 1. Release-Pakete schnüren
```bash
# Kompiliert das SIMD-Modul und baut alle Release-Archive (LHA für Amiga, ZIPs für Linux/Pi/CachyOS)
python3 make_release.py
```

### 2. Server starten (Linux PC, CachyOS oder Raspberry Pi)
```bash
cd cachyos-linux-server   # oder pi-server / rpi4-server
./install_server.sh           # Einmalige automatische Einrichtung
./run_server.sh               # Server starten
```

### 3. Client auf AmigaOS 4.1 starten
1. Archiv `VortexBrowser-0.99-RC3-AmigaOS4.lha` auf dem Amiga entpacken (z. B. nach `Work:Internet/VortexBrowser`).
2. Doppelklick auf das `VortexBrowser`-Icon.
3. Server-IP eintragen (oder `Amiga+1` drücken) und auf **Verbinden** klicken!

