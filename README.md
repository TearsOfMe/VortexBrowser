# VortexBrowse – High-Performance Headless Web Browser Streaming System
**(CachyOS / Raspberry Pi 4 Server $\rightarrow$ AmigaOS 4.1 PowerPC Client)**

VortexBrowse rendert moderne Webseiten über einen Headless Chromium (Playwright) auf dem **CachyOS-Server** (bzw. Raspberry Pi 4) und streamt die Bilddaten asymmetrisch komprimiert per **LZ4** an einen nativen **AmigaOS 4.1 PowerPC Reaction-Client**.

---

## Architektur & Features

1. **Binär-Protokoll (`common/protocol.h`)**
   - Gepackte Big-Endian-Strukturen (`#pragma pack(push, 2)`) für native PowerPC-Verarbeitung ohne Byte-Swapping.
   - Pakettypen: `MSG_FRAME` (`0x01`), `MSG_INPUT` (`0x02`), `MSG_DOWNLOAD_START` (`0x03`), `MSG_DOWNLOAD_RESPONSE` (`0x04`), `MSG_DOWNLOAD_CHUNK` (`0x05`), `MSG_DOWNLOAD_COMPLETE` (`0x06`).

2. **CachyOS / Pi Streaming-Server (`pi-server/`)**
   - **Native SIMD-Beschleunigung (`fast_pixconv.c` / `fast_pixconv.so`):** Konvertiert RGB24-Frames mit `-O3 -march=native` direkt in Big-Endian `ARGB32` (`PIXF_A8R8G8B8`) oder `RGB565` (`PIXF_R5G6B5`).
   - **LZ4-Block-Kompression:** `lz4.block.compress(raw_bytes, store_size=False)` mit Frame-Deduplizierung (MD5-Hash).
   - **Download-Interceptor:** Fängt Browser-Downloads via `page.on("download")` ab, meldet Dateiname und Größe via `MSG_DOWNLOAD_START` an den Amiga und streamt nach Bestätigung (`MSG_DOWNLOAD_RESPONSE`) in **64-KB-Blöcken** (`MSG_DOWNLOAD_CHUNK`).

3. **AmigaOS 4.1 PowerPC Client (`amiga-client/`)**
   - **Reaction GUI (`src/gui.c`):** Native BOOPSI-Oberfläche (`window.class`, `layout.gadget`, `button.gadget`, `string.gadget`, `space.gadget`, `chooser.gadget`, `fuelgauge.gadget`).
   - **Double-Buffering & GPU-Skalierung (`src/renderer.c`):** Zwei RTG-BitMaps (`src_bm[0..1]`) + stufenlose Hardware-Skalierung über `IGraphics->CompositeTags` (`COMPFLAG_SrcFilter | COMPFLAG_IgnoreDestAlpha`).
   - **Favoriten-Verwaltung (`src/favorites.c`):** Lädt und speichert Lesezeichen persistent in `ENVARC:VortexBrowse/favorites.prefs` und `ENV:VortexBrowse/favorites.prefs`.
   - **ASL-Download-Proxy (`src/download.c`):** Öffnet bei eingehenden Downloads einen nativen `asl.library` Save-FileRequester und zeigt den Fortschritt live im `fuelgauge.gadget`.

---

## Build & Start

### 1. Komplett-Build (CachyOS SIMD-Modul + AmigaOS 4 Cross-Compile)
```bash
./build.sh
```

### 2. Server auf CachyOS starten
```bash
./pi-server/run_server.sh --host 0.0.0.0 --port 7788 --width 1280 --height 720 --fps 15
```

### 3. Client auf AmigaOS 4.1 starten
In der AmigaShell (unter QEMU erreicht der Gast den CachyOS-Host unter `10.0.2.2`):
```amigados
SHARED:Download/VortexBrowse/VortexBrowse --host 10.0.2.2 --port 7788
```
