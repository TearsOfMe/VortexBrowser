# VortexBrowser Companion Server — Native Windows 10 / 11 (x64)

Nativer Windows-10/11-Server für **VortexBrowser (AmigaOS 4.1)** — ohne VM, ohne WSL2 und ohne Docker.  
Native Windows 10/11 server for **VortexBrowser (AmigaOS 4.1)** — no VM, no WSL2, and no Docker required.

## Highlights
- **Direkte LAN-Verbindung:** Port `7788` (primär) & `9757` (LAN) ohne Port-Forwarding-Umwege.
- **Unsichtbarer Hintergrund- & System-Tray-Modus (ohne Konsolenfenster):**
  - Doppelklick auf **`run_windows_hidden.vbs`** startet den Server komplett ohne Shell-/CMD-Fenster im Hintergrund mit einem kompakten **System-Tray-Symbol** (zeigt LAN-IP + Port, Rechtsklick-Menü: *Open Server Log*, *Restart Server*, *Stop & Exit*).
  - Mit `wscript run_windows_hidden.vbs --no-tray` läuft der Server sogar vollständig unsichtbar ohne Tray-Icon (Stoppen per **`stop_windows.bat`**).
  - Optionaler Windows-Autostart über `.\install_windows.ps1 -InstallAutostart`.
- **Stummer Windows-Host — Ton NUR auf dem Amiga (AHI 44.1 kHz Stereo):**
  - **Standard (kein virtueller Treiber nötig):** Eingebaute **Silent Browser TabCapture Audio Bridge** (MV3 `chrome.tabCapture` + Offscreen-Document). Chromium leitet den Tab-Ton direkt als 44.1 kHz Stereo-PCM an den Amiga weiter und **schaltet die Windows-Lautsprecher für den Browser automatisch komplett stumm** — kein doppelter Ton mehr bei YouTube, Twitch & Co.!
  - **Automatischer Fallback:** Falls ein gebrandeter Browser `--load-extension` blockiert oder `--audio-device wasapi` / VB-Cable explizit gewählt wird, greift der Server auf Windows-WASAPI-Loopback (`PyAudioWPatch`) bzw. DirectShow zurück.
- **Native SIMD-Grafikpipeline (ohne Compiler):** Vorkompilierte, eigenständige `fast_pixconv_avx2.dll` (AVX2+FMA) und `fast_pixconv.dll` (universell x86-64 SSE2) inkl. statisch gelinktem `libjpeg-turbo` & `liblz4` — automatische CPU-Erkennung beim Start.
- **GPU-H.264-Hardware-Encoding:** Automatische Erkennung von **NVIDIA NVENC** (`h264_nvenc`), **AMD AMF** (`h264_amf`), **Intel QuickSync** (`h264_qsv`), **Windows Media Foundation** (`h264_mf`) oder `libx264`-Fallback.
- **Chromium mit D3D11-Hardwarebeschleunigung:** Nutzt Playwright Chromium (*Chrome for Testing*) oder Ungoogled-Chromium unsichtbar im Hintergrund (`--headless=new`) inkl. Silent-Audio-Bridge, uBlock Origin & h264ify.

---

## Schnellstart / Quick Start (Windows 10 / 11)

1. Entpacke `VortexBrowser-1.1-Server-Windows.zip` in einen Ordner (z. B. `C:\VortexBrowser-Server\`).
2. Öffne **PowerShell** im Server-Ordner (für automatische Firewall-Regel idealerweise **als Administrator**):
   ```powershell
   powershell -ExecutionPolicy Bypass -File .\install_windows.ps1
   ```
   *(Optional mit Windows-Autostart / Optional with Windows Startup autostart:)*
   ```powershell
   powershell -ExecutionPolicy Bypass -File .\install_windows.ps1 -InstallAutostart
   ```
   Das Skript richtet automatisch ein:
   - Python 3.10+ (bei Bedarf per `winget`) und `.venv` mit `rustwright`, `playwright`, `websockets`, `lz4`, `Pillow`, `PyAudioWPatch`
   - `ffmpeg` (bei Bedarf per `winget`)
   - **Playwright Chromium** (*Chrome for Testing*, garantiert volle Extension-Unterstützung für stummes Windows-Audio & uBlock Origin)
   - Prüfung der nativen `fast_pixconv*.dll`
   - Windows Defender Firewall-Freigabe für TCP `7788` & `9757` (Privates Netzwerk)
3. **Server starten:**
   - **Unsichtbar im Hintergrund + System-Tray (empfohlen):** Doppelklick auf **`run_windows_hidden.vbs`**
   - **Mit Konsolenfenster (für Live-Logs):** Doppelklick auf **`run_windows.bat`**
   - **Hintergrund-Server sauber beenden:** Rechtsklick auf das Tray-Icon → **Stop & Exit** oder Doppelklick auf **`stop_windows.bat`**
4. Gib die angezeigte **Windows-LAN-IP** im AmigaOS-4-Client ein (Amiga+E / Einstellungen → Server-IP).

---

## Nützliche Optionen & Umgebungsvariablen

Du kannst `run_windows.bat` oder `run_windows_hidden.vbs` direkt mit Argumenten aufrufen oder Umgebungsvariablen setzen:

| Option / Variable | Beschreibung |
|---|---|
| `run_windows_hidden.vbs` | Unsichtbarer Hintergrundstart mit System-Tray-Icon (Logs unter `%APPDATA%\VortexBrowser\server.log`) |
| `wscript run_windows_hidden.vbs --no-tray` | Vollständig unsichtbarer Hintergrundstart ohne Tray-Icon (beenden via `stop_windows.bat`) |
| `--no-audio` / `VORTEX_NO_AUDIO=1` | Audio-Streaming deaktivieren |
| `--audio-device tabcapture` | Standard unter Windows: Stummes Browser-TabCapture (Ton nur am Amiga, Windows-Lautsprecher stumm) |
| `--audio-device wasapi` / `--audio-device "CABLE Output (VB-Audio Virtual Cable)"` | Erzwingt Windows-WASAPI-Loopback oder ein bestimmtes DirectShow-Gerät |
| `--fps 30` / `VORTEX_FPS=30` | Ziel-Framerate (Standard: `30`) |
| `--width 1920 --height 1080` | Startauflösung (passt sich automatisch 1:1 an das Amiga-Fenster an) |
| `--browser-bin "C:\...\chrome.exe"` | Bestimmtes Chromium/Chrome/Edge-Binary erzwingen |
| `--disable-gpu` | Software-Rasterisierung erzwingen (bei Treiberproblemen) |
| `VORTEX_SHOW_BROWSER=1` | Chromium-Fenster sichtbar anzeigen (nur für Debugging) |
| `VORTEX_PROFILE_DIR=D:\VortexProfile` | Eigenes Profil-/Cookie-Verzeichnis (Standard: `%APPDATA%\VortexBrowser`) |

---

## Fehlerbehebung / Troubleshooting

- **Amiga kann sich nicht verbinden:**
  Prüfe, ob dein Windows-Netzwerkprofil auf **Privat** steht und die Firewall-Regel für TCP `7788` & `9757` aktiv ist (als Administrator in PowerShell):
  ```powershell
  New-NetFirewallRule -DisplayName "VortexBrowser Amiga Companion Server (TCP 7788,9757)" -Direction Inbound -Protocol TCP -LocalPort 7788,9757 -Action Allow -Profile Private
  ```
- **Ton läuft noch aus den Windows-Lautsprechern (doppelter Ton):**
  Prüfe `%APPDATA%\VortexBrowser\server.log`. Dort sollte `Audio: ON (Silent Browser TabCapture -> AHI; Host Speakers Muted)` sowie `SilentTabAudioBridge: MV3 TabCapture extension connected` stehen. Falls dein System zuvor gebrandetes Google Chrome 137+ genutzt hat (welches `--load-extension` sperrt), führe einmalig `.\install_windows.ps1 -InstallPlaywrightChromium` aus — der Server bevorzugt Playwright Chromium danach automatisch.
- **Server-Logs im Hintergrundmodus ansehen:**
  Rechtsklick auf das VortexBrowser-Tray-Icon → **Open Server Log** (öffnet `%APPDATA%\VortexBrowser\server.log`).
