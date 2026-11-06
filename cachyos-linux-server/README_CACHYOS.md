# VortexBrowser Server – CachyOS-Linux Edition

Spezial-Build des VortexBrowser Rendering-Servers für **CachyOS Linux** auf dem **HP EliteDesk 805 G6 Mini PC (AMD Ryzen 5 PRO / Ryzen 3 PRO 4000G Renoir APU mit Radeon Vega Grafik)**.

## Highlights für Full HD (1920x1080) @ 25 FPS

1. **C-Native Zero-Copy Streaming-Pipeline (`encode_jpeg_to_frame_packet` in `fast_pixconv.c`)**:
   - Dekodiert den Chromium-Screencast direkt in C über `libturbojpeg` (`tjDecompressToYUV2` mit AVX2 SIMD) ohne Umweg über RGB24 oder Python-Bytes.
   - Führt die 2D-Dirty-Bounding-Box-Erkennung (`find_and_update_dirty_rect`) mit **256-Bit AVX2-Vektorbefehlen (`_mm256_xor_si256` / `_mm256_testz_si256`)** für alle 3 Ebenen (Y, U, V) in `< 0,15 ms` aus.
   - Packt geänderte Teilbereiche (`CODEC_LZ4_RECT`) für `FMT_YUV420P` und komprimiert direkt in C mit `LZ4_compress_fast(..., accel=3)` in den fertigen Netzwerk-Paketpuffer.
   - **Benchmark auf HP EliteDesk 805 G6**: **2,61 ms pro Full-HD-Frame (entspricht 383 FPS Kapazität!)** – lässt über 37 ms pro Frame Spielraum bei 25 FPS.

2. **AMD Radeon Vega (Renoir) Hardware-Beschleunigung**:
   - Nutzt `/dev/dri/renderD128` mit Vulkan (`vulkan-radeon` / RADV), ANGLE, Zero-Copy-Rasterization und VA-API Hardware-Videodekodierung (`VaapiVideoDecoder`).

3. **Rustwright + DirectCDP Browser-Automatisierung (Zero Node.js Overhead)**:
   - Verwendet **`rustwright`** (nativer Rust-PyO3-Treiber auf Tokio-Basis) anstelle des Node.js-Playwright-Subprozesses (Startzeit `< 1 ms`, kein Node.js-IPC-Flaschenhals).
   - Verbindet sich für den `Page.screencastFrame`-Videostream über **`DirectCDPSession`** direkt per WebSocket (`ws://127.0.0.1:9222/devtools/page/<targetId>`) mit der Chromium-Engine.

4. **Automatische CPU-Skalierung via `powerprofilesctl` (`amd_pstate`)**:
   - Schaltet das System-Powerprofil der AMD Ryzen PRO APU bei Verbindung des AmigaOS 4 Clients automatisch via `powerprofilesctl set performance` auf maximale Leistung und beim Trennen wieder zurück auf `power-saver` (`powerprofilesctl set power-saver`).

## Installation & Start auf CachyOS (192.168.178.2)

```bash
cd Server-CachyOS-Linux
./install_server.sh
./run_server.sh
```

Der Server lauscht automatisch auf **Port 7788** (und **Port 9757**) mit einer Standardauflösung von **1920x1080 @ 25 FPS**.
