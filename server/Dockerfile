# ==============================================================================
# VortexBrowser Universal Companion Server - Multi-Arch Dockerfile
# Supports:
#   - linux/amd64 (Linux Desktop/Server: Intel QuickSync/VA-API, AMD Vulkan/VA-API)
#   - linux/arm64 (Raspberry Pi 4 / Raspberry Pi 5 64-bit OS: Broadcom V3D + NEON)
# ==============================================================================
FROM python:3.11-slim-bookworm

ARG TARGETARCH
ENV DEBIAN_FRONTEND=noninteractive \
    PYTHONUNBUFFERED=1 \
    OMP_NUM_THREADS=4

# 1. Install system dependencies, Chromium, FFmpeg, PulseAudio, TurboJPEG, LZ4 & GPU drivers
RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc \
    g++ \
    libc6-dev \
    libturbojpeg0-dev \
    liblz4-dev \
    chromium \
    ffmpeg \
    pulseaudio \
    pulseaudio-utils \
    dbus \
    dbus-x11 \
    procps \
    ca-certificates \
    fonts-liberation \
    fonts-dejavu-core \
    mesa-va-drivers \
    mesa-vulkan-drivers \
    libgl1-mesa-dri \
    libgles2 \
    libegl1 \
    && if [ "$TARGETARCH" = "amd64" ]; then \
        apt-get install -y --no-install-recommends \
            intel-media-va-driver \
            i965-va-driver \
            va-driver-all || true; \
    fi \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# 2. Install Python dependencies
COPY requirements.txt /app/requirements.txt
RUN pip install --no-cache-dir --upgrade pip && \
    pip install --no-cache-dir -r /app/requirements.txt

# 3. Copy server files
COPY server.py darkreader.js fast_pixconv.c sync_twitch_login.py sync_twitch_login.sh /app/
COPY extensions /app/extensions
COPY docker-entrypoint.sh /app/docker-entrypoint.sh
RUN chmod +x /app/docker-entrypoint.sh /app/sync_twitch_login.sh

# 4. Compile native C-SIMD pixel pipeline (SSE4.1 on amd64, ARMv8 NEON on arm64)
RUN if [ "$TARGETARCH" = "amd64" ]; then \
        gcc -O3 -msse4.1 -fopenmp -DHAVE_TURBOJPEG_LZ4 -shared -fPIC \
            /app/fast_pixconv.c -lturbojpeg -llz4 -o /app/fast_pixconv.so || \
        gcc -O3 -msse4.1 -DHAVE_TURBOJPEG_LZ4 -shared -fPIC \
            /app/fast_pixconv.c -lturbojpeg -llz4 -o /app/fast_pixconv.so; \
    else \
        gcc -O3 -march=armv8-a+simd -fopenmp -DHAVE_TURBOJPEG_LZ4 -shared -fPIC \
            /app/fast_pixconv.c -lturbojpeg -llz4 -o /app/fast_pixconv.so || \
        gcc -O3 -DHAVE_TURBOJPEG_LZ4 -shared -fPIC \
            /app/fast_pixconv.c -lturbojpeg -llz4 -o /app/fast_pixconv.so; \
    fi

# 7788: Main Video/Control Stream | 9757: AHI Stereo Audio Stream
EXPOSE 7788 9757

ENTRYPOINT ["/app/docker-entrypoint.sh"]
