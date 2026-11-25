# VortexBrowser Companion Server — Native Windows 10 / 11 (x64)

Nativer Windows-10/11-Server für **VortexBrowser (AmigaOS 4.1)** — ohne VM, ohne WSL2 und ohne Docker.

## Highlights
- **Direkte LAN-Verbindung:** Port `7788` (primär) & `9757` (LAN) ohne Port-Forwarding-Umwege.
- **Native SIMD-Grafikpipeline (ohne Compiler):** Vorkompilierte, eigenständige `fast_pixconv_avx2.dll` (AVX2+FMA) und `fast_pixconv.dll` (universell x86-64 SSE2) inkl. statisch gelinktem `libjpeg-turbo` & `liblz4` — automatische CPU-Erkennung beim Start.
- **GPU-H.264-Hardware-Encoding:** Automatische Erkennung von **NVIDIA NVENC** (`h264_nvenc`), **AMD AMF** (`h264_amf`), **Intel QuickSync** (`h264_qsv`), **Windows Media Foundation** (`h264_mf`) oder `libx264`-Fallback.
- **Chromium mit D3D11-Hardwarebeschleunigung:** Nutzt nativ installiertes Ungoogled-Chromium / Chrome / Edge (oder Playwright Chromium) unsichtbar im Hintergrund (`--headless=new`) inkl. uBlock Origin & h264ify.
- **Audio-Streaming zum Amiga (AHI 44.1 kHz Stereo):**
  - **Standard (ohne virtuellen Treiber):** Native Windows-**WASAPI-Loopback**-Aufnahme via `PyAudioWPatch`.
  - **Optional (Browser-Audio isoliert):** [VB-Cable](https://vb-audio.com/Cable/) installieren und Chromium in den Windows-Sound-Einstellungen (App-Lautstärke und Geräteeinstellungen) auf *CABLE Input* legen — `server.py` erkennt *CABLE Output (VB-Audio Virtual Cable)* automatisch.

---

## Schnellstart (Windows 10 / 11)

1. Entpacke `VortexBrowser-1.0-Server-Windows.zip` in einen Ordner (z. B. `C:\VortexBrowser-Server\`).
2. Öffne **PowerShell** im Server-Ordner (für automatische Firewall-Regel idealerweise **als Administrator**):
   ```powershell
   powershell -ExecutionPolicy Bypass -File .\install_windows.ps1
   ```
   Das Skript richtet automatisch ein:
   - Python 3.10+ (bei Bedarf per `winget`) und `.venv` mit `rustwright`, `playwright`, `websockets`, `lz4`, `Pillow`, `PyAudioWPatch`
   - `ffmpeg` (bei Bedarf per `winget`)
   - Browser-Prüfung (Chrome/Chromium/Edge oder automatischer Download von Playwright Chromium)
   - Prüfung der nativen `fast_pixconv*.dll`
   - Windows Defender Firewall-Freigabe für TCP `7788` & `9757` (Privates Netzwerk)
3. Starte den Server per Doppelklick auf **`run_windows.bat`** (oder in PowerShell: `.\run_windows.bat`).
4. Gib die angezeigte **Windows-LAN-IP** im AmigaOS-4-Client ein (Amiga+E / Einstellungen → Server-IP).

---

## Nützliche Optionen & Umgebungsvariablen

Du kannst `run_windows.bat` direkt mit Argumenten aufrufen oder Umgebungsvariablen setzen:

| Option / Variable | Beschreibung |
|---|---|
| `--no-audio` / `VORTEX_NO_AUDIO=1` | Audio-Streaming deaktivieren |
| `--audio-device "CABLE Output (VB-Audio Virtual Cable)"` / `VORTEX_AUDIO_DEVICE=...` | Bestimmtes Windows-DirectShow-Gerät erzwingen (oder `wasapi` für WASAPI-Loopback) |
| `--fps 30` / `VORTEX_FPS=30` | Ziel-Framerate (Standard: `30`) |
| `--width 1920 --height 1080` | Startauflösung (passt sich automatisch 1:1 an das Amiga-Fenster an) |
| `--browser-bin "C:\...\chrome.exe"` | Bestimmtes Chromium/Chrome/Edge-Binary erzwingen |
| `--disable-gpu` | Software-Rasterisierung erzwingen (bei Treiberproblemen) |
| `VORTEX_SHOW_BROWSER=1` | Chromium-Fenster sichtbar anzeigen (nur für Debugging) |
| `VORTEX_PROFILE_DIR=D:\VortexProfile` | Eigenes Profil-/Cookie-Verzeichnis (Standard: `%APPDATA%\VortexBrowser`) |

---

## Fehlerbehebung

- **Amiga kann sich nicht verbinden:**
  Prüfe, ob dein Windows-Netzwerkprofil auf **Privat** steht und die Firewall-Regel für TCP `7788` & `9757` aktiv ist (als Administrator in PowerShell):
  ```powershell
  New-NetFirewallRule -DisplayName "VortexBrowser Amiga Companion Server (TCP 7788,9757)" -Direction Inbound -Protocol TCP -LocalPort 7788,9757 -Action Allow -Profile Private
  ```
- **Kein Ton am Amiga:**
  Prüfe beim Serverstart die Log-Zeile `Audio: ON (Windows WASAPI/DirectShow -> AHI)`. Bei Windows-WASAPI-Loopback muss ein aktives Ausgabegerät (Lautsprecher/Kopfhörer/HDMI) in Windows vorhanden und nicht stummgeschaltet sein. Für völlig isolierten Browser-Ton ohne sonstige Windows-Töne empfiehlt sich VB-Cable.
