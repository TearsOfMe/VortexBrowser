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
       - **Navigationsleiste (`navigationgroup`):** Schnellzugriff auf `<` (Zurück), `>` (Vorwärts), `Neu`, `Stopp`, `Start`, `Downloads`, `Passwörter`, flexibler Abstandhalter und animierter Lade-Throbber.
       - **Schnellwahlleiste (`fastlinkparentgroup`):** Lesezeichen-Buttons mit Favicons, flexibler Abstandhalter, Server-Schnellauswahl und Verbinden-Button.
       - **Adress- & Suchzeile (`urlgroup`):** `+ Tab` Button, Sicherheits-Indikator (`[🔒 https]` / `[🌐 http]`), URL-Eingabezeile, `[X]`, `[Los]`, Trenner, `+Lesez.`, Lesezeichen-Dropdown, interaktiver `BalanceObject`-Teiler und Suchleiste mit Suchmaschinen-Wahl (`DuckDuckGo`, `Google`, `Wikipedia`, `Aminet`, `OS4Depot`, `YouTube`).
       - **Integrierte Tableiste & Web-Viewport (`pagegroup`):** Nahtlos im nativen Register-Rahmen (`MUIV_Frame_Register` mit `MUII_RegisterBack`) gebündelt – mit dynamischen Tab-Titeln, optisch verschmolzenem aktivem Tab, Tab-Schließen-Buttons `[x]`, neuem Tab `[+]` und direkt angeschlossenem Web-Viewport.
       - **Segmentierte Statusleiste (`statusgroup`):** Aufgeteilt in Sicherheits-Zone (`[🔒 Sicher]`), vertikale Trennbalken, Statusmeldung / Hover-URLs, Fortschrittsbalken und Hardware-Stream-Status (`Stream: Auto [H.264 VA-API HW] | 25 FPS | 8000 kbps`).
       - **Menüstruktur:** Entspricht der bekannten Odyssey-Menüführung inklusive Video-Bitrateneinstellungen (Standard: 8000 kbps, bis 10.000 kbps).
       - **Lautloser Start:** Keine Shell-/Konsolen-Ausgaben beim Programmstart; das aktive Streaming-System wird übersichtlich direkt in der Statusleiste angezeigt.

3. **Multi-Tab-Browsing & Sitzungswiederherstellung:**
   - Echte Tabs über `clicktab.gadget` mit AmigaOS 4 Standard-Bedienung (`Amiga+T` für neuen Tab, `Amiga+W` zum Schließen).
   - **Tab-Persistenz:** Sämtliche geöffneten Tabs, Titel und der aktive Tab werden beim Beenden automatisch in `ENVARC:` gespeichert und beim nächsten Start nahtlos wiederhergestellt.

3. **Download-Manager:**
   - Vollständiger nativer Download-Manager (`Amiga+D`) mit Live-Geschwindigkeitsanzeige, Fortschritt in Prozent, Restzeitschätzung (ETA), Datei- und Ordneröffner.
   - Saubere Umlaut- und Zeichensatz-Behandlung für Dateinamen und Reaction-Buttons.

4. **Passwort-Manager:**
   - Integrierter Passwort-Tresor (`Amiga+K`) mit starker Blowfish-CBC- und SHA-256-Verschlüsselung.
   - Benutzername und Passwort bequem per Knopfdruck ins Amiga-Clipboard kopieren.
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

## Workbench Icon-ToolTypes & Konfiguration

VortexBrowser unterstützt die vollständige Konfiguration über standardmäßige AmigaOS 4 **ToolTypes** in den `.info`-Dateien (`VortexBrowser.info`, `VortexBrowser-AltiVec.info`, `VortexBrowser-MUI.info`).

Um die ToolTypes zu bearbeiten:
1. Klicken Sie das Programm-Icon auf der Workbench an.
2. Wählen Sie im Workbench-Menü **Icons $\rightarrow$ Information...** (oder Tastenkombination **`Rechte Amiga + I`**).
3. Aktivieren Sie die gewünschten ToolTypes durch Entfernen der runden Klammern `(...)` und tragen Sie Ihre Werte ein.

### Unterstützte ToolTypes:

| ToolType | Standard | Beschreibung |
| :--- | :--- | :--- |
| `SERVER=<IP/Host>` | `(SERVER=192.168.178.2)` | IP-Adresse oder Rechnername des Rendering-Servers (z. B. `SERVER=192.168.178.2`). |
| `PORT=<Port>` | `(PORT=7788)` | TCP-Port des Rendering-Servers (Standard: `7788`). |
| `URL=<URL>` | `(URL=https://www.amiga-news.de)` | Startseite beim Starten über das Icon (`STARTURL` wird ebenfalls erkannt). |
| `STREAMMODE=<Modus>` | `(STREAMMODE=AUTO)` | Streaming-System: `AUTO` (H.264 bei Video, sonst 2D), `H264` (dauerhaftes Video) oder `LZ4` / `DIRTYRECT` (reines 2D Dirty-Rect). |
| `BITRATE=<kbps>` | `(BITRATE=8000)` | Video-Bitrate für H.264 in kbps (Wertebereich: `1000` bis `10000`). |
| `FPS=<Zahl>` | `(FPS=25)` | Ziel-Bildwiederholrate (z. B. `15`, `25`, `30`, `50`, `60`). |
| `DARKMODE=<0\|1>` | `(DARKMODE=0)` | Dunkles Erscheinungsbild beim Start erzwingen (`1` für Dark Mode, `0` für Hell). |
| `DOWNLOADDIR=<Pfad>` | `(DOWNLOADDIR=RAM:)` | Standard-Download-Verzeichnis (z. B. `DOWNLOADDIR=Work:Downloads`). |

> [!NOTE]
> **AmigaOS-Klammerkonvention:**
> Einträge in runden Klammern wie `(SERVER=...)` gelten in AmigaOS als **inaktiv / Vorlage**. Um einen ToolType scharfzuschalten, entfernen Sie die Klammern (z. B. `SERVER=192.168.178.2`) und klicken Sie auf **Speichern**.
> Der im Icon hinterlegte **Stack** beträgt **200000** Byte (~200 KB) und stellt einen stabilen Betrieb von Netzwerk, FFmpeg/VA-API und MUI/ReAction sicher.

---

## Verzeichnisstruktur des Gesamtpakets

- `amiga-client/`: Quelltext, Icons und Assets des AmigaOS 4 Clients
- `server/`: Universeller Linux Rendering-Server mit Hardware-Autodetect (Intel QuickSync/VA-API, AMD Vulkan/VA-API, Raspberry Pi V3D/EGL, AVX2/SSE4/NEON SIMD)
- `make_release.py`: Erstellt automatisch alle fertigen Release-Archive (`.lha`, `.zip`)

---

## Build & Installation

### 1. Release-Pakete schnüren
```bash
# Kompiliert das SIMD-Modul und baut alle Release-Archive (LHA für Amiga, ZIP für Server und Komplettpaket)
python3 make_release.py
```

### 2. Server starten (Linux PC, CachyOS, Intel oder Raspberry Pi)
```bash
cd server
./install_server.sh           # Einmalige automatische Einrichtung (erkennt Hardware & installiert Treiber)
./run_server.sh               # Server starten (wählt Auflösung & Beschleunigung optimal)
```

### 3. Client auf AmigaOS 4.1 starten
1. Archiv `VortexBrowser-0.99-RC3-AmigaOS4.lha` auf dem Amiga entpacken (z. B. nach `Work:Internet/VortexBrowser`).
2. Doppelklick auf das `VortexBrowser`-Icon.
3. Server-IP eintragen (oder `Amiga+1` drücken) und auf **Verbinden** klicken!

