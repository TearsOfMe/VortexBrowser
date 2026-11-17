#!/usr/bin/env bash
# ==============================================================================
# VortexBrowser Raspberry Pi 3 / 4 / 5 (ARM64) Complete SD-Card Image Builder
# Builds ready-to-flash .img.xz images based on Raspberry Pi OS Lite (arm64)
# with Ungoogled-Chromium (default), Firefox (Playwright + ESR alternative),
# target-tuned ARM64 fast_pixconv.so, PulseAudio, ZRAM (Pi 3/4), and
# auto-starting systemd service.
#
# Usage:
#   ./build_rpi_image.sh [rpi3|rpi4|rpi5|all] [out_dir]
# ==============================================================================
set -euo pipefail

TARGET="${1:-all}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUT_DIR="${2:-$REPO_ROOT/dist}"
mkdir -p "$OUT_DIR"

case "$TARGET" in
    rpi3|rpi4|rpi5) TARGETS=("$TARGET") ;;
    all)            TARGETS=("rpi3" "rpi4" "rpi5") ;;
    *)
        echo "Usage: $0 [rpi3|rpi4|rpi5|all] [out_dir]" >&2
        exit 1
        ;;
esac

RASPIOS_URL="${RASPIOS_URL:-https://downloads.raspberrypi.com/raspios_lite_arm64/images/raspios_lite_arm64-2026-10-06/2026-10-06-raspios-trixie-arm64-lite.img.xz}"
UNGOOGLED_URL="${UNGOOGLED_URL:-https://github.com/ungoogled-software/ungoogled-chromium-portablelinux/releases/download/156.0.8078.79-1/ungoogled-chromium-156.0.8078.79-1-arm64_linux.tar.xz}"
DOCKER_VOL="${DOCKER_VOL:-vortex_rpi_build}"

docker volume create "$DOCKER_VOL" >/dev/null

# Step 1: Build shared configured ARM64 base image (unless already cached in volume)
if [ "${REBUILD_BASE:-0}" = "1" ] || ! docker run --rm -v "$DOCKER_VOL:/work" debian:trixie-slim test -f /work/base_vortex.img; then
    echo "[+] Building shared ARM64 VortexBrowser base image..."
    docker run -i --rm --privileged --platform linux/arm64 \
        -v "$DOCKER_VOL:/work" \
        -v "$SCRIPT_DIR:/src_server:ro" \
        -e RASPIOS_URL="$RASPIOS_URL" \
        -e UNGOOGLED_URL="$UNGOOGLED_URL" \
        debian:trixie-slim bash -s << 'BASE_EOF'
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive

apt-get update -qq
apt-get install -y -qq curl xz-utils parted dosfstools e2fsprogs util-linux kmod openssl >/dev/null

if [ ! -f /work/raspios.img.xz ]; then
    curl -fSL -o /work/raspios.img.xz "$RASPIOS_URL"
fi
if [ ! -f /work/ungoogled.tar.xz ]; then
    curl -fSL -o /work/ungoogled.tar.xz "$UNGOOGLED_URL"
fi

rm -f /work/base_vortex.img.tmp
xz -dkc /work/raspios.img.xz > /work/base_vortex.img.tmp
truncate -s +5600M /work/base_vortex.img.tmp
parted -s /work/base_vortex.img.tmp resizepart 2 100%

read -r P1_START P1_SIZE < <(parted -m -s /work/base_vortex.img.tmp unit B print | awk -F: '$1=="1"{gsub(/B/,""); print $2, $4}')
read -r P2_START P2_SIZE < <(parted -m -s /work/base_vortex.img.tmp unit B print | awk -F: '$1=="2"{gsub(/B/,""); print $2, $4}')

LOOP_BOOT=$(losetup -f --show -o "$P1_START" --sizelimit "$P1_SIZE" /work/base_vortex.img.tmp)
LOOP_ROOT=$(losetup -f --show -o "$P2_START" --sizelimit "$P2_SIZE" /work/base_vortex.img.tmp)
cleanup() {
    set +e
    umount -lf /mnt/rpi/boot/firmware 2>/dev/null || true
    umount -lf /mnt/rpi/dev/pts 2>/dev/null || true
    umount -lf /mnt/rpi/dev 2>/dev/null || true
    umount -lf /mnt/rpi/proc 2>/dev/null || true
    umount -lf /mnt/rpi/sys 2>/dev/null || true
    umount -lf /mnt/rpi 2>/dev/null || true
    losetup -d "$LOOP_BOOT" 2>/dev/null || true
    losetup -d "$LOOP_ROOT" 2>/dev/null || true
}
trap cleanup EXIT

e2fsck -f -p "$LOOP_ROOT" || e2fsck -f -y "$LOOP_ROOT" || [ $? -le 2 ]
resize2fs "$LOOP_ROOT"

mkdir -p /mnt/rpi
mount "$LOOP_ROOT" /mnt/rpi
mkdir -p /mnt/rpi/boot/firmware
mount "$LOOP_BOOT" /mnt/rpi/boot/firmware
mount --bind /proc /mnt/rpi/proc
mount --bind /sys /mnt/rpi/sys
mount --bind /dev /mnt/rpi/dev
mount --bind /dev/pts /mnt/rpi/dev/pts

cp -L /etc/resolv.conf /mnt/rpi/etc/resolv.conf.vortexbak 2>/dev/null || true
cp -L /etc/resolv.conf /mnt/rpi/etc/resolv.conf

touch /mnt/rpi/boot/firmware/ssh
PASS_HASH=$(openssl passwd -6 "vortex")
echo "pi:${PASS_HASH}" > /mnt/rpi/boot/firmware/userconf.txt

rm -rf /mnt/rpi/opt/ungoogled-chromium
mkdir -p /mnt/rpi/opt/ungoogled-chromium
tar -xf /work/ungoogled.tar.xz -C /mnt/rpi/opt/ungoogled-chromium --strip-components=1
ln -sf /opt/ungoogled-chromium/chrome /mnt/rpi/usr/bin/ungoogled-chromium
ln -sf /opt/ungoogled-chromium/chrome /mnt/rpi/usr/local/bin/ungoogled-chromium

rm -rf /mnt/rpi/home/pi/vortexbrowse
mkdir -p /mnt/rpi/home/pi/vortexbrowse/server
tar -C /src_server \
    --exclude=".venv" \
    --exclude="__pycache__" \
    --exclude="*.so" \
    --exclude="*cookie*" \
    --exclude="*storage_state*" \
    --exclude="*twitch_token*" \
    --exclude="*chromium_profile*" \
    --exclude="*firefox_profile*" \
    --exclude="browser_bin.txt" \
    -cf - . | tar -C /mnt/rpi/home/pi/vortexbrowse/server -xf -
echo "/usr/bin/ungoogled-chromium" > /mnt/rpi/home/pi/vortexbrowse/server/browser_bin.txt

chroot /mnt/rpi /bin/bash -e << "CHROOT_EOF"
export DEBIAN_FRONTEND=noninteractive
export HOME=/root
if ! id pi &>/dev/null; then
    groupadd -g 1000 pi 2>/dev/null || true
    useradd -m -u 1000 -g 1000 -s /bin/bash pi
fi
echo "pi:vortex" | chpasswd
usermod -aG sudo,video,audio,render,input,plugdev,netdev pi 2>/dev/null || true
echo "pi ALL=(ALL) NOPASSWD: ALL" > /etc/sudoers.d/010_pi-nopasswd
chmod 0440 /etc/sudoers.d/010_pi-nopasswd

echo "vortexbrowser" > /etc/hostname
if grep -q "127.0.1.1" /etc/hosts; then
    sed -i "s/^127\.0\.1\.1.*/127.0.1.1\tvortexbrowser/" /etc/hosts
else
    echo -e "127.0.1.1\tvortexbrowser" >> /etc/hosts
fi

apt-get update -qq
apt-get install -y --no-install-recommends \
    gcc g++ build-essential pkg-config \
    python3 python3-pip python3-venv python3-dev \
    libturbojpeg0-dev liblz4-dev \
    pulseaudio pulseaudio-utils avahi-daemon \
    firefox-esr systemd-zram-generator \
    libnss3 libnspr4 libatk1.0-0 libatk-bridge2.0-0 libcups2 \
    libdrm2 libxkbcommon0 libxcomposite1 libxdamage1 libxfixes3 \
    libxrandr2 libgbm1 libasound2t64 libpango-1.0-0 libcairo2 \
    libglib2.0-0 libexpat1 libdbus-1-3 libxcb1 libx11-6 libxext6 \
    libgl1-mesa-dri libegl1 libegl-mesa0 libgles2 mesa-vulkan-drivers \
    fonts-liberation fonts-dejavu-core fonts-noto-color-emoji ca-certificates

chown -R pi:pi /home/pi/vortexbrowse
su -s /bin/bash pi -c "
set -e
cd /home/pi/vortexbrowse/server
python3 -m venv .venv
.venv/bin/pip install --no-cache-dir --upgrade pip
.venv/bin/pip install --no-cache-dir -r requirements.txt
"
/home/pi/vortexbrowse/server/.venv/bin/playwright install-deps chromium firefox || true
su -s /bin/bash pi -c "
set -e
cd /home/pi/vortexbrowse/server
.venv/bin/playwright install firefox
"
chmod +x /home/pi/vortexbrowse/server/run_server.sh /home/pi/vortexbrowse/server/install_server.sh
mkdir -p /var/lib/systemd/linger
touch /var/lib/systemd/linger/pi

cat << "SW_EOF" > /usr/local/bin/vortex-engine
#!/usr/bin/env bash
set -e
ENG="${1:-}"
if [ "$ENG" != "chromium" ] && [ "$ENG" != "firefox" ]; then
    echo "Usage: sudo vortex-engine [chromium|firefox]"
    grep "^VORTEX_ENGINE=" /etc/default/vortexbrowser || true
    exit 1
fi
sed -i "s/^VORTEX_ENGINE=.*/VORTEX_ENGINE=${ENG}/" /etc/default/vortexbrowser
systemctl restart vortexbrowser.service
echo "VortexBrowser engine switched to: $ENG"
SW_EOF
chmod +x /usr/local/bin/vortex-engine

cat << "SVC_EOF" > /etc/systemd/system/vortexbrowser.service
[Unit]
Description=VortexBrowser Universal Server for AmigaOS 4
After=network-online.target sound.target avahi-daemon.service
Wants=network-online.target

[Service]
Type=simple
User=pi
WorkingDirectory=/home/pi/vortexbrowse/server
Environment=PYTHONUNBUFFERED=1
Environment=OMP_NUM_THREADS=4
Environment=XDG_RUNTIME_DIR=/run/user/1000
EnvironmentFile=-/etc/default/vortexbrowser
ExecStart=/home/pi/vortexbrowse/server/run_server.sh
KillMode=mixed
TimeoutStopSec=15
Restart=always
RestartSec=2
Nice=-5

[Install]
WantedBy=multi-user.target
SVC_EOF

systemctl enable ssh || true
systemctl enable avahi-daemon || true
systemctl enable vortexbrowser.service
apt-get clean
rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/* /root/.cache/pip /home/pi/.cache/pip
CHROOT_EOF

if [ -f /mnt/rpi/etc/resolv.conf.vortexbak ]; then
    mv /mnt/rpi/etc/resolv.conf.vortexbak /mnt/rpi/etc/resolv.conf
fi
sync
cleanup
trap - EXIT
mv /work/base_vortex.img.tmp /work/base_vortex.img
BASE_EOF
fi

# Step 2: Specialize per target & compress natively on host
for T in "${TARGETS[@]}"; do
    OUT_NAME="vortexbrowser-${T}-arm64.img"
    echo "[+] Building target $T -> $OUT_DIR/${OUT_NAME}.xz ..."

    case "$T" in
        rpi3)
            CPU_FLAG="-mcpu=cortex-a53"
            DEF_W=1280; DEF_H=720; DEF_FPS=20; DEF_BR=3500; USE_ZRAM=1
            ;;
        rpi4)
            CPU_FLAG="-mcpu=cortex-a72"
            DEF_W=1280; DEF_H=720; DEF_FPS=25; DEF_BR=4000; USE_ZRAM=1
            ;;
        rpi5)
            CPU_FLAG="-mcpu=cortex-a76"
            DEF_W=1920; DEF_H=1080; DEF_FPS=30; DEF_BR=8000; USE_ZRAM=0
            ;;
    esac

    docker run -i --rm --privileged --platform linux/arm64 \
        -v "$DOCKER_VOL:/work" \
        -v "$OUT_DIR:/out" \
        -e TARGET="$T" -e CPU_FLAG="$CPU_FLAG" \
        -e DEF_W="$DEF_W" -e DEF_H="$DEF_H" -e DEF_FPS="$DEF_FPS" -e DEF_BR="$DEF_BR" \
        -e USE_ZRAM="$USE_ZRAM" -e OUT_NAME="$OUT_NAME" \
        debian:trixie-slim bash -s << 'TARGET_EOF'
set -euo pipefail
apt-get update -qq && apt-get install -y -qq parted e2fsprogs util-linux >/dev/null
rm -f "/work/${OUT_NAME}"
cp --sparse=auto /work/base_vortex.img "/work/${OUT_NAME}"

read -r P2_START P2_SIZE < <(parted -m -s "/work/${OUT_NAME}" unit B print | awk -F: '$1=="2"{gsub(/B/,""); print $2, $4}')
LOOP_ROOT=$(losetup -f --show -o "$P2_START" --sizelimit "$P2_SIZE" "/work/${OUT_NAME}")
cleanup() {
    set +e
    umount -lf /mnt/rpi/dev/pts 2>/dev/null || true
    umount -lf /mnt/rpi/dev 2>/dev/null || true
    umount -lf /mnt/rpi/proc 2>/dev/null || true
    umount -lf /mnt/rpi/sys 2>/dev/null || true
    umount -lf /mnt/rpi 2>/dev/null || true
    losetup -d "$LOOP_ROOT" 2>/dev/null || true
}
trap cleanup EXIT

mkdir -p /mnt/rpi
mount "$LOOP_ROOT" /mnt/rpi
mount --bind /proc /mnt/rpi/proc
mount --bind /sys /mnt/rpi/sys
mount --bind /dev /mnt/rpi/dev
mount --bind /dev/pts /mnt/rpi/dev/pts

chroot /mnt/rpi /bin/bash -e << CHROOT_EOF
RPI_HOST="vortexbrowser"
[ "${TARGET}" != "rpi4" ] && RPI_HOST="vortexbrowser-${TARGET}"
echo "\${RPI_HOST}" > /etc/hostname
sed -i "s/^127\.0\.1\.1.*/127.0.1.1\t\${RPI_HOST}/" /etc/hosts

su -s /bin/bash pi -c "
set -e
cd /home/pi/vortexbrowse/server
gcc -O3 ${CPU_FLAG} -fopenmp -DHAVE_TURBOJPEG_LZ4 -shared -fPIC fast_pixconv.c -lturbojpeg -llz4 -o fast_pixconv.so
.venv/bin/python -c \"import ctypes, playwright, rustwright, websockets, lz4, PIL, turbojpeg; ctypes.CDLL('./fast_pixconv.so'); print('${TARGET}_PYTHON_RUSTWRIGHT_PIXCONV_OK')\"
"

cat << ENV_EOF > /etc/default/vortexbrowser
# VortexBrowser Server (${TARGET})
# Engine: "chromium" (Ungoogled-Chromium, default) or "firefox" (Playwright Firefox)
VORTEX_ENGINE=chromium
VORTEX_USE_RUSTWRIGHT=1
VORTEX_WIDTH=${DEF_W}
VORTEX_HEIGHT=${DEF_H}
VORTEX_FPS=${DEF_FPS}
VORTEX_VIDEO_BITRATE=${DEF_BR}
VORTEX_LOG_LEVEL=INFO
ENV_EOF

if [ "${USE_ZRAM}" = "1" ]; then
    cat << ZRAM_EOF > /etc/systemd/zram-generator.conf
[zram0]
zram-size = ram
compression-algorithm = zstd
swap-priority = 100
ZRAM_EOF
    if [ "${TARGET}" = "rpi3" ]; then
        cat << SYSCTL_EOF > /etc/sysctl.d/99-vortex-zram.conf
vm.swappiness=80
vm.page-cluster=0
SYSCTL_EOF
    fi
else
    rm -f /etc/systemd/zram-generator.conf /etc/sysctl.d/99-vortex-zram.conf
fi

# Verify binaries & service
ldd /opt/ungoogled-chromium/chrome | grep "not found" && exit 1 || true
PW_FF=\$(ls -d /home/pi/.cache/ms-playwright/firefox-*/firefox/firefox 2>/dev/null | tail -n 1 || true)
[ -n "\$PW_FF" ] || exit 1
ldd "\$PW_FF" | grep "not found" && exit 1 || true
systemctl is-enabled vortexbrowser.service ssh.service avahi-daemon.service
CHROOT_EOF

echo "[+] Zeroing free blocks on ${TARGET}..."
dd if=/dev/zero of=/mnt/rpi/zero.fill bs=1M 2>/dev/null || true
rm -f /mnt/rpi/zero.fill
sync
cleanup
trap - EXIT
TARGET_EOF

    echo "[+] Streaming & compressing $OUT_DIR/${OUT_NAME}.xz natively on host..."
    rm -f "$OUT_DIR/${OUT_NAME}" "$OUT_DIR/${OUT_NAME}.xz.tmp"
    docker run --rm -v "$DOCKER_VOL:/work:ro" debian:trixie-slim cat "/work/${OUT_NAME}" \
        | xz -T0 -6 -c > "$OUT_DIR/${OUT_NAME}.xz.tmp"
    mv "$OUT_DIR/${OUT_NAME}.xz.tmp" "$OUT_DIR/${OUT_NAME}.xz"
    docker run --rm -v "$DOCKER_VOL:/work" debian:trixie-slim rm -f "/work/${OUT_NAME}"
    (cd "$OUT_DIR" && sha256sum "${OUT_NAME}.xz" > "${OUT_NAME}.xz.sha256")
    ls -lh "$OUT_DIR/${OUT_NAME}.xz" "$OUT_DIR/${OUT_NAME}.xz.sha256"
done
