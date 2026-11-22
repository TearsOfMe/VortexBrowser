#!/usr/bin/env python3
"""
VortexBrowse - CachyOS HP EliteDesk 805 G6 (AMD Ryzen PRO / Radeon Vega) Edition
- Specially tuned for Full HD (1920x1080) @ 25 FPS streaming to AmigaOS 4.1 PowerPC
- Uses C-native AVX2 + OpenMP + libturbojpeg + liblz4 pipeline (encode_jpeg_to_frame_packet)
- Streams 12-bpp Planar YUV420P with 2D Dirty-Rect (CODEC_LZ4_RECT) & full-frame LZ4
- Captures real browser audio via PipeWire/PulseAudio virtual sink (full Chromium + ozone-headless)
  and streams 44100Hz S16BE Stereo PCM to AmigaOS 4 AHI (ahi.device)
"""

from __future__ import annotations

import argparse
import asyncio
import atexit
import base64
import concurrent.futures
import ctypes
import fcntl
import gc
import glob
import hashlib
import html
import inspect
import io
import logging
import os
import platform
import re
import select
import signal
import socket
import subprocess
import struct
import sys
import tempfile
import time
import urllib.request
from typing import Any, Dict, List, Optional, Tuple, Union
from urllib.parse import parse_qs, quote_plus, unquote, urlencode, urlparse

import json
import lz4.block
from PIL import Image

try:
    import websockets
    _HAS_WEBSOCKETS = True
except ImportError:
    websockets = None  # type: ignore
    _HAS_WEBSOCKETS = False

from playwright.async_api import (
    async_playwright as _playwright_async,
    Browser,
    BrowserContext,
    Page,
    Download,
    CDPSession,
    Frame,
)

try:
    from rustwright.async_api import (
        async_playwright as _rustwright_async,
        BrowserContext as _RustwrightBrowserContext,
    )
    _HAS_RUSTWRIGHT = True
    _PERSISTENT_CONTEXT_TYPES = (BrowserContext, _RustwrightBrowserContext)
except ImportError:
    _rustwright_async = None
    _HAS_RUSTWRIGHT = False
    _PERSISTENT_CONTEXT_TYPES = (BrowserContext,)

# ============================================================================
# Protocol Constants (matching common/protocol.h with #pragma pack(push, 2))
# ============================================================================
MSG_FRAME             = 0x01
MSG_INPUT             = 0x02
MSG_DOWNLOAD_START    = 0x03
MSG_DOWNLOAD_RESPONSE = 0x04
MSG_DOWNLOAD_CHUNK    = 0x05
MSG_DOWNLOAD_COMPLETE = 0x06
MSG_AUDIO             = 0x07
MSG_NAV_UPDATE        = 0x08
MSG_CONTEXT_MENU      = 0x09
MSG_PAGE_PROGRESS     = 0x0A
MSG_CLIPBOARD         = 0x0B
MSG_UPLOAD_REQUEST    = 0x0C
MSG_UPLOAD_RESPONSE   = 0x0D
MSG_UPLOAD_CHUNK      = 0x0E
MSG_PASSWORD_SAVE     = 0x0F

FMT_ARGB32 = 1
FMT_RGB565 = 2
FMT_YUV420P = 3

CODEC_RAW       = 0
CODEC_LZ4       = 1
CODEC_LZ4_SLICE = 2
CODEC_JPEG      = 3
CODEC_LZ4_RECT  = 4
CODEC_H264      = 5

STREAM_VIDEO_AUTO      = 0
STREAM_VIDEO_DIRTYRECT = 1
STREAM_VIDEO_H264      = 2

INPUT_CLICK      = 1
INPUT_MOUSE_MOVE = 2
INPUT_WHEEL      = 3
INPUT_KEY_DOWN   = 4
INPUT_KEY_UP     = 5
INPUT_GOTO_URL   = 6
INPUT_MOUSE_DOWN = 7
INPUT_MOUSE_UP   = 8

STRUCT_MSG_FRAME        = struct.Struct(">BHHBBI")
STRUCT_SLICE_HDR        = struct.Struct(">HH")
STRUCT_RECT_HDR         = struct.Struct(">HHHH")
STRUCT_MSG_INPUT_BODY   = struct.Struct(">BxhhhH512s")
STRUCT_MSG_DL_START     = struct.Struct(">BIQ256s")
STRUCT_MSG_DL_RESP_BODY = struct.Struct(">IBx")
STRUCT_MSG_DL_CHUNK     = struct.Struct(">BII")
STRUCT_MSG_DL_COMPLETE  = struct.Struct(">BI")
STRUCT_MSG_AUDIO        = struct.Struct(">BHBBII")
STRUCT_MSG_NAV_UPDATE   = struct.Struct(">BI512s128s")
STRUCT_MSG_CTX_MENU     = struct.Struct(">B512s128s512s")
STRUCT_MSG_PROGRESS     = struct.Struct(">BBx64s")
STRUCT_MSG_UPLOAD_REQ   = struct.Struct(">BII256s")
STRUCT_MSG_UPLOAD_RESP  = struct.Struct(">IIBB256s")
STRUCT_MSG_UPLOAD_CHUNK = struct.Struct(">BII")
STRUCT_MSG_PASSWORD_SAVE = struct.Struct(">B256s128s128s")

DOWNLOAD_CHUNK_SIZE = 64 * 1024
AUDIO_SAMPLE_RATE   = 44100
AUDIO_CHANNELS      = 2
AUDIO_CHUNK_BYTES   = 8192  # ~46ms of 44100Hz 16-bit stereo PCM (CD quality)
AUDIO_SINK_NAME     = "vortexbrowse_sink"

_LOG_LEVEL_STR = os.environ.get("VORTEX_LOG_LEVEL", "CRITICAL").upper()
_LOG_LEVEL = getattr(logging, _LOG_LEVEL_STR, logging.CRITICAL)
logging.basicConfig(
    level=_LOG_LEVEL,
    format="[%(asctime)s] [VortexServer] %(levelname)s: %(message)s",
    datefmt="%H:%M:%S",
)
logger = logging.getLogger("VortexServer")
logger.setLevel(_LOG_LEVEL)

AMIGA_RAWKEY_MAP = {
    0x40: "Space",
    0x41: "Backspace",
    0x42: "Tab",
    0x43: "Enter",
    0x44: "Enter",
    0x45: "Escape",
    0x46: "Delete",
    0x48: "PageUp",
    0x49: "PageDown",
    0x4C: "ArrowUp",
    0x4D: "ArrowDown",
    0x4E: "ArrowRight",
    0x4F: "ArrowLeft",
    0x50: "F1",
    0x51: "F2",
    0x52: "F3",
    0x53: "F4",
    0x54: "F5",
    0x55: "F6",
    0x56: "F7",
    0x57: "F8",
    0x58: "F9",
    0x59: "F10",
    0x70: "Home",
    0x71: "End",
}


# ============================================================================
# Dynamic Power Profile Management via powerprofilesctl (Performance on Activity)
# ============================================================================
class CPUGovernorManager:
    """Manages CPU power scaling profile via 'powerprofilesctl' (NOT cpufreq).
    Switches the system power profile to 'performance' when an Amiga client connects,
    and restores the previous profile (e.g. 'balanced' or 'power-saver') when clients
    disconnect or on shutdown.
    """
    def __init__(self, enabled: bool = True):
        self.enabled = enabled
        self.prev_profile: Optional[str] = None
        self.active_count = 0
        self.is_performance = False
        self._lock = asyncio.Lock()

        if self.enabled:
            cur = self.read_current_profile()
            if not cur:
                self.enabled = False
                logger.debug("[CPU Power] powerprofilesctl not available or inactive.")

    @staticmethod
    def read_current_profile() -> Optional[str]:
        try:
            res = subprocess.run(
                ["powerprofilesctl", "get"],
                capture_output=True,
                text=True,
                timeout=2.0,
            )
            if res.returncode == 0 and res.stdout.strip():
                return res.stdout.strip()
        except Exception:
            pass
        return None

    def set_profile(self, profile_name: str) -> bool:
        if not self.enabled:
            return False
        try:
            res = subprocess.run(
                ["powerprofilesctl", "set", profile_name],
                check=False,
                capture_output=True,
                text=True,
                timeout=2.0,
            )
            return res.returncode == 0
        except Exception:
            return False

    async def client_connected(self) -> None:
        if not self.enabled:
            return
        async with self._lock:
            self.active_count += 1
            if self.active_count == 1 and not self.is_performance:
                cur = self.read_current_profile()
                if cur and not self.prev_profile:
                    self.prev_profile = cur
                if self.set_profile("performance"):
                    self.is_performance = True
                    logger.info("[CPU Power] Amiga client active -> powerprofilesctl switched to 'performance' (was '%s')", self.prev_profile or "default")
                else:
                    logger.debug("[CPU Power] Could not set powerprofilesctl to performance")

    async def client_disconnected(self) -> None:
        if not self.enabled:
            return
        async with self._lock:
            self.active_count = max(0, self.active_count - 1)
            if self.active_count == 0 and self.is_performance:
                restored_to = self.prev_profile or "balanced"
                self.restore()
                logger.info("[CPU Power] No active clients -> powerprofilesctl restored to '%s'", restored_to)

    def restore(self) -> None:
        if self.is_performance:
            target = self.prev_profile if (self.prev_profile and self.prev_profile != "performance") else "balanced"
            try:
                subprocess.run(
                    ["powerprofilesctl", "set", target],
                    check=False,
                    capture_output=True,
                    timeout=2.0,
                )
            except Exception:
                pass
            self.is_performance = False
            self.prev_profile = None


cpu_gov_global: Optional[CPUGovernorManager] = None


def _cleanup_cpu_gov() -> None:
    if cpu_gov_global:
        cpu_gov_global.restore()


atexit.register(_cleanup_cpu_gov)


# ============================================================================
# PipeWire / PulseAudio Virtual Sink & Full Chromium Binary Discovery
# ============================================================================
def setup_audio_environment() -> None:
    """Ensures XDG_RUNTIME_DIR and PulseAudio socket directory exist (crucial on DietPi/headless)."""
    if "XDG_RUNTIME_DIR" not in os.environ or not os.path.isdir(os.environ["XDG_RUNTIME_DIR"]):
        uid = os.getuid()
        candidate = f"/run/user/{uid}"
        if os.path.isdir(candidate):
            os.environ["XDG_RUNTIME_DIR"] = candidate
        else:
            tmp_xdg = f"/tmp/xdg_runtime_{uid}"
            try:
                os.makedirs(tmp_xdg, mode=0o700, exist_ok=True)
                os.environ["XDG_RUNTIME_DIR"] = tmp_xdg
            except Exception:
                pass


def _find_macos_audio_device() -> Optional[str]:
    """Finds a virtual loopback audio device on macOS (BlackHole 2ch, Soundflower, etc.)."""
    try:
        res = subprocess.run(
            ["ffmpeg", "-f", "avfoundation", "-list_devices", "true", "-i", ""],
            capture_output=True, text=True, timeout=4
        )
        output = (res.stderr or "") + "\n" + (res.stdout or "")
        in_audio = False
        for line in output.splitlines():
            if "AVFoundation audio devices:" in line:
                in_audio = True
                continue
            if in_audio:
                for cand in ("BlackHole 2ch", "BlackHole 16ch", "BlackHole", "Soundflower (2ch)", "VB-Cable"):
                    if cand.lower() in line.lower():
                        return cand
    except Exception:
        pass
    return None


def ensure_audio_sink() -> bool:
    """Creates and unmutes a dedicated PipeWire/PulseAudio null-sink (Linux) or checks BlackHole (macOS)."""
    if platform.system() == "Darwin":
        dev = _find_macos_audio_device()
        if dev:
            logger.info("macOS virtual audio device '%s' detected for AHI streaming.", dev)
            return True
        logger.warning("Kein macOS Virtual Audio Device (z. B. BlackHole 2ch) gefunden. Audio-Streaming deaktiviert.")
        logger.info("Hinweis fuer macOS Audio: 'brew install blackhole-2ch' und als Standard-Ausgabe waehlen.")
        return False

    setup_audio_environment()

    # 1. Test if sound server is reachable; if not, try to start PulseAudio daemon
    try:
        subprocess.check_call(["pactl", "info"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=2)
    except Exception:
        logger.info("PulseAudio / PipeWire sound daemon not running. Attempting to start pulseaudio...")
        try:
            subprocess.call(
                ["pulseaudio", "--start", "--exit-idle-time=-1", "--disallow-exit"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                timeout=4,
            )
            time.sleep(1.2)
        except Exception as _pe:
            logger.debug("Failed starting pulseaudio daemon: %s", _pe)

    try:
        out = subprocess.check_output(["pactl", "list", "short", "sinks"], text=True, timeout=3)
        if AUDIO_SINK_NAME not in out:
            subprocess.check_call(
                [
                    "pactl",
                    "load-module",
                    "module-null-sink",
                    f"sink_name={AUDIO_SINK_NAME}",
                    "sink_properties=device.description=VortexBrowse_Amiga_Audio",
                ],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                timeout=3,
            )
            logger.info("Created PipeWire/PulseAudio virtual sink '%s'.", AUDIO_SINK_NAME)
        subprocess.call(["pactl", "set-sink-mute", AUDIO_SINK_NAME, "0"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        subprocess.call(["pactl", "set-sink-volume", AUDIO_SINK_NAME, "100%"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        logger.info("Audio sink '%s' ready for AHI streaming.", AUDIO_SINK_NAME)
        return True
    except Exception as exc:
        logger.warning("Could not configure PipeWire/PulseAudio virtual sink: %s", exc)
        logger.info("Hinweis: Installiere PulseAudio mit 'sudo apt-get install -y pulseaudio && pulseaudio --start'")
        return False


def unmute_all_browser_sink_inputs() -> None:
    """Ensures PipeWire module-stream-restore never leaves Chromium sink-inputs muted at 0%."""
    if platform.system() == "Darwin":
        return
    try:
        out = subprocess.check_output(["pactl", "list", "short", "sink-inputs"], text=True, timeout=2)
        for line in out.splitlines():
            parts = line.split()
            if parts:
                sid = parts[0]
                subprocess.call(["pactl", "set-sink-input-mute", sid, "0"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                subprocess.call(["pactl", "set-sink-input-volume", sid, "100%"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except Exception:
        pass


def is_snap_binary(path: str) -> bool:
    """Returns True if the given browser binary is an Ubuntu Snap wrapper."""
    if not path or not os.path.exists(path):
        return False
    if "/snap/" in path or path.startswith("/snap/"):
        return True
    try:
        real = os.path.realpath(path)
        if "/snap/" in real or real.startswith("/snap/"):
            return True
        if os.path.isfile(path) and not os.path.islink(path):
            with open(path, "rb") as f:
                header = f.read(512)
                if b"/snap" in header or b"snap run" in header:
                    return True
    except Exception:
        pass
    return False


def find_full_chromium_executable(custom_bin: Optional[str] = None) -> Optional[str]:
    """
    Finds the full Chromium / Ungoogled-Chromium binary on Linux (x86_64, aarch64, Raspberry Pi 4).
    Prefers native (non-snap) binaries and Playwright's native Chromium over Ubuntu Snap wrappers
    to avoid AppArmor confinement and permission issues.
    """
    if custom_bin and os.path.isfile(custom_bin) and os.access(custom_bin, os.X_OK):
        return custom_bin

    # Check for configured browser from installer in browser_bin.txt
    cfg_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "browser_bin.txt")
    if os.path.exists(cfg_path):
        try:
            with open(cfg_path, "r", encoding="utf-8") as f:
                saved = f.readline().strip()
            if saved and os.path.isfile(saved) and os.access(saved, os.X_OK):
                return saved
        except Exception:
            pass

    # Tier 1: Real native installed browsers (.deb / native packages / macOS .app bundles)
    if platform.system() == "Darwin":
        native_patterns = [
            "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
            "/Applications/Chromium.app/Contents/MacOS/Chromium",
            "/Applications/Brave Browser.app/Contents/MacOS/Brave Browser",
            "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge",
            "/opt/homebrew/bin/chromium",
            "/usr/local/bin/chromium",
            os.path.expanduser("~/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"),
            os.path.expanduser("~/Applications/Chromium.app/Contents/MacOS/Chromium"),
        ]
    else:
        native_patterns = [
            os.path.expanduser("~/.local/opt/ungoogled-chromium/usr/lib/chromium/chromium"),
            os.path.expanduser("~/.local/bin/ungoogled-chromium"),
            "/usr/bin/ungoogled-chromium",
            "/usr/lib/ungoogled-chromium/ungoogled-chromium",
            "/usr/bin/google-chrome-stable",
            "/usr/bin/google-chrome",
            "/usr/bin/brave-browser",
            "/usr/bin/brave",
            "/usr/bin/chromium-browser",
            "/usr/bin/chromium",
            "/usr/lib/chromium-browser/chromium-browser",
            "/usr/lib/chromium/chromium",
        ]
    for p in native_patterns:
        if os.path.isfile(p) and os.access(p, os.X_OK) and not is_snap_binary(p):
            return p

    # Tier 2: Playwright native Chromium (clean, standalone, no snap confinement, has audio!)
    if platform.system() == "Darwin":
        pw_patterns = [
            os.path.expanduser("~/Library/Caches/ms-playwright/chromium-*/chrome-mac-arm64/Google Chrome for Testing.app/Contents/MacOS/Google Chrome for Testing"),
            os.path.expanduser("~/Library/Caches/ms-playwright/chromium-*/chrome-mac-x64/Google Chrome for Testing.app/Contents/MacOS/Google Chrome for Testing"),
            os.path.expanduser("~/Library/Caches/ms-playwright/chromium-*/chrome-mac/Chromium.app/Contents/MacOS/Chromium"),
        ]
    else:
        pw_patterns = [
            os.path.expanduser("~/.cache/ms-playwright/chromium-*/chrome-linux64/chrome"),
            os.path.expanduser("~/.cache/ms-playwright/chromium-*/chrome-linux/chrome"),
        ]
    candidates = []
    for pat in pw_patterns:
        candidates.extend(glob.glob(pat))
    candidates.sort(reverse=True)
    for c in candidates:
        if os.path.isfile(c) and os.access(c, os.X_OK):
            return c

    # Tier 3: Ubuntu Snap Chromium as last fallback (may have confinement issues)
    snap_patterns = [
        "/usr/bin/chromium-browser",
        "/usr/bin/chromium",
        "/snap/bin/chromium",
    ]
    for p in snap_patterns:
        if os.path.isfile(p) and os.access(p, os.X_OK):
            logger.warning("Using Ubuntu Snap Chromium (%s) as fallback. Snap may enforce strict AppArmor restrictions.", p)
            return p

    return None


def find_all_extension_paths() -> List[str]:
    """Finds all bundled or system unpacked Chrome extensions (uBlock Origin, h264ify, etc.)."""
    found: List[str] = []
    base_dir = os.path.dirname(os.path.abspath(__file__))
    candidates = [
        os.path.join(base_dir, "extensions", "ublock", "uBlock0.chromium"),
        os.path.join(base_dir, "extensions", "ublock"),
        os.path.join(base_dir, "extensions", "h264ify"),
        "/usr/share/chromium/extensions/ublock",
        "/usr/share/chromium/extensions/h264ify",
    ]
    for c in candidates:
        if os.path.isdir(c) and os.path.exists(os.path.join(c, "manifest.json")):
            if c not in found:
                found.append(c)
    ext_root = os.path.join(base_dir, "extensions")
    if os.path.isdir(ext_root):
        for name in os.listdir(ext_root):
            p = os.path.join(ext_root, name)
            if os.path.isdir(p) and os.path.exists(os.path.join(p, "manifest.json")):
                if p not in found:
                    found.append(p)
    return found


def find_ublock_extension_path() -> Optional[str]:
    """Finds bundled or system uBlock Origin unpacked extension directory."""
    exts = find_all_extension_paths()
    for e in exts:
        if "ublock" in e.lower():
            return e
    return exts[0] if exts else None


DARKREADER_JS: str = ""
try:
    dr_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "darkreader.js")
    if os.path.exists(dr_path):
        with open(dr_path, "r", encoding="utf-8") as f:
            DARKREADER_JS = f.read()
        logger.info("Loaded DarkReader engine (%d bytes)", len(DARKREADER_JS))
    else:
        logger.warning("darkreader.js not found at %s", dr_path)
except Exception as exc:
    logger.warning("Could not read darkreader.js: %s", exc)


# ============================================================================
# Built-in Local Portal & Test HTTP Server
# ============================================================================
PORTAL_HTML = """<!DOCTYPE html>
<html lang="de">
<head>
<meta charset="utf-8">
<title>VortexBrowser - Startseite &amp; Schnellwahl</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    font-family: 'DejaVu Sans', 'Liberation Sans', Arial, Helvetica, sans-serif;
    background: linear-gradient(180deg, #e6eaf0 0%, #d3dae4 100%);
    color: #1e293b;
    min-height: 100vh;
    padding: 16px 22px;
  }
  .header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    background: linear-gradient(180deg, #375375 0%, #20354f 100%);
    border: 1px solid #1a2c42;
    border-radius: 6px;
    padding: 10px 16px;
    margin-bottom: 14px;
    box-shadow: 0 2px 4px rgba(0,0,0,0.18);
  }
  .title-box h1 {
    font-size: 20px;
    color: #ffffff;
    letter-spacing: 0.5px;
    text-shadow: 1px 1px 2px #0f172a;
    display: flex;
    align-items: center;
    gap: 8px;
  }
  .title-box p {
    font-size: 12px;
    color: #cbd5e1;
    margin-top: 2px;
  }
  .badge {
    background: linear-gradient(180deg, #dc2626 0%, #991b1b 100%);
    border: 1px solid #ef4444;
    color: #ffffff;
    padding: 5px 12px;
    border-radius: 4px;
    font-weight: bold;
    font-size: 11px;
    letter-spacing: 0.5px;
    box-shadow: inset 0 1px 0 rgba(255,255,255,0.3);
  }
  .search-bar {
    display: flex;
    gap: 8px;
    margin-bottom: 14px;
    background: #f8fafc;
    border: 1px solid #94a3b8;
    border-radius: 6px;
    padding: 5px;
    box-shadow: inset 0 1px 3px rgba(0,0,0,0.12);
  }
  .search-bar input {
    flex: 1;
    padding: 8px 12px;
    border-radius: 4px;
    border: 1px solid #cbd5e1;
    background: #ffffff;
    color: #0f172a;
    font-size: 14px;
  }
  .search-bar input:focus {
    outline: none;
    border-color: #2563eb;
    box-shadow: 0 0 0 2px rgba(37,99,235,0.25);
  }
  .btn-search, .btn-action {
    padding: 8px 16px;
    border-radius: 4px;
    border: 1px solid #64748b;
    background: linear-gradient(180deg, #f1f5f9 0%, #cbd5e1 100%);
    color: #0f172a;
    font-weight: bold;
    font-size: 13px;
    cursor: pointer;
    text-decoration: none;
    display: inline-block;
    box-shadow: 0 1px 2px rgba(0,0,0,0.12);
  }
  .btn-search:hover, .btn-action:hover {
    background: linear-gradient(180deg, #ffffff 0%, #e2e8f0 100%);
    border-color: #475569;
  }
  .btn-search:active, .btn-action:active {
    background: #cbd5e1;
    box-shadow: inset 0 1px 3px rgba(0,0,0,0.25);
  }
  .grid {
    display: grid;
    grid-template-columns: repeat(3, 1fr);
    gap: 12px;
    margin-bottom: 14px;
  }
  .card {
    background: #ffffff;
    border: 1px solid #94a3b8;
    border-radius: 6px;
    overflow: hidden;
    box-shadow: 0 2px 4px rgba(0,0,0,0.08);
    display: flex;
    flex-direction: column;
  }
  .card-top {
    height: 5px;
  }
  .card-top-blue   { background: #2563eb; }
  .card-top-orange { background: #ea580c; }
  .card-top-red    { background: #dc2626; }
  .card-top-teal   { background: #0d9488; }
  .card-top-green  { background: #16a34a; }
  .card-top-purple { background: #7c3aed; }
  .card-body {
    padding: 10px 14px;
    flex: 1;
    display: flex;
    flex-direction: column;
    justify-content: space-between;
  }
  .card-body h3 {
    color: #1e3a8a;
    font-size: 15px;
    margin-bottom: 4px;
    display: flex;
    align-items: center;
    gap: 6px;
  }
  .card-body p {
    color: #475569;
    font-size: 12px;
    margin-bottom: 8px;
    line-height: 1.35;
  }
  .card-action {
    display: flex;
    justify-content: flex-end;
  }
  .dl-strip {
    background: #ffffff;
    border: 1px solid #94a3b8;
    border-left: 5px solid #d97706;
    border-radius: 6px;
    padding: 10px 14px;
    display: flex;
    align-items: center;
    justify-content: space-between;
    box-shadow: 0 2px 4px rgba(0,0,0,0.08);
  }
  .dl-strip h3 { color: #b45309; font-size: 14px; margin-bottom: 2px; }
  .dl-strip p { color: #64748b; font-size: 11.5px; }
  .btn-amber {
    background: linear-gradient(180deg, #f59e0b 0%, #d97706 100%);
    border: 1px solid #b45309;
    color: #ffffff;
    padding: 7px 14px;
    border-radius: 4px;
    text-decoration: none;
    font-weight: bold;
    font-size: 12px;
    box-shadow: 0 1px 2px rgba(0,0,0,0.15);
  }
  .btn-amber:hover {
    background: linear-gradient(180deg, #fbbf24 0%, #b45309 100%);
  }
  #click-status {
    margin-top: 4px;
    color: #15803d;
    font-weight: bold;
    font-size: 11.5px;
  }
  @media (prefers-color-scheme: dark) {
    body {
      background: linear-gradient(180deg, #18181b 0%, #09090b 100%);
      color: #f4f4f5;
    }
    .header {
      background: linear-gradient(180deg, #27272a 0%, #18181b 100%);
      border-color: #3f3f46;
      box-shadow: 0 2px 4px rgba(0,0,0,0.5);
    }
    .title-box p {
      color: #a1a1aa;
    }
    .search-bar {
      background: #27272a;
      border-color: #3f3f46;
      box-shadow: inset 0 1px 3px rgba(0,0,0,0.4);
    }
    .search-bar input {
      background: #18181b;
      color: #fafafa;
      border-color: #3f3f46;
    }
    .search-bar input:focus {
      border-color: #3b82f6;
    }
    .btn-search, .btn-action {
      background: linear-gradient(180deg, #3f3f46 0%, #27272a 100%);
      border-color: #52525b;
      color: #f4f4f5;
    }
    .btn-search:hover, .btn-action:hover {
      background: linear-gradient(180deg, #52525b 0%, #3f3f46 100%);
    }
    .card {
      background: #27272a;
      border-color: #3f3f46;
      box-shadow: 0 2px 4px rgba(0,0,0,0.4);
    }
    .card-body h3 {
      color: #60a5fa;
    }
    .card-body p {
      color: #a1a1aa;
    }
    .dl-strip {
      background: #27272a;
      border-color: #3f3f46;
      border-left: 5px solid #d97706;
      box-shadow: 0 2px 4px rgba(0,0,0,0.4);
    }
    .dl-strip p {
      color: #a1a1aa;
    }
  }
</style>
<script>
function playAmigaSynth() {
  try {
    const AudioCtx = window.AudioContext || window.webkitAudioContext;
    if (!window._vbAudioCtx) {
      window._vbAudioCtx = new AudioCtx();
    }
    const ctx = window._vbAudioCtx;
    if (ctx.state === 'suspended') {
      ctx.resume();
    }
    const notes = [261.63, 329.63, 392.00, 523.25, 659.25, 523.25, 392.00, 523.25];
    notes.forEach((freq, idx) => {
      const osc = ctx.createOscillator();
      const gain = ctx.createGain();
      osc.type = 'sawtooth';
      osc.frequency.setValueAtTime(freq, ctx.currentTime + idx * 0.12);
      gain.gain.setValueAtTime(0.35, ctx.currentTime + idx * 0.12);
      gain.gain.exponentialRampToValueAtTime(0.01, ctx.currentTime + idx * 0.12 + 0.11);
      osc.connect(gain);
      gain.connect(ctx.destination);
      osc.start(ctx.currentTime + idx * 0.12);
      osc.stop(ctx.currentTime + idx * 0.12 + 0.115);
    });
  } catch (e) {}
}
</script>
</head>
<body>
  <div class="header">
    <div class="title-box">
      <h1>VortexBrowser &bull; Startseite &amp; Schnellwahl</h1>
      <p>Schnellwahl &bull; Turbo LZ4-Streaming &bull; AHI Stereo-Audio &bull; AmigaOS 4 Kontextmen&uuml; &bull; ASL Download</p>
    </div>
    <div class="badge">AmigaOS 4.1 PPC</div>
  </div>

  <form class="search-bar" onsubmit="event.preventDefault(); const q=document.getElementById('q').value.trim(); if(!q) return; if(q.startsWith('http://')||q.startsWith('https://')) { window.location.href=q; return; } const eng=document.getElementById('se').value; window.location.href=eng.replace('%s', encodeURIComponent(q));">
    <span style="color:#ffffff;font-weight:bold;font-size:13px;align-self:center;padding:0 4px;">Suche:</span>
    <select id="se" style="padding:8px 10px;border-radius:4px;border:1px solid #cbd5e1;background:#f8fafc;color:#0f172a;font-weight:bold;font-size:13px;">
      <option value="https://duckduckgo.com/?q=%s" selected>DuckDuckGo</option>
      <option value="https://www.google.de/search?q=%s">Google</option>
      <option value="https://www.bing.com/search?q=%s">Bing</option>
      <option value="https://www.startpage.com/sp/search?query=%s">Startpage</option>
      <option value="https://www.ecosia.org/search?q=%s">Ecosia</option>
      <option value="https://de.wikipedia.org/w/index.php?search=%s">Wikipedia (DE)</option>
      <option value="https://www.youtube.com/results?search_query=%s">YouTube</option>
      <option value="https://aminet.net/search?query=%s">Aminet</option>
      <option value="https://os4depot.net/?function=search&ias=1&s=%s">OS4Depot</option>
    </select>
    <input id="q" type="text" autofocus placeholder="Suchbegriff oder URL eingeben (Standard: DuckDuckGo)..." />
    <button type="submit" class="btn-search">Suche</button>
  </form>

  <div class="grid">
    <div class="card">
      <div class="card-top card-top-orange"></div>
      <div class="card-body">
        <div>
          <h3>Amiga-News.de</h3>
          <p>Aktuelle Nachrichten rund um AmigaOS 4, PowerPC Hardware und Community-Projekte.</p>
        </div>
        <div class="card-action">
          <a class="btn-action" href="https://www.amiga-news.de">&Ouml;ffnen</a>
        </div>
      </div>
    </div>

    <div class="card">
      <div class="card-top card-top-blue"></div>
      <div class="card-body">
        <div>
          <h3>OS4Depot.net</h3>
          <p>Das zentrale Archiv f&uuml;r native AmigaOS 4 Software, Bibliotheken und Spiele.</p>
        </div>
        <div class="card-action">
          <a class="btn-action" href="https://os4depot.net">&Ouml;ffnen</a>
        </div>
      </div>
    </div>

    <div class="card">
      <div class="card-top card-top-teal"></div>
      <div class="card-body">
        <div>
          <h3>Amigans.net</h3>
          <p>Das internationale AmigaOS 4 Forum f&uuml;r Anwender, Entwickler und Erfahrungsaustausch.</p>
        </div>
        <div class="card-action">
          <a class="btn-action" href="https://www.amigans.net">&Ouml;ffnen</a>
        </div>
      </div>
    </div>

    <div class="card">
      <div class="card-top card-top-green"></div>
      <div class="card-body">
        <div>
          <h3>Aminet</h3>
          <p>Das weltgr&ouml;&szlig;te Amiga-Softwarearchiv f&uuml;r Classic, PPC und AmigaOS 4.</p>
        </div>
        <div class="card-action">
          <a class="btn-action" href="https://aminet.net">&Ouml;ffnen</a>
        </div>
      </div>
    </div>

    <div class="card">
      <div class="card-top" style="background: linear-gradient(135deg, #0ea5e9, #0284c7);"></div>
      <div class="card-body">
        <div>
          <h3>A1k.org Amiga Forum</h3>
          <p>Das gro&szlig;e deutschsprachige Forum rund um klassische Amigas und AmigaOS.</p>
        </div>
        <div class="card-action">
          <a class="btn-action" href="https://www.a1k.org">&Ouml;ffnen</a>
        </div>
      </div>
    </div>

    <div class="card">
      <div class="card-top" style="background: linear-gradient(135deg, #64748b, #334155);"></div>
      <div class="card-body">
        <div>
          <h3>Wikipedia (DE)</h3>
          <p>Die freie Enzyklop&auml;die mit Millionen Artikeln auf Deutsch.</p>
        </div>
        <div class="card-action">
          <a class="btn-action" href="https://de.wikipedia.org">&Ouml;ffnen</a>
        </div>
      </div>
    </div>

    <div class="card">
      <div class="card-top card-top-red"></div>
      <div class="card-body">
        <div>
          <h3>YouTube</h3>
          <p>Videos &amp; Musik mit direkter 44100Hz AHI-Stereo-Ton&uuml;bertragung (CD-Qualit&auml;t) auf die Workbench.</p>
        </div>
        <div class="card-action">
          <a class="btn-action" href="https://www.youtube.com">&Ouml;ffnen</a>
          <a class="btn-action" href="https://m.youtube.com" style="margin-left: 6px; background: linear-gradient(180deg, #d32f2f, #9a0007); color: #fff;">Lite / Mobil</a>
        </div>
      </div>
    </div>

    <div class="card">
      <div class="card-top" style="background: linear-gradient(135deg, #9146ff, #5c16c5);"></div>
      <div class="card-body">
        <div>
          <h3>Twitch</h3>
          <p>Live-Streaming mit Turbo-H.264 &amp; 30 FPS Optimierung. Ton direkt via AHI Stereo.</p>
        </div>
        <div class="card-action">
          <a class="btn-action" href="https://www.twitch.tv">&Ouml;ffnen</a>
          <a class="btn-action" href="https://m.twitch.tv" style="margin-left: 6px; background: linear-gradient(180deg, #9146ff, #5c16c5); color: #fff;">Lite / Mobil</a>
        </div>
      </div>
    </div>

    <div class="card">
      <div class="card-top card-top-purple"></div>
      <div class="card-body">
        <div>
          <h3>AHI Sound &amp; Klick-Test</h3>
          <p>Testet AHI 16-Bit Stereo-Ausgabe &uuml;ber PipeWire und direkte Dirty-Slice Bildaktualisierung.</p>
        </div>
        <div class="card-action" style="flex-direction: column; align-items: flex-end;">
          <button class="btn-action" id="btn-test" onclick="window._clicks=(window._clicks||0)+1; playAmigaSynth(); document.getElementById('click-status').textContent='Ton + Klick OK! ('+window._clicks+'x)';">Ton &amp; Klick-Test</button>
          <div id="click-status">Bereit f&uuml;r AHI-Sound.</div>
        </div>
      </div>
    </div>
  </div>

  <div class="dl-strip">
    <div>
      <h3>ASL Download &amp; Kontextmen&uuml;-Test (Rechtsklick auf den Button!)</h3>
      <p>Rechte Maustaste auf den Button &rarr; &bdquo;Verkn&uuml;pftes Ziel speichern unter...&ldquo; &ouml;ffnet den nativen AmigaOS 4 ASL FileRequester.</p>
    </div>
    <a class="btn-amber" id="btn-download" href="/download/VortexBrowse_TestPackage.lha">Test-Paket herunterladen (.lha)</a>
  </div>
</body>
</html>
"""


def _unwrap_ddg_url(raw_href: str) -> str:
    href = html.unescape(raw_href.strip())
    if "uddg=" in href:
        try:
            qpart = href.split("uddg=", 1)[1].split("&", 1)[0]
            return unquote(qpart)
        except Exception:
            pass
    if href.startswith("//"):
        return "https:" + href
    return href


_BARE_LOCALE_MAP: Dict[str, str] = {
    "de": "de-DE", "deutsch": "de-DE", "german": "de-DE",
    "fr": "fr-FR", "francais": "fr-FR", "french": "fr-FR",
    "it": "it-IT", "italiano": "it-IT", "italian": "it-IT",
    "es": "es-ES", "espanol": "es-ES", "spanish": "es-ES",
    "en": "en-US", "english": "en-US",
    "nl": "nl-NL", "dutch": "nl-NL", "nederlands": "nl-NL",
    "pl": "pl-PL", "polish": "pl-PL", "polski": "pl-PL",
    "pt": "pt-PT", "portuguese": "pt-PT", "portugues": "pt-PT",
    "sv": "sv-SE", "swedish": "sv-SE", "svenska": "sv-SE",
    "da": "da-DK", "danish": "da-DK", "dansk": "da-DK",
    "fi": "fi-FI", "finnish": "fi-FI", "suomi": "fi-FI",
    "no": "nb-NO", "nb": "nb-NO", "norwegian": "nb-NO", "norsk": "nb-NO",
    "cs": "cs-CZ", "czech": "cs-CZ", "cestina": "cs-CZ",
    "hu": "hu-HU", "hungarian": "hu-HU", "magyar": "hu-HU",
    "el": "el-GR", "greek": "el-GR",
    "tr": "tr-TR", "turkish": "tr-TR", "turkce": "tr-TR",
    "ru": "ru-RU", "russian": "ru-RU",
    "ja": "ja-JP", "japanese": "ja-JP",
    "ko": "ko-KR", "korean": "ko-KR",
    "zh": "zh-CN", "chinese": "zh-CN",
}
_LOCALE_TAG_RE = re.compile(r"^[a-z]{2,3}(?:-[A-Z0-9]{2,4})?$")


def normalize_bcp47_locale(raw: Optional[str]) -> Optional[str]:
    """Normalizes POSIX/OS locale strings (e.g. 'fr_FR.UTF-8', 'francais') to strict BCP-47 tags."""
    if not raw:
        return None
    s = str(raw).strip().strip('"').strip("'")
    if not s:
        return None
    s = s.split("@", 1)[0].split(".", 1)[0].strip()
    if not s:
        return None
    low = s.lower()
    if low in ("c", "posix"):
        return None
    if low in _BARE_LOCALE_MAP:
        return _BARE_LOCALE_MAP[low]
    parts = [p for p in re.split(r"[-_]", s) if p]
    if not parts:
        return None
    lang = parts[0].lower()
    if len(parts) == 1:
        return _BARE_LOCALE_MAP.get(lang, lang if re.fullmatch(r"[a-z]{2,3}", lang) else None)
    # Take primary language + region (skip intermediate script subtag like zh_Hans_CN -> zh-CN)
    region = parts[-1].upper()
    candidate = f"{lang}-{region}"
    if _LOCALE_TAG_RE.fullmatch(candidate):
        return candidate
    return None


def detect_system_locale(explicit_locale: str = "") -> Tuple[str, bool]:
    """Detects host OS locale unless explicitly overridden. Returns (bcp47_tag, is_explicit)."""
    explicit_raw = (explicit_locale or "").strip() or os.environ.get("VORTEX_LOCALE", "").strip()
    if explicit_raw:
        norm = normalize_bcp47_locale(explicit_raw)
        if norm:
            return norm, True

    for env_key in ("LC_ALL", "LC_MESSAGES", "LANG", "LANGUAGE"):
        val = os.environ.get(env_key, "").strip()
        if not val:
            continue
        if env_key == "LANGUAGE":
            val = val.split(":", 1)[0]
        norm = normalize_bcp47_locale(val)
        if norm:
            return norm, False

    for cfg_path in ("/etc/locale.conf", "/etc/default/locale"):
        if os.path.isfile(cfg_path):
            try:
                with open(cfg_path, "r", encoding="utf-8", errors="ignore") as f:
                    vals: Dict[str, str] = {}
                    for line in f:
                        line = line.strip()
                        if not line or line.startswith("#") or "=" not in line:
                            continue
                        k, v = line.split("=", 1)
                        vals[k.strip()] = v.strip().strip('"').strip("'")
                for k in ("LC_ALL", "LC_MESSAGES", "LANG"):
                    norm = normalize_bcp47_locale(vals.get(k, ""))
                    if norm:
                        return norm, False
            except Exception:
                pass

    if platform.system() == "Darwin":
        try:
            res = subprocess.run(
                ["defaults", "read", "-g", "AppleLocale"],
                capture_output=True, text=True, timeout=2.0,
            )
            if res.returncode == 0:
                norm = normalize_bcp47_locale(res.stdout.strip())
                if norm:
                    return norm, False
        except Exception:
            pass

    return "en-US", False


def build_accept_language(bcp47: str) -> str:
    norm = normalize_bcp47_locale(bcp47) or "en-US"
    base = norm.split("-", 1)[0].lower()
    if norm == "en-US":
        return "en-US,en;q=0.9"
    if base == "en":
        return f"{norm},en;q=0.9,en-US;q=0.8"
    if "-" in norm:
        return f"{norm},{base};q=0.9,en-US;q=0.8,en;q=0.7"
    return f"{norm},en-US;q=0.8,en;q=0.7"


def build_navigator_languages(bcp47: str) -> List[str]:
    norm = normalize_bcp47_locale(bcp47) or "en-US"
    base = norm.split("-", 1)[0].lower()
    out: List[str] = [norm]
    if base not in out:
        out.append(base)
    if base != "en":
        out.extend(["en-US", "en"])
    elif norm != "en-US":
        out.append("en-US")
    return out


_SERVER_LOCALE, _LOCALE_EXPLICIT = detect_system_locale()


def _fetch_search_results_sync(query: str) -> str:
    q_esc = html.escape(query)
    items_html = []
    try:
        url = "https://html.duckduckgo.com/html/?" + urlencode({"q": query})
        req = urllib.request.Request(
            url,
            headers={
                "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:128.0) Gecko/20100101 Firefox/128.0",
                "Accept": "text/html,application/xhtml+xml",
                "Accept-Language": build_accept_language(_SERVER_LOCALE),
            },
        )
        with urllib.request.urlopen(req, timeout=8) as resp:
            page_html = resp.read().decode("utf-8", errors="replace")

        blocks = page_html.split('class="result__body"')
        for blk in blocks[1:16]:
            m_link = re.search(r'<a[^>]+class="result__a"[^>]+href="([^"]+)"[^>]*>(.*?)</a>', blk, re.S)
            if not m_link:
                continue
            real_url = _unwrap_ddg_url(m_link.group(1))
            if not real_url.startswith("http"):
                continue
            title_clean = re.sub(r"<[^>]+>", "", m_link.group(2)).strip()
            m_snip = re.search(r'<a[^>]+class="result__snippet"[^>]*>(.*?)</a>', blk, re.S)
            snippet_clean = re.sub(r"<[^>]+>", "", m_snip.group(1)).strip() if m_snip else ""
            u_esc = html.escape(real_url)
            items_html.append(
                f'<div class="res-card">'
                f'<a class="res-title" href="{u_esc}">{title_clean}</a>'
                f'<div class="res-url">{u_esc}</div>'
                f'<div class="res-snip">{snippet_clean}</div>'
                f'</div>'
            )
    except Exception as exc:
        logger.warning("Search fetch error for '%s': %s", query, exc)

    if not items_html:
        items_html.append(
            f'<div class="res-card">'
            f'<div class="res-snip">Keine direkten Treffer gefunden. Direkt weiter zu: '
            f'<a class="res-title" href="https://www.google.de/search?q={quote_plus(query)}">Google-Suche nach &bdquo;{q_esc}&ldquo;</a></div>'
            f'</div>'
        )

    results_joined = "\n".join(items_html)
    return f"""<!DOCTYPE html>
<html lang="de">
<head>
<meta charset="utf-8">
<title>VortexBrowser Suche: {q_esc}</title>
<style>
  * {{ box-sizing: border-box; margin: 0; padding: 0; }}
  body {{
    font-family: 'DejaVu Sans', 'Liberation Sans', Arial, Helvetica, sans-serif;
    background: linear-gradient(180deg, #e6eaf0 0%, #d3dae4 100%);
    color: #1e293b;
    padding: 16px 22px;
  }}
  .top-bar {{
    display: flex;
    gap: 10px;
    align-items: center;
    margin-bottom: 14px;
    background: linear-gradient(180deg, #375375 0%, #20354f 100%);
    border: 1px solid #1a2c42;
    border-radius: 6px;
    padding: 10px 14px;
    box-shadow: 0 2px 4px rgba(0,0,0,0.18);
  }}
  .home-btn {{
    background: linear-gradient(180deg, #f1f5f9 0%, #cbd5e1 100%);
    border: 1px solid #64748b;
    color: #0f172a;
    padding: 7px 14px;
    border-radius: 4px;
    text-decoration: none;
    font-weight: bold;
    font-size: 13px;
    box-shadow: 0 1px 2px rgba(0,0,0,0.12);
  }}
  .home-btn:hover {{
    background: linear-gradient(180deg, #ffffff 0%, #e2e8f0 100%);
  }}
  .search-form {{
    display: flex;
    flex: 1;
    gap: 8px;
  }}
  .search-form input {{
    flex: 1;
    padding: 8px 12px;
    border-radius: 4px;
    border: 1px solid #cbd5e1;
    background: #ffffff;
    color: #0f172a;
    font-size: 14px;
  }}
  .search-form button {{
    padding: 8px 16px;
    border-radius: 4px;
    border: 1px solid #64748b;
    background: linear-gradient(180deg, #f1f5f9 0%, #cbd5e1 100%);
    color: #0f172a;
    font-weight: bold;
    font-size: 13px;
    cursor: pointer;
  }}
  .search-form button:hover {{
    background: linear-gradient(180deg, #ffffff 0%, #e2e8f0 100%);
  }}
  .res-card {{
    background: #ffffff;
    border: 1px solid #94a3b8;
    border-radius: 6px;
    padding: 12px 16px;
    margin-bottom: 10px;
    box-shadow: 0 2px 4px rgba(0,0,0,0.06);
  }}
  .res-title {{
    color: #1e40af;
    font-size: 16px;
    font-weight: bold;
    text-decoration: none;
    display: block;
    margin-bottom: 3px;
  }}
  .res-title:hover {{ text-decoration: underline; }}
  .res-url {{
    color: #15803d;
    font-size: 12px;
    margin-bottom: 5px;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }}
  .res-snip {{
    color: #334155;
    font-size: 13px;
    line-height: 1.4;
  }}
</style>
</head>
<body>
  <div class="top-bar">
    <a class="home-btn" href="/">Schnellwahl</a>
    <form class="search-form" onsubmit="event.preventDefault(); const q=document.getElementById('q').value.trim(); if(!q) return; if(q.startsWith('http://')||q.startsWith('https://')) window.location.href=q; else if(q.includes('.') && !q.includes(' ')) window.location.href='https://'+q; else window.location.href='/search?q='+encodeURIComponent(q);">
      <input id="q" type="text" value="{q_esc}" placeholder="Suchbegriff oder URL eingeben..." />
      <button type="submit">Suchen</button>
    </form>
  </div>
  {results_joined}
</body>
</html>"""


class LocalPortalHTTPServer:
    """Lightweight asyncio HTTP server providing the start portal, CAPTCHA-free web search, and a test download file."""

    def __init__(self, host: str = "127.0.0.1", port: int = 8089):
        self.host = host
        self.port = port
        self.server: Optional[asyncio.AbstractServer] = None

    async def start(self) -> str:
        candidates = [self.port] if self.port else []
        candidates.extend([8090, 8091, 8092, 8093, 8094, 0])
        last_err = None
        for p in candidates:
            try:
                self.server = await asyncio.start_server(self._handle_http, self.host, p)
                sockets = self.server.sockets
                if sockets:
                    self.port = sockets[0].getsockname()[1]
                url = f"http://{self.host}:{self.port}/"
                logger.info("Local portal HTTP server listening on %s", url)
                return url
            except OSError as exc:
                last_err = exc
                logger.info("Port %s busy for local portal (%s), trying alternative...", p, exc)
                continue
        if last_err:
            raise last_err
        raise OSError("Could not bind local portal HTTP server on any port")


    async def stop(self) -> None:
        if self.server:
            self.server.close()
            await self.server.wait_closed()

    async def _handle_http(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        try:
            req_line = await asyncio.wait_for(reader.readline(), timeout=5.0)
            if not req_line:
                writer.close()
                return
            parts = req_line.decode("utf-8", errors="replace").strip().split(" ")
            path = parts[1] if len(parts) > 1 else "/"

            while True:
                line = await asyncio.wait_for(reader.readline(), timeout=5.0)
                if not line or line in (b"\r\n", b"\n"):
                    break

            if path.startswith("/download/"):
                filename = os.path.basename(path) or "VortexBrowse_Test.bin"
                header_txt = (
                    f"VortexBrowse AmigaOS 4 Download Proxy Test File ({filename})\n"
                    "Streamed in 64KB chunks over MSG_DOWNLOAD_CHUNK.\n"
                ).encode("utf-8")
                pattern = b"AMIGAOS4-VORTEXBROWSE-LZ4-STREAM-OK\n" * 4500
                body = (header_txt + pattern)[:163840]

                headers = (
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: application/octet-stream\r\n"
                    f'Content-Disposition: attachment; filename="{filename}"\r\n'
                    f"Content-Length: {len(body)}\r\n"
                    "Connection: close\r\n\r\n"
                ).encode("utf-8")
                writer.write(headers + body)
                await writer.drain()
            elif path.startswith("/search"):
                parsed_p = urlparse(path)
                qs = parse_qs(parsed_p.query)
                query = (qs.get("q") or [""])[0].strip()
                loop = asyncio.get_running_loop()
                html_str = await loop.run_in_executor(None, _fetch_search_results_sync, query)
                body = html_str.encode("utf-8")
                headers = (
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/html; charset=utf-8\r\n"
                    f"Content-Length: {len(body)}\r\n"
                    "Connection: close\r\n\r\n"
                ).encode("utf-8")
                writer.write(headers + body)
                await writer.drain()
            else:
                body = PORTAL_HTML.encode("utf-8")
                headers = (
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/html; charset=utf-8\r\n"
                    f"Content-Length: {len(body)}\r\n"
                    "Connection: close\r\n\r\n"
                ).encode("utf-8")
                writer.write(headers + body)
                await writer.drain()
        except Exception as exc:
            logger.debug("Portal HTTP handler error: %s", exc)
        finally:
            try:
                writer.close()
                await writer.wait_closed()
            except Exception:
                pass


# ============================================================================
# Native SIMD Pixel Converter & Dirty-Slice Detector (x86_64 & ARM64/RPi4)
# ============================================================================
_FAST_PIXCONV = None
_HAS_NATIVE_PACKET_ENCODER = False
_HAS_NATIVE_YUV_DECODER = False

def _bind_fast_pixconv_symbols(_lib) -> bool:
    global _HAS_NATIVE_PACKET_ENCODER, _HAS_NATIVE_YUV_DECODER
    _lib.rgb24_to_be_argb32.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_size_t]
    _lib.rgb24_to_be_argb32.restype = None
    _lib.rgb24_to_be_rgb565.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_size_t]
    _lib.rgb24_to_be_rgb565.restype = None
    _lib.rgb24_to_yuv420p.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int, ctypes.c_int]
    _lib.rgb24_to_yuv420p.restype = None
    _lib.find_and_update_dirty_slice.argtypes = [
        ctypes.c_char_p,
        ctypes.c_char_p,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.POINTER(ctypes.c_int),
        ctypes.POINTER(ctypes.c_int),
    ]
    _lib.find_and_update_dirty_slice.restype = ctypes.c_int
    _lib.find_and_update_dirty_rect.argtypes = [
        ctypes.c_char_p,
        ctypes.c_char_p,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.POINTER(ctypes.c_int),
        ctypes.POINTER(ctypes.c_int),
        ctypes.POINTER(ctypes.c_int),
        ctypes.POINTER(ctypes.c_int),
    ]
    _lib.find_and_update_dirty_rect.restype = ctypes.c_int
    _lib.pack_sub_rect.argtypes = [
        ctypes.c_char_p,
        ctypes.c_char_p,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
    ]
    _lib.pack_sub_rect.restype = None
    if hasattr(_lib, "pack_sub_rect_yuv420p"):
        _lib.pack_sub_rect_yuv420p.argtypes = [
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_int,
        ]
        _lib.pack_sub_rect_yuv420p.restype = None
    _lib.pcm_s16be_is_silent.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_int]
    _lib.pcm_s16be_is_silent.restype = ctypes.c_int
    if hasattr(_lib, "fast_pixconv_release_tls"):
        _lib.fast_pixconv_release_tls.argtypes = []
        _lib.fast_pixconv_release_tls.restype = None
    if hasattr(_lib, "decode_jpeg_to_yuv420p"):
        _lib.decode_jpeg_to_yuv420p.argtypes = [
            ctypes.c_char_p,
            ctypes.c_size_t,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_char_p,
        ]
        _lib.decode_jpeg_to_yuv420p.restype = ctypes.c_int
        _HAS_NATIVE_YUV_DECODER = True
    else:
        _HAS_NATIVE_YUV_DECODER = False
    if hasattr(_lib, "encode_jpeg_to_frame_packet"):
        _lib.encode_jpeg_to_frame_packet.argtypes = [
            ctypes.c_char_p,
            ctypes.c_size_t,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.c_int,
        ]
        _lib.encode_jpeg_to_frame_packet.restype = ctypes.c_int
        _HAS_NATIVE_PACKET_ENCODER = True
        return True
    _HAS_NATIVE_PACKET_ENCODER = False
    return False

try:
    _base_dir = os.path.dirname(os.path.abspath(__file__))
    _is_macos = (platform.system() == "Darwin")
    _is_arm = platform.machine().lower().startswith(("aarch64", "arm"))
    if _is_macos:
        _candidates = ["fast_pixconv.dylib", "fast_pixconv_arm64.dylib", "fast_pixconv.so"]
    elif _is_arm:
        _candidates = ["fast_pixconv_aarch64.so", "fast_pixconv.so"]
    else:
        _candidates = ["fast_pixconv.so", "fast_pixconv_aarch64.so"]
    _loaded_path = None
    for _so_name in _candidates:
        _candidate_path = os.path.join(_base_dir, _so_name)
        if os.path.exists(_candidate_path):
            try:
                _lib = ctypes.CDLL(_candidate_path)
                _bind_fast_pixconv_symbols(_lib)
                _FAST_PIXCONV = _lib
                _loaded_path = _candidate_path
                break
            except Exception as _cdll_err:
                logger.debug("Could not load candidate %s: %s", _candidate_path, _cdll_err)

    # If not found or missing C-native TurboJPEG+LZ4 packet encoder, attempt auto-compilation
    if _FAST_PIXCONV is None or not _HAS_NATIVE_PACKET_ENCODER:
        _c_src = os.path.join(_base_dir, "fast_pixconv.c")
        if os.path.exists(_c_src):
            if _is_macos:
                _target_so_name = "fast_pixconv.dylib"
            else:
                _target_so_name = "fast_pixconv_aarch64.so" if _is_arm else "fast_pixconv.so"
            _target_so_path = os.path.join(_base_dir, _target_so_name)
            try:
                logger.info("Attempting auto-compilation of native SIMD + TurboJPEG + LZ4 helper (%s)...", _target_so_name)
                if _is_macos:
                    _hb_prefix = "/opt/homebrew" if os.path.isdir("/opt/homebrew") else "/usr/local"
                    _hb_tj = f"{_hb_prefix}/opt/jpeg-turbo"
                    _hb_lz4 = f"{_hb_prefix}/opt/lz4"
                    _inc_flags = [f"-I{_hb_prefix}/include", f"-I{_hb_tj}/include", f"-I{_hb_lz4}/include"]
                    _lib_flags = [f"-L{_hb_prefix}/lib", f"-L{_hb_tj}/lib", f"-L{_hb_lz4}/lib"]
                    _arch_flags = ["-mcpu=apple-m1"] if _is_arm else ["-march=native"]
                    _compile_cmds = [
                        ["clang", "-O3", *_arch_flags, *_inc_flags, *_lib_flags, "-DHAVE_TURBOJPEG_LZ4", "-dynamiclib", "-fPIC", "-o", _target_so_path, _c_src, "-lturbojpeg", "-llz4"],
                        ["clang", "-O3", *_arch_flags, "-dynamiclib", "-fPIC", "-o", _target_so_path, _c_src],
                    ]
                elif _is_arm:
                    _compile_cmds = [
                        ["gcc", "-O3", "-march=armv8-a+crc", "-mtune=cortex-a72", "-fopenmp", "-DHAVE_TURBOJPEG_LZ4", "-shared", "-fPIC", "-o", _target_so_path, _c_src, "-lturbojpeg", "-llz4"],
                        ["gcc", "-O3", "-march=armv8-a+crc", "-mtune=cortex-a72", "-shared", "-fPIC", "-o", _target_so_path, _c_src],
                    ]
                else:
                    _compile_cmds = [
                        ["gcc", "-O3", "-march=native", "-mavx2", "-mfma", "-fopenmp", "-DHAVE_TURBOJPEG_LZ4", "-shared", "-fPIC", "-o", _target_so_path, _c_src, "-lturbojpeg", "-llz4"],
                        ["gcc", "-O3", "-march=native", "-fopenmp", "-DHAVE_TURBOJPEG_LZ4", "-shared", "-fPIC", "-o", _target_so_path, _c_src, "-lturbojpeg", "-llz4"],
                        ["gcc", "-O3", "-march=native", "-shared", "-fPIC", "-o", _target_so_path, _c_src],
                    ]
                for _cmd in _compile_cmds:
                    _ret = subprocess.run(_cmd, capture_output=True, timeout=15)
                    if _ret.returncode == 0 and os.path.exists(_target_so_path):
                        _lib = ctypes.CDLL(_target_so_path)
                        _bind_fast_pixconv_symbols(_lib)
                        _FAST_PIXCONV = _lib
                        _loaded_path = _target_so_path
                        break
            except Exception as _comp_err:
                logger.debug("Auto-compilation failed: %s", _comp_err)

    if _FAST_PIXCONV is not None:
        logger.info(
            "Loaded native SIMD pixel & audio helper: %s (arch=%s, C-TurboJPEG+LZ4=%s)",
            os.path.basename(_loaded_path),
            platform.machine(),
            "YES" if _HAS_NATIVE_PACKET_ENCODER else "NO",
        )
    else:
        logger.warning("No native fast_pixconv found, falling back to pure Python/Pillow")
except Exception as _exc:
    logger.warning("Could not initialize native fast_pixconv helper: %s", _exc)

_TURBO_JPEG = None
try:
    from turbojpeg import TurboJPEG, TJPF_RGB, TJSAMP_420
    _TURBO_JPEG = TurboJPEG()
    logger.info("Loaded native libjpeg-turbo SIMD decoder (ultra-fast direct JPEG/YUV pipeline)")
except Exception as _tj_init_err:
    logger.debug("TurboJPEG initialization skipped (%s), using Pillow fallback", _tj_init_err)
    _TURBO_JPEG = None



def image_to_big_endian_pixels(
    img: Image.Image,
    width: int,
    height: int,
    fmt: int,
    argb_buf: Optional[ctypes.Array] = None,
    rgb565_buf: Optional[ctypes.Array] = None,
    yuv_buf: Optional[ctypes.Array] = None,
) -> bytes:
    """Convert an image to Big-Endian pixel bytes.

    When pre-allocated buffers are passed, they are reused across frames instead of
    allocating new ctypes buffers on every frame (major allocation-churn win).
    """
    if img.size != (width, height):
        img = img.resize((width, height), Image.Resampling.NEAREST)

    rgb_img = img.convert("RGB") if img.mode != "RGB" else img
    num_pixels = width * height

    if _FAST_PIXCONV is not None:
        raw_rgb = rgb_img.tobytes()
        if fmt == FMT_ARGB32:
            if argb_buf is None or len(argb_buf) != num_pixels * 4:
                argb_buf = ctypes.create_string_buffer(num_pixels * 4)
            _FAST_PIXCONV.rgb24_to_be_argb32(raw_rgb, argb_buf, num_pixels)
            return argb_buf.raw
        elif fmt == FMT_RGB565:
            if rgb565_buf is None or len(rgb565_buf) != num_pixels * 2:
                rgb565_buf = ctypes.create_string_buffer(num_pixels * 2)
            _FAST_PIXCONV.rgb24_to_be_rgb565(raw_rgb, rgb565_buf, num_pixels)
            return rgb565_buf.raw
        elif fmt == FMT_YUV420P:
            if yuv_buf is None or len(yuv_buf) != (num_pixels * 3) // 2:
                yuv_buf = ctypes.create_string_buffer((num_pixels * 3) // 2)
            _FAST_PIXCONV.rgb24_to_yuv420p(raw_rgb, yuv_buf, width, height)
            return yuv_buf.raw

    if fmt == FMT_ARGB32:
        r, g, b = rgb_img.split()
        a = Image.new("L", (width, height), 255)
        argb_img = Image.merge("RGBA", (a, r, g, b))
        return argb_img.tobytes()
    elif fmt == FMT_RGB565:
        raw_rgb = rgb_img.tobytes()
        out = bytearray(num_pixels * 2)
        idx_out = 0
        for idx_in in range(0, len(raw_rgb), 3):
            r = raw_rgb[idx_in]
            g = raw_rgb[idx_in + 1]
            b = raw_rgb[idx_in + 2]
            val = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            out[idx_out] = (val >> 8) & 0xFF
            out[idx_out + 1] = val & 0xFF
            idx_out += 2
        return bytes(out)
    elif fmt == FMT_YUV420P:
        ycbcr = rgb_img.convert("YCbCr")
        y, cb, cr = ycbcr.split()
        half_w, half_h = width // 2, height // 2
        cb_sub = cb.resize((half_w, half_h), Image.Resampling.BILINEAR)
        cr_sub = cr.resize((half_w, half_h), Image.Resampling.BILINEAR)
        return y.tobytes() + cb_sub.tobytes() + cr_sub.tobytes()
    else:
        raise ValueError(f"Unsupported pixel format: {fmt}")


# ============================================================================
# Memory Reclamation & Safe CDP Helpers
# ============================================================================
def _reclaim_python_memory() -> None:
    gc.collect()
    if platform.system() == "Linux":
        try:
            ctypes.CDLL("libc.so.6").malloc_trim(0)
        except Exception:
            pass


async def _safe_detach_cdp_session(cdp_obj: Any) -> None:
    if cdp_obj is None:
        return
    detach_fn = getattr(cdp_obj, "detach", None)
    if not callable(detach_fn):
        return
    try:
        res = detach_fn()
        if inspect.isawaitable(res):
            await asyncio.wait_for(res, timeout=1.0)
    except Exception:
        pass


_VIDEO_PLATFORM_URL_TOKENS = (
    "youtube.com/watch",
    "youtu.be/",
    "twitch.tv/",
    "/watch?v=",
    "vimeo.com/",
    "dailymotion.com/",
    "ardmediathek.de",
    "zdf.de",
    "kick.com/",
    "netflix.com/watch",
    "primevideo.com",
    "disneyplus.com",
)


def _is_video_platform_url(url: str) -> bool:
    """Returns True when URL matches a known video-streaming platform path."""
    lower = (url or "").lower()
    return bool(lower) and any(tok in lower for tok in _VIDEO_PLATFORM_URL_TOKENS)


# ============================================================================
# DirectCDP Session (Direct WebSocket to Chromium DevTools Protocol)
# ============================================================================
class DirectCDPSession:
    """Direct async WebSocket client for Chromium DevTools Protocol (DirectCDP).
    Bypasses Playwright / Driver IPC completely for zero-overhead Page.screencastFrame
    push streaming, Page.captureScreenshot, and low-latency CDP commands.
    """

    def __init__(self, ws_url: str, fallback_cdp: Optional[CDPSession] = None):
        self.ws_url = ws_url
        self.fallback_cdp = fallback_cdp
        self.ws = None
        self._msg_id = 0
        self._pending: Dict[int, asyncio.Future] = {}
        self._listeners: Dict[str, List] = {}
        self._reader_task: Optional[asyncio.Task] = None
        self.connected = False

    async def connect(self) -> bool:
        if not _HAS_WEBSOCKETS or not self.ws_url:
            return False
        try:
            self.ws = await asyncio.wait_for(
                websockets.connect(
                    self.ws_url,
                    max_size=32 * 1024 * 1024,
                    ping_interval=None,
                    compression=None,
                ),
                timeout=2.0,
            )
            self.connected = True
            self._reader_task = asyncio.create_task(self._read_loop())
            logger.info("DirectCDP WebSocket connected: %s", self.ws_url)
            return True
        except Exception as exc:
            logger.debug("DirectCDP connect fallback (%s): %s", self.ws_url, exc)
            self.connected = False
            return False

    async def _read_loop(self) -> None:
        try:
            async for raw_msg in self.ws:
                try:
                    data = json.loads(raw_msg)
                except Exception:
                    continue
                msg_id = data.get("id")
                if msg_id is not None:
                    fut = self._pending.pop(msg_id, None)
                    if fut and not fut.done():
                        if "error" in data:
                            fut.set_exception(RuntimeError(str(data["error"])))
                        else:
                            fut.set_result(data.get("result", {}))
                else:
                    method = data.get("method")
                    if method and method in self._listeners:
                        params = data.get("params", {})
                        for cb in list(self._listeners.get(method, [])):
                            try:
                                cb(params)
                            except Exception:
                                pass
        except Exception:
            pass
        finally:
            self.connected = False
            for fut in self._pending.values():
                if not fut.done():
                    fut.set_exception(RuntimeError("DirectCDP WebSocket closed"))
            self._pending.clear()

    def on(self, event: str, callback) -> None:
        self._listeners.setdefault(event, []).append(callback)
        if not self.connected and self.fallback_cdp:
            try:
                self.fallback_cdp.on(event, callback)
            except Exception:
                pass

    def remove_listener(self, event: str, callback) -> None:
        if event in self._listeners:
            try:
                self._listeners[event].remove(callback)
            except ValueError:
                pass
        if self.fallback_cdp:
            try:
                self.fallback_cdp.remove_listener(event, callback)
            except Exception:
                pass

    async def send(self, method: str, params: Optional[ dict ] = None) -> dict:
        if self.connected and self.ws is not None:
            self._msg_id += 1
            mid = self._msg_id
            loop = asyncio.get_running_loop()
            fut = loop.create_future()
            self._pending[mid] = fut
            payload = json.dumps({"id": mid, "method": method, "params": params or {}})
            try:
                await self.ws.send(payload)
                return await fut
            except Exception:
                self._pending.pop(mid, None)
                if self.fallback_cdp is not None:
                    return await self.fallback_cdp.send(method, params or {})
                raise
            except BaseException:
                self._pending.pop(mid, None)
                raise
        if self.fallback_cdp is not None:
            return await self.fallback_cdp.send(method, params or {})
        raise RuntimeError("DirectCDP not connected and no fallback CDP session")

    async def close(self) -> None:
        self.connected = False
        if self._reader_task is not None:
            if not self._reader_task.done():
                self._reader_task.cancel()
                try:
                    await self._reader_task
                except (asyncio.CancelledError, Exception):
                    pass
            self._reader_task = None
        if self.ws is not None:
            try:
                await self.ws.close()
            except Exception:
                pass
            self.ws = None
        for fut in self._pending.values():
            if not fut.done():
                fut.set_exception(RuntimeError("DirectCDP WebSocket closed"))
        self._pending.clear()
        self._listeners.clear()
        if self.fallback_cdp is not None:
            fb = self.fallback_cdp
            self.fallback_cdp = None
            await _safe_detach_cdp_session(fb)


# ============================================================================
# H.264 Live Stream Hardware/Software Encoder Pipe (va.library / VA-API)
# ============================================================================
class H264LiveStreamEncoder:
    """Ultra-low latency H.264 live stream encoder using an asynchronous ffmpeg pipe.
    Probes hardware VA-API render nodes (all /dev/dri/renderD* devices) or VideoToolbox
    once per process and cleanly falls back to libx264 / libopenh264 without crash loops.
    """
    _cached_backend: Optional[Tuple[str, Optional[str]]] = None

    @classmethod
    def _test_ffmpeg_cmd(cls, cmd: List[str], frame_bytes: bytes) -> bool:
        try:
            res = subprocess.run(
                cmd,
                input=frame_bytes,
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
                timeout=4,
            )
            return res.returncode == 0 and len(res.stdout) > 0
        except Exception:
            return False

    @classmethod
    def _probe_backend(cls) -> Tuple[str, Optional[str]]:
        if cls._cached_backend is not None:
            return cls._cached_backend

        test_w, test_h = 128, 128
        test_yuv = b"\x80" * (test_w * test_h * 3 // 2)

        if platform.system() == "Darwin":
            if cls._test_ffmpeg_cmd([
                "ffmpeg", "-y", "-loglevel", "error",
                "-f", "rawvideo", "-pix_fmt", "yuv420p", "-s", f"{test_w}x{test_h}", "-r", "25", "-i", "-",
                "-frames:v", "1", "-c:v", "h264_videotoolbox", "-realtime", "true", "-allow_sw", "1", "-f", "h264", "-"
            ], test_yuv):
                cls._cached_backend = ("videotoolbox", None)
                logger.info("H.264 Encoder Backend: h264_videotoolbox (Apple hardware)")
                return cls._cached_backend
        else:
            for node in sorted(glob.glob("/dev/dri/renderD*")):
                if cls._test_ffmpeg_cmd([
                    "ffmpeg", "-y", "-loglevel", "error",
                    "-init_hw_device", f"vaapi=va:{node}", "-filter_hw_device", "va",
                    "-f", "rawvideo", "-pix_fmt", "yuv420p", "-s", f"{test_w}x{test_h}", "-r", "25", "-i", "-",
                    "-frames:v", "1", "-vf", "format=nv12,hwupload", "-c:v", "h264_vaapi", "-f", "h264", "-"
                ], test_yuv):
                    cls._cached_backend = ("vaapi", node)
                    logger.info("H.264 Encoder Backend: h264_vaapi (%s)", node)
                    return cls._cached_backend

        if cls._test_ffmpeg_cmd([
            "ffmpeg", "-y", "-loglevel", "error",
            "-f", "rawvideo", "-pix_fmt", "yuv420p", "-s", f"{test_w}x{test_h}", "-r", "25", "-i", "-",
            "-frames:v", "1", "-c:v", "libx264", "-preset", "ultrafast", "-tune", "zerolatency", "-f", "h264", "-"
        ], test_yuv):
            cls._cached_backend = ("libx264", None)
            logger.info("H.264 Encoder Backend: libx264 (CPU ultrafast zerolatency; VA-API H.264 encode unavailable on GPU)")
            return cls._cached_backend

        if cls._test_ffmpeg_cmd([
            "ffmpeg", "-y", "-loglevel", "error",
            "-f", "rawvideo", "-pix_fmt", "yuv420p", "-s", f"{test_w}x{test_h}", "-r", "25", "-i", "-",
            "-frames:v", "1", "-c:v", "libopenh264", "-f", "h264", "-"
        ], test_yuv):
            cls._cached_backend = ("libopenh264", None)
            logger.info("H.264 Encoder Backend: libopenh264 (software fallback)")
            return cls._cached_backend

        cls._cached_backend = ("none", None)
        logger.warning("No working H.264 encoder found in ffmpeg (check VA-API/libx264); using LZ4+DirtySlice.")
        return cls._cached_backend

    def __init__(self, width: int, height: int, fps: int = 25, bitrate_k: int = 8000):
        self.width = width
        self.height = height
        self.fps = max(1, min(60, fps))
        self.bitrate_k = bitrate_k
        self.proc: Optional[subprocess.Popen] = None
        self._fd: Optional[int] = None
        self._backend: str = "none"
        self._device: Optional[str] = None
        self._consecutive_failures: int = 0
        self._start_encoder()

    def _build_cmd(self, backend: str, device: Optional[str]) -> Optional[List[str]]:
        if backend == "videotoolbox":
            return [
                "ffmpeg", "-y", "-loglevel", "error",
                "-f", "rawvideo", "-pix_fmt", "yuv420p",
                "-s", f"{self.width}x{self.height}",
                "-r", str(self.fps),
                "-i", "-",
                "-c:v", "h264_videotoolbox",
                "-realtime", "true",
                "-allow_sw", "1",
                "-b:v", f"{self.bitrate_k}k",
                "-maxrate", f"{self.bitrate_k * 3 // 2}k",
                "-bufsize", f"{self.bitrate_k // 2}k",
                "-g", str(self.fps),
                "-bf", "0",
                "-f", "h264", "-"
            ]
        if backend == "vaapi" and device:
            return [
                "ffmpeg", "-y", "-loglevel", "error",
                "-init_hw_device", f"vaapi=va:{device}",
                "-filter_hw_device", "va",
                "-f", "rawvideo", "-pix_fmt", "yuv420p",
                "-s", f"{self.width}x{self.height}",
                "-r", str(self.fps),
                "-i", "-",
                "-vf", "format=nv12,hwupload",
                "-c:v", "h264_vaapi",
                "-b:v", f"{self.bitrate_k}k",
                "-maxrate", f"{self.bitrate_k * 3 // 2}k",
                "-bufsize", f"{self.bitrate_k // 2}k",
                "-g", str(self.fps),
                "-bf", "0",
                "-f", "h264", "-"
            ]
        if backend == "libx264":
            return [
                "ffmpeg", "-y", "-loglevel", "error",
                "-f", "rawvideo", "-pix_fmt", "yuv420p",
                "-s", f"{self.width}x{self.height}",
                "-r", str(self.fps),
                "-i", "-",
                "-c:v", "libx264",
                "-preset", "ultrafast",
                "-tune", "zerolatency",
                "-threads", "4",
                "-pix_fmt", "yuv420p",
                "-g", str(self.fps),
                "-bf", "0",
                "-x264-params", f"keyint={self.fps}:min-keyint={self.fps}:scenecut=0:repeat-headers=1",
                "-b:v", f"{self.bitrate_k}k",
                "-maxrate", f"{self.bitrate_k * 3 // 2}k",
                "-bufsize", f"{self.bitrate_k // 2}k",
                "-f", "h264", "-"
            ]
        if backend == "libopenh264":
            return [
                "ffmpeg", "-y", "-loglevel", "error",
                "-f", "rawvideo", "-pix_fmt", "yuv420p",
                "-s", f"{self.width}x{self.height}",
                "-r", str(self.fps),
                "-i", "-",
                "-c:v", "libopenh264",
                "-b:v", f"{self.bitrate_k}k",
                "-g", str(self.fps),
                "-f", "h264", "-"
            ]
        return None

    def _start_encoder(self) -> None:
        self.stop()
        backend, device = self._probe_backend()
        if backend == "none":
            self._backend = "none"
            return

        cmd = self._build_cmd(backend, device)
        if not cmd:
            return
        try:
            self.proc = subprocess.Popen(
                cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL
            )
            self._backend = backend
            self._device = device
        except Exception as e:
            logger.warning("Could not launch ffmpeg (%s): %s", backend, e)
            self.proc = None
            return

        if self.proc and self.proc.stdout:
            self._fd = self.proc.stdout.fileno()
            fl = fcntl.fcntl(self._fd, fcntl.F_GETFL)
            fcntl.fcntl(self._fd, fcntl.F_SETFL, fl | os.O_NONBLOCK)

    def _handle_proc_failure(self) -> None:
        self._consecutive_failures += 1
        if self._backend == "vaapi":
            logger.warning("VA-API H.264 encoder failed at runtime on %s; permanently switching to libx264.", self._device)
            H264LiveStreamEncoder._cached_backend = ("libx264", None)
            self._consecutive_failures = 0
            self._start_encoder()
        elif self._consecutive_failures >= 3:
            logger.warning("H.264 encoder (%s) failed repeatedly; disabling H.264 pipe for session.", self._backend)
            H264LiveStreamEncoder._cached_backend = ("none", None)
            self.stop()
            self._backend = "none"
        else:
            self._start_encoder()

    def encode_frame(self, yuv_bytes: Union[bytes, bytearray, memoryview]) -> Optional[bytes]:
        if self._backend == "none":
            return None
        if not self.proc or not self.proc.stdin or self._fd is None:
            return None
        if self.proc.poll() is not None:
            self._handle_proc_failure()
            if not self.proc or not self.proc.stdin or self._fd is None:
                return None
        try:
            self.proc.stdin.write(yuv_bytes)
            self.proc.stdin.flush()
        except (BrokenPipeError, OSError):
            self._handle_proc_failure()
            return None

        # Wait up to 40ms for ffmpeg to output the encoded NAL frame
        r, _, _ = select.select([self._fd], [], [], 0.04)
        chunks = []
        if r:
            for _ in range(30):
                try:
                    chunk = os.read(self._fd, 65536)
                    if not chunk:
                        break
                    chunks.append(chunk)
                except (BlockingIOError, OSError):
                    break
        if chunks:
            self._consecutive_failures = 0
            return b"".join(chunks)
        return None

    def stop(self) -> None:
        if self.proc:
            try:
                if self.proc.stdin:
                    self.proc.stdin.close()
                self.proc.terminate()
                self.proc.wait(timeout=0.2)
            except Exception:
                try:
                    self.proc.kill()
                except Exception:
                    pass
            self.proc = None
            self._fd = None


# ============================================================================
# Persistent Profile, Session Storage & Security Helpers
# ============================================================================
def get_vortex_config_dir() -> str:
    """Returns the user config/profile directory (~/.config/vortexbrowse or $VORTEX_PROFILE_DIR)."""
    env_dir = os.environ.get("VORTEX_PROFILE_DIR", "").strip()
    cfg_dir = os.path.expanduser(env_dir) if env_dir else os.path.expanduser("~/.config/vortexbrowse")
    os.makedirs(cfg_dir, mode=0o700, exist_ok=True)
    return cfg_dir


def get_vortex_profile_dir(engine: str) -> str:
    subdir = "firefox_profile" if engine == "firefox" else "chromium_profile"
    return os.path.join(get_vortex_config_dir(), subdir)


def get_vortex_storage_state_path(engine: str) -> str:
    return os.path.join(get_vortex_config_dir(), f"{engine}_storage_state.json")


def check_profile_needs_recovery(profile_dir: str, state_file: str, engine: str, is_fallback: bool) -> bool:
    """Checks whether backup storage_state recovery is needed (crash, missing DB, or temp fallback)."""
    if not os.path.isfile(state_file):
        return False
    if is_fallback:
        return True
    try:
        state_mtime = os.path.getmtime(state_file)
        if engine == "firefox":
            cookies_db = os.path.join(profile_dir, "cookies.sqlite")
            if not os.path.isfile(cookies_db) or os.path.getsize(cookies_db) < 4096:
                return True
            return state_mtime > (os.path.getmtime(cookies_db) + 2.0)

        cookies_db = os.path.join(profile_dir, "Default", "Cookies")
        if not os.path.isfile(cookies_db) or os.path.getsize(cookies_db) < 4096:
            return True
        prefs_file = os.path.join(profile_dir, "Default", "Preferences")
        if os.path.isfile(prefs_file):
            try:
                with open(prefs_file, "r", encoding="utf-8", errors="ignore") as f:
                    prefs = json.load(f)
                exit_type = (prefs.get("profile") or {}).get("exit_type")
                if exit_type and exit_type != "Normal":
                    return True
            except Exception:
                return True
        return state_mtime > (os.path.getmtime(cookies_db) + 2.0)
    except Exception:
        return False


def _collect_open_origins(context: BrowserContext) -> set[str]:
    origins: set[str] = set()
    try:
        pages = list(getattr(context, "pages", []) or [])
    except Exception:
        pages = []
    for p in pages:
        try:
            if p.is_closed():
                continue
            parsed = urlparse((p.url or "").strip())
            if parsed.scheme in ("http", "https") and parsed.netloc:
                origins.add(f"{parsed.scheme}://{parsed.netloc}")
        except Exception:
            pass
    return origins


def _sanitize_origin_storage(item: Any) -> Optional[Tuple[str, List[Dict[str, str]]]]:
    if not isinstance(item, dict):
        return None
    orig = item.get("origin")
    if not isinstance(orig, str) or not orig:
        return None
    ls_out: List[Dict[str, str]] = []
    origin_bytes = 0
    for entry in item.get("localStorage") or []:
        if not isinstance(entry, dict):
            continue
        k = entry.get("name")
        v = entry.get("value")
        if not isinstance(k, str) or not isinstance(v, str):
            continue
        item_bytes = len(k) + len(v)
        if item_bytes > 65536:
            continue
        if origin_bytes + item_bytes > 131072:
            break
        ls_out.append({"name": k, "value": v})
        origin_bytes += item_bytes
    return orig, ls_out


async def export_merged_storage_state(context: BrowserContext, state_file: str) -> bool:
    """Exports cookies and sanitized localStorage cleanly without bloating IPC/disk via IndexedDB."""
    if not context:
        return False
    try:
        new_state = await asyncio.wait_for(context.storage_state(), timeout=5.0)
        if not isinstance(new_state, dict):
            return False

        prev_state: dict = {}
        if os.path.isfile(state_file):
            try:
                with open(state_file, "r", encoding="utf-8") as f:
                    loaded = json.load(f)
                if isinstance(loaded, dict):
                    prev_state = loaded
            except Exception:
                prev_state = {}

        open_origins = _collect_open_origins(context)
        prev_closed: dict[str, dict] = {}
        for item in prev_state.get("origins") or []:
            san = _sanitize_origin_storage(item)
            if san is None:
                continue
            orig, ls_out = san
            if orig not in open_origins and ls_out:
                prev_closed[orig] = {"origin": orig, "localStorage": ls_out}

        active_new: dict[str, dict] = {}
        other_new: dict[str, dict] = {}
        for item in new_state.get("origins") or []:
            san = _sanitize_origin_storage(item)
            if san is None:
                continue
            orig, ls_out = san
            prev_closed.pop(orig, None)
            if ls_out:
                entry = {"origin": orig, "localStorage": ls_out}
                if orig in open_origins:
                    active_new[orig] = entry
                else:
                    other_new[orig] = entry

        # Cap total retained origins at 64, preferring currently open origins and newest additions
        retained: dict[str, dict] = {}
        for d in (active_new, other_new):
            for k, v in d.items():
                if len(retained) >= 64:
                    break
                retained[k] = v
        if len(retained) < 64 and prev_closed:
            for k, v in reversed(list(prev_closed.items())):
                if len(retained) >= 64:
                    break
                if k not in retained:
                    retained[k] = v

        payload = {
            "cookies": new_state.get("cookies") if isinstance(new_state.get("cookies"), list) else (prev_state.get("cookies") or []),
            "origins": list(retained.values()),
        }

        parent_dir = os.path.dirname(state_file)
        os.makedirs(parent_dir, mode=0o700, exist_ok=True)
        tmp_path = f"{state_file}.tmp.{os.getpid()}"
        with open(tmp_path, "w", encoding="utf-8") as f:
            json.dump(payload, f)
        try:
            os.chmod(tmp_path, 0o600)
        except Exception:
            pass
        os.replace(tmp_path, state_file)
        return True
    except Exception as exc:
        logger.debug("export_merged_storage_state warning: %s", exc)
        return False


def _build_local_storage_recovery_script(origins: list) -> str:
    """Builds an origin-scoped init script to recover missing localStorage keys after crashes."""
    origin_map: Dict[str, Dict[str, str]] = {}
    total_bytes = 0
    for item in origins:
        if not isinstance(item, dict):
            continue
        origin = item.get("origin")
        ls_items = item.get("localStorage") or []
        if not isinstance(origin, str) or not (origin.startswith("http://") or origin.startswith("https://")):
            continue
        if origin.startswith("http://127.0.0.1") or origin.startswith("http://localhost"):
            continue
        kv: Dict[str, str] = {}
        for entry in ls_items:
            if not isinstance(entry, dict):
                continue
            k = entry.get("name")
            v = entry.get("value")
            if isinstance(k, str) and isinstance(v, str):
                pair_bytes = len(k) + len(v)
                if total_bytes + pair_bytes > 262144:
                    continue
                kv[k] = v
                total_bytes += pair_bytes
        if kv:
            origin_map[origin] = kv
            if len(origin_map) >= 64:
                break
    if not origin_map:
        return ""
    encoded_map = json.dumps(origin_map)
    return f"""(() => {{
        if (window.__vortexLsRecovered) return;
        window.__vortexLsRecovered = true;
        try {{
            if (window.sessionStorage && window.sessionStorage.getItem('__vortex_ls_rec_done') === '1') return;
        }} catch (e) {{}}
        try {{
            const recoveryMap = {encoded_map};
            const entries = recoveryMap[window.location.origin];
            if (!entries) return;
            for (const [k, v] of Object.entries(entries)) {{
                try {{
                    if (window.localStorage.getItem(k) === null) {{
                        window.localStorage.setItem(k, v);
                    }}
                }} catch (e) {{}}
            }}
            try {{
                if (window.sessionStorage) window.sessionStorage.setItem('__vortex_ls_rec_done', '1');
            }} catch (e) {{}}
        }} catch (e) {{}}
    }})();"""


async def recover_profile_storage_state(context: BrowserContext, state_file: str) -> None:
    """Restores missing non-expired cookies and backup localStorage after crash or temp fallback."""
    if not context or not os.path.isfile(state_file):
        return
    try:
        with open(state_file, "r", encoding="utf-8") as f:
            payload = json.load(f)
        if not isinstance(payload, dict):
            return
    except Exception as exc:
        logger.debug("recover_profile_storage_state read warning: %s", exc)
        return

    try:
        existing_cookies = await context.cookies()
        existing_keys = {
            (c.get("name"), c.get("domain"), c.get("path", "/"))
            for c in existing_cookies
            if isinstance(c, dict)
        }
    except Exception:
        existing_keys = set()

    now_ts = time.time()
    to_restore = []
    for raw in payload.get("cookies") or []:
        if not isinstance(raw, dict):
            continue
        name = raw.get("name")
        value = raw.get("value")
        domain = raw.get("domain")
        path = raw.get("path", "/")
        if not name or value is None or not domain:
            continue
        if (name, domain, path) in existing_keys:
            continue
        exp = raw.get("expires")
        if isinstance(exp, (int, float)) and exp > 0 and exp <= now_ts:
            continue
        ck: Dict[str, Any] = {
            "name": name,
            "value": value,
            "domain": domain,
            "path": path,
            "secure": bool(raw.get("secure", False)),
            "httpOnly": bool(raw.get("httpOnly", False)),
        }
        if isinstance(exp, (int, float)) and exp > 0:
            ck["expires"] = float(exp)
        same_site = raw.get("sameSite")
        if same_site in ("Strict", "Lax", "None"):
            ck["sameSite"] = same_site
        to_restore.append(ck)

    if to_restore:
        try:
            await context.add_cookies(to_restore)
            logger.info("Recovered %d missing persistent cookie(s) from %s", len(to_restore), state_file)
        except Exception as exc:
            logger.debug("recover_profile_storage_state add_cookies warning: %s", exc)

    recovery_js = _build_local_storage_recovery_script(payload.get("origins") or [])
    if recovery_js:
        try:
            await context.add_init_script(recovery_js)
            logger.info("Installed localStorage crash-recovery fallback from %s", state_file)
        except Exception as exc:
            logger.debug("recover_profile_storage_state init script warning: %s", exc)


def _detect_chromium_ua(executable_path: Optional[str]) -> Optional[str]:
    """Detects installed Chromium major version for a consistent Linux Desktop User-Agent."""
    if not executable_path:
        return None
    try:
        out = subprocess.check_output(
            [executable_path, "--version"],
            text=True,
            stderr=subprocess.DEVNULL,
            timeout=2,
        )
        m = re.search(r"\b(\d+)\.\d+\.\d+\.\d+\b", out)
        if m:
            major = m.group(1)
            return (
                f"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                f"(KHTML, like Gecko) Chrome/{major}.0.0.0 Safari/537.36"
            )
    except Exception:
        pass
    return None


def _merge_chromium_feature_flags(args_list: List[str]) -> List[str]:
    """Merges repeated --enable-features / --disable-features flags so none overwrite earlier ones."""
    enable_vals: List[str] = []
    disable_vals: List[str] = []
    other_args: List[str] = []
    for arg in args_list:
        if arg.startswith("--enable-features="):
            for feat in arg.split("=", 1)[1].split(","):
                feat = feat.strip()
                if feat and feat not in enable_vals:
                    enable_vals.append(feat)
        elif arg.startswith("--disable-features="):
            for feat in arg.split("=", 1)[1].split(","):
                feat = feat.strip()
                if feat and feat not in disable_vals:
                    disable_vals.append(feat)
        else:
            other_args.append(arg)
    if enable_vals:
        other_args.append(f"--enable-features={','.join(enable_vals)}")
    if disable_vals:
        other_args.append(f"--disable-features={','.join(disable_vals)}")
    return other_args


# ============================================================================
# Client Session Handler
# ============================================================================
class AmigaClientSession:
    """Manages a single connected AmigaOS 4 client, its Playwright page, CDP screencast, audio streamer, and downloads."""

    def __init__(
        self,
        reader: asyncio.StreamReader,
        writer: asyncio.StreamWriter,
        browser: Union[Browser, BrowserContext],
        width: int,
        height: int,
        pixel_format: int,
        target_fps: int,
        start_url: str,
        portal_url: str,
        audio_enabled: bool = True,
        jpeg_quality: int = 100,
        codec_mode: str = "auto",
        engine: str = "chromium",
        stream_mode: str = "auto",
        video_bitrate: int = 4000,
        storage_state_file: str = "",
        context_state: Optional[Dict[str, Any]] = None,
        browser_locale: str = "",
        locale_explicit: bool = False,
    ):
        self.reader = reader
        self.writer = writer
        self.browser = browser
        self.is_persistent_context: bool = isinstance(browser, _PERSISTENT_CONTEXT_TYPES)
        self.engine: str = engine.lower()
        self.storage_state_file: str = storage_state_file or get_vortex_storage_state_path(self.engine)
        self._context_state: Dict[str, Any] = context_state if context_state is not None else {}
        self.browser_locale: str = normalize_bcp47_locale(browser_locale) or _SERVER_LOCALE
        self.locale_explicit: bool = bool(locale_explicit)
        self.width = width
        self.height = height
        self.pixel_format = pixel_format
        self.target_fps = max(1, min(60, target_fps))
        self.start_url = start_url
        self.portal_url = portal_url
        self.audio_enabled = audio_enabled
        self.base_jpeg_quality: int = max(20, min(100, jpeg_quality))
        self.video_jpeg_quality: int = min(self.base_jpeg_quality, 75)
        self.current_screencast_quality: int = self.base_jpeg_quality
        self.jpeg_quality: int = self.base_jpeg_quality
        self.is_video_mode: bool = False
        self._last_video_eval_time: float = 0.0
        self._last_video_state_time: float = 0.0
        self._video_element_playing: bool = False
        self._frames_last_sec: int = 0
        self._sec_start_time: float = time.monotonic()
        self.codec_mode: str = codec_mode
        self.stream_mode: str = stream_mode.lower()
        self.video_bitrate_k: int = max(500, min(50000, video_bitrate))
        self._h264_encoder: Optional[H264LiveStreamEncoder] = None
        self._total_h264_frames: int = 0

        self.write_lock = asyncio.Lock()
        self.running = True
        self.force_frame_event = asyncio.Event()
        self.force_full_frame = True
        self.active_until: float = time.monotonic() + 2.5
        self.pause_frames_until: float = 0.0
        self.last_frame_hash: Optional[bytes] = None
        self.prev_raw_buf = None
        self.prev_dims: Tuple[int, int] = (0, 0)

        self.context: Optional[BrowserContext] = None
        self.page: Optional[Page] = None
        self.cdp: Optional[Union[CDPSession, DirectCDPSession]] = None
        self.tabs: List[Page] = []
        self.active_tab_idx: int = 0
        self.cdp_port: int = 9222

        self.soft_scroll: bool = True
        self.pause_bg_media: bool = False
        self.last_hover_url: Optional[str] = None
        self.last_hover_time: float = 0.0
        self.hover_task: Optional[asyncio.Task] = None

        # Explicit Navigation History Stack (immune to HTTP/JS redirect loops)
        self.nav_history: List[str] = []
        self.nav_times: List[float] = []
        self.nav_idx: int = -1
        self._in_history_jump: bool = False

        self.next_download_id: int = 1
        self.pending_downloads: Dict[int, asyncio.Future] = {}
        self.next_upload_id: int = 1
        self.pending_uploads: Dict[int, Tuple[FileChooser, asyncio.Future]] = {}
        self.background_tasks: set = set()
        self._storage_save_lock = asyncio.Lock()
        self._last_storage_save_time: float = 0.0
        self._pending_storage_save_task: Optional[asyncio.Task] = None
        self._closed_event = asyncio.Event()
        self._cdp_lock = asyncio.Lock()
        self._switching_tab: bool = False
        self._tab_generation: int = 0
        self._pending_ack_tasks: int = 0
        self.last_audio_time: float = 0.0
        self._encode_executor = concurrent.futures.ThreadPoolExecutor(
            max_workers=1,
            thread_name_prefix="vb_enc",
        )

        self.web_zoom_pct: int = 100
        self.web_font_size: int = 16
        self.web_font_family: str = "sans-serif"
        self.web_charset: str = "utf-8"
        self.dark_mode: int = 0  # 0 = Light, 1 = Dark, 2 = Auto
        self.allow_yuv_rect: bool = False

        try:
            sock = self.writer.get_extra_info("socket")
            if sock is not None:
                sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 1024 * 1024)
        except Exception:
            pass

        # CDP screencast (push-based frame streaming, no per-frame request/response overhead)
        self.screencast_queue: Optional[asyncio.Queue] = None
        self.screencast_active: bool = False
        self._screencast_handler = None
        self.last_screencast_frame_time: float = 0.0

        # Mouse click tracking for gesture stabilization & upload trigger
        self._mouse_down_x: int = -1
        self._mouse_down_y: int = -1
        self._mouse_down_btn: str = ""
        self._mouse_down_time: float = 0.0
        self._last_click_time: float = 0.0
        self._last_click_x: int = -1
        self._last_click_y: int = -1
        self._click_count: int = 1
        self.last_upload_intercept_time: float = 0.0

        # Pre-allocated pixel conversion buffers (reused across frames to avoid alloc churn)
        self._argb_buf: Optional[ctypes.Array] = None
        self._rgb565_buf: Optional[ctypes.Array] = None
        self._yuv_buf: Optional[ctypes.Array] = None
        self._rect_buf: Optional[ctypes.Array] = None
        self._pkt_buf: Optional[ctypes.Array] = None

    def _get_h264_encoder(self, width: int, height: int) -> Optional[H264LiveStreamEncoder]:
        if (
            self._h264_encoder is not None
            and (self._h264_encoder.width != width or self._h264_encoder.height != height or self._h264_encoder.fps != self.target_fps)
        ):
            self._h264_encoder.stop()
            self._h264_encoder = None
        if self._h264_encoder is None:
            self._h264_encoder = H264LiveStreamEncoder(width, height, self.target_fps, self.video_bitrate_k)
        return self._h264_encoder

    async def _detach_cdp_unlocked(self) -> None:
        cdp = self.cdp
        self.cdp = None
        if cdp is None:
            return
        if isinstance(cdp, DirectCDPSession):
            try:
                await cdp.close()
            except Exception:
                pass
        else:
            try:
                cdp.remove_listener("Page.fileChooserOpened", self._on_cdp_file_chooser_opened)
            except Exception:
                pass
            await _safe_detach_cdp_session(cdp)

    async def _detach_cdp(self) -> None:
        async with self._cdp_lock:
            await self._detach_cdp_unlocked()

    async def _gc_current_cdp_page(self) -> None:
        if self.cdp is None:
            return
        try:
            await asyncio.wait_for(self.cdp.send("HeapProfiler.collectGarbage"), timeout=0.3)
        except Exception:
            pass

    async def _attach_direct_cdp(self, target_page: Page) -> None:
        """Creates a DirectCDP WebSocket connection directly to Chromium's DevTools port,
        detaching the bootstrap Playwright CDP session immediately once DirectCDP is connected.
        """
        async with self._cdp_lock:
            gen = self._tab_generation
            if self.page is not target_page or not self.running:
                return
            await self._detach_cdp_unlocked()
            if (
                not self.context
                or self.engine != "chromium"
                or self._tab_generation != gen
                or self.page is not target_page
                or not self.running
            ):
                return

            base_cdp = None
            target_id = None
            try:
                base_cdp = await self.context.new_cdp_session(target_page)
                t_info = await asyncio.wait_for(base_cdp.send("Target.getTargetInfo"), timeout=1.5)
                target_id = (t_info.get("targetInfo") or {}).get("targetId")
            except Exception as exc:
                logger.debug("Base CDP session creation warning: %s", exc)

            if self._tab_generation != gen or self.page is not target_page or not self.running:
                await _safe_detach_cdp_session(base_cdp)
                return

            if _HAS_WEBSOCKETS and self.cdp_port > 0:
                ws_url = None
                if target_id:
                    ws_url = f"ws://127.0.0.1:{self.cdp_port}/devtools/page/{target_id}"
                else:
                    try:
                        def _fetch_targets():
                            with urllib.request.urlopen(f"http://127.0.0.1:{self.cdp_port}/json", timeout=1.0) as r:
                                return json.loads(r.read().decode("utf-8", errors="replace"))
                        targets = await asyncio.to_thread(_fetch_targets)
                        target_url = ""
                        try:
                            target_url = target_page.url or ""
                        except Exception:
                            pass
                        page_targets = [
                            t for t in targets
                            if t.get("type") == "page" and t.get("webSocketDebuggerUrl")
                        ]
                        for t in page_targets:
                            if target_url and t.get("url") == target_url:
                                ws_url = t["webSocketDebuggerUrl"]
                                break
                        if not ws_url and len(page_targets) == 1:
                            ws_url = page_targets[0]["webSocketDebuggerUrl"]
                    except Exception:
                        pass

                if ws_url:
                    direct = DirectCDPSession(ws_url, fallback_cdp=None)
                    if await direct.connect():
                        await _safe_detach_cdp_session(base_cdp)
                        if self._tab_generation != gen or self.page is not target_page or not self.running:
                            await direct.close()
                            return
                        self.cdp = direct
                        if getattr(self, "_init_turbo_js", None) and not getattr(target_page, "_vb_turbo_injected", False):
                            try:
                                await self.cdp.send("Page.enable")
                                await self.cdp.send("Page.setInterceptFileChooserDialog", {"enabled": True})
                                await self.cdp.send("Page.addScriptToEvaluateOnNewDocument", {"source": self._init_turbo_js})
                                await self.cdp.send("Runtime.evaluate", {"expression": self._init_turbo_js})
                                try:
                                    setattr(target_page, "_vb_turbo_injected", True)
                                except Exception:
                                    pass
                            except Exception:
                                pass
                        else:
                            try:
                                await self.cdp.send("Page.enable")
                                await self.cdp.send("Page.setInterceptFileChooserDialog", {"enabled": True})
                            except Exception:
                                pass
                        # Listen for Page.fileChooserOpened on DirectCDP
                        if (
                            self.cdp is direct
                            and self._tab_generation == gen
                            and self.page is target_page
                            and self.running
                        ):
                            self.cdp.on("Page.fileChooserOpened", self._on_cdp_file_chooser_opened)
                        return
                    await direct.close()

            if self._tab_generation != gen or self.page is not target_page or not self.running:
                await _safe_detach_cdp_session(base_cdp)
                return
            self.cdp = base_cdp

    async def send_packet(self, data: bytes) -> bool:
        if not self.running:
            return False
        async with self.write_lock:
            if not self.running:
                return False
            try:
                self.writer.write(data)
                await asyncio.wait_for(self.writer.drain(), timeout=10.0)
                return True
            except Exception as exc:
                logger.warning("Socket write failed: %s", exc)
                self.running = False
                try:
                    self.writer.close()
                except Exception:
                    pass
                return False

    def _send_nav_update(self, url: str, tab_idx: int = -1, title: str = "") -> None:
        if not self.running or not url:
            return

        async def _do_send():
            nonlocal title
            t_idx = tab_idx
            if t_idx < 0:
                try:
                    if self.page and self.tabs and self.page in self.tabs:
                        t_idx = self.tabs.index(self.page)
                    else:
                        t_idx = self.active_tab_idx
                except Exception:
                    t_idx = 0

            target_page = self.tabs[t_idx] if (0 <= t_idx < len(self.tabs)) else self.page

            # Try to fetch real document title from target page
            if not title and target_page:
                try:
                    raw_title = await asyncio.wait_for(target_page.title(), timeout=0.6)
                    title = (raw_title or "").strip()
                except Exception:
                    title = ""

            # Fallback title if empty
            if not title:
                if "127.0.0.1" in url or "localhost" in url or url.startswith("@"):
                    title = "Startseite"
                else:
                    h = url
                    if h.startswith("http://"): h = h[7:]
                    elif h.startswith("https://"): h = h[8:]
                    if h.startswith("www."): h = h[4:]
                    h = h.split("/", 1)[0].split(":", 1)[0]
                    title = h if h else "Webseite"

            url_bytes = url.encode("utf-8", errors="replace")[:511].ljust(512, b"\x00")
            title_bytes = title.encode("utf-8", errors="replace")[:127].ljust(128, b"\x00")
            pkt = STRUCT_MSG_NAV_UPDATE.pack(MSG_NAV_UPDATE, max(0, t_idx), url_bytes, title_bytes)
            await self.send_packet(pkt)

        t = asyncio.create_task(_do_send())
        self.background_tasks.add(t)
        t.add_done_callback(self.background_tasks.discard)

    def _send_progress(self, percent: int, status: str = "") -> None:
        if not self.running:
            return
        status_bytes = status.encode("utf-8", errors="replace")[:63].ljust(64, b"\x00")
        pkt = STRUCT_MSG_PROGRESS.pack(MSG_PAGE_PROGRESS, max(0, min(100, int(percent))), status_bytes)
        t = asyncio.create_task(self.send_packet(pkt))
        self.background_tasks.add(t)
        t.add_done_callback(self.background_tasks.discard)

    def _send_password_save(self, url: str, username: str, password: str) -> None:
        if not self.running or not url or not username or not password:
            return
        url_bytes = url.encode("utf-8", errors="replace")[:255].ljust(256, b"\x00")
        user_bytes = username.encode("utf-8", errors="replace")[:127].ljust(128, b"\x00")
        pass_bytes = password.encode("utf-8", errors="replace")[:127].ljust(128, b"\x00")
        pkt = STRUCT_MSG_PASSWORD_SAVE.pack(MSG_PASSWORD_SAVE, url_bytes, user_bytes, pass_bytes)
        t = asyncio.create_task(self.send_packet(pkt))
        self.background_tasks.add(t)
        t.add_done_callback(self.background_tasks.discard)

    async def _on_password_submitted(self, source, domain: str, username: str, password: str) -> None:
        if not domain or not username or not password:
            return
        if "127.0.0.1" in domain or "localhost" in domain:
            return
        now = time.monotonic()
        key = (domain, username, password)
        if getattr(self, "_last_pw_save_key", None) == key and now - getattr(self, "_last_pw_save_time", 0.0) < 5.0:
            return
        self._last_pw_save_key = key
        self._last_pw_save_time = now
        logger.info("Captured submitted login credentials: domain=%s user=%s", domain, username)
        self._send_password_save(domain, username, password)
        self._schedule_storage_save(delay=1.5)

    def _setup_page_listeners(self, page: Page) -> None:
        page.on("filechooser", self._on_filechooser_event)
        page.on("download", self._on_download_event)
        page.on("framenavigated", self._on_main_frame_navigated)
        page.on("load", self._on_page_loaded)
        page.on("domcontentloaded", self._on_dom_content_loaded)
        page.on("close", self._on_page_closed)

    def _detach_page_listeners(self, page: Any) -> None:
        if page is None:
            return
        for ev, cb in (
            ("filechooser", self._on_filechooser_event),
            ("download", self._on_download_event),
            ("framenavigated", self._on_main_frame_navigated),
            ("load", self._on_page_loaded),
            ("domcontentloaded", self._on_dom_content_loaded),
            ("close", self._on_page_closed),
        ):
            try:
                page.remove_listener(ev, cb)
            except Exception:
                pass

    def _on_page_closed(self, *args) -> None:
        closed_page = args[0] if args else None
        if closed_page is None:
            return
        self._detach_page_listeners(closed_page)
        if not self.tabs or closed_page not in self.tabs:
            return
        idx = self.tabs.index(closed_page)
        self.tabs.pop(idx)
        was_active = (closed_page == self.page)
        if idx < self.active_tab_idx:
            self.active_tab_idx -= 1
        elif self.active_tab_idx >= len(self.tabs):
            self.active_tab_idx = max(0, len(self.tabs) - 1)
        if was_active:
            if self.running and self.tabs:
                self.page = self.tabs[self.active_tab_idx]
                t = asyncio.create_task(self._activate_tab(self.active_tab_idx))
                self.background_tasks.add(t)
                t.add_done_callback(self.background_tasks.discard)
            else:
                self.page = None

    def _on_page_loaded(self, *args) -> None:
        if not self.running:
            return
        if args and args[0] != self.page:
            return
        self._send_progress(100, "Fertig")
        self._on_page_updated()
        if self.page and self.page.url:
            self._send_nav_update(self.page.url)
        self._schedule_storage_save(delay=2.0)

    def _on_dom_content_loaded(self, *args) -> None:
        if not self.running:
            return
        if args and args[0] != self.page:
            return
        self._send_progress(75, "Erstelle Seite...")
        self._on_page_updated()
        if self.page and self.page.url:
            self._send_nav_update(self.page.url)

    def _on_main_frame_navigated(self, frame: Frame) -> None:
        if not self.running or not self.page or frame != self.page.main_frame:
            return
        url = (frame.url or "").strip()
        if not url or url.startswith("about:") or url.startswith("javascript:"):
            return

        now = time.monotonic()
        self.force_full_frame = True
        self.active_until = now + 2.0
        self.force_frame_event.set()
        self._schedule_delayed_frames()
        self._send_nav_update(url)
        self._schedule_storage_save(delay=3.0)

        if self._in_history_jump:
            if 0 <= self.nav_idx < len(self.nav_history):
                self.nav_history[self.nav_idx] = url
                self.nav_times[self.nav_idx] = now
            return

        if self.nav_idx >= 0 and self.nav_idx < len(self.nav_history):
            if self.nav_history[self.nav_idx] == url:
                return
            # If the previous navigation occurred < 1.2s ago (and wasn't the portal),
            # treat this as an automatic HTTP/JS redirect and update the top entry in-place
            # so clicking Back doesn't get stuck in a redirect loop!
            elapsed = now - self.nav_times[self.nav_idx]
            prev_url = self.nav_history[self.nav_idx]
            if elapsed < 1.2 and prev_url != self.portal_url:
                self.nav_history[self.nav_idx] = url
                self.nav_times[self.nav_idx] = now
                logger.info("History updated redirect in-place [%d]: %s", self.nav_idx, url)
                return

        # Push new history entry (bounded to 200 entries)
        self.nav_history = self.nav_history[: self.nav_idx + 1]
        self.nav_times = self.nav_times[: self.nav_idx + 1]
        if len(self.nav_history) >= 200:
            overflow = len(self.nav_history) - 199
            self.nav_history = self.nav_history[overflow:]
            self.nav_times = self.nav_times[overflow:]
        self.nav_history.append(url)
        self.nav_times.append(now)
        self.nav_idx = len(self.nav_history) - 1
        logger.info("History push [%d/%d]: %s", self.nav_idx, len(self.nav_history), url)

        if self.dark_mode == 1 and self.page:
            t = asyncio.create_task(self._apply_dark_mode(self.page))
            self.background_tasks.add(t)
            t.add_done_callback(self.background_tasks.discard)

    def _on_popup_page(self, new_page: Page) -> None:
        """Redirects any target='_blank' or window.open popup into the main streamed page."""
        if not self.running or getattr(self, "_creating_tab", False):
            return
        if self.tabs and new_page in self.tabs:
            return
        if not self.page or new_page == self.page:
            return

        async def _adopt_popup():
            try:
                if not self.running or getattr(self, "_creating_tab", False):
                    return
                if self.tabs and new_page in self.tabs:
                    return
                if not self.page or new_page == self.page:
                    return
                # Wait for the popup page to receive its target URL before redirecting
                popup_url = (new_page.url or "").strip()
                if not popup_url or popup_url == "about:blank":
                    for _ in range(15):
                        await asyncio.sleep(0.1)
                        if not self.running or getattr(self, "_creating_tab", False):
                            return
                        if self.tabs and new_page in self.tabs:
                            return
                        popup_url = (new_page.url or "").strip()
                        if popup_url and popup_url != "about:blank":
                            break
                if not self.running or getattr(self, "_creating_tab", False):
                    return
                if self.tabs and new_page in self.tabs:
                    return
                try:
                    await new_page.close()
                except Exception:
                    pass
                if (
                    popup_url
                    and not popup_url.startswith("about:")
                    and not popup_url.startswith("chrome:")
                    and not popup_url.startswith("resource:")
                    and self.page
                ):
                    if popup_url == self.page.url:
                        return
                    logger.info("Redirecting popup tab to main viewport: %s", popup_url)
                    await self._handle_navigation(popup_url)
            except Exception as exc:
                logger.debug("Popup adoption error: %s", exc)

        t = asyncio.create_task(_adopt_popup())
        self.background_tasks.add(t)
        t.add_done_callback(self.background_tasks.discard)

    async def run(self) -> None:
        peer = self.writer.get_extra_info("peername")
        logger.info("AmigaOS 4 client connected from %s", peer)

        try:
            if self.is_persistent_context:
                self.context = self.browser  # type: ignore
                logger.info("Using persistent %s profile context (saved cookies & website settings active)", self.engine.capitalize())
            else:
                self.context = await self.browser.new_context(
                    viewport={"width": self.width, "height": self.height},
                    accept_downloads=True,
                    locale=self.browser_locale,
                )
            # Pre-set Google & YouTube Cookie Consent (SOCS / CONSENT) so cookie walls never block browsing
            try:
                await self.context.add_cookies([
                    {"name": "SOCS", "value": "CAISHAgBEhJnd3NfMjAyMzA4MTUtMF9SQzIaAmRlIAEaBgiAo_CmBg", "domain": ".youtube.com", "path": "/"},
                    {"name": "SOCS", "value": "CAISHAgBEhJnd3NfMjAyMzA4MTUtMF9SQzIaAmRlIAEaBgiAo_CmBg", "domain": ".google.com", "path": "/"},
                    {"name": "SOCS", "value": "CAISHAgBEhJnd3NfMjAyMzA4MTUtMF9SQzIaAmRlIAEaBgiAo_CmBg", "domain": ".google.de", "path": "/"},
                    {"name": "CONSENT", "value": "YES+cb.20230531-04-p0.de+FX+999", "domain": ".youtube.com", "path": "/"},
                    {"name": "CONSENT", "value": "YES+cb.20230531-04-p0.de+FX+999", "domain": ".google.com", "path": "/"},
                    {"name": "CONSENT", "value": "YES+cb.20230531-04-p0.de+FX+999", "domain": ".google.de", "path": "/"},
                ])
            except Exception:
                pass

            # Pre-load Twitch auth / full session cookies if configured in user profile dir (never from repo CWD)
            try:
                cfg_dir = get_vortex_config_dir()
                cookie_candidates = list(dict.fromkeys([
                    os.path.join(cfg_dir, "twitch_cookies.json"),
                    os.path.expanduser("~/.config/vortexbrowse/twitch_cookies.json"),
                ]))
                loaded_twitch = False
                for j_path in cookie_candidates:
                    if os.path.exists(j_path):
                        try:
                            with open(j_path, "r", encoding="utf-8") as f:
                                raw_cookies = json.load(f)
                            clean_list = []
                            for c in raw_cookies:
                                ck = {
                                    "name": c["name"],
                                    "value": c["value"],
                                    "domain": c["domain"],
                                    "path": c.get("path", "/"),
                                    "secure": c.get("secure", True),
                                    "httpOnly": c.get("httpOnly", False)
                                }
                                if "sameSite" in c and c["sameSite"] in ("Strict", "Lax", "None"):
                                    ck["sameSite"] = c["sameSite"]
                                clean_list.append(ck)
                            if clean_list:
                                await self.context.add_cookies(clean_list)
                                logger.info("Pre-loaded %d Twitch session cookies from %s", len(clean_list), j_path)
                                loaded_twitch = True
                                break
                        except Exception as _je:
                            logger.debug("Twitch json cookie load error: %s", _je)

                if not loaded_twitch:
                    token_candidates = list(dict.fromkeys([
                        os.path.join(cfg_dir, "twitch_token.txt"),
                        os.path.expanduser("~/.config/vortexbrowse/twitch_token.txt"),
                    ]))
                    for t_path in token_candidates:
                        if os.path.exists(t_path):
                            with open(t_path, "r", encoding="utf-8") as f:
                                t_val = f.read().strip()
                                if t_val:
                                    await self.context.add_cookies([{
                                        "name": "auth-token",
                                        "value": t_val,
                                        "domain": ".twitch.tv",
                                        "path": "/"
                                    }])
                                    logger.info("Pre-loaded Twitch auth-token from %s", t_path)
                                    break
            except Exception as _te:
                logger.debug("Twitch cookie load error: %s", _te)

            # Anti-Bot & Anti-Detection Stealth + YouTube/Twitch Dirty-Rect Turbo Script
            nav_langs_json = json.dumps(build_navigator_languages(self.browser_locale))
            self._init_turbo_js = """(() => {
                if (window.__vortexTurboInjected) return;
                window.__vortexTurboInjected = true;
                try {
                    delete Object.getPrototypeOf(navigator).webdriver;
                } catch (e) {
                    try {
                        Object.defineProperty(navigator, 'webdriver', { get: () => undefined });
                    } catch (e2) {}
                }
                if (!window.chrome) {
                    window.chrome = {
                        runtime: {},
                        loadTimes: function() {},
                        csi: function() {},
                        app: { isInstalled: false }
                    };
                }
                if (!navigator.plugins || navigator.plugins.length === 0) {
                    const fakePlugins = [
                        { name: 'PDF Viewer', filename: 'internal-pdf-viewer', description: 'Portable Document Format' },
                        { name: 'Chrome PDF Viewer', filename: 'internal-pdf-viewer', description: 'Portable Document Format' },
                        { name: 'Chromium PDF Viewer', filename: 'internal-pdf-viewer', description: 'Portable Document Format' }
                    ];
                    Object.defineProperty(navigator, 'plugins', { get: () => fakePlugins });
                }
                try {
                    const _defaultLangs = """ + nav_langs_json + """;
                    Object.defineProperty(navigator, 'languages', { get: () => window.__vortex_languages || _defaultLangs });
                    Object.defineProperty(navigator, 'language', { get: () => (window.__vortex_languages || _defaultLangs)[0] });
                } catch (e) {}

                document.addEventListener('click', function(e) {
                    const a = e.target && e.target.closest ? e.target.closest('a[href]') : null;
                    if (a && a.target && a.target !== '_self') {
                        a.target = '_self';
                    }
                }, true);

                // Media & YouTube Performance Turbo (H.264 only, block 60fps & VP9/AV1, disable Ambient Mode)
                try {
                    const origCanPlayType = HTMLMediaElement.prototype.canPlayType;
                    HTMLMediaElement.prototype.canPlayType = function(type) {
                        if (!type) return '';
                        const lower = String(type).toLowerCase();
                        if (lower.includes('vp9') || lower.includes('vp09') || lower.includes('av01') || lower.includes('av1') || lower.includes('webm')) {
                            return '';
                        }
                        return origCanPlayType.call(this, type);
                    };

                    if (window.MediaSource && window.MediaSource.isTypeSupported) {
                        const origIsTypeSupported = window.MediaSource.isTypeSupported.bind(window.MediaSource);
                        window.MediaSource.isTypeSupported = function(type) {
                            if (!type) return false;
                            const lower = String(type).toLowerCase();
                            if (lower.includes('vp9') || lower.includes('vp09') || lower.includes('vp8') ||
                                lower.includes('av01') || lower.includes('av1') || lower.includes('webm') ||
                                lower.includes('framerate=60') || lower.includes('fps=60') || lower.includes('framerate=50')) {
                                return false;
                            }
                            return origIsTypeSupported(type);
                        };
                    }

                    const injectYtStyle = () => {
                        if (document.getElementById('__vortex_yt_style')) return;
                        const ytStyle = document.createElement('style');
                        ytStyle.id = '__vortex_yt_style';
                        ytStyle.textContent = `
                            #cinematics, #cinematics-container, #background.ytd-cinematics, ytd-cinematics,
                            canvas.ytd-cinematics, .ytp-ambient-lighting, #movie_player .html5-ambient-video,
                            ytd-video-preview, #preview, .ytd-video-preview,
                            tp-yt-paper-spinner-lite, .ytp-spinner {
                                display: none !important;
                                visibility: hidden !important;
                                opacity: 0 !important;
                                pointer-events: none !important;
                            }
                        `;
                        (document.head || document.documentElement).appendChild(ytStyle);
                    };
                    if (document.head || document.documentElement) {
                        injectYtStyle();
                    }
                    document.addEventListener('DOMContentLoaded', injectYtStyle);

                    try {
                        window.localStorage.setItem('yt-player-quality', JSON.stringify({
                            data: '{"quality":720,"previousQuality":720}',
                            creation: Date.now()
                        }));
                        window.localStorage.setItem('yt-player-ambient-mode-enabled', JSON.stringify({
                            data: 'false',
                            creation: Date.now()
                        }));
                    } catch(e) {}

                    if (window === window.top && /(?:^|\\.)youtube\\.com$/i.test(location.hostname)) {
                        setInterval(() => {
                            if (document.hidden) return;
                            injectYtStyle();
                            const flexy = document.querySelector('ytd-watch-flexy[cinematics-enabled]');
                            if (flexy) flexy.removeAttribute('cinematics-enabled');
                            const cin = document.getElementById('cinematics');
                            if (cin && cin.innerHTML) cin.innerHTML = '';
                            const player = document.getElementById('movie_player');
                            if (player && typeof player.setPlaybackQualityRange === 'function') {
                                try {
                                    player.setPlaybackQualityRange('hd720', 'hd720');
                                } catch(e) {}
                            }
                        }, 1500);
                    }
                } catch(e) {}

                // Twitch Performance Turbo (Pre-set 720p30 / freeze animated emotes / hide bloat)
                try {
                    try {
                        window.localStorage.setItem('video-quality', JSON.stringify({ default: '720p30' }));
                        window.localStorage.setItem('twilight.player.quality', '"720p30"');
                        window.localStorage.setItem('lowLatencyMode', 'false');
                        if (location.hostname.includes('twitch.tv')) {
                            try {
                                const m = document.cookie.match(/(?:^|;\\s*)auth-token=([^;]+)/);
                                if (m && !window.localStorage.getItem('auth-token')) {
                                    window.localStorage.setItem('auth-token', decodeURIComponent(m[1]));
                                }
                            } catch(e) {}
                        }
                    } catch(e) {}

                    const injectTwStyle = () => {
                        if (document.getElementById('__vortex_tw_style')) return;
                        const twStyle = document.createElement('style');
                        twStyle.id = '__vortex_tw_style';
                        twStyle.textContent = `
                            .chat-image, .chat-line__message--emote {
                                animation-play-state: paused !important;
                            }
                            .community-points-summary,
                            .channel-points-reward-line,
                            .bits-card,
                            .tw-carousel,
                            .carousel-player,
                            .front-page-carousel,
                            [data-a-target="player-overlay-mature-accept"],
                            [data-a-target="user-channel-status-notice"] {
                                display: none !important;
                                visibility: hidden !important;
                            }
                        `;
                        (document.head || document.documentElement).appendChild(twStyle);
                    };
                    if (document.head || document.documentElement) {
                        injectTwStyle();
                    }
                    document.addEventListener('DOMContentLoaded', injectTwStyle);
                } catch(e) {}

                // Forum/phpBB File Attachment Helper:
                // Auto-scroll "Dateien hinzufügen" button into center view when "Dateianhänge" tab is clicked,
                // style button prominently, and ensure click always triggers file chooser.
                // Universal Forum File Attachment Helper (phpBB Plupload, XenForo, standard inputs)
                try {
                    const upStyle = document.createElement('style');
                    upStyle.id = '__vb_upload_style';
                    upStyle.textContent = `
                        #add_files, .attach-panel-multi input[type=button], .js-attachmentUpload {
                            cursor: pointer !important;
                            pointer-events: auto !important;
                        }
                    `;
                    const injectUpStyle = () => {
                        if (document.head || document.documentElement) {
                            (document.head || document.documentElement).appendChild(upStyle);
                        }
                    };
                    injectUpStyle();
                    document.addEventListener('DOMContentLoaded', injectUpStyle);

                    const syncMoxieShim = () => {
                        const btn = document.getElementById('add_files') || document.querySelector('.attach-panel-multi input[type=button]');
                        const shim = document.querySelector('.moxie-shim, .moxie-shim-html5');
                        if (btn && shim) {
                            shim.style.top = btn.offsetTop + 'px';
                            shim.style.left = btn.offsetLeft + 'px';
                            shim.style.width = btn.offsetWidth + 'px';
                            shim.style.height = btn.offsetHeight + 'px';
                            shim.style.zIndex = '999999';
                        }
                    };

                    // Prevent HTML5 drag-and-drop from hijacking text selections over links/images
                    window.addEventListener('dragstart', (e) => {
                        e.preventDefault();
                    }, true);

                    // Ensure text selection is visibly highlighted in headless Chromium
                    try {
                        const selStyle = document.createElement('style');
                        selStyle.id = '__vortex_selection_style';
                        selStyle.textContent = '*::selection, ::selection, *::-moz-selection, ::-moz-selection { background-color: #3390ff !important; color: #ffffff !important; -webkit-text-fill-color: #ffffff !important; }';
                        (document.head || document.documentElement).appendChild(selStyle);
                    } catch(e) {}

                    // Direct DOM-level selection controller using caretRangeFromPoint for 100% reliable freeform drag selection
                    window.__vortex_selection = {
                        startX: -1,
                        startY: -1,
                        isDragging: false,

                        start: function(x, y) {
                            this.startX = x;
                            this.startY = y;
                            this.isDragging = false;
                            let el = document.elementFromPoint(x, y);
                            if (el && (el.tagName === 'INPUT' || el.tagName === 'TEXTAREA' || el.isContentEditable)) return;
                            let r = document.caretRangeFromPoint ? document.caretRangeFromPoint(x, y) : null;
                            if (r) {
                                let s = window.getSelection();
                                if (s) {
                                    s.removeAllRanges();
                                    s.addRange(r);
                                }
                            }
                        },

                        update: function(x, y) {
                            if (this.startX < 0 || this.startY < 0) return;
                            let dist = Math.hypot(x - this.startX, y - this.startY);
                            if (dist < 4) return;
                            this.isDragging = true;

                            let el = document.elementFromPoint(this.startX, this.startY);
                            if (el && (el.tagName === 'INPUT' || el.tagName === 'TEXTAREA')) return;

                            let r1 = document.caretRangeFromPoint ? document.caretRangeFromPoint(this.startX, this.startY) : null;
                            let r2 = document.caretRangeFromPoint ? document.caretRangeFromPoint(x, y) : null;
                            if (!r1 || !r2) return;

                            let range = document.createRange();
                            let comp = r1.startContainer.compareDocumentPosition(r2.startContainer);
                            if (comp === 0) {
                                if (r1.startOffset <= r2.startOffset) {
                                    range.setStart(r1.startContainer, r1.startOffset);
                                    range.setEnd(r2.startContainer, r2.startOffset);
                                } else {
                                    range.setStart(r2.startContainer, r2.startOffset);
                                    range.setEnd(r1.startContainer, r1.startOffset);
                                }
                            } else if (comp & Node.DOCUMENT_POSITION_FOLLOWING) {
                                range.setStart(r1.startContainer, r1.startOffset);
                                range.setEnd(r2.startContainer, r2.startOffset);
                            } else {
                                range.setStart(r2.startContainer, r2.startOffset);
                                range.setEnd(r1.startContainer, r1.startOffset);
                            }

                            let s = window.getSelection();
                            if (s) {
                                s.removeAllRanges();
                                s.addRange(range);
                            }
                        },

                        finish: function(x, y) {
                            let dragged = this.isDragging;
                            this.startX = -1;
                            this.startY = -1;
                            this.isDragging = false;
                            let s = window.getSelection() ? window.getSelection().toString() : '';
                            if (s && s.length > 0) return { dragged: dragged, text: s };

                            let el = document.activeElement;
                            if (el && (el.tagName === 'TEXTAREA' || (el.tagName === 'INPUT' && el.type === 'text'))) {
                                try {
                                    let sub = el.value.substring(el.selectionStart, el.selectionEnd);
                                    if (sub && sub.length > 0) return { dragged: dragged, text: sub };
                                } catch (e) {}
                            }
                            return { dragged: dragged, text: '' };
                        }
                    };

                    document.addEventListener('click', (e) => {
                        const tab = e.target && e.target.closest ? e.target.closest('#attach-panel-tab, [data-subpanel="attach-panel"], a[href*="attach"]') : null;
                        if (tab) {
                            setTimeout(() => {
                                const btn = document.getElementById('add_files') || document.querySelector('#attach-panel input[type=button]');
                                if (btn) {
                                    btn.scrollIntoView({ behavior: 'auto', block: 'center' });
                                    if (window.phpbb && window.phpbb.plupload && window.phpbb.plupload.uploader) {
                                        try { window.phpbb.plupload.uploader.refresh(); } catch(err) {}
                                    }
                                    syncMoxieShim();
                                }
                            }, 80);
                        }
                    }, true);
                } catch(e) {}

                // --- Automatic Login & Password Submission Interceptor ---
                function _vortexExtractCredentials(formOrElem) {
                    try {
                        if (!window.__vortex_save_password) return;
                        const root = formOrElem ? (formOrElem.closest ? (formOrElem.closest('form') || formOrElem) : formOrElem) : document;
                        let pwInputs = Array.from(root.querySelectorAll ? root.querySelectorAll('input[type="password"]') : []);
                        if (pwInputs.length === 0 && formOrElem && formOrElem.form) {
                            pwInputs = Array.from(formOrElem.form.querySelectorAll('input[type="password"]'));
                        }
                        if (pwInputs.length === 0) {
                            pwInputs = Array.from(document.querySelectorAll('input[type="password"]'));
                        }
                        let pwInput = pwInputs.find(p => p.value && p.value.length > 0);
                        if (!pwInput) return;
                        const pass = pwInput.value;
                        if (!pass) return;

                        let form = pwInput.closest('form') || root;
                        let userInputs = Array.from(form.querySelectorAll(
                            'input[autocomplete="username"], input[autocomplete="email"], input[type="email"], ' +
                            'input[name*="user" i], input[name*="login" i], input[name*="email" i], input[name*="account" i], ' +
                            'input[id*="user" i], input[id*="login" i], input[id*="email" i], input[id*="account" i], ' +
                            'input[type="text"]'
                        ));
                        let userVal = '';
                        for (const u of userInputs) {
                            if (u !== pwInput && u.value && u.value.trim().length > 0) {
                                userVal = u.value.trim();
                                break;
                            }
                        }
                        if (!userVal) {
                            const allText = Array.from(document.querySelectorAll('input[type="text"], input[type="email"], input:not([type])'));
                            for (const u of allText) {
                                if (u !== pwInput && u.value && u.value.trim().length > 0) {
                                    userVal = u.value.trim();
                                    break;
                                }
                            }
                        }
                        if (!userVal) return;

                        let domain = window.location.hostname || '';
                        if (!domain && window.location.href) {
                            try { domain = new URL(window.location.href).hostname; } catch(e) {}
                        }
                        if (domain) {
                            window.__vortex_save_password(domain, userVal, pass);
                        }
                    } catch(err) {}
                }

                document.addEventListener('submit', function(e) {
                    _vortexExtractCredentials(e.target);
                }, true);

                document.addEventListener('click', function(e) {
                    const btn = e.target && e.target.closest ? e.target.closest('button, input[type="submit"], input[type="button"], a[role="button"]') : null;
                    if (btn) {
                        _vortexExtractCredentials(btn);
                    }
                }, true);

                document.addEventListener('keydown', function(e) {
                    if (e.key === 'Enter') {
                        _vortexExtractCredentials(e.target);
                    }
                }, true);

                // --- Lightweight WeakRef Media & AudioContext Registry for Background Tab Pause ---
                if (!window.__vortexMediaReg) {
                    window.__vortexMediaReg = true;
                    const _vbMediaRefs = new Set();
                    const _vbAudioCtxRefs = new Set();
                    const _vbPruneRefs = (s, maxLen) => {
                        if (s.size <= maxLen) return;
                        for (const r of s) {
                            if (!r.deref()) s.delete(r);
                        }
                    };
                    const _vbTrackMedia = (el) => {
                        if (!el || typeof WeakRef === 'undefined') return;
                        try {
                            for (const r of _vbMediaRefs) {
                                if (r.deref() === el) return;
                            }
                            _vbMediaRefs.add(new WeakRef(el));
                            _vbPruneRefs(_vbMediaRefs, 64);
                        } catch (e) {}
                    };
                    const _vbTrackCtx = (ctx) => {
                        if (!ctx || typeof WeakRef === 'undefined') return;
                        try {
                            for (const r of _vbAudioCtxRefs) {
                                if (r.deref() === ctx) return;
                            }
                            _vbAudioCtxRefs.add(new WeakRef(ctx));
                            _vbPruneRefs(_vbAudioCtxRefs, 32);
                        } catch (e) {}
                    };
                    try {
                        if (window.HTMLMediaElement && HTMLMediaElement.prototype.play) {
                            const origPlay = HTMLMediaElement.prototype.play;
                            HTMLMediaElement.prototype.play = function() {
                                _vbTrackMedia(this);
                                return origPlay.apply(this, arguments);
                            };
                        }
                    } catch (e) {}
                    const _vbWrapAudioCtx = (propName) => {
                        try {
                            const OrigCtx = window[propName];
                            if (!OrigCtx || OrigCtx.__vbWrapped) return;
                            const WrappedCtx = function(...args) {
                                const inst = new OrigCtx(...args);
                                _vbTrackCtx(inst);
                                return inst;
                            };
                            WrappedCtx.prototype = OrigCtx.prototype;
                            Object.setPrototypeOf(WrappedCtx, OrigCtx);
                            WrappedCtx.__vbWrapped = true;
                            window[propName] = WrappedCtx;
                        } catch (e) {}
                    };
                    _vbWrapAudioCtx('AudioContext');
                    _vbWrapAudioCtx('webkitAudioContext');
                    const _vbResumeSuspendedCtx = () => {
                        for (const r of _vbAudioCtxRefs) {
                            const c = r.deref();
                            if (!c) { _vbAudioCtxRefs.delete(r); continue; }
                            try {
                                if (c.__vbSuspendedByTabSwitch && c.state === 'suspended' && typeof c.resume === 'function') {
                                    c.__vbSuspendedByTabSwitch = false;
                                    Promise.resolve(c.resume()).catch(() => {});
                                }
                            } catch (e) {}
                        }
                    };
                    window.addEventListener('pointerdown', _vbResumeSuspendedCtx, true);
                    window.addEventListener('keydown', _vbResumeSuspendedCtx, true);
                    window.__vortexPauseMedia = () => {
                        const pauseRoot = (root) => {
                            if (!root || !root.querySelectorAll) return;
                            try {
                                root.querySelectorAll('video, audio').forEach(m => {
                                    try { if (!m.paused) m.pause(); } catch (e) {}
                                });
                            } catch (e) {}
                            try {
                                root.querySelectorAll('*').forEach(el => {
                                    if (el.shadowRoot) pauseRoot(el.shadowRoot);
                                });
                            } catch (e) {}
                        };
                        pauseRoot(document);
                        for (const r of _vbMediaRefs) {
                            const m = r.deref();
                            if (!m) { _vbMediaRefs.delete(r); continue; }
                            try { if (!m.paused && typeof m.pause === 'function') m.pause(); } catch (e) {}
                        }
                        for (const r of _vbAudioCtxRefs) {
                            const c = r.deref();
                            if (!c) { _vbAudioCtxRefs.delete(r); continue; }
                            try {
                                if (c.state === 'running' && typeof c.suspend === 'function') {
                                    c.__vbSuspendedByTabSwitch = true;
                                    Promise.resolve(c.suspend()).catch(() => {});
                                }
                            } catch (e) {}
                        }
                    };
                }
            })()"""
            self._context_state["active_session"] = self
            if not self._context_state.get("scripts_initialized"):
                ctx_state = self._context_state

                async def _dispatch_pw_save(source, domain: str, username: str, password: str) -> None:
                    sess = ctx_state.get("active_session")
                    if sess is not None:
                        await sess._on_password_submitted(source, domain, username, password)

                try:
                    await self.context.expose_binding("__vortex_save_password", _dispatch_pw_save)
                except Exception as _pe:
                    logger.debug("expose_binding warning: %s", _pe)
                try:
                    await self.context.add_init_script(self._init_turbo_js)
                except Exception:
                    pass
                self._context_state["scripts_initialized"] = True

            if self.is_persistent_context and self.context.pages:
                self.page = self.context.pages[0]
                for stray in list(self.context.pages[1:]):
                    try:
                        await stray.close()
                    except Exception:
                        pass
                try:
                    await self.page.set_viewport_size({"width": self.width, "height": self.height})
                except Exception:
                    pass
            else:
                self.page = await self.context.new_page()
            self.tabs = [self.page]
            self.active_tab_idx = 0
            self.context.on("page", self._on_popup_page)
            if self.engine == "chromium":
                await self._attach_direct_cdp(self.page)
            else:
                self.cdp = None

            self._setup_page_listeners(self.page)
            await self._apply_dark_mode(self.page)

            # Pre-allocate ONLY the pixel conversion buffers needed for active format
            self._ensure_work_buffers(self.width, self.height, self.pixel_format)

            # Start CDP screencast (push-based frame streaming, no request/response overhead)
            if self.engine == "chromium":
                await self._start_screencast()

            tasks = [
                asyncio.create_task(self._frame_stream_loop()),
                asyncio.create_task(self._input_read_loop()),
            ]
            if self.is_persistent_context:
                tasks.append(asyncio.create_task(self._periodic_storage_sync_loop()))
            if self.audio_enabled:
                tasks.append(asyncio.create_task(self._audio_stream_loop()))

            curr_url = self.page.url if self.page else ""
            has_existing_page = bool(curr_url and not curr_url.startswith("about:") and curr_url != self.portal_url)

            initial_url = self.start_url if self.start_url else self.portal_url
            logger.info("Navigating initial viewport (%dx%d) to %s (has_existing_page=%s)", self.width, self.height, initial_url, has_existing_page)
            async def _init_nav():
                if not has_existing_page:
                    try:
                        await self.page.goto(initial_url, wait_until="domcontentloaded", timeout=12000)
                    except Exception as nav_exc:
                        logger.warning("Initial navigation warning (%s), falling back to portal", nav_exc)
                        if initial_url != self.portal_url:
                            try:
                                await self.page.goto(self.portal_url, wait_until="domcontentloaded", timeout=8000)
                            except Exception:
                                pass
                else:
                    self._send_nav_update(self.page.url)
                self.active_until = time.monotonic() + 3.0
                self._trigger_full_refresh(True)
                self._schedule_delayed_frames()

            t_init = asyncio.create_task(_init_nav())
            self.background_tasks.add(t_init)
            t_init.add_done_callback(self.background_tasks.discard)

            done, pending = await asyncio.wait(
                tasks,
                return_when=asyncio.FIRST_COMPLETED,
            )
            self.running = False
            for t in pending:
                t.cancel()
        except Exception as exc:
            logger.error("Session error: %s", exc)
        finally:
            try:
                self.running = False
                if self._h264_encoder is not None:
                    self._h264_encoder.stop()
                    self._h264_encoder = None
                await self._stop_screencast()
                await self._detach_cdp()
                if self.context:
                    try:
                        self.context.remove_listener("page", self._on_popup_page)
                    except Exception:
                        pass
                for tab in list(self.tabs):
                    self._detach_page_listeners(tab)
                for fut in self.pending_downloads.values():
                    if not fut.done():
                        fut.cancel()
                self.pending_downloads.clear()
                for entry in self.pending_uploads.values():
                    if len(entry) >= 2 and entry[1] and not entry[1].done():
                        entry[1].cancel()
                self.pending_uploads.clear()
                if self._pending_storage_save_task and not self._pending_storage_save_task.done():
                    self._pending_storage_save_task.cancel()
                for t in list(self.background_tasks):
                    t.cancel()
                self.background_tasks.clear()
                if self._context_state.get("active_session") is self:
                    self._context_state["active_session"] = None
                if self.is_persistent_context:
                    try:
                        await self.save_storage_state(force=True)
                    except Exception:
                        pass
                    for tab in list(self.tabs[1:]):
                        try:
                            if not tab.is_closed():
                                await tab.close()
                        except Exception:
                            pass
                    if self.context:
                        try:
                            for stray in list(getattr(self.context, "pages", []) or [])[1:]:
                                self._detach_page_listeners(stray)
                                try:
                                    if not stray.is_closed():
                                        await stray.close()
                                except Exception:
                                    pass
                        except Exception:
                            pass
                    if self.tabs:
                        try:
                            await asyncio.wait_for(
                                self.tabs[0].goto("about:blank", wait_until="commit", timeout=3000),
                                timeout=3.5,
                            )
                        except Exception:
                            pass
                elif self.context:
                    try:
                        await self.context.close()
                    except Exception:
                        pass
                self.tabs.clear()
                self.page = None
                self.nav_history.clear()
                self.nav_times.clear()
                self.prev_raw_buf = None
                self._argb_buf = None
                self._rgb565_buf = None
                self._yuv_buf = None
                self._rect_buf = None
                self._pkt_buf = None
                try:
                    self.writer.close()
                    await self.writer.wait_closed()
                except Exception:
                    pass
                if getattr(self, "_encode_executor", None) is not None:
                    if _FAST_PIXCONV is not None and hasattr(_FAST_PIXCONV, "fast_pixconv_release_tls"):
                        try:
                            self._encode_executor.submit(_FAST_PIXCONV.fast_pixconv_release_tls)
                        except Exception:
                            pass
                    try:
                        self._encode_executor.shutdown(wait=False)
                    except Exception:
                        pass
                    self._encode_executor = None
                _reclaim_python_memory()
                logger.info("AmigaOS 4 client session closed (%s)", peer)
            finally:
                self._closed_event.set()

    def _safe_dispatch_resize(self) -> None:
        if not self.page or not self.running:
            return
        async def _eval() -> None:
            try:
                if self.page and self.running:
                    await self.page.evaluate("window.dispatchEvent(new Event('resize'))")
            except Exception:
                pass
        t = asyncio.create_task(_eval())
        self.background_tasks.add(t)
        t.add_done_callback(self.background_tasks.discard)

    def _trigger_full_refresh(self, full_frame: bool = False) -> None:
        if full_frame:
            self.force_full_frame = True
            self.last_frame_hash = None
        self.active_until = time.monotonic() + 3.0
        self.force_frame_event.set()

    def _ack_screencast_frame(self, session_id: Optional[int]) -> None:
        cdp_ref = self.cdp
        if session_id is None or cdp_ref is None or not self.running:
            return
        if self._pending_ack_tasks >= 8:
            return

        self._pending_ack_tasks += 1

        async def _send_ack() -> None:
            try:
                await asyncio.wait_for(
                    cdp_ref.send("Page.screencastFrameAck", {"sessionId": session_id}),
                    timeout=1.5,
                )
            except Exception:
                pass

        def _on_ack_done(task: asyncio.Task) -> None:
            self.background_tasks.discard(task)
            self._pending_ack_tasks = max(0, self._pending_ack_tasks - 1)

        try:
            t = asyncio.create_task(_send_ack())
            self.background_tasks.add(t)
            t.add_done_callback(_on_ack_done)
        except Exception:
            self._pending_ack_tasks = max(0, self._pending_ack_tasks - 1)

    async def _start_screencast(self) -> bool:
        """Start CDP Page.screencast — push-based frame delivery with 2-frame backpressure."""
        if not self.cdp or not self.page or self.screencast_active:
            return False
        try:
            self.screencast_queue = asyncio.Queue(maxsize=2)
            self.screencast_active = True

            def _screencast_handler(params: dict) -> None:
                self.last_screencast_frame_time = time.monotonic()
                q = self.screencast_queue
                if q is not None:
                    if q.full():
                        try:
                            dropped = q.get_nowait()
                            self._ack_screencast_frame(dropped.get("sessionId"))
                        except asyncio.QueueEmpty:
                            pass
                    try:
                        q.put_nowait(params)
                    except asyncio.QueueFull:
                        self._ack_screencast_frame(params.get("sessionId"))
                self.force_frame_event.set()

            self._screencast_handler = _screencast_handler
            self.cdp.on("Page.screencastFrame", _screencast_handler)

            await asyncio.wait_for(
                self.cdp.send(
                    "Page.startScreencast",
                    {
                        "format": "jpeg",
                        "quality": self.jpeg_quality,
                        "everyNthFrame": 1,
                        "maxWidth": self.width,
                        "maxHeight": self.height,
                    },
                ),
                timeout=2.0,
            )
            self.last_screencast_frame_time = time.monotonic()
            logger.info(
                "CDP Page.screencast started via %s (push-based frame streaming, JPEG q%d)",
                "DirectCDP WebSocket" if isinstance(self.cdp, DirectCDPSession) and self.cdp.connected else "Rustwright/CDP",
                self.jpeg_quality,
            )
            return True
        except Exception as exc:
            logger.debug("Could not start CDP screencast: %s", exc)
            self.screencast_active = False
            self.screencast_queue = None
            return False

    async def _stop_screencast(self) -> None:
        if not self.screencast_active:
            return
        self.screencast_active = False
        try:
            if self.cdp and self._screencast_handler:
                try:
                    self.cdp.remove_listener("Page.screencastFrame", self._screencast_handler)
                except Exception:
                    pass
            if self.cdp:
                try:
                    await self.cdp.send("Page.stopScreencast")
                except Exception:
                    pass
        except Exception:
            pass
        finally:
            if self.screencast_queue is not None:
                while not self.screencast_queue.empty():
                    try:
                        self.screencast_queue.get_nowait()
                    except asyncio.QueueEmpty:
                        break
            self._screencast_handler = None
            self.screencast_queue = None

    async def _set_screencast_quality(self, new_quality: int) -> None:
        """Dynamically adjusts CDP screencast JPEG quality (e.g. q60 during video, q100 for crisp text)."""
        new_quality = max(20, min(100, int(new_quality)))
        if new_quality == self.current_screencast_quality:
            return
        self.current_screencast_quality = new_quality
        self.jpeg_quality = new_quality
        if self.cdp and self.screencast_active and self._screencast_handler:
            try:
                # Stop and immediately restart screencast with new quality.
                # Event listener remains attached and receives frames seamlessly.
                await self.cdp.send("Page.stopScreencast")
                await self.cdp.send(
                    "Page.startScreencast",
                    {
                        "format": "jpeg",
                        "quality": new_quality,
                        "everyNthFrame": 1,
                        "maxWidth": self.width,
                        "maxHeight": self.height,
                    },
                )
                logger.info(
                    "Dynamic JPEG quality switched to %d (Video mode: %s)",
                    new_quality,
                    new_quality < self.base_jpeg_quality,
                )
            except Exception as exc:
                logger.debug("Failed updating screencast quality: %s", exc)

    async def send_clipboard_to_amiga(self, text: str) -> bool:
        if not text or not self.running:
            return False
        try:
            payload = text.encode("utf-8")
            pkt = struct.pack(">BI", MSG_CLIPBOARD, len(payload)) + payload
            return await self.send_packet(pkt)
        except Exception as e:
            logger.warning("Error sending clipboard to Amiga: %s", e)
            return False

    async def save_storage_state(self, force: bool = False) -> None:
        if not self.context:
            return
        now = time.monotonic()
        if not force and (now - self._last_storage_save_time) < 15.0:
            return
        async with self._storage_save_lock:
            now2 = time.monotonic()
            if not force and (now2 - self._last_storage_save_time) < 15.0:
                return
            saved = await export_merged_storage_state(self.context, self.storage_state_file)
            if saved:
                self._last_storage_save_time = time.monotonic()
                logger.info("Saved persistent website cookies and storage state to %s", self.storage_state_file)

    def _schedule_storage_save(self, delay: float = 3.0) -> None:
        if not self.running or not self.is_persistent_context or not self.context:
            return
        if self._pending_storage_save_task and not self._pending_storage_save_task.done():
            return

        async def _delayed_save() -> None:
            try:
                await asyncio.sleep(delay)
                await self.save_storage_state(force=True)
            except asyncio.CancelledError:
                pass
            except Exception as e:
                logger.debug("_delayed_save warning: %s", e)

        task = asyncio.create_task(_delayed_save())
        self._pending_storage_save_task = task
        self.background_tasks.add(task)
        task.add_done_callback(self.background_tasks.discard)

    async def _periodic_storage_sync_loop(self) -> None:
        while self.running:
            try:
                await asyncio.sleep(120.0)
                if not self.running:
                    break
                await self.save_storage_state(force=False)
            except asyncio.CancelledError:
                break
            except Exception as e:
                logger.debug("Periodic storage sync warning: %s", e)

    # ------------------------------------------------------------------------
    # Real-Time Audio Capture & Streaming Loop (PipeWire -> Amiga AHI)
    # ------------------------------------------------------------------------
    async def _audio_stream_loop(self) -> None:
        proc: Optional[asyncio.subprocess.Process] = None
        try:
            unmute_all_browser_sink_inputs()
            if platform.system() == "Darwin":
                mac_dev = _find_macos_audio_device()
                if not mac_dev:
                    return
                proc = await asyncio.create_subprocess_exec(
                    "ffmpeg", "-loglevel", "error",
                    "-f", "avfoundation",
                    "-i", f":{mac_dev}",
                    "-ar", str(AUDIO_SAMPLE_RATE),
                    "-ac", str(AUDIO_CHANNELS),
                    "-f", "s16be",
                    "-",
                    stdout=asyncio.subprocess.PIPE,
                    stderr=asyncio.subprocess.DEVNULL,
                )
                logger.info(
                    "macOS Audio stream capture active (%s -> %dHz S16BE Stereo)",
                    mac_dev,
                    AUDIO_SAMPLE_RATE,
                )
            else:
                proc = await asyncio.create_subprocess_exec(
                    "parec",
                    f"--device={AUDIO_SINK_NAME}.monitor",
                    "--format=s16be",
                    f"--rate={AUDIO_SAMPLE_RATE}",
                    f"--channels={AUDIO_CHANNELS}",
                    "--latency-msec=45",
                    stdout=asyncio.subprocess.PIPE,
                    stderr=asyncio.subprocess.DEVNULL,
                )
                logger.info(
                    "Audio stream capture active (%s.monitor -> %dHz S16BE Stereo)",
                    AUDIO_SINK_NAME,
                    AUDIO_SAMPLE_RATE,
                )
            chunks_sent = 0
            loop_iter = 0
            silent_streak = 0
            audio_loop = asyncio.get_running_loop()
            while self.running and proc.stdout:
                chunk = await proc.stdout.readexactly(AUDIO_CHUNK_BYTES)
                if not self.running:
                    break

                loop_iter += 1
                if loop_iter == 1 or (silent_streak >= 25 and loop_iter % 120 == 1):
                    await audio_loop.run_in_executor(None, unmute_all_browser_sink_inputs)

                if _FAST_PIXCONV is not None:
                    if _FAST_PIXCONV.pcm_s16be_is_silent(chunk, len(chunk), 10) == 1:
                        silent_streak += 1
                        continue
                else:
                    if chunk == b"\x00" * len(chunk):
                        silent_streak += 1
                        continue
                silent_streak = 0

                comp = lz4.block.compress(chunk, store_size=False)
                if len(comp) < len(chunk):
                    codec = CODEC_LZ4
                    payload = comp
                else:
                    codec = CODEC_RAW
                    payload = chunk

                hdr = STRUCT_MSG_AUDIO.pack(
                    MSG_AUDIO,
                    AUDIO_SAMPLE_RATE,
                    AUDIO_CHANNELS,
                    codec,
                    len(chunk),
                    len(payload),
                )
                await self.send_packet(hdr + payload)
                self.last_audio_time = time.monotonic()
                chunks_sent += 1
                if chunks_sent == 1 or chunks_sent % 50 == 0:
                    logger.info("Streamed %d audio chunks to Amiga AHI (latest=%dB)", chunks_sent, len(payload))
        except asyncio.CancelledError:
            pass
        except asyncio.IncompleteReadError:
            pass
        except Exception as exc:
            logger.debug("Audio capture loop ended: %s", exc)
        finally:
            if proc is not None:
                try:
                    proc.terminate()
                    await asyncio.wait_for(proc.wait(), timeout=1.0)
                except Exception:
                    try:
                        proc.kill()
                    except Exception:
                        pass

    # ------------------------------------------------------------------------
    # Frame Capture & Dirty-Slice LZ4 Streaming Loop
    # ------------------------------------------------------------------------
    async def _capture_jpeg_fast(self) -> Optional[bytes]:
        gen = self._tab_generation
        cdp_ref = self.cdp
        page_ref = self.page
        if self._switching_tab or not self.running or page_ref is None:
            return None
        if cdp_ref is not None:
            try:
                res = await asyncio.wait_for(
                    cdp_ref.send(
                        "Page.captureScreenshot",
                        {
                            "format": "jpeg",
                            "quality": self.jpeg_quality,
                            "optimizeForSpeed": True,
                        },
                    ),
                    timeout=2.5,
                )
                b64_data = res.get("data")
                if b64_data:
                    return base64.b64decode(b64_data)
            except Exception as exc:
                logger.debug("CDP screenshot error: %s", exc)
                if self._switching_tab or self._tab_generation != gen or self.cdp is not cdp_ref or not self.running:
                    return None
                exc_str = str(exc)
                if "Not attached to an active page" in exc_str:
                    return None
                if self.screencast_active:
                    if "DirectCDP WebSocket closed" not in exc_str:
                        try:
                            await cdp_ref.send("Page.stopScreencast")
                            res = await asyncio.wait_for(
                                cdp_ref.send(
                                    "Page.captureScreenshot",
                                    {
                                        "format": "jpeg",
                                        "quality": self.jpeg_quality,
                                        "optimizeForSpeed": True,
                                    },
                                ),
                                timeout=2.0,
                            )
                            b64_data = res.get("data")
                            if b64_data:
                                return base64.b64decode(b64_data)
                        except Exception:
                            pass
                        finally:
                            if (
                                self.screencast_active
                                and not self._switching_tab
                                and self._tab_generation == gen
                                and self.cdp is cdp_ref
                            ):
                                try:
                                    await cdp_ref.send(
                                        "Page.startScreencast",
                                        {
                                            "format": "jpeg",
                                            "quality": self.jpeg_quality,
                                            "everyNthFrame": 1,
                                            "maxWidth": self.width,
                                            "maxHeight": self.height,
                                        },
                                    )
                                except Exception:
                                    pass
                elif (
                    not self._switching_tab
                    and self._tab_generation == gen
                    and self.context
                    and self.page is page_ref
                    and self.engine == "chromium"
                ):
                    try:
                        await asyncio.wait_for(
                            self._attach_direct_cdp(page_ref),
                            timeout=2.0,
                        )
                    except Exception:
                        if self._tab_generation == gen and self.cdp is cdp_ref:
                            self.cdp = None
        if self._switching_tab or self._tab_generation != gen or self.page is not page_ref or page_ref is None or not self.running:
            return None
        try:
            return await asyncio.wait_for(
                page_ref.screenshot(
                    type="jpeg",
                    quality=self.jpeg_quality,
                    full_page=False,
                    timeout=2000,
                    animations="disabled",
                    caret="initial",
                ),
                timeout=2.5,
            )
        except Exception:
            return None

    async def _capture_and_send_frame(self, force: bool = False) -> None:
        if not self.running or not self.page or self._switching_tab:
            return
        gen = self._tab_generation

        # Prefer CDP screencast (push-based) when available — no request/response overhead
        if self.screencast_active and self.screencast_queue is not None:
            params = None
            while not self.screencast_queue.empty():
                try:
                    nxt = self.screencast_queue.get_nowait()
                    if params is not None:
                        self._ack_screencast_frame(params.get("sessionId"))
                    params = nxt
                except asyncio.QueueEmpty:
                    break

            if params is not None:
                try:
                    b64_data = params.get("data")
                    self._ack_screencast_frame(params.get("sessionId"))
                    if b64_data:
                        if self._switching_tab or self._tab_generation != gen:
                            return
                        jpeg_bytes = base64.b64decode(b64_data)
                        await self._encode_and_send_png(jpeg_bytes, self.force_full_frame, tab_gen=gen)
                        return
                except Exception as exc:
                    logger.debug("Screencast frame decode error: %s", exc)

            if self._switching_tab or self._tab_generation != gen:
                return

            # If screencast is active and queue was empty, capture fallback screenshot if:
            # - No initial frame has EVER been sent yet (self.last_frame_hash is None), OR
            # - An explicit full refresh was requested (self.force_full_frame), OR
            # - Video/active interaction is ongoing and CDP screencast hasn't pushed a frame for > 65ms.
            now = time.monotonic()
            is_dynamic = (
                self.is_video_mode
                or self._video_element_playing
                or (now < self.active_until)
            )
            screencast_stalled = is_dynamic and ((now - self.last_screencast_frame_time) >= 0.065)
            needs_frame = self.force_full_frame or (self.last_frame_hash is None) or screencast_stalled
            if not needs_frame:
                return

        if self._switching_tab or self._tab_generation != gen:
            return

        # Fallback to direct fast CDP screenshot (when screencast is not active or frame is forced)
        try:
            jpeg_bytes = await self._capture_jpeg_fast()
            if not jpeg_bytes:
                return
        except Exception as exc:
            logger.debug("Screenshot skipped during navigation: %s", exc)
            return

        if self._switching_tab or self._tab_generation != gen:
            return

        await self._encode_and_send_png(jpeg_bytes, self.force_full_frame, tab_gen=gen)

    def _encode_h264_frame_sync(
        self,
        png_bytes: bytes,
        w: int,
        h: int,
        yuv_buf: ctypes.Array,
    ) -> Optional[bytes]:
        """Decode JPEG -> YUV420P and encode H.264 NAL packet in worker thread."""
        yuv_raw: Optional[Union[bytes, bytearray, memoryview]] = None
        expected_len = (w * h * 3) // 2

        if _FAST_PIXCONV is not None and _HAS_NATIVE_YUV_DECODER:
            try:
                if _FAST_PIXCONV.decode_jpeg_to_yuv420p(png_bytes, len(png_bytes), w, h, yuv_buf) == 0:
                    yuv_raw = memoryview(yuv_buf).cast("B")
            except Exception:
                pass

        if yuv_raw is None and _TURBO_JPEG is not None:
            try:
                yarr, _ = _TURBO_JPEG.decode_to_yuv(png_bytes, pad=1)
                if len(yarr) == expected_len:
                    yuv_raw = memoryview(yarr)
            except Exception:
                pass

        if yuv_raw is None:
            try:
                im = Image.open(io.BytesIO(png_bytes)).convert("YCbCr")
                if im.size != (w, h):
                    im = im.resize((w, h))
                y, cb, cr = im.split()
                y_b = y.tobytes()
                cb_sub = cb.resize((w // 2, h // 2), Image.Resampling.BILINEAR).tobytes()
                cr_sub = cr.resize((w // 2, h // 2), Image.Resampling.BILINEAR).tobytes()
                yuv_raw = y_b + cb_sub + cr_sub
            except Exception:
                pass

        if yuv_raw is None:
            return None

        enc = self._get_h264_encoder(w, h)
        if not enc:
            return None
        return enc.encode_frame(yuv_raw)

    def _ensure_work_buffers(self, width: int, height: int, fmt: int) -> Tuple[int, int]:
        num_px = width * height
        if fmt == FMT_YUV420P:
            bpp = 1
            frame_bytes_len = (num_px * 3) // 2
            if self._yuv_buf is None or len(self._yuv_buf) != frame_bytes_len:
                self._yuv_buf = ctypes.create_string_buffer(frame_bytes_len)
            self._argb_buf = None
            self._rgb565_buf = None
        elif fmt == FMT_RGB565:
            bpp = 2
            frame_bytes_len = num_px * 2
            if self._rgb565_buf is None or len(self._rgb565_buf) != frame_bytes_len:
                self._rgb565_buf = ctypes.create_string_buffer(frame_bytes_len)
            self._argb_buf = None
            self._yuv_buf = None
        else:
            bpp = 4
            frame_bytes_len = num_px * 4
            if self._argb_buf is None or len(self._argb_buf) != frame_bytes_len:
                self._argb_buf = ctypes.create_string_buffer(frame_bytes_len)
            self._rgb565_buf = None
            self._yuv_buf = None

        max_pkt_len = frame_bytes_len + (frame_bytes_len // 255) + 4096
        if self.prev_raw_buf is None or len(self.prev_raw_buf) != frame_bytes_len or self.prev_dims != (width, height):
            self.prev_raw_buf = ctypes.create_string_buffer(frame_bytes_len)
            self.prev_dims = (width, height)
            self._pkt_buf = ctypes.create_string_buffer(max_pkt_len)
        elif self._pkt_buf is None or len(self._pkt_buf) < max_pkt_len:
            self._pkt_buf = ctypes.create_string_buffer(max_pkt_len)
        return bpp, frame_bytes_len

    async def _encode_and_send_png(self, png_bytes: bytes, force: bool = False, tab_gen: Optional[int] = None) -> None:
        """Decode PNG/JPEG, run dirty-region detection, LZ4 compress, and send to Amiga."""
        gen = self._tab_generation if tab_gen is None else tab_gen
        if self._switching_tab or self._tab_generation != gen or not self.running:
            return

        t0 = time.monotonic()
        w = self.width
        h = self.height
        fmt = self.pixel_format

        # Use Motion-JPEG only if explicitly requested via @CODEC:jpeg / @CODEC:mjpeg.
        # Otherwise default to LZ4 dirty-slice streaming which is 100x faster for the Amiga CPU!
        use_mjpeg = (self.codec_mode == "jpeg")
        cur_url = (self.page.url or "").lower() if (not use_mjpeg and self.page) else ""
        is_video_site = _is_video_platform_url(cur_url)
        last_audio = getattr(self, "last_audio_time", 0.0)
        audio_active = (last_audio > 0.0) and (last_audio > t0 - 2.0)
        # Emit H.264 video stream when in video stream mode, on video sites or when requested
        should_h264 = (not use_mjpeg) and (
            (self.codec_mode == "h264")
            or (self.stream_mode == "h264")
            or (self.stream_mode == "auto" and (self.is_video_mode or self._video_element_playing or (is_video_site and audio_active)))
        )

        was_h264 = getattr(self, "_was_h264", False)
        force_full = self.force_full_frame or force or (self.last_frame_hash is None) or (not should_h264 and was_h264)

        digest: Optional[bytes] = None
        if not should_h264:
            digest = hashlib.md5(png_bytes).digest()
            if not force_full and digest == self.last_frame_hash:
                return

        if use_mjpeg:
            if self._switching_tab or self._tab_generation != gen or not self.running:
                return
            self.last_frame_hash = digest
            self.force_full_frame = False
            self._was_h264 = False
            hdr = STRUCT_MSG_FRAME.pack(
                MSG_FRAME,
                w,
                h,
                FMT_ARGB32,
                CODEC_JPEG,
                len(png_bytes),
            )
            await self.send_packet(hdr + png_bytes)
            return

        if should_h264:
            yuv_len = (w * h * 3) // 2
            if self._yuv_buf is None or len(self._yuv_buf) != yuv_len:
                self._yuv_buf = ctypes.create_string_buffer(yuv_len)
            loop = asyncio.get_running_loop()
            h264_pkt = await loop.run_in_executor(
                self._encode_executor,
                self._encode_h264_frame_sync,
                png_bytes,
                w,
                h,
                self._yuv_buf,
            )
            if self._switching_tab or self._tab_generation != gen or not self.running:
                return
            if h264_pkt:
                self.force_full_frame = False
                self.last_frame_hash = b"H264"
                self._was_h264 = True
                hdr = STRUCT_MSG_FRAME.pack(
                    MSG_FRAME,
                    w,
                    h,
                    FMT_YUV420P,
                    CODEC_H264,
                    len(h264_pkt),
                )
                await self.send_packet(hdr + h264_pkt)
                self._total_h264_frames += 1
                if (self._total_h264_frames % 50) == 1:
                    logger.info("Sent H.264 video frame #%d: len=%d (%dx%d via VA-API/libx264)",
                                self._total_h264_frames, len(h264_pkt), w, h)
                return
            # Fallback to LZ4 if H.264 encoder produced no frame yet
            digest = hashlib.md5(png_bytes).digest()
            if was_h264:
                force_full = True

        prev_dims_before = self.prev_dims
        prev_buf_before = self.prev_raw_buf
        bpp, _ = self._ensure_work_buffers(w, h, fmt)
        if prev_buf_before is None or prev_dims_before != (w, h):
            force_full = True

        t_decode = time.monotonic()
        loop = asyncio.get_running_loop()
        header_and_payload = await loop.run_in_executor(
            self._encode_executor,
            self._encode_frame_or_slice_sync,
            png_bytes,
            w,
            h,
            fmt,
            bpp,
            self.prev_raw_buf,
            force_full,
            self._argb_buf,
            self._rgb565_buf,
            self._yuv_buf,
            self._pkt_buf,
            self.allow_yuv_rect,
            self.is_video_mode or self._video_element_playing,
        )
        if self._switching_tab or self._tab_generation != gen or not self.running:
            return
        self.last_frame_hash = digest
        self.force_full_frame = False
        self._was_h264 = False
        t_encode = time.monotonic()
        if header_and_payload:
            await self.send_packet(header_and_payload)
            if not hasattr(self, '_total_frames_sent'):
                self._total_frames_sent = 0
            self._total_frames_sent += 1
            if self._total_frames_sent <= 10 or (self._total_frames_sent % 30) == 0:
                logger.info("Sent frame #%d to Amiga: len=%d (dims=%dx%d, fmt=%d, force=%s)",
                            self._total_frames_sent, len(header_and_payload), w, h, fmt, force_full)
        t_send = time.monotonic()

        # Log per-frame timing every 5 seconds to find the bottleneck
        now2 = time.monotonic()
        if not hasattr(self, '_perf_log_time'):
            self._perf_log_time = now2
            self._perf_times = []
        if now2 - self._perf_log_time >= 5.0:
            self._perf_times.append((t_decode - t0, t_encode - t_decode, t_send - t_encode, t_send - t0))
            if len(self._perf_times) >= 30:
                avg_decode = sum(x[0] for x in self._perf_times) / len(self._perf_times)
                avg_encode = sum(x[1] for x in self._perf_times) / len(self._perf_times)
                avg_send = sum(x[2] for x in self._perf_times) / len(self._perf_times)
                avg_total = sum(x[3] for x in self._perf_times) / len(self._perf_times)
                logger.info("Frame timing (ms): decode=%.1f encode=%.1f send=%.1f total=%.1f (target=33ms)",
                            avg_decode * 1000, avg_encode * 1000, avg_send * 1000, avg_total * 1000)
                self._perf_times = []
                self._perf_log_time = now2

    @staticmethod
    def _encode_frame_or_slice_sync(
        jpeg_bytes: bytes,
        width: int,
        height: int,
        pixel_format: int,
        bpp: int,
        prev_buf: ctypes.Array,
        force_full: bool,
        argb_buf: Optional[ctypes.Array] = None,
        rgb565_buf: Optional[ctypes.Array] = None,
        yuv_buf: Optional[ctypes.Array] = None,
        pkt_buf: Optional[ctypes.Array] = None,
        allow_yuv_rect: bool = False,
        is_video_mode: bool = False,
    ) -> Optional[bytes]:
        try:
            # 1. Ultra-Fast C-Native Pipeline (libturbojpeg SIMD + AVX2 dirty-rect + liblz4 in a single C call)
            if _FAST_PIXCONV is not None and _HAS_NATIVE_PACKET_ENCODER and pkt_buf is not None:
                work_buf = yuv_buf if pixel_format == FMT_YUV420P else (rgb565_buf if pixel_format == FMT_RGB565 else argb_buf)
                if work_buf is not None:
                    lz4_accel = (3 | 0x100) if is_video_mode else 3
                    pkt_len = _FAST_PIXCONV.encode_jpeg_to_frame_packet(
                        jpeg_bytes,
                        len(jpeg_bytes),
                        width,
                        height,
                        pixel_format,
                        1 if force_full else 0,
                        1 if allow_yuv_rect else 0,
                        lz4_accel,
                        prev_buf,
                        work_buf,
                        pkt_buf,
                        len(pkt_buf),
                    )
                    if pkt_len == 0:
                        return None
                    if pkt_len > 0:
                        return ctypes.string_at(pkt_buf, pkt_len)

            raw_bytes: Optional[bytes] = None
            if _TURBO_JPEG is not None:
                try:
                    jw, jh, jsub, _ = _TURBO_JPEG.decode_header(jpeg_bytes)
                    if jw == width and jh == height:
                        if pixel_format == FMT_YUV420P and jsub == TJSAMP_420:
                            # Direct hardware-SIMD planar YUV420 extraction (skips RGB conversion completely!)
                            # MUST pass pad=1 so row stride has NO alignment padding, exactly matching
                            # the Amiga client's (w * h * 3) // 2 planar layout!
                            yuv_arr, _ = _TURBO_JPEG.decode_to_yuv(jpeg_bytes, pad=1)
                            expected_len = (width * height * 3) // 2
                            if len(yuv_arr) == expected_len:
                                raw_bytes = yuv_arr.tobytes()
                        else:
                            rgb_arr = _TURBO_JPEG.decode(jpeg_bytes, pixel_format=TJPF_RGB)
                            raw_rgb = rgb_arr.tobytes()
                            num_pixels = width * height
                            if pixel_format == FMT_RGB565:
                                if rgb565_buf is None or len(rgb565_buf) != num_pixels * 2:
                                    rgb565_buf = ctypes.create_string_buffer(num_pixels * 2)
                                if _FAST_PIXCONV is not None:
                                    _FAST_PIXCONV.rgb24_to_be_rgb565(raw_rgb, rgb565_buf, num_pixels)
                                    raw_bytes = rgb565_buf.raw
                            elif pixel_format == FMT_ARGB32:
                                if argb_buf is None or len(argb_buf) != num_pixels * 4:
                                    argb_buf = ctypes.create_string_buffer(num_pixels * 4)
                                if _FAST_PIXCONV is not None:
                                    _FAST_PIXCONV.rgb24_to_be_argb32(raw_rgb, argb_buf, num_pixels)
                                    raw_bytes = argb_buf.raw
                            elif pixel_format == FMT_YUV420P:
                                if yuv_buf is None or len(yuv_buf) != (num_pixels * 3) // 2:
                                    yuv_buf = ctypes.create_string_buffer((num_pixels * 3) // 2)
                                if _FAST_PIXCONV is not None:
                                    _FAST_PIXCONV.rgb24_to_yuv420p(raw_rgb, yuv_buf, width, height)
                                    raw_bytes = yuv_buf.raw
                except Exception as _tj_err:
                    raw_bytes = None

            if raw_bytes is None:
                img = Image.open(io.BytesIO(jpeg_bytes))
                raw_bytes = image_to_big_endian_pixels(
                    img, width, height, pixel_format, argb_buf, rgb565_buf, yuv_buf
                )

            if pixel_format == FMT_YUV420P and not allow_yuv_rect:
                # 12 bpp Planar YUV420: Stream Y, U, V planes compressed with LZ4
                if not force_full and prev_buf is not None and len(prev_buf) == len(raw_bytes):
                    if prev_buf.raw == raw_bytes:
                        return None
                if prev_buf is not None and len(prev_buf) == len(raw_bytes):
                    ctypes.memmove(prev_buf, raw_bytes, len(raw_bytes))
                compressed = lz4.block.compress(raw_bytes, store_size=False)
                hdr = STRUCT_MSG_FRAME.pack(
                    MSG_FRAME,
                    width,
                    height,
                    pixel_format,
                    CODEC_LZ4,
                    len(compressed),
                )
                return hdr + compressed

            if not force_full and _FAST_PIXCONV is not None:
                rx = ctypes.c_int(0)
                ry = ctypes.c_int(0)
                rw = ctypes.c_int(0)
                rh = ctypes.c_int(0)
                changed = _FAST_PIXCONV.find_and_update_dirty_rect(
                    raw_bytes,
                    prev_buf,
                    width,
                    height,
                    bpp,
                    ctypes.byref(rx),
                    ctypes.byref(ry),
                    ctypes.byref(rw),
                    ctypes.byref(rh),
                )
                if changed == 0:
                    return None
                rx_val = rx.value
                ry_val = ry.value
                rw_val = rw.value
                rh_val = rh.value

                # 2D Bounding Box optimization: If changed area is < 85% of total viewport, send CODEC_LZ4_RECT!
                if 0 < rw_val <= width and 0 < rh_val <= height and (rw_val * rh_val < int(width * height * 0.85)):
                    if pixel_format == FMT_YUV420P and hasattr(_FAST_PIXCONV, "pack_sub_rect_yuv420p"):
                        pack_buf = ctypes.create_string_buffer((rw_val * rh_val * 3) // 2)
                        _FAST_PIXCONV.pack_sub_rect_yuv420p(raw_bytes, pack_buf, width, height, rx_val, ry_val, rw_val, rh_val)
                    else:
                        pack_buf = ctypes.create_string_buffer(rw_val * rh_val * bpp)
                        _FAST_PIXCONV.pack_sub_rect(raw_bytes, pack_buf, width, bpp, rx_val, ry_val, rw_val, rh_val)
                    comp_rect = lz4.block.compress(pack_buf.raw, store_size=False)
                    rect_payload = STRUCT_RECT_HDR.pack(rx_val, ry_val, rw_val, rh_val) + comp_rect
                    hdr = STRUCT_MSG_FRAME.pack(
                        MSG_FRAME,
                        width,
                        height,
                        pixel_format,
                        CODEC_LZ4_RECT,
                        len(rect_payload),
                    )
                    return hdr + rect_payload
            else:
                ctypes.memmove(prev_buf, raw_bytes, len(raw_bytes))

            compressed = lz4.block.compress(raw_bytes, store_size=False)
            hdr = STRUCT_MSG_FRAME.pack(
                MSG_FRAME,
                width,
                height,
                pixel_format,
                CODEC_LZ4,
                len(compressed),
            )
            return hdr + compressed
        except Exception as exc:
            logger.error("Frame encoding error: %s", exc)
            return None

    async def _frame_stream_loop(self) -> None:
        interval = 1.0 / float(self.target_fps)
        await self._capture_and_send_frame(force=True)

        idle_ticks = 0
        frames_sent = 0
        frames_in_sec = 0
        fps_report_time = time.monotonic()
        while self.running:
            if time.monotonic() < self.pause_frames_until:
                await asyncio.sleep(0.04)
                continue
            if self._switching_tab:
                await asyncio.sleep(0.02)
                continue

            now_tick = time.monotonic()
            if now_tick - self._sec_start_time >= 1.0:
                self._frames_last_sec = frames_in_sec
                frames_in_sec = 0
                self._sec_start_time = now_tick

            # Dynamic Video Detection:
            # 1. Real audio streaming active (paired with video-platform URL or <video> playback)
            last_audio = getattr(self, "last_audio_time", 0.0)
            audio_active = (last_audio > 0.0) and (last_audio > now_tick - 2.0)

            # 2. Page URL check (YouTube watch page, Twitch, video platforms)
            cur_url = (self.page.url or "").lower() if self.page else ""
            is_video_site = _is_video_platform_url(cur_url)

            # 3. Query HTML5 <video> element playback state asynchronously every 0.6 seconds
            if self.page and not self._switching_tab and (now_tick - self._last_video_eval_time) >= 0.6:
                self._last_video_eval_time = now_tick
                target_page = self.page
                eval_gen = self._tab_generation
                async def _eval_video(tp=target_page, eg=eval_gen):
                    try:
                        if (
                            not tp
                            or not self.running
                            or self._switching_tab
                            or self._tab_generation != eg
                            or self.page is not tp
                        ):
                            return
                        check_js = """() => {
                            try {
                                const checkRoot = (root) => {
                                    if (!root || !root.querySelectorAll) return false;
                                    for (const v of root.querySelectorAll('video')) {
                                        if (!v.paused && !v.ended && v.readyState >= 2) {
                                            const r = v.getBoundingClientRect ? v.getBoundingClientRect() : null;
                                            if (!r || (r.width >= 32 && r.height >= 32)) return true;
                                        }
                                    }
                                    for (const el of root.querySelectorAll('*')) {
                                        if (el.shadowRoot && checkRoot(el.shadowRoot)) return true;
                                    }
                                    return false;
                                };
                                if (checkRoot(document)) return true;
                                const yt = document.getElementById('movie_player');
                                if (yt && typeof yt.getPlayerState === 'function') {
                                    const st = yt.getPlayerState();
                                    if (st === 1 || st === 3) return true;
                                }
                            } catch (e) {}
                            return false;
                        }"""
                        is_playing = False
                        frames = list(getattr(tp, "frames", None) or [])[:6]
                        if not frames:
                            frames = [tp]
                        for frame in frames:
                            if (
                                not self.running
                                or self._switching_tab
                                or self._tab_generation != eg
                                or self.page is not tp
                            ):
                                return
                            try:
                                res = await asyncio.wait_for(frame.evaluate(check_js), timeout=0.35)
                                if res:
                                    is_playing = True
                                    break
                            except Exception:
                                pass
                        if (
                            self.running
                            and not self._switching_tab
                            and self._tab_generation == eg
                            and self.page is tp
                        ):
                            self._video_element_playing = bool(is_playing)
                    except Exception:
                        pass
                t_vid = asyncio.create_task(_eval_video())
                self.background_tasks.add(t_vid)
                t_vid.add_done_callback(self.background_tasks.discard)

            video_active = self._video_element_playing or (is_video_site and audio_active)
            if video_active:
                self._last_video_state_time = now_tick
                if not self.is_video_mode:
                    self.is_video_mode = True
                    logger.info("Video playback detected (audio=%s, elem=%s, site=%s) -> Entering Video Stream mode",
                                audio_active, self._video_element_playing, is_video_site)
                    await self._set_screencast_quality(self.video_jpeg_quality)
            elif self.is_video_mode:
                # Video ended/paused; wait 3.0s of static/quiet before restoring crisp high quality (e.g. q80)
                if (now_tick - self._last_video_state_time) >= 3.0:
                    self.is_video_mode = False
                    logger.info("Video playback ended/idle -> Restoring standard browsing mode")
                    await self._set_screencast_quality(self.base_jpeg_quality)
            elif (
                self._h264_encoder is not None
                and self.stream_mode != "h264"
                and self.codec_mode != "h264"
                and (now_tick - self._last_video_state_time) >= 10.0
            ):
                logger.info("Video idle for 10s -> stopping H.264 encoder subprocess to reclaim RAM")
                self._h264_encoder.stop()
                self._h264_encoder = None

            t0 = time.monotonic()
            forced = self.force_frame_event.is_set()
            if forced:
                self.force_frame_event.clear()
                idle_ticks = 0

            prev_hash = self.last_frame_hash
            await self._capture_and_send_frame(force=forced)

            # A frame was actually sent if the content hash changed
            if self.last_frame_hash != prev_hash:
                frames_sent += 1
                frames_in_sec += 1
                idle_ticks = 0
            else:
                now_loop = time.monotonic()
                if (
                    self.is_video_mode
                    or self._video_element_playing
                    or now_loop < self.active_until
                ):
                    idle_ticks = 0
                else:
                    idle_ticks += 1

            elapsed = time.monotonic() - t0
            # When page has been static for > 5 frames, back off to 6 FPS to save Pi 4 CPU,
            # while waking up instantly (0ms) on user input or animation
            cur_interval = interval if idle_ticks < 6 else 0.16
            wait_time = cur_interval - elapsed
            if wait_time > 0.001:
                try:
                    await asyncio.wait_for(self.force_frame_event.wait(), timeout=wait_time)
                except asyncio.TimeoutError:
                    pass
            else:
                # Cooperatively yield if behind schedule without adding artificial delays
                await asyncio.sleep(0)

            # Log actual FPS every 5 seconds
            now = time.monotonic()
            if now - fps_report_time >= 5.0:
                actual_fps = frames_sent / (now - fps_report_time)
                logger.info("Streaming FPS: %.1f (target=%d, idle_ticks=%d, format=%s)",
                            actual_fps, self.target_fps, idle_ticks,
                            "YUV420P" if self.pixel_format == FMT_YUV420P else
                            "RGB565" if self.pixel_format == FMT_RGB565 else "ARGB32")
                frames_sent = 0
                fps_report_time = now

    # ------------------------------------------------------------------------
    # Native AmigaOS 4 Right-Click Context Menu Target Inspection
    # ------------------------------------------------------------------------
    async def _send_native_context_menu(self, cx: int, cy: int) -> None:
        """Inspects the DOM element at (cx, cy) and sends MSG_CONTEXT_MENU to open AmigaOS 4 popupmenu.class."""
        self.pause_frames_until = time.monotonic() + 1.2
        link_url = ""
        link_text = ""
        media_url = ""
        if self.page:
            try:
                info = await asyncio.wait_for(
                    self.page.evaluate(
                        """([x, y]) => {
                            const probePoints = [
                                [0, 0], [0, -3], [0, 3], [-4, 0], [4, 0],
                                [-4, -4], [4, -4], [-4, 4], [4, 4],
                                [0, -7], [0, 7], [-8, 0], [8, 0],
                                [-12, 0], [12, 0], [-16, 0], [16, 0]
                            ];
                            let foundA = null;
                            let foundImg = null;
                            let foundMedia = null;

                            for (const [dx, dy] of probePoints) {
                                const px = Math.max(0, x + dx);
                                const py = Math.max(0, y + dy);
                                let el = document.elementFromPoint(px, py);
                                if (!el) continue;
                                while (el && el.shadowRoot && el.shadowRoot.elementFromPoint) {
                                    const inner = el.shadowRoot.elementFromPoint(px, py);
                                    if (!inner || inner === el) break;
                                    el = inner;
                                }
                                if (!foundA) {
                                    const a = el.closest ? el.closest('a[href], a[xlink\\\\:href], [role="link"]') : (el.tagName === 'A' ? el : null);
                                    if (a) {
                                        let h = '';
                                        if (typeof a.href === 'string' && a.href) h = a.href;
                                        else if (a.href && typeof a.href.baseVal === 'string') h = a.href.baseVal;
                                        else h = a.getAttribute('href') || a.getAttribute('xlink:href') || '';
                                        if (h && !h.startsWith('javascript:')) foundA = a;
                                    }
                                }
                                if (!foundImg) {
                                    const img = (el.tagName === 'IMG') ? el : (el.closest ? el.closest('img[src]') : null);
                                    if (img && (img.currentSrc || img.src)) foundImg = img;
                                }
                                if (!foundMedia) {
                                    const media = el.closest ? el.closest('video,audio,source') : null;
                                    if (media) foundMedia = media;
                                }
                                if (foundA) break;
                            }

                            let linkHref = '';
                            let linkLabel = '';
                            if (foundA) {
                                let rawHref = '';
                                if (typeof foundA.href === 'string' && foundA.href) {
                                    rawHref = foundA.href;
                                } else if (foundA.href && typeof foundA.href.baseVal === 'string') {
                                    rawHref = foundA.href.baseVal;
                                } else {
                                    rawHref = foundA.getAttribute('href') || foundA.getAttribute('xlink:href') || '';
                                }
                                try {
                                    linkHref = new URL(rawHref, document.baseURI).href;
                                } catch (e) {
                                    linkHref = rawHref;
                                }
                                linkLabel = (foundA.innerText || foundA.textContent || foundA.getAttribute('title') || foundA.getAttribute('aria-label') || linkHref || '').trim().replace(/\\s+/g, ' ').slice(0, 72);
                            }

                            let mSrc = '';
                            if (foundImg) {
                                mSrc = foundImg.currentSrc || foundImg.src || '';
                            } else if (foundMedia) {
                                mSrc = foundMedia.currentSrc || foundMedia.src || '';
                                if (!mSrc && foundMedia.querySelector) {
                                    const s = foundMedia.querySelector('source[src]');
                                    if (s) mSrc = s.src || '';
                                }
                            }
                            if (mSrc) {
                                try {
                                    mSrc = new URL(mSrc, document.baseURI).href;
                                } catch (e) {}
                            }
                            return { linkHref, linkLabel, mSrc };
                        }""",
                        [cx, cy],
                    ),
                    timeout=1.0,
                )
                if isinstance(info, dict):
                    link_url = str(info.get("linkHref") or "")
                    link_text = str(info.get("linkLabel") or "")
                    media_url = str(info.get("mSrc") or "")
            except Exception as exc:
                logger.warning("Context menu target inspection warning at (%d, %d): %s", cx, cy, exc)

            if not link_url and self.last_hover_url:
                link_url = self.last_hover_url
                logger.info("Using last_hover_url fallback for context menu: %s", link_url)
                if not link_text:
                    link_text = link_url.split("/")[-1] or link_url

        logger.info(
            "Sending Native AmigaOS 4 Context Menu for (%d, %d): link=%r text=%r media=%r",
            cx, cy, link_url, link_text, media_url,
        )
        link_b = link_url.encode("utf-8", errors="replace")[:511].ljust(512, b"\x00")
        text_b = link_text.encode("latin-1", errors="replace")[:127].ljust(128, b"\x00")
        media_b = media_url.encode("utf-8", errors="replace")[:511].ljust(512, b"\x00")
        pkt = STRUCT_MSG_CTX_MENU.pack(MSG_CONTEXT_MENU, link_b, text_b, media_b)
        await self.send_packet(pkt)

    # ------------------------------------------------------------------------
    # Input & Control Channel Reader (Amiga -> Server)
    # ------------------------------------------------------------------------
    async def _input_read_loop(self) -> None:
        while self.running:
            try:
                type_byte = await self.reader.readexactly(1)
            except (asyncio.IncompleteReadError, ConnectionResetError):
                logger.info("Client closed connection.")
                break
            except Exception as exc:
                logger.error("Socket read error: %s", exc)
                break

            msg_type = type_byte[0]
            if msg_type == MSG_INPUT:
                try:
                    body = await self.reader.readexactly(STRUCT_MSG_INPUT_BODY.size)
                except asyncio.IncompleteReadError:
                    break
                event_type, x, y, delta_y, key_code, raw_text = STRUCT_MSG_INPUT_BODY.unpack(body)
                if event_type == INPUT_WHEEL:
                    pkt_len = 1 + STRUCT_MSG_INPUT_BODY.size
                    buf = getattr(self.reader, "_buffer", None)
                    while buf is not None and len(buf) >= pkt_len and buf[0] == MSG_INPUT and buf[1] == INPUT_WHEEL:
                        try:
                            nxt = await self.reader.readexactly(pkt_len)
                            _, nx, ny, ndy, _, _ = STRUCT_MSG_INPUT_BODY.unpack(nxt[1:])
                            x, y = nx, ny
                            delta_y += ndy
                        except Exception:
                            break
                elif event_type == INPUT_MOUSE_MOVE:
                    pkt_len = 1 + STRUCT_MSG_INPUT_BODY.size
                    buf = getattr(self.reader, "_buffer", None)
                    while buf is not None and len(buf) >= pkt_len and buf[0] == MSG_INPUT and buf[1] == INPUT_MOUSE_MOVE:
                        try:
                            nxt = await self.reader.readexactly(pkt_len)
                            _, nx, ny, _, _, _ = STRUCT_MSG_INPUT_BODY.unpack(nxt[1:])
                            x, y = nx, ny
                        except Exception:
                            break
                text_str = raw_text.split(b"\x00", 1)[0].decode("utf-8", errors="replace")
                await self._handle_input(event_type, x, y, delta_y, key_code, text_str)

            elif msg_type == MSG_DOWNLOAD_RESPONSE:
                try:
                    body = await self.reader.readexactly(STRUCT_MSG_DL_RESP_BODY.size)
                except asyncio.IncompleteReadError:
                    break
                download_id, accepted = STRUCT_MSG_DL_RESP_BODY.unpack(body)
                logger.info("Received MSG_DOWNLOAD_RESPONSE: id=%d, accepted=%d", download_id, accepted)
                fut = self.pending_downloads.get(download_id)
                if fut and not fut.done():
                    fut.set_result(bool(accepted == 1))

            elif msg_type == MSG_UPLOAD_RESPONSE:
                try:
                    body = await self.reader.readexactly(STRUCT_MSG_UPLOAD_RESP.size)
                except asyncio.IncompleteReadError:
                    break
                upload_id, file_size, accepted, pad, raw_fname = STRUCT_MSG_UPLOAD_RESP.unpack(body)
                fname = raw_fname.split(b"\x00", 1)[0].decode("utf-8", errors="replace")
                logger.info("Received MSG_UPLOAD_RESPONSE: id=%d, size=%d, accepted=%d, fname='%s'",
                            upload_id, file_size, accepted, fname)
                entry = self.pending_uploads.get(upload_id)
                if entry:
                    fc, fut, chunk_queue = entry
                    if not fut.done():
                        fut.set_result((bool(accepted == 1), file_size, fname))

            elif msg_type == MSG_UPLOAD_CHUNK:
                try:
                    chunk_hdr = await self.reader.readexactly(struct.calcsize(">II"))
                    u_id, chunk_len = struct.unpack(">II", chunk_hdr)
                    chunk_data = await self.reader.readexactly(chunk_len)
                except asyncio.IncompleteReadError:
                    break
                entry = self.pending_uploads.get(u_id)
                if entry:
                    _, _, chunk_queue = entry
                    await chunk_queue.put(chunk_data)

            elif msg_type == MSG_CLIPBOARD:
                try:
                    clip_hdr = await self.reader.readexactly(struct.calcsize(">I"))
                    (text_len,) = struct.unpack(">I", clip_hdr)
                    if 0 < text_len <= 1048576:
                        clip_bytes = await self.reader.readexactly(text_len)
                        clip_text = clip_bytes.decode("utf-8", errors="replace")
                        logger.info("Received MSG_CLIPBOARD from Amiga (%d chars): %r", len(clip_text), clip_text[:60])
                        if self.page:
                            await self.page.keyboard.insert_text(clip_text)
                            self._trigger_full_refresh(True)
                except (asyncio.IncompleteReadError, Exception) as e:
                    logger.warning("Error reading MSG_CLIPBOARD: %s", e)
            else:
                logger.warning("Unknown msg_type=0x%02x from client", msg_type)

    def _schedule_delayed_frames(self) -> None:
        async def _follow_up():
            for delay in (0.05, 0.15, 0.35, 0.65, 1.10, 1.80, 2.80):
                await asyncio.sleep(delay)
                if not self.running:
                    break
                self.force_frame_event.set()
        t = asyncio.create_task(_follow_up())
        self.background_tasks.add(t)
        t.add_done_callback(self.background_tasks.discard)

    async def _handle_input(
        self,
        event_type: int,
        x: int,
        y: int,
        delta_y: int,
        key_code: int,
        text_or_url: str,
    ) -> None:
        if not self.page:
            return

        cx = max(0, min(self.width - 1, int(x)))
        cy = max(0, min(self.height - 1, int(y)))

        try:
            if event_type == INPUT_CLICK:
                btn = "left"
                if key_code == 2:
                    btn = "middle"
                elif key_code == 3:
                    btn = "right"
                logger.info("INPUT_CLICK (%s) at (%d, %d)", btn, cx, cy)

                if btn == "right":
                    # Inspect element at (cx, cy) and trigger native AmigaOS 4 popupmenu.class
                    await self._send_native_context_menu(cx, cy)
                    return

                if btn == "left" and cx >= (self.width - 20):
                    ratio = float(cy) / float(max(1, self.height))
                    await self.page.evaluate(
                        f"window.scrollTo(0, Math.max(0, (document.documentElement.scrollHeight - window.innerHeight) * {ratio:.4f}))"
                    )
                else:
                    await self.page.mouse.click(cx, cy, button=btn)
                    if btn == "left":
                        self._schedule_storage_save(delay=3.5)
                self._trigger_full_refresh(True)
                self._schedule_delayed_frames()

            elif event_type == INPUT_MOUSE_DOWN:
                btn = "left"
                if key_code == 2:
                    btn = "middle"
                elif key_code == 3:
                    btn = "right"
                logger.info("INPUT_MOUSE_DOWN (%s) at (%d, %d)", btn, cx, cy)

                now = time.monotonic()
                if btn == "left":
                    if (now - self._last_click_time) < 0.35 and abs(cx - self._last_click_x) <= 4 and abs(cy - self._last_click_y) <= 4:
                        self._click_count = min(3, self._click_count + 1)
                    else:
                        self._click_count = 1
                    self._last_click_time = now
                    self._last_click_x = cx
                    self._last_click_y = cy
                else:
                    self._click_count = 1

                self._mouse_down_x = cx
                self._mouse_down_y = cy
                self._mouse_down_btn = btn
                self._mouse_down_time = now

                if btn == "right":
                    await self._send_native_context_menu(cx, cy)
                    return

                if btn == "left" and cx >= (self.width - 20):
                    ratio = float(cy) / float(max(1, self.height))
                    await self.page.evaluate(
                        f"window.scrollTo(0, Math.max(0, (document.documentElement.scrollHeight - window.innerHeight) * {ratio:.4f}))"
                    )
                else:
                    if btn == "left":
                        try:
                            await self.page.evaluate("([x, y]) => window.__vortex_selection && window.__vortex_selection.start(x, y)", [cx, cy])
                        except Exception:
                            pass
                    await self.page.mouse.move(cx, cy)
                    await self.page.mouse.down(button=btn, click_count=self._click_count)
                self._trigger_full_refresh(True)
                self._schedule_delayed_frames()

            elif event_type == INPUT_MOUSE_UP:
                btn = "left"
                if key_code == 2:
                    btn = "middle"
                elif key_code == 3:
                    btn = "right"
                logger.info("INPUT_MOUSE_UP (%s) at (%d, %d)", btn, cx, cy)

                if btn != "right":
                    down_x = self._mouse_down_x if self._mouse_down_x >= 0 else cx
                    down_y = self._mouse_down_y if self._mouse_down_y >= 0 else cy
                    dist = ((cx - down_x) ** 2 + (cy - down_y) ** 2) ** 0.5

                    dragged = False
                    sel_text = ""
                    if btn == "left":
                        try:
                            res = await self.page.evaluate("([x, y]) => window.__vortex_selection ? window.__vortex_selection.finish(x, y) : {dragged: false, text: ''}", [cx, cy])
                            if res and isinstance(res, dict):
                                dragged = bool(res.get("dragged", False))
                                sel_text = res.get("text", "") or ""
                        except Exception:
                            pass

                    # If user dragged, release with click_count=1 to finalize the freeform selection
                    if dist > 4 or dragged:
                        self._click_count = 1
                    await self.page.mouse.move(cx, cy)
                    await self.page.mouse.up(button=btn, click_count=self._click_count)

                    self._mouse_down_btn = ""

                    # Universal check: only if stationary click AND not dragged AND no upload is pending/recent
                    now_up = time.monotonic()
                    if btn == "left" and dist <= 10 and not dragged and not self.pending_uploads and (now_up - self.last_upload_intercept_time) >= 3.0:
                        try:
                            upload_hit = await self.page.evaluate("""([x, y]) => {
                                let el = document.elementFromPoint(x, y);
                                if (!el) return false;
                                let elem = el.nodeType === 1 ? el : el.parentElement;
                                if (!elem) return false;

                                // 1. Direct file input
                                if (elem.tagName === 'INPUT' && elem.type === 'file') {
                                    elem.click();
                                    return true;
                                }

                                // 2. Contains file input
                                let finp = elem.querySelector ? elem.querySelector('input[type="file"]') : null;
                                if (finp) {
                                    finp.click();
                                    return true;
                                }

                                // 3. Label pointing to file input
                                let lbl = elem.closest ? elem.closest('label') : null;
                                if (lbl && lbl.htmlFor) {
                                    let inp = document.getElementById(lbl.htmlFor);
                                    if (inp && inp.type === 'file') {
                                        inp.click();
                                        return true;
                                    }
                                }

                                // 4. Button / anchor / upload trigger element
                                let uploadBtn = elem.closest ? elem.closest(
                                    '#add_files, .attach-panel-multi, .js-attachmentUpload, .js-attachButton, [data-action="upload"], ' +
                                    'input[type="file"], .moxie-shim, .moxie-shim-html5'
                                ) : null;
                                if (uploadBtn) {
                                    let moxieInp = document.querySelector('.moxie-shim input[type="file"], .moxie-shim-html5 input[type="file"]');
                                    if (moxieInp) {
                                        moxieInp.click();
                                        return true;
                                    }
                                    let allInp = Array.from(document.querySelectorAll('input[type="file"]'));
                                    for (let inp of allInp) {
                                        if (inp.id !== 'fileupload' || allInp.length === 1) {
                                            inp.click();
                                            return true;
                                        }
                                    }
                                    if (allInp.length > 0) {
                                        allInp[0].click();
                                        return true;
                                    }
                                }
                                return false;
                            }""", [down_x, down_y])
                            if upload_hit:
                                logger.info("Fallback upload click triggered at (%d, %d)", down_x, down_y)
                                self.last_upload_intercept_time = time.monotonic()
                        except Exception as e:
                            logger.info("Upload element check notice: %s", e)

                    if btn == "left":
                        if sel_text and len(sel_text.strip()) > 0:
                            logger.info("Selected text (%d chars): %r", len(sel_text), sel_text[:60])
                            await self.send_clipboard_to_amiga(sel_text)
                        else:
                            try:
                                sel = await self.page.evaluate("""() => {
                                    let s = window.getSelection() ? window.getSelection().toString() : '';
                                    if (s && s.length > 0) return s;
                                    let el = document.activeElement;
                                    if (el && (el.tagName === 'TEXTAREA' || (el.tagName === 'INPUT' && el.type === 'text'))) {
                                        try {
                                            return el.value.substring(el.selectionStart, el.selectionEnd);
                                        } catch (e) {}
                                    }
                                    return '';
                                }""")
                                if sel and len(sel.strip()) > 0:
                                    logger.info("Selected text (%d chars): %r", len(sel), sel[:60])
                                    await self.send_clipboard_to_amiga(sel)
                            except Exception:
                                pass
                        self._schedule_storage_save(delay=3.5)
                self._trigger_full_refresh(True)
                self._schedule_delayed_frames()

            elif event_type == INPUT_MOUSE_MOVE:
                try:
                    if self._mouse_down_btn == "left":
                        # If user moves mouse with button down, update selection directly via caretRangeFromPoint
                        dist = ((cx - self._mouse_down_x) ** 2 + (cy - self._mouse_down_y) ** 2) ** 0.5
                        if dist > 4:
                            self._click_count = 1
                            try:
                                await self.page.evaluate("([x, y]) => window.__vortex_selection && window.__vortex_selection.update(x, y)", [cx, cy])
                            except Exception:
                                pass
                        await self.page.mouse.move(cx, cy)
                        self._trigger_full_refresh(False)
                    else:
                        await self.page.mouse.move(cx, cy)
                except Exception:
                    pass

                now = time.monotonic()
                if (now - self.last_hover_time) >= 0.25:
                    if self.hover_task and not self.hover_task.done():
                        pass
                    else:
                        self.last_hover_time = now
                        async def _check_hover_async(hx: int, hy: int):
                            if not self.page:
                                return
                            try:
                                hover_url = await asyncio.wait_for(
                                    self.page.evaluate(
                                        """([x, y]) => {
                                            const el = document.elementFromPoint(x, y);
                                            if (!el) return null;
                                            const a = el.closest('a');
                                            return (a && a.href) ? a.href : null;
                                        }""",
                                        [hx, hy],
                                    ),
                                    timeout=0.08,
                                )
                                if hover_url != self.last_hover_url:
                                    self.last_hover_url = hover_url
                                    if hover_url:
                                        self._send_progress(100, f"Link: {hover_url[:55]}")
                                    else:
                                        self._send_progress(100, "Bereit")
                            except Exception:
                                pass
                        self.hover_task = asyncio.create_task(_check_hover_async(cx, cy))
                        self.background_tasks.add(self.hover_task)
                        self.hover_task.add_done_callback(self.background_tasks.discard)

            elif event_type == INPUT_WHEEL:
                dy = max(-960, min(960, int(delta_y)))
                if dy == 0 and delta_y != 0:
                    dy = 120 if delta_y > 0 else -120
                logger.info("INPUT_WHEEL dy=%d at (%d, %d) soft=%s", dy, cx, cy, self.soft_scroll)
                scrolled = False
                try:
                    scrolled = bool(
                        await self.page.evaluate(
                            """([x, y, dy]) => {
                                let el = document.elementFromPoint(x, y);
                                while (el && el !== document.body && el !== document.documentElement) {
                                    const style = window.getComputedStyle(el);
                                    const oy = style ? style.overflowY : '';
                                    if ((oy === 'auto' || oy === 'scroll' || oy === 'overlay') &&
                                        el.clientHeight >= 120 && el.clientWidth >= 150 &&
                                        el.scrollHeight > el.clientHeight + 24) {
                                        const canUp = (dy < 0 && el.scrollTop > 1);
                                        const canDown = (dy > 0 && (el.scrollTop + el.clientHeight) < (el.scrollHeight - 2));
                                        if (canUp || canDown) {
                                            const prev = el.scrollTop;
                                            el.scrollBy({ top: dy, left: 0, behavior: 'instant' });
                                            if (el.scrollTop === prev) {
                                                el.scrollTop = prev + dy;
                                            }
                                            if (el.scrollTop !== prev) return true;
                                        }
                                    }
                                    el = el.parentElement;
                                }
                                const root = document.documentElement;
                                const prevBehavior = root ? root.style.scrollBehavior : '';
                                if (root) root.style.scrollBehavior = 'auto';
                                const se = document.scrollingElement || document.documentElement || document.body;
                                const prevWin = window.scrollY || window.pageYOffset || (se ? se.scrollTop : 0) || 0;
                                window.scrollBy({ top: dy, left: 0, behavior: 'instant' });
                                let afterWin = window.scrollY || window.pageYOffset || (se ? se.scrollTop : 0) || 0;
                                if (afterWin === prevWin && se) {
                                    se.scrollTop = prevWin + dy;
                                    afterWin = se.scrollTop;
                                }
                                if (afterWin === prevWin && document.body) {
                                    const pb = document.body.scrollTop;
                                    document.body.scrollBy({ top: dy, left: 0, behavior: 'instant' });
                                    if (document.body.scrollTop === pb) document.body.scrollTop = pb + dy;
                                    if (document.body.scrollTop !== pb) afterWin = document.body.scrollTop;
                                }
                                if (root) root.style.scrollBehavior = prevBehavior;
                                return (afterWin !== prevWin) || (dy < 0 && prevWin <= 0);
                            }""",
                            [cx, cy, dy],
                        )
                    )
                except Exception:
                    scrolled = False
                if not scrolled:
                    try:
                        await self.page.mouse.move(cx, cy)
                        await self.page.mouse.wheel(0, float(dy))
                    except Exception:
                        pass
                if self.screencast_queue is not None:
                    while not self.screencast_queue.empty():
                        try:
                            dropped = self.screencast_queue.get_nowait()
                            self._ack_screencast_frame(dropped.get("sessionId"))
                        except asyncio.QueueEmpty:
                            break
                self._trigger_full_refresh(True)

            elif event_type == INPUT_KEY_DOWN:
                await self._handle_key_down(key_code, text_or_url)
                if text_or_url in ("Enter", "Return") or key_code in (13, 0x44):
                    self._schedule_storage_save(delay=3.5)
                self.force_frame_event.set()
                self.active_until = time.monotonic() + 1.5

            elif event_type == INPUT_KEY_UP:
                pass

            elif event_type == INPUT_GOTO_URL:
                await self._handle_navigation(text_or_url)
                self._schedule_storage_save(delay=3.0)
                self._trigger_full_refresh(True)
        except Exception as exc:
            logger.warning("Input execution warning (type=%d): %s", event_type, exc)

    async def _scroll_if_not_editing(self, dy: int, absolute: Optional[str] = None) -> bool:
        if not self.page:
            return False
        try:
            is_editing = await self.page.evaluate(
                """() => {
                    const el = document.activeElement;
                    if (!el) return false;
                    const tag = (el.tagName || '').toUpperCase();
                    return (tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT' || el.isContentEditable);
                }"""
            )
            if is_editing:
                return False
            if absolute == "top":
                await self.page.evaluate("window.scrollTo({ top: 0, left: 0, behavior: 'instant' })")
            elif absolute == "bottom":
                await self.page.evaluate("window.scrollTo({ top: document.documentElement.scrollHeight, left: 0, behavior: 'instant' })")
            else:
                await self.page.evaluate(f"window.scrollBy({{ top: {int(dy)}, left: 0, behavior: 'instant' }})")
            return True
        except Exception:
            return False

    async def _handle_key_down(self, key_code: int, text_or_url: str) -> None:
        if not self.page:
            return
        logger.info("INPUT_KEY_DOWN key_code=0x%02x text=%r", key_code, text_or_url)

        # 1. Explicit key name or character sent in text_or_url (preferred, unambiguous path)
        if text_or_url:
            if text_or_url == "ArrowDown":
                if await self._scroll_if_not_editing(120):
                    return
                await self.page.keyboard.press("ArrowDown")
                return
            elif text_or_url == "ArrowUp":
                if await self._scroll_if_not_editing(-120):
                    return
                await self.page.keyboard.press("ArrowUp")
                return
            elif text_or_url == "PageDown":
                if await self._scroll_if_not_editing(520):
                    return
                await self.page.keyboard.press("PageDown")
                return
            elif text_or_url == "PageUp":
                if await self._scroll_if_not_editing(-520):
                    return
                await self.page.keyboard.press("PageUp")
                return
            elif text_or_url == "Home":
                if await self._scroll_if_not_editing(0, absolute="top"):
                    return
                await self.page.keyboard.press("Home")
                return
            elif text_or_url == "End":
                if await self._scroll_if_not_editing(0, absolute="bottom"):
                    return
                await self.page.keyboard.press("End")
                return
            elif text_or_url in (" ", "Space"):
                if await self._scroll_if_not_editing(420):
                    return
                await self.page.keyboard.type(" ")
                return
            elif text_or_url in (
                "Enter", "Backspace", "Tab", "Escape", "Delete",
                "ArrowLeft", "ArrowRight",
                "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10",
            ):
                await self.page.keyboard.press(text_or_url)
                return
            elif text_or_url in ("Copy", "Control+c", "Control+C") or key_code == 3:
                try:
                    sel = await self.page.evaluate("window.getSelection() ? window.getSelection().toString() : ''")
                    if sel and len(sel.strip()) > 0:
                        logger.info("Copying selection (%d chars) to Amiga clipboard", len(sel))
                        await self.send_clipboard_to_amiga(sel)
                except Exception:
                    pass
                await self.page.keyboard.press("Control+c")
                return
            elif text_or_url in ("Paste", "Control+v", "Control+V") or key_code == 22:
                await self.page.keyboard.press("Control+v")
                return
            else:
                await self.page.keyboard.type(text_or_url)
                return

        # 2. Fallback when text_or_url is empty: check ASCII / Latin-1 FIRST before AMIGA_RAWKEY_MAP
        if key_code == 8:
            await self.page.keyboard.press("Backspace")
        elif key_code == 127:
            await self.page.keyboard.press("Delete")
        elif key_code in (10, 13):
            await self.page.keyboard.press("Enter")
        elif key_code == 9:
            await self.page.keyboard.press("Tab")
        elif key_code == 27:
            await self.page.keyboard.press("Escape")
        elif key_code == 32:
            if not await self._scroll_if_not_editing(420):
                await self.page.keyboard.type(" ")
        elif (33 <= key_code <= 126) or (160 <= key_code <= 255):
            await self.page.keyboard.type(chr(key_code))
        elif key_code in AMIGA_RAWKEY_MAP:
            kname = AMIGA_RAWKEY_MAP[key_code]
            await self.page.keyboard.press(" " if kname == "Space" else kname)

    async def _activate_tab(self, tidx: int, old_page=None) -> None:
        if tidx < 0 or tidx >= len(self.tabs):
            return
        self._tab_generation += 1
        self._switching_tab = True
        target_page = self.tabs[tidx]
        try:
            if old_page and old_page != target_page:
                try:
                    await asyncio.wait_for(self._pause_page_media(old_page), timeout=0.6)
                except Exception:
                    pass
            await self._gc_current_cdp_page()
            await self._stop_screencast()
            await self._detach_cdp()
            self.active_tab_idx = tidx
            self.page = target_page
            self._video_element_playing = False
            self._last_video_eval_time = 0.0
            if self.is_video_mode and self.stream_mode != "h264" and self.codec_mode != "h264":
                self.is_video_mode = False
                self.jpeg_quality = self.base_jpeg_quality
                self.current_screencast_quality = self.base_jpeg_quality
            self.last_frame_hash = None
            self.force_full_frame = True

            # Wake Chromium renderer immediately from background throttling
            try:
                await target_page.bring_to_front()
            except Exception:
                pass

            # Only resize viewport when dimensions actually changed — avoids triggering
            # expensive full-page re-layout / re-pagination on heavy apps like read.amazon.com
            try:
                vp = target_page.viewport_size
                if not vp or vp.get("width") != self.width or vp.get("height") != self.height:
                    await target_page.set_viewport_size({"width": self.width, "height": self.height})
            except Exception:
                pass

            if self.context and self.engine == "chromium":
                await self._attach_direct_cdp(target_page)
                await self._start_screencast()
            else:
                self.cdp = None
        finally:
            self._switching_tab = False

        # Trigger immediate frame capture BEFORE running background JS tasks
        self.last_frame_hash = None
        self.force_full_frame = True
        self._trigger_full_refresh(True)
        cur_page = self.page
        if cur_page is not None:
            self._send_nav_update(cur_page.url or "", tab_idx=tidx)
        self._schedule_delayed_frames()

        async def _post_switch_tasks() -> None:
            p = cur_page
            if not p or self.page is not p or not self.running:
                return
            if getattr(p, "_vb_applied_dark_mode", None) != self.dark_mode:
                await self._apply_dark_mode(p)
                try:
                    setattr(p, "_vb_applied_dark_mode", self.dark_mode)
                except Exception:
                    pass
                if self.page is p:
                    self._trigger_full_refresh(True)

        t = asyncio.create_task(_post_switch_tasks())
        self.background_tasks.add(t)
        t.add_done_callback(self.background_tasks.discard)

    async def _pause_page_media(self, page) -> None:
        if not self.pause_bg_media or not page:
            return
        pause_js = """
        (() => {
            try {
                if (typeof window.__vortexPauseMedia === 'function') {
                    window.__vortexPauseMedia();
                }
            } catch(e) {}
            try {
                const pauseRoot = (root) => {
                    if (!root || !root.querySelectorAll) return;
                    root.querySelectorAll('video, audio').forEach(m => {
                        try { if (!m.paused) m.pause(); } catch(e) {}
                    });
                    root.querySelectorAll('*').forEach(el => {
                        if (el.shadowRoot) pauseRoot(el.shadowRoot);
                    });
                };
                pauseRoot(document);
            } catch(e) {}
        })();
        """
        try:
            frames = list(getattr(page, "frames", None) or [])
            if not frames:
                frames = [page]
            await asyncio.gather(
                *(asyncio.wait_for(frame.evaluate(pause_js), timeout=0.45) for frame in frames),
                return_exceptions=True,
            )
            self.last_audio_time = 0.0
        except Exception:
            pass

    async def _apply_dark_mode(self, page=None) -> None:
        target_page = page or self.page
        if not target_page:
            return
        is_dark = (self.dark_mode == 1)
        color_scheme = "dark" if is_dark else ("light" if self.dark_mode == 0 else "no-preference")
        try:
            await target_page.emulate_media(color_scheme=color_scheme)
        except Exception as exc:
            logger.debug("emulate_media color_scheme error: %s", exc)

        page_url = ""
        try:
            page_url = target_page.url or ""
        except Exception:
            pass

        lower_url = page_url.lower()
        is_twitch = "twitch.tv" in lower_url
        is_kindle = ("read.amazon." in lower_url) or ("lesen.amazon." in lower_url)

        if self.cdp is not None and self.engine == "chromium":
            try:
                # Do NOT force Chromium Emulation.setAutoDarkModeOverride on Twitch or Kindle
                # because AutoDarkMode forcefully inverts video/canvas book surfaces into unreadable boxes!
                auto_dark = is_dark and not is_twitch and not is_kindle
                await self.cdp.send("Emulation.setAutoDarkModeOverride", {"enabled": auto_dark})
            except Exception as exc:
                logger.debug("Emulation.setAutoDarkModeOverride error: %s", exc)

        if is_kindle:
            # Kindle Cloud Reader renders book pages onto <canvas> / transparent bitmap <img> tiles.
            # If the user already has Kindle's native dark theme active (.kg-client-theme--dark or dark body),
            # we MUST NOT apply DarkReader, AutoDarkMode, or CSS inversion — otherwise black turns white!
            # Only apply CSS inversion when Kindle is in light mode (light background).
            try:
                if is_dark:
                    kindle_dark_js = """
                    (() => {
                        try {
                            if (typeof DarkReader !== 'undefined') DarkReader.disable();
                            let f = document.getElementById('__vb_dark_flash');
                            if (f) f.remove();
                            let s = document.getElementById('__vb_kindle_dark_fix');
                            const hasNativeDark = !!document.querySelector('.kg-client-theme--dark');
                            let isDarkBg = hasNativeDark;
                            if (!isDarkBg && document.body) {
                                const m = getComputedStyle(document.body).backgroundColor.match(/\\d+/g);
                                if (m && m.length >= 3) {
                                    const lum = parseInt(m[0], 10) + parseInt(m[1], 10) + parseInt(m[2], 10);
                                    if (lum < 200 && !(m[3] === '0')) isDarkBg = true;
                                }
                            }
                            if (isDarkBg) {
                                if (s) s.remove();
                            } else {
                                if (!s && document.head) {
                                    s = document.createElement('style');
                                    s.id = '__vb_kindle_dark_fix';
                                    document.head.appendChild(s);
                                }
                                if (s) {
                                    s.textContent = `
                                        html {
                                            filter: invert(0.91) hue-rotate(180deg) !important;
                                            background-color: #ffffff !important;
                                        }
                                        img[src*="images-amazon"], img[src*="media-amazon"], video {
                                            filter: invert(1) hue-rotate(180deg) !important;
                                        }
                                    `;
                                }
                            }
                        } catch(e) {}
                    })();
                    """
                    await target_page.evaluate(kindle_dark_js)
                else:
                    kindle_light_js = """
                    (() => {
                        try {
                            let s = document.getElementById('__vb_kindle_dark_fix');
                            if (s) s.remove();
                        } catch(e) {}
                    })();
                    """
                    await target_page.evaluate(kindle_light_js)
            except Exception as exc:
                logger.debug("Kindle dark mode evaluate error: %s", exc)

        elif is_twitch:
            # Native Twitch Dark Theme toggle (preserves video element, flawless dark UI)
            try:
                if is_dark:
                    tw_dark_js = """
                    (() => {
                        try {
                            if (typeof DarkReader !== 'undefined') DarkReader.disable();
                            let f = document.getElementById('__vb_dark_flash');
                            if (f) f.remove();
                            document.documentElement.classList.remove('tw-root--theme-light');
                            document.documentElement.classList.add('tw-root--theme-dark');
                            try { localStorage.setItem('twilight.theme', '1'); } catch(e) {}
                            let s = document.getElementById('__vb_tw_dark_fix');
                            if (!s && document.head) {
                                s = document.createElement('style');
                                s.id = '__vb_tw_dark_fix';
                                document.head.appendChild(s);
                            }
                            if (s) {
                                s.textContent = `
                                    .video-player, .video-player__container, .video-player__default-player, video {
                                        background-color: #000000 !important;
                                        filter: none !important;
                                    }
                                `;
                            }
                        } catch(e) {}
                    })();
                    """
                    await target_page.evaluate(tw_dark_js)
                else:
                    tw_light_js = """
                    (() => {
                        try {
                            document.documentElement.classList.remove('tw-root--theme-dark');
                            document.documentElement.classList.add('tw-root--theme-light');
                            try { localStorage.setItem('twilight.theme', '0'); } catch(e) {}
                            let s = document.getElementById('__vb_tw_dark_fix');
                            if (s) s.remove();
                        } catch(e) {}
                    })();
                    """
                    await target_page.evaluate(tw_light_js)
            except Exception as exc:
                logger.debug("Twitch native theme evaluate error: %s", exc)

        elif DARKREADER_JS:
            try:
                if is_dark:
                    has_dr = False
                    try:
                        has_dr = bool(await asyncio.wait_for(
                            target_page.evaluate("typeof DarkReader !== 'undefined'"),
                            timeout=0.5,
                        ))
                    except Exception:
                        has_dr = False
                    prefix = "" if has_dr else (DARKREADER_JS + "\n")
                    js = (
                        "(() => {"
                        "try {"
                        + prefix +
                        "if (typeof DarkReader !== 'undefined') {"
                        "DarkReader.enable({ brightness: 100, contrast: 90, sepia: 10 });"
                        "}"
                        "let s = document.getElementById('__vb_dark_flash');"
                        "if (!s && document.head) {"
                        "s = document.createElement('style');"
                        "s.id = '__vb_dark_flash';"
                        "document.head.appendChild(s);"
                        "}"
                        "if (s) s.textContent = 'html, body { background-color: #18181b !important; color: #f4f4f5 !important; }';"
                        "} catch (e) {}"
                        "})();"
                    )
                    await target_page.evaluate(js)
                else:
                    js = """
                    (() => {
                        try {
                            if (typeof DarkReader !== 'undefined') {
                                DarkReader.disable();
                            }
                            let s = document.getElementById('__vb_dark_flash');
                            if (s) s.remove();
                        } catch (e) {}
                    })();
                    """
                    await target_page.evaluate(js)
            except Exception as exc:
                logger.debug("DarkReader evaluate error: %s", exc)

        await self._apply_web_styling(target_page)
        try:
            setattr(target_page, "_vb_applied_dark_mode", self.dark_mode)
        except Exception:
            pass

    async def _apply_web_styling(self, page=None) -> None:
        target_page = page or self.page
        if not target_page:
            return
        try:
            css_font = self.web_font_family if self.web_font_family in ("serif", "monospace") else "sans-serif"
            fsize = max(10, min(36, int(getattr(self, "web_font_size", 16) or 16)))
            base_zoom = max(50, min(300, int(self.web_zoom_pct or 100)))
            # Compute target font size in pixels without using CSS style.zoom on <html>
            # (CSS style.zoom breaks hardware-composited <video> / YouTube preview video coordinates)
            effective_px = max(10, min(36, int(round(fsize * (base_zoom / 100.0)))))

            if self.cdp is not None and self.engine == "chromium":
                try:
                    await self.cdp.send(
                        "Page.setFontSizes",
                        {
                            "fontSizes": {
                                "standard": effective_px,
                                "fixed": max(10, effective_px - 2),
                            }
                        },
                    )
                except Exception:
                    pass

            page_url = ""
            try:
                page_url = (target_page.url or "").lower()
            except Exception:
                pass
            is_kindle = ("read.amazon." in page_url) or ("lesen.amazon." in page_url)

            dark_css = ""
            if not is_kindle:
                if self.dark_mode == 1:
                    dark_css = ":root, html { color-scheme: dark !important; } html, body { background-color: #18181b !important; color: #f4f4f5 !important; }"
                elif self.dark_mode == 0:
                    dark_css = ":root, html { color-scheme: light !important; }"

            js = f"""
            (() => {{
                try {{
                    if (document.documentElement) {{
                        document.documentElement.style.zoom = '';
                    }}
                    let style = document.getElementById('__vb_font_override');
                    if (!style && document.head) {{
                        style = document.createElement('style');
                        style.id = '__vb_font_override';
                        document.head.appendChild(style);
                    }}
                    if (style) {{
                        style.textContent = 'html {{ scroll-behavior: auto !important; }} body, input, select, textarea {{ font-family: {css_font}; }} {dark_css}';
                    }}
                }} catch (e) {{}}
            }})();
            """
            await target_page.evaluate(js)
        except Exception:
            pass

    def _on_page_updated(self) -> None:
        self._trigger_full_refresh()
        if self.page and self.running:
            t = asyncio.create_task(self._apply_dark_mode(self.page))
            self.background_tasks.add(t)
            t.add_done_callback(self.background_tasks.discard)

    async def _handle_navigation(self, raw_cmd: str) -> None:
        if not self.page or not raw_cmd:
            return
        cmd = raw_cmd.strip()
        if not cmd:
            return

        if cmd.upper() == "@CAP:YUV_RECT":
            self.allow_yuv_rect = True
            logger.info("Client enabled 2D YUV420P dirty-rect streaming (CODEC_LZ4_RECT)")
            return

        if cmd.upper().startswith("@STREAM_MODE:"):
            m = cmd.split(":", 1)[1].strip().lower()
            if m in ("h264", "video"):
                self.stream_mode = "h264"
                logger.info("Client requested STREAM_MODE: H.264 continuous video stream")
            elif m in ("dirtyrect", "lz4"):
                self.stream_mode = "lz4"
                if self._h264_encoder:
                    self._h264_encoder.stop()
                    self._h264_encoder = None
                logger.info("Client requested STREAM_MODE: LZ4 dirty-rect (classic)")
            elif m in ("auto", "adaptive"):
                self.stream_mode = "auto"
                logger.info("Client requested STREAM_MODE: Auto (adaptive H.264 for video, LZ4 for browsing)")
            self._trigger_full_refresh(True)
            return

        if cmd.upper().startswith("@VIDEO_BITRATE:"):
            try:
                br = int(cmd.split(":", 1)[1].strip())
                self.video_bitrate_k = max(500, min(50000, br))
                if self._h264_encoder:
                    self._h264_encoder.stop()
                    self._h264_encoder = None
                logger.info("Client set H.264 video stream bitrate to %d kbps", self.video_bitrate_k)
            except Exception:
                pass
            return

        if cmd.upper().startswith("@CODEC:"):
            c = cmd.split(":", 1)[1].strip().lower()
            if c in ("mjpeg", "jpeg"):
                self.codec_mode = "jpeg"
                logger.info("Switched streaming codec to Motion-JPEG (CODEC_JPEG)")
            elif c == "lz4":
                self.codec_mode = "lz4"
                logger.info("Switched streaming codec to LZ4 (CODEC_LZ4)")
            elif c in ("h264", "vaapi"):
                self.codec_mode = "h264"
                logger.info("Switched streaming codec to H.264 (CODEC_H264)")
            elif c == "auto":
                self.codec_mode = "auto"
                logger.info("Switched streaming codec to Auto")
            self._trigger_full_refresh(True)
            return

        if cmd.upper().startswith("@FORMAT:"):
            f = cmd.split(":", 1)[1].strip().lower()
            new_fmt = None
            if f in ("rgb565", "16", "16bit"):
                new_fmt = FMT_RGB565
            elif f in ("argb32", "32", "32bit"):
                new_fmt = FMT_ARGB32
            elif f in ("yuv", "yuv420", "yuv420p", "12", "12bit"):
                new_fmt = FMT_YUV420P
            if new_fmt is not None and new_fmt != self.pixel_format:
                self.pixel_format = new_fmt
                if new_fmt == FMT_YUV420P:
                    fmt_name = "Planar YUV420p (12-bit)"
                elif new_fmt == FMT_RGB565:
                    fmt_name = "RGB565 (16-bit)"
                else:
                    fmt_name = "ARGB32 (32-bit)"
                logger.info("Client requested pixel format switch to %s", fmt_name)
                self._ensure_work_buffers(self.width, self.height, new_fmt)
                self._trigger_full_refresh(True)
            return

        if cmd.upper().startswith("@JPEG_QUALITY:"):
            try:
                q = int(cmd.split(":", 1)[1].strip())
                if self.width * self.height >= 1500000 and q > 85:
                    logger.info("1080p Full HD mode active: clamping requested JPEG quality %d -> 85 for optimal 25 FPS bandwidth", q)
                    q = 85
                self.base_jpeg_quality = max(20, min(100, q))
                self.video_jpeg_quality = min(self.base_jpeg_quality, 75)
                target_q = self.video_jpeg_quality if self.is_video_mode else self.base_jpeg_quality
                await self._set_screencast_quality(target_q)
                logger.info("Set JPEG/MJPEG base quality to %d (current=%d)", self.base_jpeg_quality, self.jpeg_quality)
            except Exception:
                pass
            return

        if cmd.upper().startswith("@FPS:"):
            try:
                fps = int(cmd.split(":", 1)[1].strip())
                self.target_fps = max(1, min(60, fps))
                logger.info("Set target streaming FPS to %d", self.target_fps)
            except Exception:
                pass
            return

        if cmd.upper() in ("@REFRESH", "@REDRAW"):
            logger.info("Client requested immediate frame refresh")
            self._trigger_full_refresh(True)
            return

        if cmd.upper().startswith("@RESIZE:"):
            try:
                dims = cmd.split(":", 1)[1].lower().split("x")
                new_w = max(320, min(1920, int(dims[0]) & ~1))
                new_h = max(200, min(1080, int(dims[1]) & ~1))
                if (new_w, new_h) != (self.width, self.height):
                    logger.info("Syncing browser viewport 1:1 to Amiga window: %dx%d", new_w, new_h)
                    self.width = new_w
                    self.height = new_h
                    if new_w * new_h >= 1500000 and self.base_jpeg_quality > 85:
                        self.base_jpeg_quality = 85
                        if not self.is_video_mode:
                            self.jpeg_quality = 85
                            self.current_screencast_quality = 85
                    self._trigger_full_refresh(True)
                    await self.page.set_viewport_size({"width": new_w, "height": new_h})
                    if self.engine == "chromium":
                        await self._stop_screencast()
                        await self._start_screencast()
                    self._trigger_full_refresh(True)
                else:
                    self._trigger_full_refresh(True)
            except Exception as exc:
                logger.warning("Viewport resize error: %s", exc)
            return

        if cmd.upper().startswith("@ZOOM:"):
            try:
                zoom_val = int(cmd.split(":", 1)[1].strip())
                self.web_zoom_pct = max(50, min(300, zoom_val))
                logger.info("Setting web zoom to %d%%", self.web_zoom_pct)
                await self._apply_web_styling()
                self._trigger_full_refresh(True)
            except Exception as exc:
                logger.warning("Zoom error: %s", exc)
            return

        if cmd.upper().startswith("@FONTSIZE:"):
            try:
                fs_val = int(cmd.split(":", 1)[1].strip())
                self.web_font_size = max(10, min(36, fs_val))
                logger.info("Setting headless browser font size to %dpx", self.web_font_size)
                await self._apply_web_styling()
                self._trigger_full_refresh(True)
            except Exception as exc:
                logger.warning("Fontsize error: %s", exc)
            return

        if cmd.upper().startswith("@FONT:"):
            try:
                self.web_font_family = cmd.split(":", 1)[1].strip().lower()
                logger.info("Setting web font family to %s", self.web_font_family)
                await self._apply_web_styling()
                self._trigger_full_refresh(True)
            except Exception as exc:
                logger.warning("Font error: %s", exc)
            return

        if cmd.upper().startswith("@CHARSET:"):
            try:
                self.web_charset = cmd.split(":", 1)[1].strip().lower()
                logger.info("Setting web charset to %s", self.web_charset)
                js = f"""
                (() => {{
                    try {{
                        let meta = document.querySelector('meta[charset]');
                        if (meta) {{
                            meta.setAttribute('charset', '{self.web_charset}');
                        }} else if (document.head) {{
                            meta = document.createElement('meta');
                            meta.setAttribute('charset', '{self.web_charset}');
                            document.head.prepend(meta);
                        }}
                    }} catch (e) {{}}
                }})();
                """
                await self.page.evaluate(js)
                self._trigger_full_refresh(True)
            except Exception as exc:
                logger.warning("Charset error: %s", exc)
            return

        if cmd.upper().startswith("@LANG:"):
            raw_lang = cmd.split(":", 1)[1].strip()
            if self.locale_explicit:
                logger.info("Ignoring client @LANG:%s (server --locale=%s explicitly set)", raw_lang, self.browser_locale)
                return
            norm = normalize_bcp47_locale(raw_lang)
            if not norm:
                logger.warning("Ignoring invalid @LANG locale: %r", raw_lang)
                return
            if norm != self.browser_locale:
                self.browser_locale = norm
                accept_lang = build_accept_language(norm)
                nav_langs_json = json.dumps(build_navigator_languages(norm))
                if self.context:
                    try:
                        await self.context.set_extra_http_headers({"Accept-Language": accept_lang})
                    except Exception:
                        pass
                    try:
                        await self.context.add_init_script(f"window.__vortex_languages = {nav_langs_json};")
                    except Exception:
                        pass
                if self.cdp and self.engine == "chromium":
                    try:
                        await self.cdp.send("Emulation.setLocaleOverride", {"locale": norm})
                    except Exception:
                        pass
                    try:
                        await self.cdp.send("Network.setExtraHTTPHeaders", {"headers": {"Accept-Language": accept_lang}})
                    except Exception:
                        pass
                if self.page:
                    try:
                        await self.page.evaluate(f"window.__vortex_languages = {nav_langs_json};")
                    except Exception:
                        pass
                logger.info("Applied Amiga client locale: %s (Accept-Language: %s)", norm, accept_lang)
            return

        if cmd.upper().startswith("@DARKMODE:") or cmd.upper().startswith("@THEME:"):
            try:
                val = cmd.split(":", 1)[1].strip()
                if val in ("1", "dark", "dunkel", "true"):
                    self.dark_mode = 1
                elif val in ("2", "auto"):
                    self.dark_mode = 2
                else:
                    self.dark_mode = 0
                logger.info("Setting Dark Mode to %d (0=Light, 1=Dark, 2=Auto)", self.dark_mode)
                await self._apply_dark_mode()
                self._trigger_full_refresh(True)
            except Exception as exc:
                logger.warning("DarkMode error: %s", exc)
            return

        if cmd.upper().startswith("@DOWNLOAD:"):
            dl_url = cmd.split(":", 1)[1].strip()
            if dl_url:
                t = asyncio.create_task(self._direct_download_url(dl_url))
                self.background_tasks.add(t)
                t.add_done_callback(self.background_tasks.discard)
            return

        if cmd.upper().startswith("@SOFTSCROLL:"):
            val = cmd.split(":", 1)[1].strip()
            self.soft_scroll = (val == "1" or val.lower() == "true")
            logger.info("Softscrolling set to %s", self.soft_scroll)
            return

        if cmd.upper().startswith("@PAUSE_BG_MEDIA:"):
            val = cmd.split(":", 1)[1].strip()
            self.pause_bg_media = (val == "1" or val.lower() == "true")
            logger.info("Pause background media set to %s", self.pause_bg_media)
            return

        if cmd.upper().startswith("@NEWTAB"):
            parts = cmd.split(":", 1)
            target = parts[1].strip() if len(parts) > 1 else ""
            if not target or target.startswith("about:") or "127.0.0.1" in target or "localhost" in target or target.upper() == "@HOME":
                target = self.portal_url
            elif not (target.startswith("http://") or target.startswith("https://") or target.startswith("file://")):
                target = "https://" + target
            if self.context:
                try:
                    self._creating_tab = True
                    self._tab_generation += 1
                    self._switching_tab = True
                    old_page = self.page
                    if old_page:
                        try:
                            await asyncio.wait_for(self._pause_page_media(old_page), timeout=0.6)
                        except Exception:
                            pass
                    await self._gc_current_cdp_page()
                    await self._stop_screencast()
                    await self._detach_cdp()
                    new_page = await self.context.new_page()
                    self.tabs.append(new_page)
                    self.active_tab_idx = len(self.tabs) - 1
                    self.page = new_page
                    self._video_element_playing = False
                    self._last_video_eval_time = 0.0
                    if self.is_video_mode and self.stream_mode != "h264" and self.codec_mode != "h264":
                        self.is_video_mode = False
                        self.jpeg_quality = self.base_jpeg_quality
                        self.current_screencast_quality = self.base_jpeg_quality
                    self.last_frame_hash = None
                    self.force_full_frame = True
                    self._setup_page_listeners(new_page)
                    try:
                        await new_page.set_viewport_size({"width": self.width, "height": self.height})
                    except Exception:
                        pass
                    if self.engine == "chromium":
                        await self._attach_direct_cdp(new_page)
                        await self._start_screencast()
                    else:
                        self.cdp = None
                    await self._apply_dark_mode(new_page)
                    self._send_progress(20, f"Lade {target}...")
                    await new_page.goto(target, wait_until="commit", timeout=15000)
                    self._send_nav_update(new_page.url or target)
                except Exception as exc:
                    logger.warning("New tab creation error: %s", exc)
                finally:
                    self._switching_tab = False
                    self._creating_tab = False
                self.last_frame_hash = None
                self.force_full_frame = True
                self._trigger_full_refresh(True)
                self._schedule_delayed_frames()
            return

        if cmd.upper().startswith("@SWITCHTAB:"):
            try:
                tidx = int(cmd.split(":", 1)[1].strip())
                if 0 <= tidx < len(self.tabs):
                    cdp_ok = (
                        self.engine != "chromium"
                        or (
                            self.cdp is not None
                            and (not isinstance(self.cdp, DirectCDPSession) or self.cdp.connected)
                        )
                    )
                    if tidx == self.active_tab_idx and self.page is self.tabs[tidx] and cdp_ok:
                        self._trigger_full_refresh(True)
                        return
                    old_page = self.page if tidx != self.active_tab_idx else None
                    await self._activate_tab(tidx, old_page=old_page)
            except Exception as exc:
                logger.warning("Switch tab error: %s", exc)
            return

        if cmd.upper().startswith("@CLOSETAB:"):
            try:
                tidx = int(cmd.split(":", 1)[1].strip())
                if 0 <= tidx < len(self.tabs) and len(self.tabs) > 1:
                    p_to_close = self.tabs.pop(tidx)
                    was_active = (p_to_close == self.page)
                    if was_active:
                        self._tab_generation += 1
                        self._switching_tab = True
                        try:
                            await self._stop_screencast()
                            await self._detach_cdp()
                        except Exception:
                            self._switching_tab = False
                    self._detach_page_listeners(p_to_close)
                    try:
                        await p_to_close.close()
                    except Exception:
                        pass
                    if tidx < self.active_tab_idx:
                        self.active_tab_idx -= 1
                    elif self.active_tab_idx >= len(self.tabs):
                        self.active_tab_idx = len(self.tabs) - 1
                    if was_active:
                        try:
                            await self._activate_tab(self.active_tab_idx)
                        finally:
                            self._switching_tab = False
                    _reclaim_python_memory()
            except Exception as exc:
                self._switching_tab = False
                logger.warning("Close tab error: %s", exc)
            return

        if cmd.upper().startswith("@TWITCH_TOKEN:"):
            token = cmd.split(":", 1)[1].strip()
            if token:
                try:
                    await self.context.add_cookies([{
                        "name": "auth-token",
                        "value": token,
                        "domain": ".twitch.tv",
                        "path": "/"
                    }])
                    token_path = os.path.join(get_vortex_config_dir(), "twitch_token.txt")
                    tmp_tok = f"{token_path}.tmp.{os.getpid()}"
                    with open(tmp_tok, "w", encoding="utf-8") as f:
                        f.write(token)
                    try:
                        os.chmod(tmp_tok, 0o600)
                    except Exception:
                        pass
                    os.replace(tmp_tok, token_path)
                    logger.info("Saved Twitch auth-token to %s and active cookie jar", token_path)
                    self._schedule_storage_save(delay=0.5)
                    t = asyncio.create_task(self.page.goto("https://www.twitch.tv", wait_until="commit", timeout=15000))
                    self.background_tasks.add(t)
                    t.add_done_callback(self.background_tasks.discard)
                except Exception as exc:
                    logger.warning("Error applying twitch token: %s", exc)
            return

        if cmd.upper().startswith("@AUTOLOGIN_FILL:"):
            payload = cmd.split(":", 1)[1]
            user, pwd = (payload.split("\t", 1) if "\t" in payload else (payload.split(" ", 1) if " " in payload else (payload, "")))
            await self._perform_auto_login(user, pwd, submit=False)
            return

        if cmd.upper().startswith("@AUTOLOGIN:"):
            payload = cmd.split(":", 1)[1]
            user, pwd = (payload.split("\t", 1) if "\t" in payload else (payload.split(" ", 1) if " " in payload else (payload, "")))
            await self._perform_auto_login(user, pwd, submit=True)
            return

        if cmd.startswith("@"):
            upper_cmd = cmd.upper()
            known_nav = ("@BACK", "@FORWARD", "@RELOAD", "@STOP", "@HOME")
            if upper_cmd not in known_nav:
                logger.warning("Ignoring unhandled internal '@' command: %s", cmd.split(":", 1)[0])
                return

        logger.info("INPUT_GOTO_URL: %s (history idx=%d/%d)", cmd, self.nav_idx, len(self.nav_history))
        self._trigger_full_refresh()
        self._schedule_delayed_frames()

        async def _nav_bg():
            try:
                self._send_progress(25, f"Lade {cmd[:30]}...")
                if cmd.upper() == "@BACK":
                    if self.nav_idx > 0:
                        self.nav_idx -= 1
                        target_url = self.nav_history[self.nav_idx]
                        logger.info("Navigating @BACK to history[%d]: %s", self.nav_idx, target_url)
                        self._in_history_jump = True
                        try:
                            await self.page.goto(target_url, wait_until="commit", timeout=12000)
                        finally:
                            self._in_history_jump = False
                    elif self.page.url != self.portal_url:
                        logger.info("Navigating @BACK fallback to portal: %s", self.portal_url)
                        await self.page.goto(self.portal_url, wait_until="commit", timeout=10000)
                elif cmd.upper() == "@FORWARD":
                    if self.nav_idx + 1 < len(self.nav_history):
                        self.nav_idx += 1
                        target_url = self.nav_history[self.nav_idx]
                        logger.info("Navigating @FORWARD to history[%d]: %s", self.nav_idx, target_url)
                        self._in_history_jump = True
                        try:
                            await self.page.goto(target_url, wait_until="commit", timeout=12000)
                        finally:
                            self._in_history_jump = False
                elif cmd.upper() == "@RELOAD":
                    await self.page.reload(wait_until="commit", timeout=15000)
                elif cmd.upper() == "@STOP":
                    try:
                        await self.page.evaluate("window.stop()")
                    except Exception:
                        pass
                elif cmd.upper() == "@HOME":
                    await self.page.goto(self.portal_url, wait_until="commit", timeout=10000)
                else:
                    target = cmd
                    if (
                        target.startswith("http://127.0.0.1:")
                        or target.startswith("http://localhost:")
                    ):
                        logger.info("Redirecting local portal request '%s' to '%s'", target, self.portal_url)
                        target = self.portal_url
                    elif not (
                        target.startswith("http://")
                        or target.startswith("https://")
                        or target.startswith("file://")
                        or target.startswith("about:")
                    ):
                        if target.startswith("/search?q="):
                            q_raw = unquote(target.split("/search?q=", 1)[1])
                            target = f"https://duckduckgo.com/?q={quote_plus(q_raw)}"
                        elif " " in target or "." not in target:
                            target = f"https://duckduckgo.com/?q={quote_plus(target)}"
                        else:
                            target = "https://" + target
                    await self.page.goto(target, wait_until="domcontentloaded", timeout=20000)
                    try:
                        await self.page.wait_for_load_state("load", timeout=2500)
                    except Exception:
                        pass
            except Exception as exc:
                logger.warning("Navigation warning for '%s': %s", cmd, exc)
            finally:
                self._send_progress(100, "Fertig")
                if self.page and self.page.url:
                    self._send_nav_update(self.page.url)
                self.active_until = time.monotonic() + 3.0
                self._trigger_full_refresh(True)
                self._schedule_delayed_frames()

        t = asyncio.create_task(_nav_bg())
        self.background_tasks.add(t)
        t.add_done_callback(self.background_tasks.discard)

    async def _perform_auto_login(self, username: str, password: str, submit: bool = True) -> None:
        if not self.page:
            return
        logger.info("Executing Auto-Login for user: %r (submit=%s)", username, submit)
        if submit:
            self._send_progress(50, f"Auto-Login fuer '{username}'...")
        else:
            self._send_progress(50, "Zugangsdaten einfuellen...")

        js_script = """
        ([user, pass, shouldSubmit]) => {
            function isVisible(el) {
                if (!el) return false;
                const style = window.getComputedStyle(el);
                if (style.display === 'none' || style.visibility === 'hidden' || style.opacity === '0') return false;
                const rect = el.getBoundingClientRect();
                return rect.width > 0 && rect.height > 0;
            }

            function setVal(elem, val) {
                if (!elem) return false;
                elem.focus();
                const valueSetter = Object.getOwnPropertyDescriptor(elem, 'value')?.set;
                const prototype = Object.getPrototypeOf(elem);
                const protoSetter = Object.getOwnPropertyDescriptor(prototype, 'value')?.set;
                if (protoSetter && valueSetter !== protoSetter) {
                    protoSetter.call(elem, val);
                } else if (valueSetter) {
                    valueSetter.call(elem, val);
                } else {
                    elem.value = val;
                }
                elem.dispatchEvent(new Event('focus', { bubbles: true }));
                elem.dispatchEvent(new Event('input', { bubbles: true }));
                elem.dispatchEvent(new Event('change', { bubbles: true }));
                elem.dispatchEvent(new Event('blur', { bubbles: true }));
                return true;
            }

            const doc = document;
            let pwInputs = Array.from(doc.querySelectorAll('input[type="password"]')).filter(isVisible);
            if (pwInputs.length === 0) {
                pwInputs = Array.from(doc.querySelectorAll('input[name*="pass" i], input[id*="pass" i]')).filter(isVisible);
            }
            const pwInput = pwInputs[0] || null;

            let userInputs = Array.from(doc.querySelectorAll(
                'input[autocomplete="username"], input[autocomplete="email"], input[type="email"], ' +
                'input[name*="user" i], input[name*="login" i], input[name*="email" i], input[name*="account" i], ' +
                'input[id*="user" i], input[id*="login" i], input[id*="email" i], input[id*="account" i], ' +
                'input[placeholder*="user" i], input[placeholder*="benutzer" i], input[placeholder*="login" i], ' +
                'input[placeholder*="e-mail" i], input[placeholder*="email" i], ' +
                'input[aria-label*="user" i], input[aria-label*="benutzer" i], input[aria-label*="login" i], input[aria-label*="email" i]'
            )).filter(isVisible);

            let userInput = userInputs[0] || null;
            if (!userInput && pwInput) {
                const form = pwInput.closest('form') || doc;
                const textInputs = Array.from(form.querySelectorAll('input[type="text"], input:not([type])')).filter(isVisible);
                if (textInputs.length > 0) {
                    userInput = textInputs[0];
                }
            }

            /* Never pre-fill non-login inputs (like search bars) when no login/password field exists */
            if (!pwInput && (!shouldSubmit || !userInput)) {
                return { userFilled: false, passFilled: false, submitted: false, foundUser: !!userInput, foundPass: false, foundSubmit: false };
            }

            let userFilled = false;
            let passFilled = false;
            if (userInput && user) {
                userFilled = setVal(userInput, user);
            }
            if (pwInput && pass) {
                passFilled = setVal(pwInput, pass);
            }

            let submitBtn = null;
            const form = (pwInput && pwInput.closest('form')) || (userInput && userInput.closest('form'));
            if (form) {
                submitBtn = form.querySelector('button[type="submit"], input[type="submit"]');
            }
            if (!submitBtn && shouldSubmit) {
                const allButtons = Array.from(doc.querySelectorAll('button, input[type="submit"], input[type="button"], a[role="button"]')).filter(isVisible);
                for (const btn of allButtons) {
                    const txt = (btn.innerText || btn.value || btn.getAttribute('aria-label') || '').trim().toLowerCase();
                    if (txt.includes('login') || txt.includes('log in') || txt.includes('sign in') || txt.includes('signin') ||
                        txt.includes('anmelden') || txt.includes('einloggen') || txt.includes('weiter') || txt.includes('next')) {
                        submitBtn = btn;
                        break;
                    }
                }
            }

            let submitted = false;
            if (shouldSubmit) {
                if (submitBtn) {
                    try { submitBtn.click(); submitted = true; } catch (e) {}
                } else if (form && (userFilled || passFilled)) {
                    try { form.submit(); submitted = true; } catch (e) {}
                }
            }
            return { userFilled, passFilled, submitted, foundUser: !!userInput, foundPass: !!pwInput, foundSubmit: !!submitBtn };
        }
        """
        try:
            frames = [self.page] + list(self.page.frames)
            res = None
            for frame in frames:
                try:
                    res = await frame.evaluate(js_script, [username, password, submit])
                    if res and (res.get("userFilled") or res.get("passFilled")):
                        break
                except Exception:
                    continue

            if submit and res and not res.get("submitted") and (res.get("userFilled") or res.get("passFilled")):
                try:
                    await self.page.keyboard.press("Enter")
                except Exception:
                    pass

            if res and (res.get("userFilled") or res.get("passFilled")):
                self._send_progress(100, f"Auto-Login ausgefuehrt ({username})" if submit else f"Zugangsdaten eingefuellt ({username})")
                self.active_until = time.monotonic() + 2.0
                self._trigger_full_refresh(True)
                self._schedule_delayed_frames()
            else:
                self._send_progress(100, "Kein Login-Feld gefunden" if submit else "Bereit")
        except Exception as exc:
            logger.warning("Auto-Login error: %s", exc)
            self._send_progress(100, "Auto-Login Fehler")

    # ------------------------------------------------------------------------
    # Download Interceptor & Direct URL ASL Download Streamer (Server -> Amiga)
    # ------------------------------------------------------------------------
    def _on_download_event(self, download: Download) -> None:
        t = asyncio.create_task(self._process_download(download))
        self.background_tasks.add(t)
        t.add_done_callback(self.background_tasks.discard)

    async def _direct_download_url(self, url: str) -> None:
        """
        Triggered from the Right-Click Context Menu ('Ziel speichern unter (ASL Download)...').
        Fast & responsive: retrieves HTTP headers within milliseconds, immediately prompts the Amiga
        user with the ASL FileRequester, and streams chunks in real-time as they are downloaded.
        """
        if not url:
            return
        download_id = self.next_download_id
        self.next_download_id += 1

        if url.startswith("/"):
            url = f"http://127.0.0.1:8089{url}"

        parsed = urlparse(url)
        raw_name = unquote(os.path.basename(parsed.path)).strip()
        if not raw_name or raw_name in ("/", "download.php", "index.php"):
            # Check query string for ?file=... (e.g. OS4Depot download.php?file=video/media/v-mplayer.lha)
            if "file=" in parsed.query:
                qpart = parsed.query.split("file=", 1)[1].split("&", 1)[0]
                raw_name = os.path.basename(unquote(qpart))
        suggested = raw_name or f"download_{download_id}.bin"

        # Extract browser session cookies to forward
        cookie_header = ""
        if self.context:
            try:
                cookies = await self.context.cookies()
                cookie_header = "; ".join(f"{c['name']}={c['value']}" for c in cookies if c.get("name") and c.get("value"))
            except Exception:
                pass

        headers = {
            "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36",
            "Accept": "*/*",
            "Referer": self.page.url if self.page else url,
        }
        if cookie_header:
            headers["Cookie"] = cookie_header

        def _fetch_headers_sync():
            req = urllib.request.Request(url, headers=headers)
            return urllib.request.urlopen(req, timeout=30)

        logger.info("Direct Download id=%d fast connect for '%s'...", download_id, url)
        resp = None
        try:
            resp = await asyncio.to_thread(_fetch_headers_sync)
        except Exception as exc:
            logger.warning("Fast urllib connect failed for '%s' (%s), falling back to Playwright request...", url, exc)
            if self.context:
                try:
                    resp_pw = await self.context.request.get(url, timeout=30000)
                    cd = resp_pw.headers.get("content-disposition", "")
                    if "filename=" in cd:
                        fn = cd.split("filename=", 1)[1].strip().strip('"').strip("'")
                        if fn:
                            suggested = os.path.basename(fn)
                    body = await resp_pw.body()
                    tmp_fd, tmp_path = tempfile.mkstemp(prefix=f"vb_direct_dl_{download_id}_")
                    with os.fdopen(tmp_fd, "wb") as f:
                        f.write(body)
                    await self._stream_cached_file_to_amiga(download_id, tmp_path, suggested)
                except Exception as e2:
                    logger.error("Download fallback failed: %s", e2)
            return

        try:
            # 1. Extract metadata from headers immediately
            cd = resp.headers.get("content-disposition", "")
            if "filename=" in cd:
                fn = cd.split("filename=", 1)[1].strip().strip('"').strip("'")
                if fn:
                    suggested = os.path.basename(fn)
            elif resp.geturl():
                final_path = urlparse(resp.geturl()).path
                base = unquote(os.path.basename(final_path)).strip()
                if base and "." in base:
                    suggested = base

            try:
                total_size = int(resp.headers.get("content-length", 0))
            except Exception:
                total_size = 0

            logger.info("Direct Download id=%d ready for ASL: '%s' (%d bytes)", download_id, suggested, total_size)

            # 2. Immediately send MSG_DOWNLOAD_START to open ASL Requester on Amiga!
            loop = asyncio.get_running_loop()
            resp_future = loop.create_future()
            self.pending_downloads[download_id] = resp_future

            fname_bytes = suggested.encode("utf-8", errors="replace")[:255]
            fname_padded = fname_bytes.ljust(256, b"\x00")
            start_pkt = STRUCT_MSG_DL_START.pack(
                MSG_DOWNLOAD_START,
                download_id,
                total_size,
                fname_padded,
            )
            if not await self.send_packet(start_pkt):
                resp.close()
                return

            # 3. Wait for user response from ASL requester
            accepted = await asyncio.wait_for(resp_future, timeout=300.0)
            if not accepted:
                logger.info("Download id=%d cancelled by Amiga user.", download_id)
                resp.close()
                return

            # 4. Stream chunks directly from HTTP response to Amiga socket!
            total_sent = 0
            while self.running:
                chunk = await asyncio.to_thread(resp.read, DOWNLOAD_CHUNK_SIZE)
                if not chunk:
                    break
                chunk_len = len(chunk)
                chunk_hdr = STRUCT_MSG_DL_CHUNK.pack(MSG_DOWNLOAD_CHUNK, download_id, chunk_len)
                if not await self.send_packet(chunk_hdr + chunk):
                    logger.warning("Socket disconnected during streaming of download id=%d", download_id)
                    resp.close()
                    return
                total_sent += chunk_len
                await asyncio.sleep(0.002)

            logger.info("Direct Download id=%d streaming complete (%d bytes sent).", download_id, total_sent)
            complete_pkt = STRUCT_MSG_DL_COMPLETE.pack(MSG_DOWNLOAD_COMPLETE, download_id)
            await self.send_packet(complete_pkt)

        except Exception as exc:
            logger.error("Direct download streaming error for '%s': %s", url, exc)
        finally:
            try:
                if resp:
                    resp.close()
            except Exception:
                pass
            self.pending_downloads.pop(download_id, None)

    async def _process_download(self, download: Download) -> None:
        download_id = self.next_download_id
        self.next_download_id += 1

        suggested = download.suggested_filename or f"download_{download_id}.bin"
        logger.info("Intercepted browser download id=%d: '%s' (url=%s)", download_id, suggested, download.url)

        tmp_fd, tmp_path = tempfile.mkstemp(prefix=f"vb_dl_{download_id}_")
        os.close(tmp_fd)

        try:
            await download.save_as(tmp_path)
        except Exception as exc:
            logger.error("Error saving browser download id=%d: %s", download_id, exc)
            try:
                if os.path.exists(tmp_path):
                    os.remove(tmp_path)
            except Exception:
                pass
            return

        await self._stream_cached_file_to_amiga(download_id, tmp_path, suggested)

    async def _stream_cached_file_to_amiga(self, download_id: int, tmp_path: str, suggested: str) -> None:
        try:
            total_size = os.path.getsize(tmp_path)
            logger.info("Download id=%d cached on server (%d bytes). Waiting for Amiga ASL response...", download_id, total_size)

            loop = asyncio.get_running_loop()
            resp_future = loop.create_future()
            self.pending_downloads[download_id] = resp_future

            fname_bytes = suggested.encode("utf-8", errors="replace")[:255]
            fname_padded = fname_bytes.ljust(256, b"\x00")

            start_pkt = STRUCT_MSG_DL_START.pack(
                MSG_DOWNLOAD_START,
                download_id,
                total_size,
                fname_padded,
            )
            if not await self.send_packet(start_pkt):
                return

            accepted = await asyncio.wait_for(resp_future, timeout=300.0)
            if not accepted:
                logger.info("Download id=%d rejected/cancelled by Amiga user.", download_id)
                return

            logger.info("Download id=%d accepted! Streaming %d bytes in 64KB chunks...", download_id, total_size)
            sent_bytes = 0
            with open(tmp_path, "rb") as f:
                while self.running:
                    chunk = f.read(DOWNLOAD_CHUNK_SIZE)
                    if not chunk:
                        break
                    chunk_hdr = STRUCT_MSG_DL_CHUNK.pack(
                        MSG_DOWNLOAD_CHUNK,
                        download_id,
                        len(chunk),
                    )
                    if not await self.send_packet(chunk_hdr + chunk):
                        return
                    sent_bytes += len(chunk)
                    await asyncio.sleep(0.002)

            comp_pkt = STRUCT_MSG_DL_COMPLETE.pack(MSG_DOWNLOAD_COMPLETE, download_id)
            await self.send_packet(comp_pkt)
            logger.info("Download id=%d completed (%d bytes streamed to Amiga).", download_id, sent_bytes)

        except asyncio.TimeoutError:
            logger.warning("Download id=%d timed out waiting for Amiga ASL response.", download_id)
        except Exception as exc:
            logger.error("Error processing download id=%d: %s", download_id, exc)
        finally:
            self.pending_downloads.pop(download_id, None)
            try:
                if os.path.exists(tmp_path):
                    os.remove(tmp_path)
            except Exception:
                pass

    # ------------------------------------------------------------------------
    # File Upload Interceptor (Amiga ASL FileRequester -> Server -> Browser)
    # ------------------------------------------------------------------------
    def _on_cdp_file_chooser_opened(self, params: dict) -> None:
        """Triggered directly by Chromium CDP Page.fileChooserOpened."""
        logger.info("DirectCDP Page.fileChooserOpened received: %r", params)
        now = time.monotonic()
        if self.pending_uploads:
            logger.info("Ignoring duplicate Page.fileChooserOpened: upload already in progress (%r)", list(self.pending_uploads.keys()))
            return
        if (now - self.last_upload_intercept_time) < 3.0:
            logger.info("Ignoring duplicate Page.fileChooserOpened: cooldown active (%.2fs)", now - self.last_upload_intercept_time)
            return
        self.last_upload_intercept_time = now

        backend_node_id = params.get("backendNodeId")
        frame_id = params.get("frameId")
        mode = params.get("mode", "selectSingle")
        t = asyncio.create_task(self._process_cdp_filechooser(backend_node_id, mode))
        self.background_tasks.add(t)
        t.add_done_callback(self.background_tasks.discard)

    async def _process_cdp_filechooser(self, backend_node_id: Optional[int], mode: str) -> None:
        upload_id = self.next_upload_id
        self.next_upload_id += 1

        logger.info("Intercepted CDP file upload chooser id=%d (backendNodeId=%s, mode=%s, page=%s)",
                    upload_id, backend_node_id, mode, self.page.url if self.page else "")

        loop = asyncio.get_running_loop()
        resp_future = loop.create_future()
        chunk_queue = asyncio.Queue()
        self.pending_uploads[upload_id] = (None, resp_future, chunk_queue)

        tmp_path = None
        try:
            # 1. Send MSG_UPLOAD_REQUEST to Amiga to open ASL FileRequester
            accept_bytes = b"".ljust(256, b"\x00")
            req_pkt = STRUCT_MSG_UPLOAD_REQ.pack(
                MSG_UPLOAD_REQUEST,
                upload_id,
                0,  # max_size (0 = unconstrained)
                accept_bytes,
            )
            if not await self.send_packet(req_pkt):
                logger.warning("Could not send MSG_UPLOAD_REQUEST id=%d to Amiga", upload_id)
                return

            # 2. Wait up to 5 minutes for Amiga user to select a file in ASL FileRequester
            try:
                accepted, file_size, filename = await asyncio.wait_for(resp_future, timeout=300.0)
            except asyncio.TimeoutError:
                logger.warning("Upload id=%d timed out waiting for Amiga ASL selection", upload_id)
                return

            if not accepted or file_size == 0:
                logger.info("Upload id=%d cancelled by Amiga user or empty file", upload_id)
                return

            logger.info("Upload id=%d confirmed by Amiga: '%s' (%d bytes). Receiving chunks...",
                        upload_id, filename, file_size)

            # 3. Create temporary file on server to receive uploaded chunks
            safe_fname = re.sub(r'[^a-zA-Z0-9_.-]', '_', filename) or f"upload_{upload_id}.bin"
            tmp_fd, tmp_path = tempfile.mkstemp(prefix=f"vb_up_{upload_id}_", suffix=f"_{safe_fname}")

            received_bytes = 0
            with os.fdopen(tmp_fd, "wb") as f:
                while received_bytes < file_size and self.running:
                    try:
                        chunk_data = await asyncio.wait_for(chunk_queue.get(), timeout=30.0)
                    except asyncio.TimeoutError:
                        logger.warning("Upload id=%d timed out waiting for chunk from queue", upload_id)
                        break
                    f.write(chunk_data)
                    received_bytes += len(chunk_data)

            if received_bytes >= file_size:
                abs_tmp_path = os.path.abspath(tmp_path)
                logger.info("Upload id=%d fully received (%d bytes). Setting files in browser (%s)!",
                            upload_id, received_bytes, abs_tmp_path)
                set_ok = False
                if self.cdp and isinstance(self.cdp, DirectCDPSession) and backend_node_id:
                    try:
                        await self.cdp.send("DOM.setFileInputFiles", {
                            "files": [abs_tmp_path],
                            "backendNodeId": backend_node_id
                        })
                        set_ok = True
                        logger.info("Upload id=%d: successfully set files via DirectCDP DOM.setFileInputFiles", upload_id)
                    except Exception as cdp_err:
                        logger.warning("Upload id=%d: DOM.setFileInputFiles failed: %s", upload_id, cdp_err)

                if not set_ok and self.page:
                    for selector in [
                        '.moxie-shim input[type="file"]',
                        '.moxie-shim-html5 input[type="file"]',
                        '#add_files_container input[type="file"]',
                        'input[type="file"]'
                    ]:
                        try:
                            el = await self.page.query_selector(selector)
                            if el:
                                await self.page.set_input_files(selector, [abs_tmp_path])
                                set_ok = True
                                logger.info("Upload id=%d: set files via fallback selector '%s'", upload_id, selector)
                                break
                        except Exception:
                            pass

                self._trigger_full_refresh(True)
                self._schedule_delayed_frames()
            else:
                logger.warning("Upload id=%d incomplete: received %d of %d bytes",
                               upload_id, received_bytes, file_size)

        except Exception as exc:
            logger.error("Error processing CDP file upload id=%d: %s", upload_id, exc)
        finally:
            self.pending_uploads.pop(upload_id, None)
            if tmp_path:
                try:
                    async def _delayed_unlink(p: str):
                        await asyncio.sleep(60.0)
                        try:
                            if os.path.exists(p):
                                os.remove(p)
                        except Exception:
                            pass
                    t_del = asyncio.create_task(_delayed_unlink(tmp_path))
                    self.background_tasks.add(t_del)
                    t_del.add_done_callback(self.background_tasks.discard)
                except Exception:
                    pass

    def _on_filechooser_event(self, file_chooser: FileChooser) -> None:
        """Triggered whenever a website clicks an HTML <input type="file">."""
        now = time.monotonic()
        if self.pending_uploads:
            logger.info("Ignoring duplicate filechooser event: upload already in progress (%r)", list(self.pending_uploads.keys()))
            return
        if (now - self.last_upload_intercept_time) < 3.0:
            logger.info("Ignoring duplicate filechooser event: cooldown active (%.2fs)", now - self.last_upload_intercept_time)
            return
        self.last_upload_intercept_time = now

        t = asyncio.create_task(self._process_filechooser(file_chooser))
        self.background_tasks.add(t)
        t.add_done_callback(self.background_tasks.discard)

    async def _process_filechooser(self, file_chooser: FileChooser) -> None:
        upload_id = self.next_upload_id
        self.next_upload_id += 1

        element = file_chooser.element
        accept_types = ""
        try:
            if element:
                accept_types = (await element.get_attribute("accept")) or ""
        except Exception:
            pass

        logger.info("Intercepted file upload chooser id=%d (page=%s, accept='%s')",
                    upload_id, self.page.url if self.page else "", accept_types)

        loop = asyncio.get_running_loop()
        resp_future = loop.create_future()
        chunk_queue = asyncio.Queue()
        self.pending_uploads[upload_id] = (file_chooser, resp_future, chunk_queue)

        tmp_path = None
        try:
            # 1. Send MSG_UPLOAD_REQUEST to Amiga to open ASL FileRequester
            accept_bytes = accept_types.encode("utf-8", errors="replace")[:255].ljust(256, b"\x00")
            req_pkt = STRUCT_MSG_UPLOAD_REQ.pack(
                MSG_UPLOAD_REQUEST,
                upload_id,
                0,  # max_size (0 = unconstrained)
                accept_bytes,
            )
            if not await self.send_packet(req_pkt):
                logger.warning("Could not send MSG_UPLOAD_REQUEST id=%d to Amiga", upload_id)
                return

            # 2. Wait up to 5 minutes for Amiga user to select a file in ASL FileRequester
            try:
                accepted, file_size, filename = await asyncio.wait_for(resp_future, timeout=300.0)
            except asyncio.TimeoutError:
                logger.warning("Upload id=%d timed out waiting for Amiga ASL selection", upload_id)
                return

            if not accepted or file_size == 0:
                logger.info("Upload id=%d cancelled by Amiga user or empty file", upload_id)
                return

            logger.info("Upload id=%d confirmed by Amiga: '%s' (%d bytes). Receiving chunks...",
                        upload_id, filename, file_size)

            # 3. Create temporary file on server to receive uploaded chunks
            safe_fname = re.sub(r'[^a-zA-Z0-9_.-]', '_', filename) or f"upload_{upload_id}.bin"
            tmp_fd, tmp_path = tempfile.mkstemp(prefix=f"vb_up_{upload_id}_", suffix=f"_{safe_fname}")

            received_bytes = 0

            with os.fdopen(tmp_fd, "wb") as f:
                while received_bytes < file_size and self.running:
                    try:
                        chunk_data = await asyncio.wait_for(chunk_queue.get(), timeout=30.0)
                    except asyncio.TimeoutError:
                        logger.warning("Upload id=%d timed out waiting for chunk from queue", upload_id)
                        break
                    f.write(chunk_data)
                    received_bytes += len(chunk_data)

            if received_bytes >= file_size:
                logger.info("Upload id=%d fully received (%d bytes). Setting files in browser!",
                            upload_id, received_bytes)
                await file_chooser.set_files(tmp_path)
                self._trigger_full_refresh(True)
                self._schedule_delayed_frames()
            else:
                logger.warning("Upload id=%d incomplete: received %d of %d bytes",
                               upload_id, received_bytes, file_size)

        except Exception as exc:
            logger.error("Error processing file upload id=%d: %s", upload_id, exc)
        finally:
            self.pending_uploads.pop(upload_id, None)
            if tmp_path:
                try:
                    # Allow browser time to finish form submission before deleting temp file
                    async def _delayed_unlink(p: str):
                        await asyncio.sleep(60.0)
                        try:
                            if os.path.exists(p):
                                os.remove(p)
                        except Exception:
                            pass
                    t_del = asyncio.create_task(_delayed_unlink(tmp_path))
                    self.background_tasks.add(t_del)
                    t_del.add_done_callback(self.background_tasks.discard)
                except Exception:
                    pass


# ============================================================================
# Hardware Detection & GPU Acceleration Helper
# ============================================================================
def detect_hardware_profile() -> dict:
    """
    Detects CPU architecture, CPU vendor/model, core count, GPU vendor (Intel/AMD/NVIDIA/Broadcom),
    and available hardware acceleration devices (/dev/dri, VA-API, etc.).
    """
    profile = {
        "arch": platform.machine().lower(),
        "cpu_vendor": "Unknown",
        "cpu_model": "Unknown",
        "cpu_cores": os.cpu_count() or 4,
        "is_low_power": False,
        "has_intel_gpu": False,
        "has_amd_gpu": False,
        "has_nvidia_gpu": False,
        "has_dri": False,
        "dri_render_nodes": [],
        "drm_drivers": [],
        "recommended_driver": None,
    }

    # 1. Inspect /proc/cpuinfo on Linux
    if os.path.exists("/proc/cpuinfo"):
        try:
            with open("/proc/cpuinfo", "r", encoding="utf-8", errors="ignore") as f:
                content = f.read()
                for line in content.splitlines():
                    if "vendor_id" in line and profile["cpu_vendor"] == "Unknown":
                        val = line.split(":", 1)[1].strip()
                        if "GenuineIntel" in val:
                            profile["cpu_vendor"] = "Intel"
                        elif "AuthenticAMD" in val:
                            profile["cpu_vendor"] = "AMD"
                    elif "model name" in line and profile["cpu_model"] == "Unknown":
                        profile["cpu_model"] = line.split(":", 1)[1].strip()
                    elif "Hardware" in line and profile["cpu_model"] == "Unknown":
                        profile["cpu_model"] = line.split(":", 1)[1].strip()
        except Exception:
            pass

    # Check for Raspberry Pi and low-power CPUs (Intel Atom, Celeron, Pentium, older chips with <= 4 low-IPC cores)
    model_lower = profile["cpu_model"].lower()
    if os.path.exists("/sys/firmware/devicetree/base/model"):
        try:
            with open("/sys/firmware/devicetree/base/model", "r", encoding="utf-8", errors="ignore") as f:
                dt_model = f.read().lower()
                if "raspberry pi" in dt_model:
                    profile["is_rpi"] = True
                    profile["is_low_power"] = True
                    profile["cpu_vendor"] = "Broadcom"
        except Exception:
            pass

    if any(k in model_lower for k in ["raspberry", "bcm2711", "bcm2712", "bcm2837", "cortex-a72", "cortex-a76"]):
        profile["is_rpi"] = True
        profile["is_low_power"] = True
        if profile["cpu_vendor"] == "Unknown":
            profile["cpu_vendor"] = "Broadcom"
    elif any(k in model_lower for k in ["atom", "x5-", "x7-", "celeron", "pentium", "j1900", "n3450", "n4000", "n5000", "silvermont", "airmont", "goldmont"]):
        profile["is_low_power"] = True
    elif profile["cpu_vendor"] == "Intel" and profile["cpu_cores"] <= 4 and ("core" not in model_lower and "xeon" not in model_lower):
        profile["is_low_power"] = True

    # 2. Check DRM / GPU nodes
    if os.path.exists("/dev/dri"):
        render_nodes = sorted(glob.glob("/dev/dri/renderD*"))
        card_nodes = sorted(glob.glob("/dev/dri/card*"))
        profile["has_dri"] = bool(render_nodes or card_nodes)
        profile["dri_render_nodes"] = render_nodes

        # Check vendor IDs in sysfs
        for vpath in glob.glob("/sys/class/drm/card*/device/vendor"):
            try:
                with open(vpath, "r", encoding="utf-8", errors="ignore") as f:
                    vendor_id = f.read().strip().lower()
                    if vendor_id == "0x8086":
                        profile["has_intel_gpu"] = True
                    elif vendor_id in ("0x1002", "0x1022"):
                        profile["has_amd_gpu"] = True
                    elif vendor_id == "0x10de":
                        profile["has_nvidia_gpu"] = True
            except Exception:
                pass

        # Detect active DRM kernel driver names (e.g. 'radeon' vs 'amdgpu', 'i915', 'vc4', 'v3d')
        drm_drivers = []
        for dpath in sorted(glob.glob("/sys/class/drm/card*/device/driver")):
            try:
                drv_name = os.path.basename(os.path.realpath(dpath))
                if drv_name and drv_name not in drm_drivers:
                    drm_drivers.append(drv_name)
            except Exception:
                pass
        profile["drm_drivers"] = drm_drivers

    # Fallback vendor detection from cpu_vendor if integrated graphics
    if profile["has_dri"] and not (profile["has_intel_gpu"] or profile["has_amd_gpu"] or profile["has_nvidia_gpu"]):
        if profile["cpu_vendor"] == "Intel":
            profile["has_intel_gpu"] = True
        elif profile["cpu_vendor"] == "AMD":
            profile["has_amd_gpu"] = True

    # Check VA-API driver recommendation for Intel
    if profile["has_intel_gpu"]:
        # Atom x5/x7 (Cherry Trail / Braswell, Gen 8) and older Haswell/Broadwell run best with i965
        if profile["is_low_power"] or any(k in model_lower for k in ["z8350", "z8300", "z8500", "z8700", "n3000", "n3050", "n3150", "n3700", "cherryview", "braswell"]):
            profile["recommended_driver"] = "i965"
        else:
            profile["recommended_driver"] = "iHD"

    return profile


def get_gpu_launch_flags(disable_gpu: bool = False, hw_profile: Optional[dict] = None) -> Tuple[List[str], bool]:
    """
    Determines optimal Chromium GPU hardware acceleration flags for headless streaming.
    Returns (flags_list, is_gpu_detected).
    """
    if disable_gpu:
        return ["--disable-gpu", "--disable-software-rasterizer"], False

    if hw_profile is None:
        hw_profile = detect_hardware_profile()

    has_dri = hw_profile.get("has_dri", False)
    drm_drivers = hw_profile.get("drm_drivers", [])

    # Legacy AMD radeon DRM (e.g. FirePro D500 / Tahiti GCN 1.0): --ignore-gpu-blocklist,
    # --enable-zero-copy and VA-API flags trigger Viz/DRM ring lockups that freeze the
    # entire Chromium browser process. Use stable CPU rasterization unless explicitly overridden.
    is_legacy_radeon = (
        hw_profile.get("has_amd_gpu", False)
        and not hw_profile.get("has_intel_gpu", False)
        and not hw_profile.get("has_nvidia_gpu", False)
        and ("radeon" in drm_drivers)
        and ("amdgpu" not in drm_drivers)
    )
    if is_legacy_radeon:
        if os.environ.get("VORTEX_FORCE_LEGACY_RADEON_GPU", "0") == "1":
            return ["--enable-gpu", "--use-gl=angle", "--use-angle=gles-egl"], has_dri
        return ["--disable-gpu", "--disable-software-rasterizer"], False

    gpu_flags = [
        "--enable-gpu",
        "--ignore-gpu-blocklist",
        "--enable-gpu-rasterization",
        "--enable-zero-copy",
    ]

    if hw_profile.get("is_rpi"):
        # Raspberry Pi (Broadcom VC4 / V3D GPU): ANGLE on OpenGL ES / EGL (no VA-API on Pi)
        gpu_flags.extend([
            "--use-gl=angle",
            "--use-angle=gles-egl",
            "--enable-features=CanvasOopRasterization",
        ])
    elif hw_profile.get("has_intel_gpu") and not hw_profile.get("has_amd_gpu") and not hw_profile.get("has_nvidia_gpu"):
        # Intel GPU: ANGLE GLES/EGL backend + VA-API decoder & encoder
        gpu_flags.extend([
            "--use-gl=angle",
            "--use-angle=gles-egl",
            "--enable-features=CanvasOopRasterization,VaapiVideoDecoder,VaapiVideoEncoder,VaapiIgnoreDriverChecks",
            "--disable-features=UseChromeOSDirectVideoDecoder",
        ])
    elif hw_profile.get("has_amd_gpu"):
        if "radeon" in drm_drivers and "amdgpu" not in drm_drivers:
            # Legacy AMD radeon DRM (e.g. hybrid setups): use Mesa radeonsi via ANGLE GLES/EGL.
            gpu_flags.extend([
                "--use-gl=angle",
                "--use-angle=gles-egl",
                "--enable-features=CanvasOopRasterization",
            ])
        else:
            # Modern AMD amdgpu DRM: ANGLE with Vulkan backend + VA-API
            gpu_flags.extend([
                "--use-gl=angle",
                "--use-angle=vulkan",
                "--enable-features=CanvasOopRasterization,VaapiVideoDecoder,VaapiVideoEncoder,VaapiIgnoreDriverChecks",
            ])
    else:
        # Generic Mesa / NVIDIA / ARM GPU: ANGLE GLES/EGL
        gpu_flags.extend([
            "--use-gl=angle",
            "--use-angle=gles-egl",
            "--enable-features=CanvasOopRasterization,VaapiVideoDecoder,VaapiVideoEncoder,VaapiIgnoreDriverChecks",
        ])

    return gpu_flags, has_dri


def _wait_pids_exit(pids: List[int], timeout: float) -> List[int]:
    """Polls /proc until all PIDs exit or timeout elapses; returns remaining alive PIDs."""
    deadline = time.monotonic() + timeout
    remaining = list(pids)
    while remaining and time.monotonic() < deadline:
        still_alive = []
        for pid in remaining:
            if os.path.exists(f"/proc/{pid}"):
                still_alive.append(pid)
        remaining = still_alive
        if not remaining:
            break
        time.sleep(0.1)
    return [pid for pid in remaining if os.path.exists(f"/proc/{pid}")]


def _find_proc_pids_matching(predicate) -> List[int]:
    my_pid = os.getpid()
    parent_pid = os.getppid()
    matches: List[int] = []
    try:
        entries = os.listdir("/proc")
    except Exception:
        return matches
    for entry in entries:
        if not entry.isdigit():
            continue
        pid = int(entry)
        if pid in (my_pid, parent_pid):
            continue
        try:
            with open(f"/proc/{pid}/cmdline", "rb") as f:
                raw = f.read()
            if not raw:
                continue
            argv = [part.decode("utf-8", errors="ignore") for part in raw.split(b"\x00") if part]
            if argv and predicate(pid, argv):
                matches.append(pid)
        except Exception:
            continue
    return matches


def _extract_cli_port(argv: List[str], default_port: int = 7788) -> int:
    for idx, arg in enumerate(argv):
        if arg == "--port" and idx + 1 < len(argv):
            if argv[idx + 1].isdigit():
                return int(argv[idx + 1])
        elif arg.startswith("--port="):
            val = arg.split("=", 1)[1]
            if val.isdigit():
                return int(val)
    return default_port


def _cleanup_existing_server_and_locks(port: int, profile_dir: str, config_dir: str) -> None:
    """Gracefully stops older matching server.py or orphaned browser locks without corrupting SQLite."""
    pid_file = os.path.join(config_dir, "server.pid")
    other_pids: List[int] = []

    if os.path.isfile(pid_file):
        try:
            with open(pid_file, "r", encoding="utf-8") as f:
                recorded_pid = int(f.read().strip())
            if recorded_pid != os.getpid() and os.path.exists(f"/proc/{recorded_pid}"):
                with open(f"/proc/{recorded_pid}/cmdline", "rb") as cf:
                    argv = [p.decode("utf-8", errors="ignore") for p in cf.read().split(b"\x00") if p]
                if any(os.path.basename(a) == "server.py" for a in argv):
                    other_pids.append(recorded_pid)
        except Exception:
            pass

    proc_matches = _find_proc_pids_matching(
        lambda pid, argv: any(os.path.basename(a) == "server.py" for a in argv)
        and _extract_cli_port(argv) == port
    )
    for pid in proc_matches:
        if pid not in other_pids:
            other_pids.append(pid)

    if other_pids:
        logger.info("Gracefully stopping previous VortexBrowser server instance(s) %s...", other_pids)
        for pid in other_pids:
            try:
                os.kill(pid, signal.SIGTERM)
            except Exception:
                pass
        remaining = _wait_pids_exit(other_pids, timeout=5.0)
        for pid in remaining:
            logger.warning("Previous server PID %d did not exit after SIGTERM timeout; sending SIGKILL", pid)
            try:
                os.kill(pid, signal.SIGKILL)
            except Exception:
                pass
        if remaining:
            _wait_pids_exit(remaining, timeout=1.0)

    # Clean up any orphaned Chromium processes still holding --user-data-dir={profile_dir}
    real_profile = os.path.realpath(profile_dir)
    user_data_flags = {f"--user-data-dir={profile_dir}", f"--user-data-dir={real_profile}"}

    def _is_orphaned_chrome(pid: int, argv: List[str], main_only: bool) -> bool:
        if not any(a in user_data_flags for a in argv):
            return False
        if main_only and any(a.startswith("--type=") for a in argv):
            return False
        return True

    main_chrome_pids = _find_proc_pids_matching(lambda pid, argv: _is_orphaned_chrome(pid, argv, True))
    if main_chrome_pids:
        logger.info("Gracefully stopping orphaned browser process(es) %s for %s...", main_chrome_pids, profile_dir)
        for pid in main_chrome_pids:
            try:
                os.kill(pid, signal.SIGTERM)
            except Exception:
                pass
        _wait_pids_exit(main_chrome_pids, timeout=3.0)

    stray_pids = _find_proc_pids_matching(lambda pid, argv: _is_orphaned_chrome(pid, argv, False))
    if stray_pids:
        for pid in stray_pids:
            try:
                os.kill(pid, signal.SIGTERM)
            except Exception:
                pass
        remaining_chrome = _wait_pids_exit(stray_pids, timeout=2.0)
        for pid in remaining_chrome:
            try:
                os.kill(pid, signal.SIGKILL)
            except Exception:
                pass
        if remaining_chrome:
            _wait_pids_exit(remaining_chrome, timeout=0.5)

    # Clean stale Chromium SingletonLock
    for lock_name in ("SingletonLock", "SingletonCookie", "SingletonSocket"):
        lp = os.path.join(profile_dir, lock_name)
        if os.path.lexists(lp):
            try:
                os.remove(lp)
            except Exception:
                pass


def _write_server_pid(pid_file: str) -> None:
    try:
        tmp_pid = f"{pid_file}.tmp.{os.getpid()}"
        with open(tmp_pid, "w", encoding="utf-8") as f:
            f.write(str(os.getpid()))
        try:
            os.chmod(tmp_pid, 0o600)
        except Exception:
            pass
        os.replace(tmp_pid, pid_file)
    except Exception:
        pass


def _remove_server_pid(pid_file: str) -> None:
    try:
        if os.path.isfile(pid_file):
            with open(pid_file, "r", encoding="utf-8") as f:
                if f.read().strip() == str(os.getpid()):
                    os.unlink(pid_file)
    except Exception:
        pass


async def _close_browser_gracefully(browser: BrowserContext, use_rustwright: bool) -> None:
    """Sends CDP Browser.close when needed and waits cleanly so SQLite/LevelDB flush to disk."""
    if not browser:
        return
    if use_rustwright and _HAS_WEBSOCKETS and websockets is not None:
        try:
            def _fetch_ws_url() -> Optional[str]:
                with urllib.request.urlopen("http://127.0.0.1:9222/json/version", timeout=1.5) as resp:
                    info = json.loads(resp.read().decode("utf-8", errors="ignore"))
                    return info.get("webSocketDebuggerUrl")

            ws_url = await asyncio.get_running_loop().run_in_executor(None, _fetch_ws_url)
            if ws_url:
                async with websockets.connect(ws_url, open_timeout=1.5, close_timeout=1.5) as ws:
                    await ws.send(json.dumps({"id": 1, "method": "Browser.close"}))
                    await asyncio.sleep(0.5)
        except Exception as exc:
            logger.debug("Rustwright CDP Browser.close notice: %s", exc)
    try:
        await asyncio.wait_for(browser.close(), timeout=8.0)
    except Exception as exc:
        logger.debug("browser.close notice: %s", exc)


# ============================================================================
# Main Server Entrypoint
# ============================================================================
async def run_server(args: argparse.Namespace) -> None:
    if hasattr(args, "log_level") and args.log_level:
        lvl = getattr(logging, str(args.log_level).upper(), logging.CRITICAL)
        logger.setLevel(lvl)
        logging.getLogger().setLevel(lvl)
    global _SERVER_LOCALE, _LOCALE_EXPLICIT
    server_locale, locale_explicit = detect_system_locale(getattr(args, "locale", ""))
    _SERVER_LOCALE = server_locale
    _LOCALE_EXPLICIT = locale_explicit
    logger.info(
        "Browser Locale: %s (%s)",
        server_locale,
        "explicit --locale / VORTEX_LOCALE" if locale_explicit else "system auto-detect; Amiga @LANG override enabled",
    )
    engine = getattr(args, "engine", "chromium").lower()
    config_dir = get_vortex_config_dir()
    pid_file = os.path.join(config_dir, "server.pid")
    profile_dir = get_vortex_profile_dir(engine)
    state_file = get_vortex_storage_state_path(engine)
    is_fallback_profile = False
    try:
        os.makedirs(profile_dir, mode=0o700, exist_ok=True)
        test_file = os.path.join(profile_dir, ".write_test")
        with open(test_file, "w") as f:
            f.write("ok")
        os.unlink(test_file)
    except Exception as e:
        fallback_dir = os.path.join(tempfile.gettempdir(), f"vortexbrowse_{engine}_profile_{os.getuid()}")
        logger.warning("Profile directory '%s' is not writable (%s). Falling back to temporary directory '%s'", profile_dir, e, fallback_dir)
        profile_dir = fallback_dir
        is_fallback_profile = True
        os.makedirs(profile_dir, mode=0o700, exist_ok=True)

    _cleanup_existing_server_and_locks(args.port, profile_dir, config_dir)
    _write_server_pid(pid_file)
    needs_recovery = check_profile_needs_recovery(profile_dir, state_file, engine, is_fallback_profile)

    loop = asyncio.get_running_loop()
    stop_event = asyncio.Event()
    registered_signals = []
    for sig in (signal.SIGTERM, signal.SIGINT):
        try:
            loop.add_signal_handler(sig, stop_event.set)
            registered_signals.append(sig)
        except NotImplementedError:
            pass

    audio_ready = ensure_audio_sink()
    portal = LocalPortalHTTPServer(host="127.0.0.1", port=args.portal_port)
    portal_url = await portal.start()

    if args.format.lower() in ("yuv", "yuv420", "yuv420p", "12", "12bit"):
        pixel_fmt = FMT_YUV420P
    elif args.format.lower() == "argb32":
        pixel_fmt = FMT_ARGB32
    elif args.format.lower() == "rgb565":
        pixel_fmt = FMT_RGB565
    else:
        # Default: YUV420P — most bandwidth-efficient (1.5 bpp) and the AmigaOS 4
        # GPU handles YUV→RGB conversion natively in CompositeTags (PIXF_YUV420P),
        # matching the V-MPlayer vaPutSurface approach of keeping pixel data in
        # GPU-native formats with zero CPU-side color conversion.
        pixel_fmt = FMT_YUV420P

    browser_env = dict(os.environ)
    if audio_ready:
        browser_env["PULSE_SINK"] = AUDIO_SINK_NAME

    if engine == "chromium":
        custom_bin = getattr(args, "browser_bin", None)
        full_chrome = find_full_chromium_executable(custom_bin)
        enable_exts = getattr(args, "enable_extensions", False)
        all_exts = find_all_extension_paths() if enable_exts else []

        # Clean up stale SingletonLock/Socket/Cookie from prior crashes or killed processes
        for lock_file in ("SingletonLock", "SingletonCookie", "SingletonSocket"):
            lp = os.path.join(profile_dir, lock_file)
            if os.path.exists(lp) or os.path.islink(lp):
                try:
                    os.unlink(lp)
                    logger.info("Cleaned up stale lock file: %s", lp)
                except Exception as e:
                    logger.warning("Could not remove lock file %s: %s", lp, e)

        if os.getuid() == 0:
            logger.warning("=" * 68)
            logger.warning(" HINWEIS: Der VortexBrowser Server laeuft als ROOT (UID 0)!")
            logger.warning(" Es wird dringend empfohlen, den Server als normaler Benutzer")
            logger.warning(" (OHNE sudo) auszufuehren, um Berechtigungskonflikte zu vermeiden.")
            logger.warning("=" * 68)

        hw = detect_hardware_profile()
        drm_list = hw.get("drm_drivers", [])
        drm_str = f" ({','.join(drm_list)})" if drm_list else ""
        logger.info(
            "Hardware Profile: %s | CPU: %s (%s, %d cores%s) | DRI: %s%s",
            hw.get("arch", "x86_64"),
            hw.get("cpu_model", "Unknown"),
            hw.get("cpu_vendor", "Unknown"),
            hw.get("cpu_cores", 4),
            ", Low-Power Atom/Celeron" if hw.get("is_low_power") else "",
            "Yes" if hw.get("has_dri") else "No",
            drm_str,
        )

        # Setup Intel VA-API driver if recommended and installed
        if hw.get("has_intel_gpu") and hw.get("recommended_driver") and "LIBVA_DRIVER_NAME" not in os.environ:
            rec = hw["recommended_driver"]
            for cand in [
                f"/usr/lib/dri/{rec}_drv_video.so",
                f"/usr/lib/x86_64-linux-gnu/dri/{rec}_drv_video.so",
                f"/usr/lib64/dri/{rec}_drv_video.so",
            ]:
                if os.path.exists(cand):
                    os.environ["LIBVA_DRIVER_NAME"] = rec
                    logger.info("Intel VA-API Driver configured: LIBVA_DRIVER_NAME=%s", rec)
                    break

        gpu_flags, has_dri = get_gpu_launch_flags(
            disable_gpu=getattr(args, "disable_gpu", False),
            hw_profile=hw,
        )
        if getattr(args, "disable_gpu", False):
            logger.info("GPU Hardware Acceleration: DISABLED by user flag (--disable-gpu)")
        elif (
            not has_dri
            and hw.get("has_amd_gpu")
            and "radeon" in hw.get("drm_drivers", [])
            and "amdgpu" not in hw.get("drm_drivers", [])
        ):
            logger.info(
                "GPU Hardware Acceleration: DISABLED (Legacy AMD 'radeon' DRM detected e.g. FirePro D500/Tahiti — using stable CPU rasterization to prevent GPU/Chromium lockups; set VORTEX_FORCE_LEGACY_RADEON_GPU=1 to override)"
            )
        elif hw.get("is_rpi"):
            logger.info("GPU Hardware Acceleration: ENABLED (Raspberry Pi Broadcom V3D ANGLE GLES/EGL active)")
        elif hw.get("has_intel_gpu"):
            logger.info("GPU Hardware Acceleration: ENABLED (Intel VA-API & ANGLE GLES/EGL hardware rasterization active)")
        elif has_dri:
            logger.info("GPU Hardware Acceleration: ENABLED (Vulkan / ANGLE / VA-API / /dev/dri render node detected)")
        else:
            logger.info("GPU Hardware Acceleration: ENABLED (ANGLE Vulkan requested, fallback to SwiftShader if no DRM)")

        # On Atom / Celeron 4-core chips, dedicate 2 threads to raster and preserve 2 threads
        # for Python asyncio loop, screencast frame grab, and SIMD compression.
        is_low_mem = bool(hw.get("is_low_power") or hw.get("is_rpi"))
        js_heap_mb = 256 if is_low_mem else 512
        renderer_limit = 2 if is_low_mem else 4

        if hw.get("is_low_power") or hw.get("cpu_cores", 4) <= 4:
            raster_threads = 2
            logger.info("Low-power CPU detected: Adjusted raster threads to 2 (preserves cores for stream encoding)")
        else:
            raster_threads = 4

        launch_args = _merge_chromium_feature_flags([
            "--ozone-platform=headless",
            f"--lang={server_locale}",
            "--remote-debugging-port=9222",
            "--remote-allow-origins=*",
            "--no-sandbox",
            "--disable-dev-shm-usage",
            "--font-render-hinting=medium",
            "--enable-font-antialiasing",
            "--enable-smooth-scrolling",
            "--autoplay-policy=no-user-gesture-required",
            "--disable-blink-features=AutomationControlled",
            f"--num-raster-threads={raster_threads}",
            "--enable-low-res-tiling",
            f"--js-flags=--max-old-space-size={js_heap_mb}",
            f"--renderer-process-limit={renderer_limit}",
            "--disk-cache-size=67108864",
            "--media-cache-size=33554432",
            "--disable-features=Translate,OptimizationHints,MediaRouter,DialMediaRouteProvider",
        ] + gpu_flags)
        if all_exts:
            ext_list = ",".join(all_exts)
            logger.info("Enabling unpacked Chrome extensions (%d): %s", len(all_exts), ext_list)
            launch_args.append(f"--disable-extensions-except={ext_list}")
            launch_args.append(f"--load-extension={ext_list}")
        else:
            logger.info("Browser extensions (uBlock Origin, h264ify): DISABLED as requested")
            launch_args.append("--disable-extensions")

    disable_rustwright = getattr(args, "no_rustwright", False) or (
        os.environ.get("VORTEX_DISABLE_RUSTWRIGHT", "").strip().lower() in ("1", "true", "yes")
    )
    use_rustwright = (
        _HAS_RUSTWRIGHT
        and _rustwright_async is not None
        and engine == "chromium"
        and not disable_rustwright
    )
    pw_factory = _rustwright_async if use_rustwright else _playwright_async
    if use_rustwright:
        logger.info("Browser Automation Core: Rustwright (native Rust PyO3 + DirectCDP)")
    elif disable_rustwright and engine == "chromium":
        logger.info("Browser Automation Core: Playwright (chromium; Rustwright disabled via --no-rustwright / VORTEX_DISABLE_RUSTWRIGHT)")
    else:
        logger.info("Browser Automation Core: Playwright (%s)", engine)

    try:
        async with pw_factory() as pw:
            if engine == "firefox":
                logger.info("Launching Playwright Firefox persistent context at %s...", profile_dir)
                firefox_user_agent = (
                    "Mozilla/5.0 (X11; Linux x86_64; rv:134.0) Gecko/20100101 Firefox/134.0"
                )
                browser = await pw.firefox.launch_persistent_context(
                    user_data_dir=profile_dir,
                    headless=True,
                    ignore_default_args=["--mute-audio"],
                    env=browser_env,
                    viewport={"width": args.width, "height": args.height},
                    accept_downloads=True,
                    locale=server_locale,
                    user_agent=firefox_user_agent,
                    firefox_user_prefs={
                        "browser.startup.page": 0,
                        "browser.sessionstore.resume_from_crash": False,
                        "browser.sessionstore.max_tabs_undo": 0,
                        "browser.sessionstore.max_resumed_crashes": 0,
                        "browser.tabs.warnOnClose": False,
                        "intl.accept_languages": build_accept_language(server_locale),
                        "media.autoplay.default": 0,
                        "media.autoplay.blocking_policy": 0,
                        "media.ffmpeg.vaapi.enabled": True,
                    },
                )
            elif full_chrome:
                browser_name = "Ungoogled-Chromium" if "ungoogled" in full_chrome.lower() else "Chromium"
                logger.info("Launching %s (%s) persistent context at %s...", browser_name, full_chrome, profile_dir)
                launch_kwargs: Dict[str, Any] = {
                    "user_data_dir": profile_dir,
                    "executable_path": full_chrome,
                    "headless": False,
                    "ignore_default_args": ["--mute-audio", "--enable-automation"],
                    "env": browser_env,
                    "args": launch_args,
                    "viewport": {"width": args.width, "height": args.height},
                    "accept_downloads": True,
                    "locale": server_locale,
                }
                detected_ua = _detect_chromium_ua(full_chrome)
                if detected_ua:
                    launch_kwargs["user_agent"] = detected_ua
                browser = await pw.chromium.launch_persistent_context(**launch_kwargs)
            else:
                logger.info("Launching Headless Chromium fallback persistent context at %s...", profile_dir)
                fallback_args = _merge_chromium_feature_flags([
                    f"--lang={server_locale}",
                    "--remote-debugging-port=9222",
                    "--remote-allow-origins=*",
                    "--no-sandbox",
                    "--disable-dev-shm-usage",
                    "--font-render-hinting=medium",
                    "--enable-font-antialiasing",
                    "--enable-smooth-scrolling",
                    "--autoplay-policy=no-user-gesture-required",
                    "--disable-blink-features=AutomationControlled",
                    f"--num-raster-threads={raster_threads}",
                    "--enable-low-res-tiling",
                    f"--js-flags=--max-old-space-size={js_heap_mb}",
                    f"--renderer-process-limit={renderer_limit}",
                    "--disk-cache-size=67108864",
                    "--media-cache-size=33554432",
                    "--disable-features=Translate,OptimizationHints,MediaRouter,DialMediaRouteProvider",
                ] + gpu_flags)
                if all_exts:
                    ext_list = ",".join(all_exts)
                    fallback_args.append(f"--disable-extensions-except={ext_list}")
                    fallback_args.append(f"--load-extension={ext_list}")
                else:
                    fallback_args.append("--disable-extensions")
                fallback_kwargs: Dict[str, Any] = {
                    "user_data_dir": profile_dir,
                    "headless": True,
                    "ignore_default_args": ["--mute-audio", "--enable-automation"],
                    "env": browser_env,
                    "args": fallback_args,
                    "viewport": {"width": args.width, "height": args.height},
                    "accept_downloads": True,
                    "locale": server_locale,
                }
                detected_ua = _detect_chromium_ua(getattr(pw.chromium, "executable_path", None))
                if detected_ua:
                    fallback_kwargs["user_agent"] = detected_ua
                browser = await pw.chromium.launch_persistent_context(**fallback_kwargs)

            if needs_recovery:
                await recover_profile_storage_state(browser, state_file)

            context_state: Dict[str, Any] = {
                "scripts_initialized": False,
                "active_session": None,
            }

            global cpu_gov_global
            cpu_gov = CPUGovernorManager(enabled=not getattr(args, "no_cpu_governor", False))
            cpu_gov_global = cpu_gov

            active_session: Optional[AmigaClientSession] = None
            active_session_lock = asyncio.Lock()

            async def handle_client(reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
                nonlocal active_session
                await cpu_gov.client_connected()
                session = None
                try:
                    async with active_session_lock:
                        prev_sess = active_session
                        if prev_sess is not None:
                            logger.info("New client connected: waiting for previous session to close cleanly")
                            prev_sess.running = False
                            try:
                                prev_sess.writer.close()
                            except Exception:
                                pass
                            try:
                                await asyncio.wait_for(prev_sess._closed_event.wait(), timeout=3.0)
                            except Exception:
                                pass

                        session = AmigaClientSession(
                            reader=reader,
                            writer=writer,
                            browser=browser,
                            width=args.width,
                            height=args.height,
                            pixel_format=pixel_fmt,
                            target_fps=args.fps,
                            start_url=args.url,
                            portal_url=portal_url,
                            audio_enabled=audio_ready,
                            jpeg_quality=getattr(args, "jpeg_quality", 100),
                            codec_mode=getattr(args, "codec", "auto"),
                            engine=engine,
                            stream_mode=getattr(args, "stream_mode", "auto"),
                            video_bitrate=getattr(args, "video_bitrate", 8000),
                            storage_state_file=state_file,
                            context_state=context_state,
                            browser_locale=server_locale,
                            locale_explicit=locale_explicit,
                        )
                        active_session = session

                    try:
                        await session.run()
                    finally:
                        async with active_session_lock:
                            if active_session is session:
                                active_session = None
                finally:
                    await cpu_gov.client_disconnected()

            server = await asyncio.start_server(handle_client, args.host, args.port)
            extra_servers = []
            if args.port != 9757:
                try:
                    srv_lan = await asyncio.start_server(handle_client, args.host, 9757)
                    extra_servers.append(srv_lan)
                except OSError as exc:
                    logger.warning("Could not bind secondary LAN port 9757: %s", exc)

            all_socks = list(server.sockets or [])
            for s in extra_servers:
                all_socks.extend(s.sockets or [])
            addrs = ", ".join(str(sock.getsockname()) for sock in all_socks)
            logger.info(
                "VortexBrowse Server ready on %s | Engine: %s | Viewport: %dx%d (1:1 Auto-Sync) | Format: %s | Codec: LZ4+DirtySlice | Audio: %s",
                addrs,
                engine.upper(),
                args.width,
                args.height,
                "YUV420P" if pixel_fmt == FMT_YUV420P else ("RGB565" if pixel_fmt == FMT_RGB565 else "ARGB32"),
                "ON (Full Browser -> PipeWire -> AHI)" if audio_ready else "OFF",
            )

            try:
                async with server:
                    if not stop_event.is_set():
                        serve_task = asyncio.create_task(server.serve_forever())
                        stop_wait_task = asyncio.create_task(stop_event.wait())
                        try:
                            await asyncio.wait(
                                {serve_task, stop_wait_task},
                                return_when=asyncio.FIRST_COMPLETED,
                            )
                        finally:
                            for t in (serve_task, stop_wait_task):
                                if not t.done():
                                    t.cancel()
                                    try:
                                        await t
                                    except asyncio.CancelledError:
                                        pass
            finally:
                logger.info("Shutting down VortexBrowse Server cleanly...")
                sess = active_session
                if sess is not None and sess.running:
                    sess.running = False
                    try:
                        sess.writer.close()
                    except Exception:
                        pass
                    try:
                        await asyncio.wait_for(sess.save_storage_state(force=True), timeout=3.0)
                    except Exception:
                        pass
                cpu_gov.restore()
                for s in extra_servers:
                    s.close()
                    await s.wait_closed()
                try:
                    await asyncio.wait_for(export_merged_storage_state(browser, state_file), timeout=5.0)
                except Exception:
                    pass
                await _close_browser_gracefully(browser, use_rustwright)
    finally:
        for sig in registered_signals:
            try:
                loop.remove_signal_handler(sig)
            except Exception:
                pass
        await portal.stop()
        _remove_server_pid(pid_file)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="VortexBrowse CachyOS HP EliteDesk 805 G6 (AMD Ryzen PRO) 1080p@25fps Server")
    parser.add_argument("--host", default="0.0.0.0", help="Bind address (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=7788, help="TCP port (default: 7788, plus 9757 for LAN)")
    parser.add_argument("--portal-port", type=int, default=8089, help="Local start portal HTTP port")
    parser.add_argument("--width", type=int, default=1920, help="Initial browser width (default: 1920 Full HD)")
    parser.add_argument("--height", type=int, default=1080, help="Initial browser height (default: 1080 Full HD)")
    parser.add_argument("--format", choices=["argb32", "rgb565", "yuv420p", "yuv"], default="yuv420p", help="Wire pixel format (default: yuv420p — GPU-native YUV, lowest bandwidth)")
    parser.add_argument("--fps", type=int, default=25, help="Max target FPS (default: 25)")
    parser.add_argument("--url", default="", help="Optional initial URL (default: built-in Amiga portal)")
    parser.add_argument("--browser-bin", default=None, help="Explicit path to Chromium or Ungoogled-Chromium executable")
    parser.add_argument("--codec", choices=["auto", "jpeg", "lz4", "h264"], default="auto", help="Streaming codec (default: auto)")
    parser.add_argument("--stream-mode", choices=["auto", "lz4", "h264"], default="auto", help="Video streaming mode (default: auto = adaptive H.264 on video, LZ4 otherwise)")
    parser.add_argument("--video-bitrate", type=int, default=8000, help="H.264 video stream bitrate in kbps (default: 8000 kbps)")
    parser.add_argument("--log-level", default=os.environ.get("VORTEX_LOG_LEVEL", "CRITICAL"), help="Log level (default: CRITICAL)")
    parser.add_argument("--jpeg-quality", type=int, default=82, help="JPEG capture quality 20-100 (default: 82 for Full HD 1080p@25fps)")
    parser.add_argument("--enable-extensions", dest="enable_extensions", action="store_true", default=True, help="Enable browser extensions (uBlock, h264ify) [default: enabled]")
    parser.add_argument("--disable-extensions", dest="enable_extensions", action="store_false", help="Disable all browser extensions")
    parser.add_argument("--disable-gpu", action="store_true", help="Disable GPU hardware acceleration (forces CPU software rasterization)")
    parser.add_argument("--no-cpu-governor", action="store_true", help="Disable automatic switching of CPU governor to performance during client sessions")
    parser.add_argument(
        "--no-rustwright",
        dest="no_rustwright",
        action="store_true",
        default=os.environ.get("VORTEX_DISABLE_RUSTWRIGHT", "").strip().lower() in ("1", "true", "yes"),
        help="Disable Rustwright and use standard Playwright for Chromium [env: VORTEX_DISABLE_RUSTWRIGHT=1]",
    )
    parser.add_argument(
        "--locale",
        default=os.environ.get("VORTEX_LOCALE", ""),
        help="Browser & website language locale (e.g. en-US, fr-FR, de-DE; default: auto-detect from system/Amiga) [env: VORTEX_LOCALE]",
    )
    parser.add_argument(
        "--engine",
        choices=["chromium", "firefox"],
        default=os.environ.get("VORTEX_ENGINE", "chromium"),
        help="Headless browser engine: 'chromium' (default, CDP-accelerated) or 'firefox' (Playwright Firefox) [env: VORTEX_ENGINE]",
    )
    return parser.parse_args()


if __name__ == "__main__":
    try:
        asyncio.run(run_server(parse_args()))
    except KeyboardInterrupt:
        logger.info("VortexBrowse Server stopped.")
