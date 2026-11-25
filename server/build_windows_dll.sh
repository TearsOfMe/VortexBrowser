#!/usr/bin/env bash
# ==============================================================================
# Cross-compile self-contained Windows x64 DLLs from Linux via Docker + MinGW-w64:
#   - fast_pixconv.dll      (universal x86-64 SSE2 + static libturbojpeg + static liblz4)
#   - fast_pixconv_avx2.dll (x86-64 AVX2+FMA      + static libturbojpeg + static liblz4)
# Zero external runtime DLL dependencies beyond kernel32.dll / msvcrt.dll!
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="${1:-$SCRIPT_DIR}"
mkdir -p "$OUT_DIR"

echo "[+] Cross-compiling Windows x64 fast_pixconv.dll & fast_pixconv_avx2.dll via Docker (MinGW-w64)..."

docker run --rm -i --platform linux/amd64 \
    -v "$SCRIPT_DIR:/src:ro" \
    -v "$OUT_DIR:/out" \
    debian:trixie-slim bash -s << 'EOF'
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive

apt-get update -qq
apt-get install -y -qq \
    gcc-mingw-w64-x86-64-win32 \
    g++-mingw-w64-x86-64-win32 \
    cmake make nasm curl ca-certificates tar xz-utils >/dev/null

mkdir -p /build && cd /build

# 1. Build static libjpeg-turbo (x86-64 Windows with full NASM AVX2/SSE2 SIMD dispatch!)
TJ_VER="3.1.0"
echo "[1/3] Building static libjpeg-turbo ${TJ_VER} for Win64..."
curl -fsSL "https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/${TJ_VER}/libjpeg-turbo-${TJ_VER}.tar.gz" -o tj.tar.gz
tar -xzf tj.tar.gz
cat > /build/mingw64.cmake << 'TC'
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc-win32)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++-win32)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_ASM_NASM_OBJECT_FORMAT win64)
TC
cmake -S "libjpeg-turbo-${TJ_VER}" -B tj-build \
    -DCMAKE_TOOLCHAIN_FILE=/build/mingw64.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_SHARED=OFF \
    -DENABLE_STATIC=ON \
    -DWITH_TURBOJPEG=ON \
    -DWITH_SIMD=ON \
    -DCMAKE_INSTALL_PREFIX=/build/prefix >/dev/null
cmake --build tj-build -j"$(nproc)" >/dev/null
cmake --install tj-build >/dev/null

# 2. Build static liblz4 (x86-64 Windows)
LZ4_VER="1.10.0"
echo "[2/3] Building static liblz4 ${LZ4_VER} for Win64..."
curl -fsSL "https://github.com/lz4/lz4/releases/download/v${LZ4_VER}/lz4-${LZ4_VER}.tar.gz" -o lz4.tar.gz
tar -xzf lz4.tar.gz
x86_64-w64-mingw32-gcc-win32 -O3 -c "lz4-${LZ4_VER}/lib/lz4.c" -o /build/lz4.o
x86_64-w64-mingw32-ar rcs /build/prefix/lib/liblz4.a /build/lz4.o
cp "lz4-${LZ4_VER}/lib/lz4.h" /build/prefix/include/lz4.h

# 3. Compile universal SSE2 + AVX2+FMA fast_pixconv DLLs (fully self-contained, zero extra DLLs!)
echo "[3/3] Compiling fast_pixconv.dll (universal SSE2) and fast_pixconv_avx2.dll (AVX2+FMA)..."
COMMON_FLAGS=(
    -O3 -DHAVE_TURBOJPEG_LZ4 -shared
    -I/build/prefix/include
    /src/fast_pixconv.c
    /build/prefix/lib/libturbojpeg.a
    /build/prefix/lib/liblz4.a
    -static-libgcc -Wl,-Bstatic -Wl,-Bdynamic
)

x86_64-w64-mingw32-gcc-win32 -msse2 "${COMMON_FLAGS[@]}" -o /out/fast_pixconv.dll
x86_64-w64-mingw32-gcc-win32 -mavx2 -mfma "${COMMON_FLAGS[@]}" -o /out/fast_pixconv_avx2.dll
x86_64-w64-mingw32-strip --strip-unneeded /out/fast_pixconv.dll /out/fast_pixconv_avx2.dll

echo "=== Verifying DLL exports & runtime dependencies ==="
for dll in /out/fast_pixconv.dll /out/fast_pixconv_avx2.dll; do
    echo "--- $(basename "$dll") ---"
    x86_64-w64-mingw32-objdump -p "$dll" | grep -E "DLL Name:|encode_jpeg_to_frame_packet|decode_jpeg_to_yuv420p|fast_pixconv_release_tls"
done
EOF

ls -lh "$OUT_DIR/fast_pixconv.dll" "$OUT_DIR/fast_pixconv_avx2.dll"
