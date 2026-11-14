#!/usr/bin/env bash
set -euo pipefail

# ==============================================================================
# VortexBrowser Native macOS Installer (Apple Silicon M1-M4 & Intel x86_64)
# ==============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if [ "$(uname -s)" != "Darwin" ]; then
    echo "❌ This script is intended for macOS (Darwin) only."
    echo "   On Linux, please use ./install_server.sh instead."
    exit 1
fi

ARCH="$(uname -m)"
echo "============================================================================"
echo " VortexBrowser Native macOS Setup (${ARCH})"
echo "============================================================================"

# 1. Ensure Xcode Command Line Tools (Apple Clang)
if ! xcode-select -p >/dev/null 2>&1; then
    echo "⚠️  Xcode Command Line Tools not found. Triggering installation..."
    xcode-select --install || true
    echo "   Please finish the Xcode CLT installation dialog and re-run this script."
    exit 1
fi

# 2. Ensure Homebrew
if ! command -v brew >/dev/null 2>&1; then
    if [ -x "/opt/homebrew/bin/brew" ]; then
        eval "$(/opt/homebrew/bin/brew shellenv)"
    elif [ -x "/usr/local/bin/brew" ]; then
        eval "$(/usr/local/bin/brew shellenv)"
    else
        echo "❌ Homebrew is required. Install it from https://brew.sh and re-run:"
        echo '   /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"'
        exit 1
    fi
fi

BREW_PREFIX="$(brew --prefix)"
echo "[1/5] Installing Homebrew dependencies (python@3.11, ffmpeg, jpeg-turbo, lz4)..."
brew install python@3.11 ffmpeg jpeg-turbo lz4

# Optional: BlackHole 2ch for macOS audio capture via AVFoundation
if ! brew list --cask blackhole-2ch >/dev/null 2>&1; then
    echo ""
    echo "[Optional] Installing BlackHole 2ch virtual audio driver for Amiga audio streaming..."
    brew install --cask blackhole-2ch || echo "⚠️  BlackHole 2ch skipped (audio streaming optional)."
fi

# 3. Create Python 3.11 Virtual Environment
PY311="${BREW_PREFIX}/opt/python@3.11/bin/python3.11"
if [ ! -x "$PY311" ]; then
    PY311="$(command -v python3.11 || command -v python3)"
fi

echo "[2/5] Setting up Python virtual environment (.venv) with $PY311..."
"$PY311" -m venv "$SCRIPT_DIR/.venv"
source "$SCRIPT_DIR/.venv/bin/activate"
pip install --upgrade pip wheel
pip install -r "$SCRIPT_DIR/requirements.txt"

# 4. Playwright Chromium
echo "[3/5] Installing Playwright Chromium browser..."
python -m playwright install chromium

# 5. Compile native fast_pixconv.dylib with Apple Clang
echo "[4/5] Compiling fast_pixconv.dylib for macOS (${ARCH})..."
JPEG_PREFIX="$(brew --prefix jpeg-turbo)"
LZ4_PREFIX="$(brew --prefix lz4)"

if [ "$ARCH" = "arm64" ]; then
    ARCH_FLAGS="-mcpu=apple-m1"
else
    ARCH_FLAGS="-march=native"
fi

clang -O3 $ARCH_FLAGS -DHAVE_TURBOJPEG_LZ4 -dynamiclib -fPIC \
    -I"${JPEG_PREFIX}/include" -I"${LZ4_PREFIX}/include" \
    "$SCRIPT_DIR/fast_pixconv.c" \
    -L"${JPEG_PREFIX}/lib" -L"${LZ4_PREFIX}/lib" \
    -lturbojpeg -llz4 \
    -o "$SCRIPT_DIR/fast_pixconv.dylib"

echo "[5/5] Verifying installation..."
python -m py_compile "$SCRIPT_DIR/server.py"

echo ""
echo "============================================================================"
echo " ✅ VortexBrowser macOS Server installed successfully!"
echo "    Start server with:  ./run_macos.sh"
echo "    Hardware Encoder:   Apple VideoToolbox (h264_videotoolbox)"
echo "    SIMD Pipeline:      $SCRIPT_DIR/fast_pixconv.dylib"
echo "============================================================================"
