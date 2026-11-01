#!/usr/bin/env python3
"""
VortexBrowse - CachyOS HP EliteDesk 805 G6 (AMD Ryzen PRO / Radeon Vega) Edition
- Specially tuned for Full HD (1920x1080) @ 25 FPS streaming to AmigaOS 4.1 PowerPC
- Uses C-native AVX2 + OpenMP + libturbojpeg + liblz4 pipeline (encode_jpeg_to_frame_packet)
- Streams 12-bpp Planar YUV420P with 2D Dirty-Rect (CODEC_LZ4_RECT) & full-frame LZ4
- Captures real browser audio via PipeWire/PulseAudio virtual sink (full Chromium + ozone-headless)
  and streams 22050Hz S16BE Stereo PCM to AmigaOS 4 AHI (ahi.device)
"""

import argparse
import asyncio
import atexit
import base64
import ctypes
import glob
import hashlib
import html
import io
import logging
import os
import platform
import re
import socket
import subprocess
import struct
import sys
import tempfile
import time
import urllib.request
from typing import Dict, List, Optional, Tuple, Union
from urllib.parse import parse_qs, quote_plus, unquote, urlencode, urlparse

import lz4.block
from PIL import Image
from playwright.async_api import async_playwright, Browser, BrowserContext, Page, Download, CDPSession, Frame

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

FMT_ARGB32 = 1
FMT_RGB565 = 2
FMT_YUV420P = 3

CODEC_RAW       = 0
CODEC_LZ4       = 1
CODEC_LZ4_SLICE = 2
CODEC_JPEG      = 3
CODEC_LZ4_RECT  = 4

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
STRUCT_MSG_NAV_UPDATE   = struct.Struct(">B512s")
STRUCT_MSG_CTX_MENU     = struct.Struct(">B512s128s512s")
STRUCT_MSG_PROGRESS     = struct.Struct(">BBx64s")

DOWNLOAD_CHUNK_SIZE = 64 * 1024
AUDIO_SAMPLE_RATE   = 22050
AUDIO_CHANNELS      = 2
AUDIO_CHUNK_BYTES   = 8192  # ~92ms of 22050Hz 16-bit stereo PCM
AUDIO_SINK_NAME     = "vortexbrowse_sink"

logging.basicConfig(
    level=logging.INFO,
    format="[%(asctime)s] [VortexServer] %(levelname)s: %(message)s",
    datefmt="%H:%M:%S",
)
logger = logging.getLogger("VortexServer")

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


def ensure_audio_sink() -> bool:
    """Creates and unmutes a dedicated PipeWire/PulseAudio null-sink for AmigaOS 4 audio streaming."""
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


def find_full_chromium_executable(custom_bin: Optional[str] = None) -> Optional[str]:
    """
    Finds the full Chromium / Ungoogled-Chromium binary on Linux (x86_64, aarch64, Raspberry Pi 4).
    Uses full Chromium instead of chrome-headless-shell so that PipeWire / PulseAudio
    audio output is fully functional.
    """
    if custom_bin and os.path.isfile(custom_bin) and os.access(custom_bin, os.X_OK):
        return custom_bin

    priority_patterns = [
        os.path.expanduser("~/.local/opt/ungoogled-chromium/usr/lib/chromium/chromium"),
        os.path.expanduser("~/.local/bin/ungoogled-chromium"),
        "/usr/bin/ungoogled-chromium",
        "/usr/lib/ungoogled-chromium/ungoogled-chromium",
        "/usr/bin/brave-browser",
        "/usr/bin/brave",
        "/usr/bin/chromium-browser",
        "/usr/bin/chromium",
        "/usr/lib/chromium-browser/chromium-browser",
        "/usr/lib/chromium/chromium",
        "/snap/bin/chromium",
    ]
    for p in priority_patterns:
        if os.path.isfile(p) and os.access(p, os.X_OK):
            return p

    # Fallback to playwright cache (newest version first)
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
<title>Odyssey Web Browser - AmigaOS 4.1 Edition</title>
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
  .btn-search, .btn-odyssey {
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
  .btn-search:hover, .btn-odyssey:hover {
    background: linear-gradient(180deg, #ffffff 0%, #e2e8f0 100%);
    border-color: #475569;
  }
  .btn-search:active, .btn-odyssey:active {
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
    .btn-search, .btn-odyssey {
      background: linear-gradient(180deg, #3f3f46 0%, #27272a 100%);
      border-color: #52525b;
      color: #f4f4f5;
    }
    .btn-search:hover, .btn-odyssey:hover {
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
      <h1>Odyssey Web Browser / VortexBrowse AmigaOS 4.1</h1>
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
          <a class="btn-odyssey" href="https://www.amiga-news.de">&Ouml;ffnen</a>
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
          <a class="btn-odyssey" href="https://os4depot.net">&Ouml;ffnen</a>
        </div>
      </div>
    </div>

    <div class="card">
      <div class="card-top card-top-red"></div>
      <div class="card-body">
        <div>
          <h3>YouTube</h3>
          <p>Videos &amp; Musik mit direkter 22050Hz AHI-Stereo-Ton&uuml;bertragung auf die Workbench.</p>
        </div>
        <div class="card-action">
          <a class="btn-odyssey" href="https://www.youtube.com">&Ouml;ffnen</a>
          <a class="btn-odyssey" href="https://m.youtube.com" style="margin-left: 6px; background: linear-gradient(180deg, #d32f2f, #9a0007);">Lite / Mobil</a>
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
          <a class="btn-odyssey" href="https://www.amigans.net">&Ouml;ffnen</a>
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
          <a class="btn-odyssey" href="https://aminet.net">&Ouml;ffnen</a>
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
          <a class="btn-odyssey" href="https://www.twitch.tv">&Ouml;ffnen</a>
          <a class="btn-odyssey" href="https://m.twitch.tv" style="margin-left: 6px; background: linear-gradient(180deg, #9146ff, #5c16c5);">Lite / Mobil</a>
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
          <button class="btn-odyssey" id="btn-test" onclick="window._clicks=(window._clicks||0)+1; playAmigaSynth(); document.getElementById('click-status').textContent='Ton + Klick OK! ('+window._clicks+'x)';">Ton &amp; Klick-Test</button>
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
                "Accept-Language": "de-DE,de;q=0.9,en;q=0.8",
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
<title>Odyssey Suche: {q_esc}</title>
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

def _bind_fast_pixconv_symbols(_lib) -> bool:
    global _HAS_NATIVE_PACKET_ENCODER
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
    _is_arm = platform.machine().lower().startswith(("aarch64", "arm"))
    _candidates = ["fast_pixconv_aarch64.so", "fast_pixconv.so"] if _is_arm else ["fast_pixconv.so", "fast_pixconv_aarch64.so"]
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
            _target_so_name = "fast_pixconv_aarch64.so" if _is_arm else "fast_pixconv.so"
            _target_so_path = os.path.join(_base_dir, _target_so_name)
            try:
                logger.info("Attempting auto-compilation of native SIMD + TurboJPEG + LZ4 helper (%s)...", _target_so_name)
                if _is_arm:
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
    ):
        self.reader = reader
        self.writer = writer
        self.browser = browser
        self.is_persistent_context: bool = isinstance(browser, BrowserContext)
        self.engine: str = engine.lower()
        self.width = width
        self.height = height
        self.pixel_format = pixel_format
        self.target_fps = max(1, min(60, target_fps))
        self.start_url = start_url
        self.portal_url = portal_url
        self.audio_enabled = audio_enabled
        self.base_jpeg_quality: int = max(20, min(100, jpeg_quality))
        self.video_jpeg_quality: int = min(self.base_jpeg_quality, 60)
        self.current_screencast_quality: int = self.base_jpeg_quality
        self.jpeg_quality: int = self.base_jpeg_quality
        self.is_video_mode: bool = False
        self._last_video_eval_time: float = 0.0
        self._last_video_state_time: float = 0.0
        self._video_element_playing: bool = False
        self._frames_last_sec: int = 0
        self._sec_start_time: float = time.monotonic()
        self.codec_mode: str = codec_mode

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
        self.cdp: Optional[CDPSession] = None
        self.tabs: List[Page] = []
        self.active_tab_idx: int = 0

        self.soft_scroll: bool = True
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
        self.background_tasks: set = set()

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

        # Pre-allocated pixel conversion buffers (reused across frames to avoid alloc churn)
        self._argb_buf: Optional[ctypes.Array] = None
        self._rgb565_buf: Optional[ctypes.Array] = None
        self._yuv_buf: Optional[ctypes.Array] = None
        self._rect_buf: Optional[ctypes.Array] = None
        self._pkt_buf: Optional[ctypes.Array] = None

    async def send_packet(self, data: bytes) -> bool:
        if not self.running:
            return False
        async with self.write_lock:
            try:
                self.writer.write(data)
                await self.writer.drain()
                return True
            except Exception as exc:
                logger.warning("Socket write failed: %s", exc)
                self.running = False
                return False

    def _send_nav_update(self, url: str) -> None:
        if not self.running or not url:
            return
        url_bytes = url.encode("utf-8", errors="replace")[:511].ljust(512, b"\x00")
        pkt = STRUCT_MSG_NAV_UPDATE.pack(MSG_NAV_UPDATE, url_bytes)
        t = asyncio.create_task(self.send_packet(pkt))
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

    def _setup_page_listeners(self, page: Page) -> None:
        page.on("download", self._on_download_event)
        page.on("framenavigated", self._on_main_frame_navigated)
        page.on("load", self._on_page_loaded)
        page.on("domcontentloaded", self._on_dom_content_loaded)

    def _on_page_loaded(self, *args) -> None:
        if not self.running:
            return
        self._send_progress(100, "Fertig")
        self._on_page_updated()
        t = asyncio.create_task(self.save_storage_state())
        self.background_tasks.add(t)
        t.add_done_callback(self.background_tasks.discard)

    def _on_dom_content_loaded(self, *args) -> None:
        if not self.running:
            return
        self._send_progress(75, "Erstelle Seite...")
        self._on_page_updated()

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

        # Push new history entry
        self.nav_history = self.nav_history[: self.nav_idx + 1]
        self.nav_times = self.nav_times[: self.nav_idx + 1]
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
                    locale="de-DE",
                    user_agent=(
                        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                        "(KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"
                    ),
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

            # Pre-load Twitch auth-token if configured (bypasses Kasada login bot block entirely)
            try:
                for t_path in ["twitch_token.txt", "twitch_cookie.txt", os.path.expanduser("~/.config/vortexbrowse/twitch_token.txt")]:
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

            # Anti-Bot & Anti-Detection Stealth Script (removes automation flags without breaking window.open)
            try:
                await self.context.add_init_script(
                    """() => {
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
                            Object.defineProperty(navigator, 'languages', { get: () => ['de-DE', 'de', 'en-US', 'en'] });
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

                            const ytStyle = document.createElement('style');
                            ytStyle.textContent = `
                                #cinematics, #background.ytd-cinematics, ytd-cinematics,
                                .ytp-ambient-lighting, #movie_player .html5-ambient-video,
                                ytd-video-preview, #preview, .ytd-video-preview {
                                    display: none !important;
                                    visibility: hidden !important;
                                    opacity: 0 !important;
                                }
                            `;
                            if (document.head) {
                                document.head.appendChild(ytStyle);
                            } else {
                                document.addEventListener('DOMContentLoaded', () => {
                                    if (document.head) document.head.appendChild(ytStyle);
                                });
                            }

                            try {
                                window.localStorage.setItem('yt-player-quality', JSON.stringify({
                                    data: '{"quality":720,"previousQuality":720}',
                                    creation: Date.now()
                                }));
                            } catch(e) {}

                            setInterval(() => {
                                const player = document.getElementById('movie_player');
                                if (player && typeof player.setPlaybackQualityRange === 'function') {
                                    try {
                                        player.setPlaybackQualityRange('medium', 'hd720');
                                    } catch(e) {}
                                }
                            }, 2000);
                        } catch(e) {}

                        // Twitch Performance Turbo (Pre-set 720p30 / freeze animated emotes / hide bloat)
                        try {
                            try {
                                window.localStorage.setItem('video-quality', JSON.stringify({ default: '720p30' }));
                                window.localStorage.setItem('twilight.player.quality', '"720p30"');
                                window.localStorage.setItem('lowLatencyMode', 'false');
                            } catch(e) {}

                            const twStyle = document.createElement('style');
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
                            if (document.head) {
                                document.head.appendChild(twStyle);
                            } else {
                                document.addEventListener('DOMContentLoaded', () => {
                                    if (document.head) document.head.appendChild(twStyle);
                                });
                            }
                        } catch(e) {}
                    }"""
                )
            except Exception:
                pass

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
                try:
                    self.cdp = await self.context.new_cdp_session(self.page)
                except Exception as cdp_exc:
                    logger.debug("CDP session fallback: %s", cdp_exc)
                    self.cdp = None
            else:
                self.cdp = None

            self._setup_page_listeners(self.page)
            await self._apply_dark_mode(self.page)

            # Pre-allocate pixel conversion buffers (reused across frames to avoid alloc churn)
            num_pixels = self.width * self.height
            if self._argb_buf is None or len(self._argb_buf) != num_pixels * 4:
                self._argb_buf = ctypes.create_string_buffer(num_pixels * 4)
            if self._rgb565_buf is None or len(self._rgb565_buf) != num_pixels * 2:
                self._rgb565_buf = ctypes.create_string_buffer(num_pixels * 2)
            if self._yuv_buf is None or len(self._yuv_buf) != (num_pixels * 3) // 2:
                self._yuv_buf = ctypes.create_string_buffer((num_pixels * 3) // 2)

            # Start CDP screencast (push-based frame streaming, no request/response overhead)
            if self.engine == "chromium":
                await self._start_screencast()

            tasks = [
                asyncio.create_task(self._frame_stream_loop()),
                asyncio.create_task(self._input_read_loop()),
            ]
            if self.audio_enabled:
                tasks.append(asyncio.create_task(self._audio_stream_loop()))

            initial_url = self.start_url if self.start_url else self.portal_url
            logger.info("Navigating initial viewport (%dx%d) to %s", self.width, self.height, initial_url)
            async def _init_nav():
                try:
                    await self.page.goto(initial_url, wait_until="domcontentloaded", timeout=12000)
                except Exception as nav_exc:
                    logger.warning("Initial navigation warning (%s), falling back to portal", nav_exc)
                    if initial_url != self.portal_url:
                        try:
                            await self.page.goto(self.portal_url, wait_until="domcontentloaded", timeout=8000)
                        except Exception:
                            pass
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
            self.running = False
            await self._stop_screencast()
            if self.context:
                try:
                    self.context.remove_listener("page", self._on_popup_page)
                except Exception:
                    pass
            for tab in list(self.tabs):
                try:
                    tab.remove_listener("download", self._on_download_event)
                    tab.remove_listener("framenavigated", self._on_main_frame_navigated)
                    tab.remove_listener("load", self._on_page_loaded)
                    tab.remove_listener("domcontentloaded", self._on_dom_content_loaded)
                except Exception:
                    pass
            for fut in self.pending_downloads.values():
                if not fut.done():
                    fut.cancel()
            for t in list(self.background_tasks):
                t.cancel()
            if self.is_persistent_context:
                try:
                    await self.save_storage_state()
                except Exception:
                    pass
                for tab in self.tabs[1:]:
                    try:
                        await tab.close()
                    except Exception:
                        pass
            elif self.context:
                try:
                    await self.context.close()
                except Exception:
                    pass
            try:
                self.writer.close()
                await self.writer.wait_closed()
            except Exception:
                pass
            logger.info("AmigaOS 4 client session closed (%s)", peer)

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

    async def _start_screencast(self) -> bool:
        """Start CDP Page.screencast — push-based frame delivery.

        This avoids the request/response round-trip of Page.captureScreenshot per frame,
        which is the single biggest CPU cost on the streaming server.
        """
        if not self.cdp or not self.page or self.screencast_active:
            return False
        try:
            fmt_map = {FMT_ARGB32: "RGB", FMT_RGB565: "RGB", FMT_YUV420P: "RGB"}
            # Larger queue: Chromium pushes frames faster than we can process them.
            # A small queue causes QueueFull drops → stuttering video. 16 frames of
            # headroom lets the loop keep up without dropping frames.
            self.screencast_queue = asyncio.Queue(maxsize=16)
            self.screencast_active = True

            def _screencast_handler(params: dict) -> None:
                self.last_screencast_frame_time = time.monotonic()
                session_id = params.get("sessionId")
                if session_id is not None and self.cdp is not None:
                    try:
                        asyncio.create_task(self.cdp.send("Page.screencastFrameAck", {"sessionId": session_id}))
                    except Exception:
                        pass
                if self.screencast_queue is not None:
                    if self.screencast_queue.full():
                        try:
                            self.screencast_queue.get_nowait()
                        except asyncio.QueueEmpty:
                            pass
                    try:
                        self.screencast_queue.put_nowait(params)
                    except asyncio.QueueFull:
                        pass
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
            logger.info("CDP Page.screencast started (push-based frame streaming, JPEG q%d)", self.jpeg_quality)
            return True
        except Exception as exc:
            logger.debug("Could not start CDP screencast: %s", exc)
            self.screencast_active = False
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

    async def save_storage_state(self) -> None:
        if self.context:
            try:
                state_file = os.path.expanduser(f"~/.config/vortexbrowse/{self.engine}_storage_state.json")
                os.makedirs(os.path.dirname(state_file), exist_ok=True)
                await self.context.storage_state(path=state_file)
                logger.info("Saved persistent website cookies and storage state to %s", state_file)
            except Exception as e:
                logger.debug("save_storage_state warning: %s", e)

    # ------------------------------------------------------------------------
    # Real-Time Audio Capture & Streaming Loop (PipeWire -> Amiga AHI)
    # ------------------------------------------------------------------------
    async def _audio_stream_loop(self) -> None:
        proc: Optional[asyncio.subprocess.Process] = None
        try:
            unmute_all_browser_sink_inputs()
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
            while self.running and proc.stdout:
                chunk = await proc.stdout.readexactly(AUDIO_CHUNK_BYTES)
                if not self.running:
                    break

                loop_iter += 1
                if loop_iter % 25 == 1:
                    unmute_all_browser_sink_inputs()

                if _FAST_PIXCONV is not None:
                    if _FAST_PIXCONV.pcm_s16be_is_silent(chunk, len(chunk), 10) == 1:
                        continue
                else:
                    if chunk == b"\x00" * len(chunk):
                        continue

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
        if self.cdp is not None:
            try:
                res = await asyncio.wait_for(
                    self.cdp.send(
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
                if self.screencast_active:
                    try:
                        await self.cdp.send("Page.stopScreencast")
                        res = await asyncio.wait_for(
                            self.cdp.send(
                                "Page.captureScreenshot",
                                {
                                    "format": "jpeg",
                                    "quality": self.jpeg_quality,
                                    "optimizeForSpeed": True,
                                },
                            ),
                            timeout=2.0,
                        )
                        await self.cdp.send(
                            "Page.startScreencast",
                            {
                                "format": "jpeg",
                                "quality": self.jpeg_quality,
                                "everyNthFrame": 1,
                                "maxWidth": self.width,
                                "maxHeight": self.height,
                            },
                        )
                        b64_data = res.get("data")
                        if b64_data:
                            return base64.b64decode(b64_data)
                    except Exception:
                        pass
                elif not self.screencast_active:
                    try:
                        if self.context and self.page and self.engine == "chromium":
                            self.cdp = await asyncio.wait_for(
                                self.context.new_cdp_session(self.page),
                                timeout=1.5,
                            )
                    except Exception:
                        self.cdp = None
        if self.page is not None:
            try:
                return await asyncio.wait_for(
                    self.page.screenshot(
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
        return None

    async def _capture_and_send_frame(self, force: bool = False) -> None:
        if not self.running or not self.page:
            return

        # Prefer CDP screencast (push-based) when available — no request/response overhead
        if self.screencast_active and self.screencast_queue is not None:
            params = None
            while not self.screencast_queue.empty():
                try:
                    params = self.screencast_queue.get_nowait()
                except asyncio.QueueEmpty:
                    break

            if params is not None:
                try:
                    b64_data = params.get("data")
                    if b64_data:
                        jpeg_bytes = base64.b64decode(b64_data)
                        await self._encode_and_send_png(jpeg_bytes, self.force_full_frame)
                        return
                except Exception as exc:
                    logger.debug("Screencast frame decode error: %s", exc)

            # If screencast is active and queue was empty, capture fallback screenshot if:
            # - No initial frame has EVER been sent yet (self.last_frame_hash is None), OR
            # - An explicit full refresh was requested (self.force_full_frame), OR
            # - Video/audio/active interaction is ongoing and CDP screencast hasn't pushed a frame for > 65ms.
            now = time.monotonic()
            is_dynamic = (
                self.is_video_mode
                or self._video_element_playing
                or (getattr(self, "last_audio_time", 0.0) > now - 2.0)
                or (now < self.active_until)
            )
            screencast_stalled = is_dynamic and ((now - self.last_screencast_frame_time) >= 0.065)
            needs_frame = self.force_full_frame or (self.last_frame_hash is None) or screencast_stalled
            if not needs_frame:
                return

        # Fallback to direct fast CDP screenshot (when screencast is not active or frame is forced)
        try:
            jpeg_bytes = await self._capture_jpeg_fast()
            if not jpeg_bytes:
                return
        except Exception as exc:
            logger.debug("Screenshot skipped during navigation: %s", exc)
            return

        await self._encode_and_send_png(jpeg_bytes, self.force_full_frame)

    async def _encode_and_send_png(self, png_bytes: bytes, force: bool = False) -> None:
        """Decode PNG/JPEG, run dirty-region detection, LZ4 compress, and send to Amiga."""
        t0 = time.monotonic()
        force_full = self.force_full_frame or force or (self.last_frame_hash is None)
        digest = hashlib.md5(png_bytes).digest()
        if not force_full and digest == self.last_frame_hash:
            return
        self.last_frame_hash = digest
        self.force_full_frame = False

        w = self.width
        h = self.height
        fmt = self.pixel_format

        # Use Motion-JPEG only if explicitly requested via @CODEC:jpeg / @CODEC:mjpeg.
        # Otherwise default to LZ4 dirty-slice streaming which is 100x faster for the Amiga CPU!
        use_mjpeg = (self.codec_mode == "jpeg")
        if use_mjpeg:
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

        if fmt == FMT_YUV420P:
            bpp = 1
            frame_bytes_len = (w * h * 3) // 2
        elif fmt == FMT_RGB565:
            bpp = 2
            frame_bytes_len = w * h * 2
        else:
            bpp = 4
            frame_bytes_len = w * h * 4

        max_pkt_len = frame_bytes_len + (frame_bytes_len // 255) + 4096
        if self.prev_raw_buf is None or len(self.prev_raw_buf) != frame_bytes_len or self.prev_dims != (w, h):
            self.prev_raw_buf = ctypes.create_string_buffer(frame_bytes_len)
            self.prev_dims = (w, h)
            num_px = w * h
            self._argb_buf = ctypes.create_string_buffer(num_px * 4)
            self._rgb565_buf = ctypes.create_string_buffer(num_px * 2)
            self._yuv_buf = ctypes.create_string_buffer((num_px * 3) // 2)
            self._pkt_buf = ctypes.create_string_buffer(max_pkt_len)
            force_full = True
        elif self._pkt_buf is None or len(self._pkt_buf) < max_pkt_len:
            self._pkt_buf = ctypes.create_string_buffer(max_pkt_len)

        t_decode = time.monotonic()
        loop = asyncio.get_running_loop()
        header_and_payload = await loop.run_in_executor(
            None,
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
        )
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
    ) -> Optional[bytes]:
        try:
            # 1. Ultra-Fast C-Native Pipeline (libturbojpeg SIMD + AVX2 dirty-rect + liblz4 in a single C call)
            if _FAST_PIXCONV is not None and _HAS_NATIVE_PACKET_ENCODER and pkt_buf is not None:
                work_buf = yuv_buf if pixel_format == FMT_YUV420P else (rgb565_buf if pixel_format == FMT_RGB565 else argb_buf)
                if work_buf is not None:
                    lz4_accel = 3 if (width * height >= 1500000) else 2
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

            now_tick = time.monotonic()
            if now_tick - self._sec_start_time >= 1.0:
                self._frames_last_sec = frames_in_sec
                frames_in_sec = 0
                self._sec_start_time = now_tick

            # Dynamic Video Detection:
            # 1. Real audio streaming active (e.g. YouTube, Twitch)
            audio_active = (getattr(self, "last_audio_time", 0.0) > now_tick - 2.0)

            # 2. Query HTML5 <video> element playback state asynchronously every 0.8 seconds
            if self.page and (now_tick - self._last_video_eval_time) >= 0.8:
                self._last_video_eval_time = now_tick
                async def _eval_video():
                    try:
                        if self.page and self.running:
                            is_playing = await asyncio.wait_for(
                                self.page.evaluate(
                                    """() => {
                                        if (Array.from(document.querySelectorAll('video')).some(v => !v.paused && !v.ended && v.readyState >= 2)) {
                                            return true;
                                        }
                                        for (const f of document.querySelectorAll('iframe')) {
                                            try {
                                                if (f.contentDocument && Array.from(f.contentDocument.querySelectorAll('video')).some(v => !v.paused && !v.ended && v.readyState >= 2)) {
                                                    return true;
                                                }
                                            } catch (e) {}
                                        }
                                        return false;
                                    }"""
                                ),
                                timeout=0.08,
                            )
                            self._video_element_playing = bool(is_playing)
                    except Exception:
                        pass
                t_vid = asyncio.create_task(_eval_video())
                self.background_tasks.add(t_vid)
                t_vid.add_done_callback(self.background_tasks.discard)

            if audio_active or self._video_element_playing:
                self._last_video_state_time = now_tick
                if not self.is_video_mode:
                    self.is_video_mode = True
                    await self._set_screencast_quality(self.video_jpeg_quality)
            elif self.is_video_mode:
                # Video ended/paused; wait 3.0s of static/quiet before restoring crisp high quality (e.g. q80)
                if (now_tick - self._last_video_state_time) >= 3.0:
                    self.is_video_mode = False
                    await self._set_screencast_quality(self.base_jpeg_quality)

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
                    getattr(self, "last_audio_time", 0.0) > now_loop - 1.5
                    or self.is_video_mode
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
                self._trigger_full_refresh(True)
                self._schedule_delayed_frames()

            elif event_type == INPUT_MOUSE_DOWN:
                btn = "left"
                if key_code == 2:
                    btn = "middle"
                elif key_code == 3:
                    btn = "right"
                logger.info("INPUT_MOUSE_DOWN (%s) at (%d, %d)", btn, cx, cy)

                if btn == "right":
                    await self._send_native_context_menu(cx, cy)
                    return

                if btn == "left" and cx >= (self.width - 20):
                    ratio = float(cy) / float(max(1, self.height))
                    await self.page.evaluate(
                        f"window.scrollTo(0, Math.max(0, (document.documentElement.scrollHeight - window.innerHeight) * {ratio:.4f}))"
                    )
                else:
                    await self.page.mouse.move(cx, cy)
                    await self.page.mouse.down(button=btn)
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
                    await self.page.mouse.move(cx, cy)
                    await self.page.mouse.up(button=btn)
                    if btn == "left":
                        try:
                            sel = await self.page.evaluate("window.getSelection() ? window.getSelection().toString() : ''")
                            if sel and len(sel.strip()) > 0:
                                logger.info("Selected text (%d chars): %r", len(sel), sel[:60])
                                await self.send_clipboard_to_amiga(sel)
                        except Exception:
                            pass
                self._trigger_full_refresh(True)
                self._schedule_delayed_frames()

            elif event_type == INPUT_MOUSE_MOVE:
                try:
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
                            self.screencast_queue.get_nowait()
                        except asyncio.QueueEmpty:
                            break
                self._trigger_full_refresh(True)

            elif event_type == INPUT_KEY_DOWN:
                await self._handle_key_down(key_code, text_or_url)
                self.force_frame_event.set()
                self.active_until = time.monotonic() + 1.5

            elif event_type == INPUT_KEY_UP:
                pass

            elif event_type == INPUT_GOTO_URL:
                await self._handle_navigation(text_or_url)
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

        if self.cdp is not None and self.engine == "chromium":
            try:
                await self.cdp.send("Emulation.setAutoDarkModeOverride", {"enabled": is_dark})
            except Exception as exc:
                logger.debug("Emulation.setAutoDarkModeOverride error: %s", exc)

        if DARKREADER_JS:
            try:
                if is_dark:
                    js = (
                        "(() => {"
                        "try {"
                        "if (typeof DarkReader === 'undefined') {"
                        + DARKREADER_JS +
                        "\n}"
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

            dark_css = ""
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

        if cmd.upper().startswith("@CODEC:"):
            c = cmd.split(":", 1)[1].strip().lower()
            if c in ("mjpeg", "jpeg"):
                self.codec_mode = "jpeg"
                logger.info("Switched streaming codec to Motion-JPEG (CODEC_JPEG)")
            elif c == "lz4":
                self.codec_mode = "lz4"
                logger.info("Switched streaming codec to LZ4 (CODEC_LZ4)")
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
                    buf_len = (self.width * self.height * 3) // 2
                elif new_fmt == FMT_RGB565:
                    fmt_name = "RGB565 (16-bit)"
                    buf_len = self.width * self.height * 2
                else:
                    fmt_name = "ARGB32 (32-bit)"
                    buf_len = self.width * self.height * 4
                logger.info("Client requested pixel format switch to %s", fmt_name)
                self.prev_raw_buf = ctypes.create_string_buffer(buf_len)
                self._trigger_full_refresh(True)
            return

        if cmd.upper().startswith("@JPEG_QUALITY:"):
            try:
                q = int(cmd.split(":", 1)[1].strip())
                if self.width * self.height >= 1500000 and q > 85:
                    logger.info("1080p Full HD mode active: clamping requested JPEG quality %d -> 85 for optimal 25 FPS bandwidth", q)
                    q = 85
                self.base_jpeg_quality = max(20, min(100, q))
                self.video_jpeg_quality = min(self.base_jpeg_quality, 62)
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
                    await self._stop_screencast()
                    new_page = await self.context.new_page()
                    self.tabs.append(new_page)
                    self.active_tab_idx = len(self.tabs) - 1
                    self.page = new_page
                    self._setup_page_listeners(new_page)
                    if self.engine == "chromium":
                        try:
                            self.cdp = await self.context.new_cdp_session(new_page)
                        except Exception:
                            self.cdp = None
                    else:
                        self.cdp = None
                    await self.page.set_viewport_size({"width": self.width, "height": self.height})
                    if self.engine == "chromium":
                        await self._start_screencast()
                    await self._apply_dark_mode(new_page)
                    self._send_progress(20, f"Lade {target}...")
                    await self.page.goto(target, wait_until="commit", timeout=15000)
                    self._send_nav_update(self.page.url or target)
                    self._trigger_full_refresh(True)
                    self._schedule_delayed_frames()
                except Exception as exc:
                    logger.warning("New tab creation error: %s", exc)
                finally:
                    self._creating_tab = False
            return

        if cmd.upper().startswith("@SWITCHTAB:"):
            try:
                tidx = int(cmd.split(":", 1)[1].strip())
                if 0 <= tidx < len(self.tabs):
                    await self._stop_screencast()
                    self.active_tab_idx = tidx
                    self.page = self.tabs[tidx]
                    if self.context and self.engine == "chromium":
                        try:
                            self.cdp = await self.context.new_cdp_session(self.page)
                        except Exception:
                            self.cdp = None
                    else:
                        self.cdp = None
                    try:
                        await self.page.set_viewport_size({"width": self.width, "height": self.height})
                    except Exception:
                        pass
                    if self.engine == "chromium":
                        await self._start_screencast()
                    await self._apply_dark_mode(self.page)
                    self._send_nav_update(self.page.url or "")
                    self._trigger_full_refresh(True)
                    self._schedule_delayed_frames()
            except Exception as exc:
                logger.warning("Switch tab error: %s", exc)
            return

        if cmd.upper().startswith("@CLOSETAB:"):
            try:
                tidx = int(cmd.split(":", 1)[1].strip())
                if 0 <= tidx < len(self.tabs) and len(self.tabs) > 1:
                    await self._stop_screencast()
                    p_to_close = self.tabs.pop(tidx)
                    try:
                        await p_to_close.close()
                    except Exception:
                        pass
                    if self.active_tab_idx >= len(self.tabs):
                        self.active_tab_idx = len(self.tabs) - 1
                    self.page = self.tabs[self.active_tab_idx]
                    if self.context and self.engine == "chromium":
                        try:
                            self.cdp = await self.context.new_cdp_session(self.page)
                        except Exception:
                            self.cdp = None
                    else:
                        self.cdp = None
                    try:
                        await self.page.set_viewport_size({"width": self.width, "height": self.height})
                    except Exception:
                        pass
                    if self.engine == "chromium":
                        await self._start_screencast()
                    await self._apply_dark_mode(self.page)
                    self._send_nav_update(self.page.url or "")
                    self._trigger_full_refresh(True)
                    self._schedule_delayed_frames()
            except Exception as exc:
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
                    with open("twitch_token.txt", "w", encoding="utf-8") as f:
                        f.write(token)
                    logger.info("Saved Twitch auth-token to twitch_token.txt and active cookie jar")
                    t = asyncio.create_task(self.page.goto("https://www.twitch.tv", wait_until="commit", timeout=15000))
                    self.background_tasks.add(t)
                    t.add_done_callback(self.background_tasks.discard)
                except Exception as exc:
                    logger.warning("Error applying twitch token: %s", exc)
            return

        if cmd.upper().startswith("@AUTOLOGIN:"):
            payload = cmd.split(":", 1)[1]
            user = ""
            pwd = ""
            if "\t" in payload:
                user, pwd = payload.split("\t", 1)
            elif " " in payload:
                user, pwd = payload.split(" ", 1)
            else:
                user = payload
            await self._perform_auto_login(user, pwd)
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
                self.active_until = time.monotonic() + 3.0
                self._trigger_full_refresh(True)
                self._schedule_delayed_frames()

        t = asyncio.create_task(_nav_bg())
        self.background_tasks.add(t)
        t.add_done_callback(self.background_tasks.discard)

    async def _perform_auto_login(self, username: str, password: str) -> None:
        if not self.page:
            return
        logger.info("Executing Auto-Login for user: %r", username)
        self._send_progress(50, f"Auto-Login fuer '{username}'...")

        js_script = """
        ([user, pass]) => {
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
            // 1. Password input detection
            let pwInputs = Array.from(doc.querySelectorAll('input[type="password"]')).filter(isVisible);
            if (pwInputs.length === 0) {
                pwInputs = Array.from(doc.querySelectorAll('input[name*="pass" i], input[id*="pass" i]')).filter(isVisible);
            }
            if (pwInputs.length === 0) {
                pwInputs = Array.from(doc.querySelectorAll('input[type="password"]'));
            }
            const pwInput = pwInputs[0] || null;

            // 2. Username / Email input detection
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
            if (!userInput && !pwInput) {
                const anyText = Array.from(doc.querySelectorAll('input[type="text"], input[type="email"], input:not([type])')).filter(isVisible);
                if (anyText.length > 0) userInput = anyText[0];
            }

            let userFilled = false;
            let passFilled = false;
            if (userInput && user) {
                userFilled = setVal(userInput, user);
            }
            if (pwInput && pass) {
                passFilled = setVal(pwInput, pass);
            }

            // 3. Submit button detection
            let submitBtn = null;
            const form = (pwInput && pwInput.closest('form')) || (userInput && userInput.closest('form'));
            if (form) {
                submitBtn = form.querySelector('button[type="submit"], input[type="submit"]');
            }
            if (!submitBtn) {
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
            if (submitBtn) {
                try {
                    submitBtn.click();
                    submitted = true;
                } catch (e) {}
            } else if (form && (userFilled || passFilled)) {
                try {
                    form.submit();
                    submitted = true;
                } catch (e) {}
            }

            return {
                userFilled,
                passFilled,
                submitted,
                foundUser: !!userInput,
                foundPass: !!pwInput,
                foundSubmit: !!submitBtn
            };
        }
        """
        try:
            frames = [self.page] + list(self.page.frames)
            res = None
            for frame in frames:
                try:
                    res = await frame.evaluate(js_script, [username, password])
                    if res and (res.get("userFilled") or res.get("passFilled")):
                        break
                except Exception:
                    continue

            logger.info("Auto-Login execution result: %s", res)
            if res and not res.get("submitted") and (res.get("userFilled") or res.get("passFilled")):
                await self.page.keyboard.press("Enter")

            if res and (res.get("userFilled") or res.get("passFilled")):
                self._send_progress(100, f"Auto-Login ausgefuehrt ({username})")
            else:
                self._send_progress(100, "Keine Login-Felder gefunden")
        except Exception as exc:
            logger.warning("Auto-Login error: %s", exc)
            self._send_progress(100, "Auto-Login fehlgeschlagen")
        finally:
            self.active_until = time.monotonic() + 3.0
            self._trigger_full_refresh(True)
            self._schedule_delayed_frames()

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


# ============================================================================
# GPU Acceleration Helper
# ============================================================================
def get_gpu_launch_flags(disable_gpu: bool = False) -> Tuple[List[str], bool]:
    """
    Determines optimal Chromium GPU hardware acceleration flags for headless streaming.
    Returns (flags_list, is_gpu_detected).
    """
    if disable_gpu:
        return ["--disable-gpu", "--disable-software-rasterizer"], False

    # Check for DRM render/card nodes
    has_dri = os.path.exists("/dev/dri") and bool(
        glob.glob("/dev/dri/renderD*") or glob.glob("/dev/dri/card*")
    )

    gpu_flags = [
        "--ignore-gpu-blocklist",
        "--enable-gpu-rasterization",
        "--enable-zero-copy",
        "--use-gl=angle",
        "--use-angle=vulkan",
        "--enable-features=VaapiVideoDecoder,VaapiVideoEncoder,VaapiIgnoreDriverChecks,CanvasOopRasterization,DefaultANGLEVulkan,VulkanFromANGLE",
    ]
    return gpu_flags, has_dri


# ============================================================================
# Main Server Entrypoint
# ============================================================================
async def run_server(args: argparse.Namespace) -> None:
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

    engine = getattr(args, "engine", "chromium").lower()

    if engine == "firefox":
        profile_dir = os.path.expanduser("~/.config/vortexbrowse/firefox_profile")
        os.makedirs(profile_dir, exist_ok=True)
    else:
        custom_bin = getattr(args, "browser_bin", None)
        full_chrome = find_full_chromium_executable(custom_bin)
        enable_exts = getattr(args, "enable_extensions", False)
        all_exts = find_all_extension_paths() if enable_exts else []

        profile_dir = os.path.expanduser("~/.config/vortexbrowse/chromium_profile")
        os.makedirs(profile_dir, exist_ok=True)

        gpu_flags, has_dri = get_gpu_launch_flags(disable_gpu=getattr(args, "disable_gpu", False))
        if getattr(args, "disable_gpu", False):
            logger.info("GPU Hardware Acceleration: DISABLED by user flag (--disable-gpu)")
        elif has_dri:
            logger.info("GPU Hardware Acceleration: ENABLED (Vulkan / ANGLE / VA-API / /dev/dri render node detected)")
        else:
            logger.info("GPU Hardware Acceleration: ENABLED (ANGLE Vulkan requested, fallback to SwiftShader if no DRM)")

        launch_args = [
            "--ozone-platform=headless",
            "--no-sandbox",
            "--disable-dev-shm-usage",
            "--font-render-hinting=medium",
            "--enable-font-antialiasing",
            "--enable-smooth-scrolling",
            "--autoplay-policy=no-user-gesture-required",
            "--disable-blink-features=AutomationControlled",
            "--num-raster-threads=4",
            "--enable-low-res-tiling",
            "--disable-background-timer-throttling",
            "--disable-renderer-backgrounding",
            "--disable-features=Translate,OptimizationHints,MediaRouter,DialMediaRouteProvider",
        ] + gpu_flags
        if all_exts:
            ext_list = ",".join(all_exts)
            logger.info("Enabling unpacked Chrome extensions (%d): %s", len(all_exts), ext_list)
            launch_args.append(f"--disable-extensions-except={ext_list}")
            launch_args.append(f"--load-extension={ext_list}")
        else:
            logger.info("Browser extensions (uBlock Origin, h264ify): DISABLED as requested")
            launch_args.append("--disable-extensions")

    async with async_playwright() as pw:
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
                locale="de-DE",
                user_agent=firefox_user_agent,
                firefox_user_prefs={
                    "browser.startup.page": 0,
                    "browser.sessionstore.resume_from_crash": False,
                    "browser.sessionstore.max_tabs_undo": 0,
                    "browser.sessionstore.max_resumed_crashes": 0,
                    "browser.tabs.warnOnClose": False,
                    "media.autoplay.default": 0,
                    "media.autoplay.blocking_policy": 0,
                    "media.ffmpeg.vaapi.enabled": True,
                },
            )
        elif full_chrome:
            browser_name = "Ungoogled-Chromium" if "ungoogled" in full_chrome.lower() else "Chromium"
            logger.info("Launching %s (%s) persistent context at %s...", browser_name, full_chrome, profile_dir)
            browser = await pw.chromium.launch_persistent_context(
                user_data_dir=profile_dir,
                executable_path=full_chrome,
                headless=False,
                ignore_default_args=["--mute-audio", "--enable-automation"],
                env=browser_env,
                args=launch_args,
                viewport={"width": args.width, "height": args.height},
                accept_downloads=True,
                locale="de-DE",
                user_agent=(
                    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                    "(KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"
                ),
            )
        else:
            logger.info("Launching Headless Chromium fallback persistent context at %s...", profile_dir)
            fallback_args = [
                "--no-sandbox",
                "--disable-dev-shm-usage",
                "--font-render-hinting=medium",
                "--enable-font-antialiasing",
                "--enable-smooth-scrolling",
                "--autoplay-policy=no-user-gesture-required",
                "--disable-blink-features=AutomationControlled",
                "--num-raster-threads=4",
                "--enable-low-res-tiling",
                "--disable-background-timer-throttling",
                "--disable-renderer-backgrounding",
                "--disable-features=Translate,OptimizationHints,MediaRouter,DialMediaRouteProvider",
            ] + gpu_flags
            if all_exts:
                ext_list = ",".join(all_exts)
                fallback_args.append(f"--disable-extensions-except={ext_list}")
                fallback_args.append(f"--load-extension={ext_list}")
            else:
                fallback_args.append("--disable-extensions")
            browser = await pw.chromium.launch_persistent_context(
                user_data_dir=profile_dir,
                headless=True,
                ignore_default_args=["--mute-audio", "--enable-automation"],
                env=browser_env,
                args=fallback_args,
                viewport={"width": args.width, "height": args.height},
                accept_downloads=True,
                locale="de-DE",
                user_agent=(
                    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                    "(KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"
                ),
            )

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
                    if active_session is not None and active_session.running:
                        logger.info("New client connected: terminating previous active session")
                        active_session.running = False
                        try:
                            active_session.writer.close()
                            await asyncio.wait_for(active_session.writer.wait_closed(), timeout=0.5)
                        except Exception:
                            pass
                        for _ in range(15):
                            if not active_session or not active_session.running:
                                break
                            await asyncio.sleep(0.05)

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
                await server.serve_forever()
        finally:
            cpu_gov.restore()
            for s in extra_servers:
                s.close()
                await s.wait_closed()
            try:
                state_file = os.path.expanduser(f"~/.config/vortexbrowse/{engine}_storage_state.json")
                await browser.storage_state(path=state_file)
            except Exception:
                pass
            await browser.close()
            await portal.stop()


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
    parser.add_argument("--codec", choices=["auto", "jpeg", "lz4"], default="auto", help="Streaming codec (default: auto)")
    parser.add_argument("--jpeg-quality", type=int, default=82, help="JPEG capture quality 20-100 (default: 82 for Full HD 1080p@25fps)")
    parser.add_argument("--enable-extensions", dest="enable_extensions", action="store_true", default=True, help="Enable browser extensions (uBlock, h264ify) [default: enabled]")
    parser.add_argument("--disable-extensions", dest="enable_extensions", action="store_false", help="Disable all browser extensions")
    parser.add_argument("--disable-gpu", action="store_true", help="Disable GPU hardware acceleration (forces CPU software rasterization)")
    parser.add_argument("--no-cpu-governor", action="store_true", help="Disable automatic switching of CPU governor to performance during client sessions")
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
