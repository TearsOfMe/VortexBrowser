# VortexBrowser Server - Intel CPU & GPU Edition
**Optimiert für Intel Atom x5-Z8350, Celeron, Pentium und Intel Core Mini-PCs**

Dieses Server-Paket ist speziell auf x86-Systeme mit **Intel HD / UHD Graphics** und stromsparende Quad-Core-Prozessoren abgestimmt (z. B. Intel Compute Sticks, Mini-PCs, Beelink, Minisforum, Laptops oder ältere Office-PCs).

---

## 🚀 Warum diese optimierte Intel-Version?

1. **Intel VA-API Hardware-Beschleunigung (`/dev/dri/renderD128`):**
   * Webseiten-Layouts, Canvas und CSS-Animationen werden über die Intel-GPU hardwarebeschleunigt (EGL).
   * Web-Videos (z. B. YouTube) werden direkt über den integrierten Hardware-Videodekoder (H.264/AVC) abgespielt, statt die CPU zu überlasten.
2. **Speziell für Intel Atom x5-Z8350 (Cherry Trail / Braswell):**
   * **Intelligente Treiber-Wahl:** Für die Gen8-Grafik des Atom x5 wählt das Setup automatisch den bewährten, stabilen `i965` VA-API Treiber. Auf neueren Core-CPUs (Gen9+) wird der `iHD` Treiber genutzt.
   * **Low-Power 2-Thread Rasterung:** Auf Quad-Core CPUs ohne HyperThreading (wie dem Atom x5 mit 4 Kernen) reserviert der Server 2 Kerne für den Browser und hält 2 Kerne für das Stream-Encoding und die Netzwerkübertragung frei. Dadurch bleibt der Stream absolut flüssig und ruckelfrei.
   * **h264ify-Erweiterung:** Zwingt Videoplattformen wie YouTube zur Ausgabe im H.264-Format, welches die Intel HD 400 Grafik vollständig in Hardware dekodieren kann (statt ressourcenfressendem VP9).
3. **Native x86 SIMD-Beschleunigung (SSE2 / SSSE3 / SSE4.2 / AVX2):**
   * Die C-Pipeline (`fast_pixconv.so`) nutzt 128-Bit SSE-Vektorbefehle für Dirty-Rect-Erkennung und LZ4-Kompression, selbst wenn die CPU kein AVX2 unterstützt.

---

## 📦 Systemanforderungen

* **Hardware:**
  * Intel Atom x5-Z8350, x5-Z8300, x7-Z8700
  * oder Intel Celeron / Pentium (J1900, N3450, N4000, N5105, N100, etc.)
  * oder Intel Core i3 / i5 / i7 / i9 (jede Generation)
  * Mindestens 2 GB RAM (4 GB empfohlen)
* **Betriebssystem:**
  * Linux 64-Bit (Debian 12, Ubuntu 22.04/24.04, Linux Mint, DietPi x86, Arch Linux, CachyOS, etc.)

---

## 🛠️ Installation (1-Klick Setup)

1. Kopieren Sie den Ordner `Server-Intel/` auf Ihren Intel-PC.
2. Öffnen Sie ein Terminal im Ordner und führen Sie das Setup-Skript aus:
   ```bash
   chmod +x install_intel.sh run_intel.sh
   ./install_intel.sh
   ```
3. Das Skript prüft Ihre CPU, installiert automatisch die passenden Intel VA-API Treiber (`libva-intel-driver` / `intel-media-va-driver`), richtet Python ein und kompiliert die SIMD-Bibliothek.
4. Nach Abschluss wird Ihnen die IP-Adresse des Servers angezeigt.

---

## ▶️ Server starten

### Manuell im Terminal:
```bash
./run_intel.sh
```

### Als automatischer Hintergrunddienst (Systemd):
Um den Server bei jedem Systemstart automatisch im Hintergrund zu starten:
```bash
sudo cp vortexbrowser.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now vortexbrowser.service
```

Status prüfen:
```bash
sudo systemctl status vortexbrowser.service
```

---

## 🖥️ Empfohlene Einstellungen im AmigaOS 4 Client

Für die beste Performance auf einem **Intel Atom x5-Z8350**:

* **Fenstergröße / Auflösung:** `1024x768` (Standard) oder `1280x720` (720p HD)
* **Bildrate (FPS):** `15 FPS` oder `20 FPS`
* **Stream-Modus / Codec:** `Auto` oder `LZ4`
* **Amiga Grafikkarte:** Wenn Sie eine Radeon HD oder Radeon RX besitzen, aktivieren Sie in den VortexBrowser-Einstellungen auf dem Amiga das **Hardware-Scaling (Composite)**. So können Sie 720p verlustfrei auf die volle Monitorgröße (z. B. 1080p) skalieren lassen.

---

## 🔍 Diagnose & VA-API Überprüfung

Um zu prüfen, ob die Intel Hardware-Beschleunigung aktiv ist, können Sie folgenden Befehl im Terminal ausführen:
```bash
vainfo
```
bzw. für den Atom x5 Treiber:
```bash
LIBVA_DRIVER_NAME=i965 vainfo
```
Sie sollten eine Liste mit unterstützten Profilen sehen, darunter `VAProfileH264High`, `VAProfileH264Main`, etc.
