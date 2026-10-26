FROM debian:bookworm-slim

# Prevent interactive debconf prompts
ENV DEBIAN_FRONTEND=noninteractive \
    PYTHONUNBUFFERED=1 \
    PULSE_SERVER=unix:/tmp/xdg_runtime/pulse/native

# Install system dependencies, Chromium, PulseAudio, audio utils and fonts
RUN apt-get update && apt-get install -y --no-install-recommends \
    python3 \
    python3-pip \
    python3-venv \
    python3-dev \
    gcc \
    make \
    liblz4-dev \
    chromium \
    mesa-vulkan-drivers \
    libgl1-mesa-dri \
    pulseaudio \
    pulseaudio-utils \
    fonts-dejavu-core \
    fonts-freefont-ttf \
    procps \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Create a non-root application user for PulseAudio and Chromium with GPU access
RUN useradd -m -s /bin/bash -u 1000 vortex && \
    usermod -aG audio,video,render vortex

WORKDIR /app

# Setup Python virtual environment
RUN python3 -m venv /app/.venv
ENV PATH="/app/.venv/bin:$PATH"

# Install Python requirements
COPY requirements.txt /app/
RUN pip install --no-cache-dir --upgrade pip setuptools wheel && \
    pip install --no-cache-dir -r requirements.txt

# Copy application sources and extensions
COPY fast_pixconv.c server.py entrypoint.sh /app/
COPY extensions/ /app/extensions/

# Compile native fast_pixconv SIMD converter
RUN gcc -O3 -shared -fPIC -o fast_pixconv.so fast_pixconv.c && \
    gcc -O3 -shared -fPIC -o fast_pixconv_aarch64.so fast_pixconv.c 2>/dev/null || true

# Set execution rights and ownership
RUN chmod +x /app/entrypoint.sh /app/server.py && \
    chown -R vortex:vortex /app

USER vortex

# Ports: 7788 (Raspberry Pi default) and 9757 (Host PC default)
EXPOSE 7788 9757

ENTRYPOINT ["/app/entrypoint.sh"]
CMD ["--host", "0.0.0.0", "--port", "7788", "--fps", "25", "--format", "rgb565"]
