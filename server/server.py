#!/usr/bin/env python3
"""
VortexRemoteBrowser - CachyOS HP EliteDesk 805 G6 (AMD Ryzen PRO / Radeon Vega) Edition
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
try:
    import fcntl
except ImportError:
    fcntl = None  # type: ignore
import gc
import getpass
import glob
import hashlib
import html
import inspect
import io
import logging
import os
import platform
import queue
import re
import select
import shutil
import signal
import socket
import subprocess
import struct
import sys
import tempfile
import threading
import time
import urllib.request

_IS_WINDOWS: bool = (platform.system() == "Windows")
_SUBPROC_NO_WINDOW: int = getattr(subprocess, "CREATE_NO_WINDOW", 0) if _IS_WINDOWS else 0
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple, Union
from urllib.parse import parse_qs, quote_plus, unquote, urlencode, urljoin, urlparse

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
MSG_SERVER_INFO       = 0x10
MSG_TAB_SYNC          = 0x11

TAB_SYNC_ADD          = 1
TAB_SYNC_CLOSE        = 2
TAB_SYNC_SWITCH       = 3
VB_MAX_TABS           = 16

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
STRUCT_MSG_SERVER_INFO   = struct.Struct(">B64s")
STRUCT_MSG_TAB_SYNC      = struct.Struct(">BBBHHH512s128s")

DOWNLOAD_CHUNK_SIZE = 64 * 1024
AUDIO_SAMPLE_RATE   = 44100
AUDIO_CHANNELS      = 2
AUDIO_CHUNK_BYTES   = 8192  # ~46ms of 44100Hz 16-bit stereo PCM (CD quality)
AUDIO_SINK_NAME     = "vortexremotebrowser_sink"

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
        self.enabled = enabled and (platform.system() == "Linux")
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
    if _IS_WINDOWS or not hasattr(os, "getuid"):
        return
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


def _find_windows_dshow_audio_device(explicit_device: str = "") -> Optional[str]:
    """Finds a DirectShow loopback/virtual audio device on Windows (VB-Cable, Stereo Mix, etc.)."""
    explicit = (explicit_device or os.environ.get("VORTEX_AUDIO_DEVICE", "")).strip()
    if explicit and explicit.lower() != "wasapi":
        return explicit
    try:
        res = subprocess.run(
            ["ffmpeg", "-list_devices", "true", "-f", "dshow", "-i", "dummy"],
            capture_output=True,
            text=True,
            timeout=5,
            creationflags=_SUBPROC_NO_WINDOW,
        )
        output = (res.stderr or "") + "\n" + (res.stdout or "")
        devices: List[str] = []
        in_audio = False
        for line in output.splitlines():
            if "DirectShow audio devices" in line:
                in_audio = True
                continue
            if "DirectShow video devices" in line:
                in_audio = False
                continue
            m = re.search(r'"([^"]+)"', line)
            if m and (in_audio or "(audio)" in line.lower()):
                name = m.group(1).strip()
                if name and not name.startswith("@device_"):
                    devices.append(name)
        preferred = (
            "cable output (vb-audio virtual cable)",
            "vb-audio virtual cable",
            "vb-audio",
            "stereo mix",
            "stereomix",
            "what u hear",
            "wave out mix",
        )
        for pat in preferred:
            for dev in devices:
                if pat in dev.lower():
                    return dev
    except Exception:
        pass
    return None


def _has_windows_wasapi_loopback() -> bool:
    """Checks whether PyAudioWPatch native WASAPI loopback capture is available on Windows."""
    if not _IS_WINDOWS:
        return False
    try:
        import pyaudiowpatch as pyaudio  # type: ignore
        pa = pyaudio.PyAudio()
        try:
            dev = pa.get_default_wasapi_loopback()
            return bool(dev)
        finally:
            pa.terminate()
    except Exception:
        return False


def _select_windows_audio_backend(explicit_device: str = "") -> Tuple[str, Optional[str]]:
    """Returns ('dshow', device_name), ('wasapi', None), or ('none', None)."""
    explicit = (explicit_device or os.environ.get("VORTEX_AUDIO_DEVICE", "")).strip()
    if explicit.lower() == "wasapi":
        return ("wasapi", None) if _has_windows_wasapi_loopback() else ("none", None)
    if explicit:
        return ("dshow", explicit)
    dshow_dev = _find_windows_dshow_audio_device()
    if dshow_dev and "vb-audio" in dshow_dev.lower():
        return ("dshow", dshow_dev)
    if _has_windows_wasapi_loopback():
        return ("wasapi", None)
    if dshow_dev:
        return ("dshow", dshow_dev)
    return ("none", None)


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


_VORTEX_AUDIO_EXT_ID = "iodkfnmlbdjplhjddnpdpffogmgpceme"
_VORTEX_AUDIO_EXT_PUBKEY_B64 = (
    "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAp/aoCc4i+knAUyBbQ1wuNQ2BBw8Io1eiAHg+ssZv23/9JV8JVfqIO3Wb225ffx9Apy8+"
    "blICGrCTqMnL/gmDiXIQh6f7dTfE4FM6txx2petVjPNvjUuOK7d3VUFiTrDkiHaaSLUOWCVgIBO1ys0Jf+Zyhn7nqeUeo9+f3poSPWKqT2DyblW/"
    "f4hjOQAmM63yKHElL7pNKYSxSrMwOWR9VrBVVcb+uqZx5XRC18Krh444ydWqJfPlO3aD+DT4ERTaNdsiULVc7B/SGc8oo8rT88GBgb66fabsM0yr"
    "yJ6+8/xvoTD7dkAKKNNtifCD7FYERvmwF9XlNqIddyQHtkwhLQIDAQAB"
)


def _should_use_tabcapture_audio(engine: str, audio_device: str = "", no_audio: bool = False) -> bool:
    """Determines whether silent browser-internal MV3 TabCapture audio should be used."""
    if no_audio or engine.lower() != "chromium" or not _HAS_WEBSOCKETS or websockets is None:
        return False
    explicit = (audio_device or os.environ.get("VORTEX_AUDIO_DEVICE", "")).strip().lower()
    if explicit in ("tabcapture", "silent", "browser"):
        return True
    if explicit:
        # User explicitly requested 'wasapi' or a specific DirectShow/system audio device
        return False
    if _IS_WINDOWS:
        return True
    if platform.system() == "Darwin":
        # Zero-driver silent fallback on macOS when no BlackHole virtual cable is installed
        return _find_macos_audio_device() is None
    return False


class SilentTabAudioBridge:
    """
    Browser-internal MV3 chrome.tabCapture + Offscreen Document audio bridge.
    Captures 44100Hz 16-bit Big-Endian Stereo PCM directly from Chromium tabs over
    localhost WebSocket while automatically muting captured tabs on the host machine's
    speakers (zero virtual audio driver required, zero double-audio on Windows/macOS).
    """

    def __init__(self) -> None:
        self.ext_dir: Optional[str] = None
        self.ws_port: int = 0
        self._ws_server: Any = None
        self._consumer_queue: Optional[asyncio.Queue] = None
        self.connected_event: asyncio.Event = asyncio.Event()
        self.has_connected: bool = False
        self._active_ws_count: int = 0

    @property
    def allowlist_flag(self) -> str:
        return f"--allowlisted-extension-id={_VORTEX_AUDIO_EXT_ID}"

    def attach_consumer(self, maxsize: int = 24) -> asyncio.Queue:
        q: asyncio.Queue = asyncio.Queue(maxsize=maxsize)
        self._consumer_queue = q
        return q

    def detach_consumer(self, q: Optional[asyncio.Queue]) -> None:
        if q is None or self._consumer_queue is q:
            self._consumer_queue = None

    async def _ws_handler(self, websocket: Any, *_args: Any) -> None:
        self._active_ws_count += 1
        self.has_connected = True
        self.connected_event.set()
        logger.info("SilentTabAudioBridge: MV3 TabCapture extension connected (ws://127.0.0.1:%d)", self.ws_port)
        buf = bytearray()
        try:
            async for msg in websocket:
                if isinstance(msg, (bytes, bytearray)):
                    q = self._consumer_queue
                    if q is None:
                        buf.clear()
                        continue
                    if len(msg) == AUDIO_CHUNK_BYTES and not buf:
                        pkt = bytes(msg)
                        if q.full():
                            try:
                                q.get_nowait()
                            except asyncio.QueueEmpty:
                                pass
                        try:
                            q.put_nowait(pkt)
                        except asyncio.QueueFull:
                            pass
                    else:
                        buf.extend(msg)
                        while len(buf) >= AUDIO_CHUNK_BYTES:
                            pkt = bytes(buf[:AUDIO_CHUNK_BYTES])
                            del buf[:AUDIO_CHUNK_BYTES]
                            if q.full():
                                try:
                                    q.get_nowait()
                                except asyncio.QueueEmpty:
                                    pass
                            try:
                                q.put_nowait(pkt)
                            except asyncio.QueueFull:
                                pass
                elif isinstance(msg, str):
                    if msg == "vortex-audio-ready":
                        self.has_connected = True
                        self.connected_event.set()
        except Exception as exc:
            logger.debug("SilentTabAudioBridge WebSocket client disconnected: %s", exc)
        finally:
            self._active_ws_count = max(0, self._active_ws_count - 1)

    def _write_extension_files(self, ext_dir: str, ws_port: int) -> None:
        manifest = {
            "manifest_version": 3,
            "name": "VortexRemoteBrowser Silent Audio Bridge",
            "version": "1.1.0",
            "key": _VORTEX_AUDIO_EXT_PUBKEY_B64,
            "permissions": ["tabCapture", "offscreen", "tabs"],
            "background": {"service_worker": "sw.js"},
        }
        sw_js = """
const capturedTabs = new Set();
const capturingTabs = new Set();
let creatingOffscreen = null;

function isCapturableUrl(url) {
  return typeof url === 'string' && /^(https?|file):/i.test(url);
}

async function hasOffscreenDoc() {
  if (chrome.runtime && typeof chrome.runtime.getContexts === 'function') {
    try {
      const ctxs = await chrome.runtime.getContexts({ contextTypes: ['OFFSCREEN_DOCUMENT'] });
      if (ctxs && ctxs.length > 0) return true;
    } catch (_) {}
  }
  if (chrome.offscreen && typeof chrome.offscreen.hasDocument === 'function') {
    try {
      return await chrome.offscreen.hasDocument();
    } catch (_) {}
  }
  return false;
}

async function ensureOffscreen() {
  if (await hasOffscreenDoc()) return;
  if (creatingOffscreen) {
    await creatingOffscreen;
    return;
  }
  creatingOffscreen = chrome.offscreen.createDocument({
    url: 'offscreen.html',
    reasons: ['USER_MEDIA'],
    justification: 'Stream browser tab audio silently to AmigaOS 4 AHI without host speaker playback'
  }).catch(() => {}).finally(() => {
    creatingOffscreen = null;
  });
  await creatingOffscreen;
}

async function captureTab(tabId, url) {
  if (typeof tabId !== 'number' || tabId < 0) return;
  if (!isCapturableUrl(url)) return;
  if (capturedTabs.has(tabId) || capturingTabs.has(tabId)) return;
  capturingTabs.add(tabId);
  try {
    await ensureOffscreen();
    const streamId = await new Promise((resolve) => {
      try {
        chrome.tabCapture.getMediaStreamId({ targetTabId: tabId }, (sid) => {
          const err = chrome.runtime.lastError;
          if (err) {
            if (err.message && /active stream/i.test(err.message)) {
              capturedTabs.add(tabId);
            }
            resolve(null);
            return;
          }
          resolve(sid || null);
        });
      } catch (_) {
        resolve(null);
      }
    });
    if (!streamId) return;
    const resp = await new Promise((resolve) => {
      try {
        chrome.runtime.sendMessage({ type: 'capture', tabId, streamId }, (r) => {
          void chrome.runtime.lastError;
          resolve(r || null);
        });
      } catch (_) {
        resolve(null);
      }
    });
    if (resp && resp.ok) {
      capturedTabs.add(tabId);
    }
  } finally {
    capturingTabs.delete(tabId);
  }
}

function captureAllTabs() {
  chrome.tabs.query({}, (tabs) => {
    void chrome.runtime.lastError;
    if (!Array.isArray(tabs)) return;
    for (const t of tabs) {
      if (t && typeof t.id === 'number' && isCapturableUrl(t.url)) {
        captureTab(t.id, t.url);
      }
    }
  });
}

chrome.runtime.onMessage.addListener((msg, _sender, sendResponse) => {
  if (!msg || typeof msg !== 'object') return false;
  if (msg.type === 'offscreen-ready') {
    capturedTabs.clear();
    captureAllTabs();
    sendResponse({ ok: true });
    return false;
  }
  if (msg.type === 'track-ended' && typeof msg.tabId === 'number') {
    capturedTabs.delete(msg.tabId);
    sendResponse({ ok: true });
    return false;
  }
  if (msg.type === 'ping') {
    sendResponse({ ok: true });
    return false;
  }
  return false;
});

chrome.tabs.onUpdated.addListener((tabId, changeInfo, tab) => {
  const url = (changeInfo && changeInfo.url) || (tab && tab.url) || '';
  if (isCapturableUrl(url) && (changeInfo.audible || changeInfo.status === 'loading' || changeInfo.status === 'complete' || changeInfo.url)) {
    captureTab(tabId, url);
  }
});

chrome.tabs.onActivated.addListener(({ tabId }) => {
  chrome.tabs.get(tabId, (tab) => {
    void chrome.runtime.lastError;
    if (tab && isCapturableUrl(tab.url)) {
      captureTab(tabId, tab.url);
    }
  });
});

chrome.tabs.onRemoved.addListener((tabId) => {
  capturedTabs.delete(tabId);
  capturingTabs.delete(tabId);
  try {
    chrome.runtime.sendMessage({ type: 'release', tabId }, () => { void chrome.runtime.lastError; });
  } catch (_) {}
});

ensureOffscreen().then(() => captureAllTabs()).catch(() => {});
""".lstrip()

        offscreen_html = "<!doctype html><html><head><meta charset='utf-8'></head><body><script src='offscreen.js'></script></body></html>\n"

        offscreen_js = f"""
const WS_URL = "ws://127.0.0.1:{int(ws_port)}";
const SAMPLE_RATE = {AUDIO_SAMPLE_RATE};
const BUF_FRAMES = 2048; // 2048 stereo S16BE frames = 8192 bytes (AUDIO_CHUNK_BYTES)

let ws = null;
let ac = null;
let mixGain = null;
let procNode = null;
let silentGain = null;
const tabStreams = new Map();

function connectWs() {{
  try {{
    const s = new WebSocket(WS_URL);
    s.binaryType = 'arraybuffer';
    s.onopen = () => {{
      ws = s;
      try {{ s.send('vortex-audio-ready'); }} catch (_) {{}}
    }};
    s.onclose = () => {{
      if (ws === s) ws = null;
      setTimeout(connectWs, 1000);
    }};
    s.onerror = () => {{
      try {{ s.close(); }} catch (_) {{}}
    }};
  }} catch (_) {{
    setTimeout(connectWs, 1000);
  }}
}}

function ensureAudioGraph() {{
  if (ac) return;
  ac = new AudioContext({{ sampleRate: SAMPLE_RATE }});
  mixGain = ac.createGain();
  mixGain.gain.value = 1.0;

  procNode = ac.createScriptProcessor(BUF_FRAMES, 2, 2);
  // Zero-gain sink: guarantees Chromium executes onaudioprocess while outputting 0.0 to host speakers
  silentGain = ac.createGain();
  silentGain.gain.value = 0.0;

  const pcmBuf = new ArrayBuffer(BUF_FRAMES * 4);
  const pcmView = new DataView(pcmBuf);

  procNode.onaudioprocess = (ev) => {{
    if (!ws || ws.readyState !== 1 || ws.bufferedAmount > 65536) return;
    const inBuf = ev.inputBuffer;
    const chL = inBuf.getChannelData(0);
    const chR = inBuf.numberOfChannels > 1 ? inBuf.getChannelData(1) : chL;
    let peak = 0.0;
    for (let i = 0; i < BUF_FRAMES; i++) {{
      const al = Math.abs(chL[i]);
      const ar = Math.abs(chR[i]);
      if (al > peak) peak = al;
      if (ar > peak) peak = ar;
    }}
    if (peak < 0.0003) return;
    for (let i = 0; i < BUF_FRAMES; i++) {{
      const sl = Math.max(-1, Math.min(1, chL[i]));
      const sr = Math.max(-1, Math.min(1, chR[i]));
      const il = sl < 0 ? Math.round(sl * 32768) : Math.round(sl * 32767);
      const ir = sr < 0 ? Math.round(sr * 32768) : Math.round(sr * 32767);
      pcmView.setInt16(i * 4, il, false);
      pcmView.setInt16(i * 4 + 2, ir, false);
    }}
    try {{
      ws.send(pcmBuf.slice(0));
    }} catch (_) {{}}
  }};

  mixGain.connect(procNode);
  procNode.connect(silentGain);
  silentGain.connect(ac.destination);
  if (ac.state === 'suspended') {{
    ac.resume().catch(() => {{}});
  }}
}}

function releaseTab(tabId) {{
  const entry = tabStreams.get(tabId);
  if (!entry) return;
  tabStreams.delete(tabId);
  try {{ entry.src.disconnect(); }} catch (_) {{}}
  try {{
    for (const tr of entry.stream.getTracks()) {{
      tr.onended = null;
      tr.stop();
    }}
  }} catch (_) {{}}
}}

async function captureTabStream(tabId, streamId) {{
  releaseTab(tabId);
  ensureAudioGraph();
  const stream = await navigator.mediaDevices.getUserMedia({{
    audio: {{
      mandatory: {{
        chromeMediaSource: 'tab',
        chromeMediaSourceId: streamId
      }}
    }},
    video: false
  }});
  if (ac && ac.state === 'suspended') {{
    await ac.resume().catch(() => {{}});
  }}
  const src = ac.createMediaStreamSource(stream);
  src.connect(mixGain);
  tabStreams.set(tabId, {{ stream, src }});
  for (const tr of stream.getAudioTracks()) {{
    tr.onended = () => {{
      releaseTab(tabId);
      try {{
        chrome.runtime.sendMessage({{ type: 'track-ended', tabId }}, () => {{ void chrome.runtime.lastError; }});
      }} catch (_) {{}}
    }};
  }}
}}

chrome.runtime.onMessage.addListener((msg, _sender, sendResponse) => {{
  if (!msg || typeof msg !== 'object') return false;
  if (msg.type === 'capture' && typeof msg.tabId === 'number' && msg.streamId) {{
    captureTabStream(msg.tabId, msg.streamId)
      .then(() => sendResponse({{ ok: true }}))
      .catch((err) => sendResponse({{ ok: false, error: String(err) }}));
    return true;
  }}
  if (msg.type === 'release' && typeof msg.tabId === 'number') {{
    releaseTab(msg.tabId);
    sendResponse({{ ok: true }});
    return false;
  }}
  return false;
}});

connectWs();
ensureAudioGraph();
try {{
  chrome.runtime.sendMessage({{ type: 'offscreen-ready' }}, () => {{ void chrome.runtime.lastError; }});
}} catch (_) {{}}
setInterval(() => {{
  try {{
    chrome.runtime.sendMessage({{ type: 'ping' }}, () => {{ void chrome.runtime.lastError; }});
  }} catch (_) {{}}
}}, 20000);
""".lstrip()

        with open(os.path.join(ext_dir, "manifest.json"), "w", encoding="utf-8") as f:
            json.dump(manifest, f, indent=2)
        with open(os.path.join(ext_dir, "sw.js"), "w", encoding="utf-8") as f:
            f.write(sw_js)
        with open(os.path.join(ext_dir, "offscreen.html"), "w", encoding="utf-8") as f:
            f.write(offscreen_html)
        with open(os.path.join(ext_dir, "offscreen.js"), "w", encoding="utf-8") as f:
            f.write(offscreen_js)

    async def start(self) -> bool:
        if not _HAS_WEBSOCKETS or websockets is None:
            return False
        try:
            self._ws_server = await websockets.serve(
                self._ws_handler,
                "127.0.0.1",
                0,
                max_size=262144,
                ping_interval=20,
                ping_timeout=20,
            )
            socks = getattr(self._ws_server, "sockets", None) or []
            if not socks:
                await self.stop()
                return False
            self.ws_port = int(socks[0].getsockname()[1])
            self.ext_dir = tempfile.mkdtemp(prefix="vortex_audio_ext_")
            self._write_extension_files(self.ext_dir, self.ws_port)
            logger.info(
                "SilentTabAudioBridge ready (MV3 TabCapture -> ws://127.0.0.1:%d, host speakers muted)",
                self.ws_port,
            )
            return True
        except Exception as exc:
            logger.warning("Failed to start SilentTabAudioBridge: %s", exc)
            await self.stop()
            return False

    async def stop(self) -> None:
        self._consumer_queue = None
        if self._ws_server is not None:
            try:
                self._ws_server.close()
                await self._ws_server.wait_closed()
            except Exception:
                pass
            self._ws_server = None
        if self.ext_dir and os.path.isdir(self.ext_dir):
            try:
                shutil.rmtree(self.ext_dir, ignore_errors=True)
            except Exception:
                pass
            self.ext_dir = None


def ensure_audio_sink(audio_device: str = "", tabcapture_active: bool = False) -> bool:
    """Creates/checks audio capture backend on Linux (PipeWire/PulseAudio), macOS (BlackHole/TabCapture), or Windows (Silent TabCapture / WASAPI / DirectShow)."""
    if tabcapture_active:
        logger.info("Silent Browser TabCapture audio ready for AHI streaming (host speakers muted, no virtual driver required).")
        return True

    if _IS_WINDOWS:
        backend, dev = _select_windows_audio_backend(audio_device)
        if backend == "wasapi":
            logger.info("Windows WASAPI loopback (PyAudioWPatch) ready for AHI streaming (no virtual driver required).")
            return True
        if backend == "dshow" and dev:
            logger.info("Windows DirectShow audio device '%s' detected for AHI streaming.", dev)
            return True
        logger.warning("No Windows audio loopback backend found. Audio streaming disabled.")
        logger.info("Tip for Windows audio: install 'PyAudioWPatch' (pip install PyAudioWPatch) or VB-Cable.")
        return False

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
                    "sink_properties=device.description=VortexRemoteBrowser_Amiga_Audio",
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
    if platform.system() in ("Darwin", "Windows"):
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

    # Tier 1: Real native installed browsers (.deb / native packages / macOS .app / Windows .exe)
    if _IS_WINDOWS:
        local_app = os.environ.get("LOCALAPPDATA", os.path.expanduser("~\\AppData\\Local"))
        prog_files = os.environ.get("PROGRAMFILES", r"C:\Program Files")
        prog_files_x86 = os.environ.get("PROGRAMFILES(X86)", r"C:\Program Files (x86)")
        # On Windows, prioritize Ungoogled-Chromium and Playwright Chromium ("Chrome for Testing")
        # BEFORE branded Google Chrome / Edge because Chrome 137+ disables --load-extension in
        # branded builds (which would block Silent TabCapture Audio and uBlock Origin).
        unbranded_cands = [
            os.path.join(local_app, "Chromium", "Application", "chrome.exe"),
            os.path.join(prog_files, "Chromium", "Application", "chrome.exe"),
        ]
        for p in unbranded_cands:
            if os.path.isfile(p):
                return p
        pw_win_cands: List[str] = []
        for pat in (
            os.path.join(local_app, "ms-playwright", "chromium-*", "chrome-win64", "chrome.exe"),
            os.path.join(local_app, "ms-playwright", "chromium-*", "chrome-win", "chrome.exe"),
        ):
            pw_win_cands.extend(glob.glob(pat))
        pw_win_cands.sort(reverse=True)
        for c in pw_win_cands:
            if os.path.isfile(c):
                return c
        native_patterns = [
            os.path.join(prog_files, "BraveSoftware", "Brave-Browser", "Application", "brave.exe"),
            os.path.join(local_app, "BraveSoftware", "Brave-Browser", "Application", "brave.exe"),
            os.path.join(prog_files, "Google", "Chrome", "Application", "chrome.exe"),
            os.path.join(prog_files_x86, "Google", "Chrome", "Application", "chrome.exe"),
            os.path.join(local_app, "Google", "Chrome", "Application", "chrome.exe"),
            os.path.join(prog_files_x86, "Microsoft", "Edge", "Application", "msedge.exe"),
            os.path.join(prog_files, "Microsoft", "Edge", "Application", "msedge.exe"),
        ]
    elif platform.system() == "Darwin":
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
        if os.path.isfile(p) and (_IS_WINDOWS or (os.access(p, os.X_OK) and not is_snap_binary(p))):
            return p

    # Tier 2: Playwright native Chromium (clean, standalone, no snap confinement, has audio!)
    if _IS_WINDOWS:
        local_app = os.environ.get("LOCALAPPDATA", os.path.expanduser("~\\AppData\\Local"))
        pw_patterns = [
            os.path.join(local_app, "ms-playwright", "chromium-*", "chrome-win64", "chrome.exe"),
            os.path.join(local_app, "ms-playwright", "chromium-*", "chrome-win", "chrome.exe"),
        ]
    elif platform.system() == "Darwin":
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
        if os.path.isfile(c) and (_IS_WINDOWS or os.access(c, os.X_OK)):
            return c

    # Tier 3: Ubuntu Snap Chromium as last fallback (Linux only)
    if platform.system() == "Linux":
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
DEFAULT_STARTPAGE_HTML = """<!DOCTYPE html>
<html lang="de">
<head>
<meta charset="utf-8">
<title>VortexRemoteBrowser - Startseite</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body { font-family: 'DejaVu Sans', Arial, sans-serif; background: #e6eaf0; color: #1e293b; padding: 20px; }
  .box { max-width: 760px; margin: 24px auto; background: #fff; border: 1px solid #94a3b8; border-radius: 8px; padding: 20px; }
  h1 { font-size: 20px; margin-bottom: 12px; color: #1e3a8a; }
  form { display: flex; gap: 8px; margin-bottom: 16px; }
  input { flex: 1; padding: 8px 10px; border: 1px solid #94a3b8; border-radius: 4px; font-size: 14px; }
  button { padding: 8px 16px; border: 1px solid #475569; border-radius: 4px; background: #e2e8f0; font-weight: bold; cursor: pointer; }
  .links { display: flex; flex-wrap: wrap; gap: 8px; }
  .links a { padding: 6px 12px; border: 1px solid #94a3b8; border-radius: 4px; text-decoration: none; color: #1e3a8a; background: #f8fafc; font-weight: bold; font-size: 13px; }
  @media (prefers-color-scheme: dark) {
    body { background: #18181b; color: #f4f4f5; }
    .box { background: #27272a; border-color: #3f3f46; }
    h1 { color: #60a5fa; }
    input { background: #18181b; color: #fafafa; border-color: #3f3f46; }
    button, .links a { background: #3f3f46; color: #f4f4f5; border-color: #52525b; }
  }
</style>
<script>
function doSearch() {
  var q = document.getElementById('q').value.trim();
  if (!q) return false;
  window.location.href = (q.startsWith('http://') || q.startsWith('https://'))
    ? q : 'https://duckduckgo.com/?q=' + encodeURIComponent(q);
  return false;
}
</script>
</head>
<body>
  <div class="box">
    <h1>VortexRemoteBrowser &bull; Startseite</h1>
    <form onsubmit="return doSearch();">
      <input id="q" type="text" autofocus placeholder="Suchbegriff oder URL eingeben..." />
      <button type="submit">Suche</button>
    </form>
    <div class="links">
      <a href="https://os4depot.net">OS4Depot</a>
      <a href="https://aminet.net">Aminet</a>
      <a href="https://www.amiga-news.de">Amiga-News.de</a>
      <a href="https://os4welt.de">OS4Welt.de</a>
      <a href="https://www.amigans.net">Amigans.net</a>
      <a href="https://amigaworld.net">AmigaWorld.net</a>
    </div>
  </div>
</body>
</html>
"""


def resolve_startpage_url() -> str:
    """Returns a file:// URI pointing to the static startpage.html (no local HTTP server)."""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    bundled = os.path.join(script_dir, "startpage.html")
    if os.path.isfile(bundled):
        uri = Path(bundled).resolve().as_uri()
        logger.info("Using static start page: %s", uri)
        return uri
    cfg_path = os.path.join(get_vortex_config_dir(), "startpage.html")
    if not os.path.isfile(cfg_path):
        try:
            with open(cfg_path, "w", encoding="utf-8") as f:
                f.write(DEFAULT_STARTPAGE_HTML)
        except Exception as exc:
            logger.warning("Could not write fallback startpage.html: %s", exc)
            return "about:blank"
    uri = Path(cfg_path).resolve().as_uri()
    logger.info("Using fallback static start page: %s", uri)
    return uri


def is_startpage_url(url: str, portal_url: str = "") -> bool:
    """Returns True if url refers to the local static start page or legacy local portal."""
    if not url:
        return True
    low = url.strip().lower()
    if not low or low.startswith("about:") or low == "@home":
        return True
    if portal_url and low == portal_url.strip().lower():
        return True
    if low.startswith("file://") and low.endswith("/startpage.html"):
        return True
    if low.startswith("http://127.0.0.1:") or low.startswith("http://localhost:"):
        return True
    return False


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

    if _IS_WINDOWS:
        try:
            buf = ctypes.create_unicode_buffer(85)
            if ctypes.windll.kernel32.GetUserDefaultLocaleName(buf, len(buf)):
                norm = normalize_bcp47_locale(buf.value)
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
    if _IS_WINDOWS:
        if hasattr(os, "add_dll_directory"):
            try:
                os.add_dll_directory(_base_dir)
            except Exception:
                pass
        _has_win_avx2 = False
        try:
            # PF_AVX2_INSTRUCTIONS_AVAILABLE == 40 on Windows 10/11
            _has_win_avx2 = bool(ctypes.windll.kernel32.IsProcessorFeaturePresent(40))
        except Exception:
            _has_win_avx2 = False
        _candidates = ["fast_pixconv_avx2.dll", "fast_pixconv.dll"] if _has_win_avx2 else ["fast_pixconv.dll"]
    elif _is_macos:
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
            if _IS_WINDOWS:
                _target_so_name = "fast_pixconv.dll"
            elif _is_macos:
                _target_so_name = "fast_pixconv.dylib"
            else:
                _target_so_name = "fast_pixconv_aarch64.so" if _is_arm else "fast_pixconv.so"
            _target_so_path = os.path.join(_base_dir, _target_so_name)
            try:
                logger.info("Attempting auto-compilation of native SIMD + TurboJPEG + LZ4 helper (%s)...", _target_so_name)
                if _IS_WINDOWS:
                    _compile_cmds = [
                        ["gcc", "-O3", "-march=native", "-DHAVE_TURBOJPEG_LZ4", "-shared", "-o", _target_so_path, _c_src, "-lturbojpeg", "-llz4"],
                        ["gcc", "-O3", "-msse2", "-shared", "-o", _target_so_path, _c_src],
                    ]
                elif _is_macos:
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
                    _ret = subprocess.run(_cmd, capture_output=True, timeout=15, creationflags=_SUBPROC_NO_WINDOW)
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
    _win_tj_dll = os.path.join(os.path.dirname(os.path.abspath(__file__)), "turbojpeg.dll")
    if _IS_WINDOWS and os.path.isfile(_win_tj_dll):
        _TURBO_JPEG = TurboJPEG(_win_tj_dll)
    else:
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
    if _FAST_PIXCONV is not None and hasattr(_FAST_PIXCONV, "fast_pixconv_release_tls"):
        try:
            _FAST_PIXCONV.fast_pixconv_release_tls()
        except Exception:
            pass
    gc.collect()
    gc.collect()
    if platform.system() == "Linux":
        try:
            ctypes.CDLL("libc.so.6").malloc_trim(0)
        except Exception:
            pass
    elif _IS_WINDOWS:
        try:
            k32 = ctypes.windll.kernel32
            k32.SetProcessWorkingSetSize(ctypes.c_void_p(-1), ctypes.c_size_t(-1), ctypes.c_size_t(-1))
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
            for cb in list(self._listeners.get("_disconnected", [])):
                try:
                    cb({})
                except Exception:
                    pass

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
                timeout=5,
                creationflags=_SUBPROC_NO_WINDOW,
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

        if _IS_WINDOWS:
            win_probes = [
                ("nvenc", "NVIDIA NVENC hardware", [
                    "ffmpeg", "-y", "-loglevel", "error",
                    "-f", "rawvideo", "-pix_fmt", "yuv420p", "-s", f"{test_w}x{test_h}", "-r", "25", "-i", "-",
                    "-frames:v", "1", "-c:v", "h264_nvenc", "-preset", "p1", "-tune", "ull", "-zerolatency", "1", "-f", "h264", "-"
                ]),
                ("amf", "AMD AMF hardware", [
                    "ffmpeg", "-y", "-loglevel", "error",
                    "-f", "rawvideo", "-pix_fmt", "yuv420p", "-s", f"{test_w}x{test_h}", "-r", "25", "-i", "-",
                    "-frames:v", "1", "-c:v", "h264_amf", "-usage", "ultralowlatency", "-quality", "speed", "-f", "h264", "-"
                ]),
                ("qsv", "Intel QuickSync hardware", [
                    "ffmpeg", "-y", "-loglevel", "error",
                    "-f", "rawvideo", "-pix_fmt", "yuv420p", "-s", f"{test_w}x{test_h}", "-r", "25", "-i", "-",
                    "-frames:v", "1", "-c:v", "h264_qsv", "-preset", "veryfast", "-f", "h264", "-"
                ]),
                ("mf", "Windows Media Foundation hardware", [
                    "ffmpeg", "-y", "-loglevel", "error",
                    "-f", "rawvideo", "-pix_fmt", "yuv420p", "-s", f"{test_w}x{test_h}", "-r", "25", "-i", "-",
                    "-frames:v", "1", "-c:v", "h264_mf", "-rate_control", "cbr", "-f", "h264", "-"
                ]),
            ]
            for bname, label, pcmd in win_probes:
                if cls._test_ffmpeg_cmd(pcmd, test_yuv):
                    cls._cached_backend = (bname, None)
                    logger.info("H.264 Encoder Backend: h264_%s (%s)", bname, label)
                    return cls._cached_backend
        elif platform.system() == "Darwin":
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
            logger.info("H.264 Encoder Backend: libx264 (CPU ultrafast zerolatency)")
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
        logger.warning("No working H.264 encoder found in ffmpeg; using LZ4+DirtySlice.")
        return cls._cached_backend

    def __init__(self, width: int, height: int, fps: int = 25, bitrate_k: int = 8000):
        self.width = width
        self.height = height
        self.fps = max(1, min(60, fps))
        self.bitrate_k = bitrate_k
        self.proc: Optional[subprocess.Popen] = None
        self._fd: Optional[int] = None
        self._use_reader_thread: bool = (_IS_WINDOWS or fcntl is None)
        self._out_queue: "queue.Queue[bytes]" = queue.Queue(maxsize=64)
        self._reader_thread: Optional[threading.Thread] = None
        self._backend: str = "none"
        self._device: Optional[str] = None
        self._consecutive_failures: int = 0
        self._start_encoder()

    def _build_cmd(self, backend: str, device: Optional[str]) -> Optional[List[str]]:
        if backend == "nvenc":
            return [
                "ffmpeg", "-y", "-loglevel", "error",
                "-f", "rawvideo", "-pix_fmt", "yuv420p",
                "-s", f"{self.width}x{self.height}",
                "-r", str(self.fps),
                "-i", "-",
                "-c:v", "h264_nvenc",
                "-preset", "p1",
                "-tune", "ull",
                "-rc", "cbr",
                "-zerolatency", "1",
                "-b:v", f"{self.bitrate_k}k",
                "-maxrate", f"{self.bitrate_k * 3 // 2}k",
                "-bufsize", f"{self.bitrate_k // 2}k",
                "-g", str(self.fps),
                "-bf", "0",
                "-f", "h264", "-"
            ]
        if backend == "amf":
            return [
                "ffmpeg", "-y", "-loglevel", "error",
                "-f", "rawvideo", "-pix_fmt", "yuv420p",
                "-s", f"{self.width}x{self.height}",
                "-r", str(self.fps),
                "-i", "-",
                "-c:v", "h264_amf",
                "-usage", "ultralowlatency",
                "-quality", "speed",
                "-b:v", f"{self.bitrate_k}k",
                "-maxrate", f"{self.bitrate_k * 3 // 2}k",
                "-bufsize", f"{self.bitrate_k // 2}k",
                "-g", str(self.fps),
                "-bf", "0",
                "-f", "h264", "-"
            ]
        if backend == "qsv":
            return [
                "ffmpeg", "-y", "-loglevel", "error",
                "-f", "rawvideo", "-pix_fmt", "yuv420p",
                "-s", f"{self.width}x{self.height}",
                "-r", str(self.fps),
                "-i", "-",
                "-c:v", "h264_qsv",
                "-preset", "veryfast",
                "-b:v", f"{self.bitrate_k}k",
                "-maxrate", f"{self.bitrate_k * 3 // 2}k",
                "-bufsize", f"{self.bitrate_k // 2}k",
                "-g", str(self.fps),
                "-bf", "0",
                "-f", "h264", "-"
            ]
        if backend == "mf":
            return [
                "ffmpeg", "-y", "-loglevel", "error",
                "-f", "rawvideo", "-pix_fmt", "yuv420p",
                "-s", f"{self.width}x{self.height}",
                "-r", str(self.fps),
                "-i", "-",
                "-c:v", "h264_mf",
                "-rate_control", "cbr",
                "-scenario", "live_streaming",
                "-b:v", f"{self.bitrate_k}k",
                "-g", str(self.fps),
                "-f", "h264", "-"
            ]
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

    def _pipe_reader_worker(self, stdout_pipe: Any, q: "queue.Queue[bytes]") -> None:
        try:
            while True:
                data = stdout_pipe.read1(65536) if hasattr(stdout_pipe, "read1") else os.read(stdout_pipe.fileno(), 65536)
                if not data:
                    break
                try:
                    q.put_nowait(data)
                except queue.Full:
                    try:
                        q.get_nowait()
                    except queue.Empty:
                        pass
                    try:
                        q.put_nowait(data)
                    except queue.Full:
                        pass
        except Exception:
            pass

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
                cmd,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
                bufsize=0,
                creationflags=_SUBPROC_NO_WINDOW,
            )
            self._backend = backend
            self._device = device
        except Exception as e:
            logger.warning("Could not launch ffmpeg (%s): %s", backend, e)
            self.proc = None
            return

        if self.proc and self.proc.stdout:
            self._fd = self.proc.stdout.fileno()
            if self._use_reader_thread:
                self._out_queue = queue.Queue(maxsize=64)
                self._reader_thread = threading.Thread(
                    target=self._pipe_reader_worker,
                    args=(self.proc.stdout, self._out_queue),
                    daemon=True,
                    name="vb_h264_reader",
                )
                self._reader_thread.start()
            elif fcntl is not None:
                fl = fcntl.fcntl(self._fd, fcntl.F_GETFL)
                fcntl.fcntl(self._fd, fcntl.F_SETFL, fl | os.O_NONBLOCK)

    def _handle_proc_failure(self) -> None:
        self._consecutive_failures += 1
        if self._backend in ("vaapi", "nvenc", "amf", "qsv", "mf", "videotoolbox"):
            logger.warning("Hardware H.264 encoder (%s) failed at runtime; permanently switching to libx264.", self._backend)
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
        chunks: List[bytes] = []
        if self._use_reader_thread:
            try:
                first = self._out_queue.get(timeout=0.04)
                chunks.append(first)
                for _ in range(30):
                    try:
                        chunks.append(self._out_queue.get_nowait())
                    except queue.Empty:
                        break
            except queue.Empty:
                pass
        else:
            r, _, _ = select.select([self._fd], [], [], 0.04)
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
        proc = self.proc
        self.proc = None
        self._fd = None
        if proc:
            try:
                if proc.stdin:
                    try:
                        proc.stdin.close()
                    except Exception:
                        pass
                if proc.poll() is None:
                    proc.terminate()
                    try:
                        proc.wait(timeout=0.5)
                    except Exception:
                        try:
                            proc.kill()
                        except Exception:
                            pass
                        try:
                            proc.wait(timeout=1.0)
                        except Exception:
                            pass
                else:
                    try:
                        proc.wait(timeout=0.1)
                    except Exception:
                        pass
            except Exception:
                try:
                    proc.kill()
                    proc.wait(timeout=1.0)
                except Exception:
                    pass
            if proc.stdout:
                try:
                    proc.stdout.close()
                except Exception:
                    pass
        rt = self._reader_thread
        self._reader_thread = None
        if rt is not None and rt.is_alive():
            try:
                rt.join(timeout=0.3)
            except Exception:
                pass
        if self._out_queue is not None:
            while True:
                try:
                    self._out_queue.get_nowait()
                except queue.Empty:
                    break


# ============================================================================
# Persistent Profile, Session Storage & Security Helpers
# ============================================================================
def get_vortex_config_dir() -> str:
    """Returns the user config/profile directory (~/.config/vortexremotebrowser, %APPDATA%\\VortexRemoteBrowser, or $VORTEX_PROFILE_DIR)."""
    env_dir = os.environ.get("VORTEX_PROFILE_DIR", "").strip()
    if env_dir:
        cfg_dir = os.path.expanduser(env_dir)
    elif _IS_WINDOWS:
        base = os.environ.get("APPDATA") or os.environ.get("LOCALAPPDATA") or os.path.expanduser("~")
        cfg_dir = os.path.join(base, "VortexRemoteBrowser")
    else:
        cfg_dir = os.path.expanduser("~/.config/vortexremotebrowser")
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


def _cookie_dedup_key(cookie: Dict[str, Any]) -> Optional[Tuple[str, str, str]]:
    """Returns canonical (name, bare_domain, path) key for host-vs-dot cookie collision detection."""
    if not isinstance(cookie, dict):
        return None
    name = cookie.get("name")
    dom = cookie.get("domain")
    path = cookie.get("path") or "/"
    if not name or not dom:
        return None
    return (str(name), str(dom).lstrip(".").lower(), str(path))


def _pick_winning_cookie(items: List[Dict[str, Any]]) -> Dict[str, Any]:
    """Selects the surviving cookie when host-only and wildcard-dot variants collide."""
    all_positive = all(
        isinstance(it.get("expires"), (int, float)) and float(it.get("expires", -1)) > 0
        for it in items
    )
    if all_positive:
        return max(
            items,
            key=lambda it: (
                float(it.get("expires", 0)),
                1 if str(it.get("domain", "")).startswith(".") else 0,
            ),
        )
    return max(
        items,
        key=lambda it: (
            1 if str(it.get("domain", "")).startswith(".") else 0,
            float(it.get("expires", -1) or -1),
        ),
    )


def _deduplicate_cookie_list(cookies: Any) -> List[Dict[str, Any]]:
    """Removes colliding host-only vs .domain duplicates while leaving unrelated cookies untouched."""
    if not isinstance(cookies, list):
        return []
    grouped: Dict[Tuple[str, str, str], List[Dict[str, Any]]] = {}
    for c in cookies:
        key = _cookie_dedup_key(c)
        if key is not None:
            grouped.setdefault(key, []).append(c)

    winners: Dict[Tuple[str, str, str], Dict[str, Any]] = {}
    for key, items in grouped.items():
        if len(items) < 2:
            continue
        base_dom = key[1]
        has_bare = any(str(it.get("domain", "")).lower() == base_dom for it in items)
        has_dot = any(str(it.get("domain", "")).startswith(".") for it in items)
        if has_bare and has_dot:
            winners[key] = _pick_winning_cookie(items)

    if not winners:
        return [c for c in cookies if isinstance(c, dict)]

    emitted_keys: Set[Tuple[str, str, str]] = set()
    cleaned: List[Dict[str, Any]] = []
    for c in cookies:
        key = _cookie_dedup_key(c)
        if key is None:
            continue
        if key in winners:
            if key not in emitted_keys:
                cleaned.append(winners[key])
                emitted_keys.add(key)
        else:
            cleaned.append(c)
    return cleaned


def _build_cdp_cookie_param(raw: Dict[str, Any]) -> Optional[Dict[str, Any]]:
    """Builds a CDP Storage.setCookies payload preserving leading-dot domains and partitionKey."""
    name = raw.get("name")
    value = raw.get("value")
    domain = raw.get("domain")
    if not name or value is None or not domain:
        return None
    param: Dict[str, Any] = {
        "name": str(name),
        "value": str(value),
        "domain": str(domain),
        "path": str(raw.get("path") or "/"),
        "secure": bool(raw.get("secure", False)),
        "httpOnly": bool(raw.get("httpOnly", False)),
    }
    exp = raw.get("expires")
    if isinstance(exp, (int, float)) and float(exp) > 0:
        param["expires"] = float(exp)
    same_site = raw.get("sameSite")
    if same_site in ("Strict", "Lax", "None"):
        param["sameSite"] = same_site
    if isinstance(raw.get("partitionKey"), dict):
        param["partitionKey"] = raw["partitionKey"]
    return param


async def _cdp_set_cookies(context: BrowserContext, cookies: List[Dict[str, Any]]) -> bool:
    """Restores cookies via CDP Storage.setCookies so wildcard leading dots stay intact."""
    params = [p for c in cookies if (p := _build_cdp_cookie_param(c)) is not None]
    if not params:
        return True
    page = None
    created_temp = False
    cdp = None
    try:
        existing_pages = list(getattr(context, "pages", []) or [])
        page = existing_pages[0] if existing_pages else await context.new_page()
        created_temp = len(existing_pages) == 0
        cdp = await context.new_cdp_session(page)
        await cdp.send("Storage.setCookies", {"cookies": params})
        return True
    except Exception:
        try:
            fallback = []
            for p in params:
                ck = dict(p)
                ck.pop("partitionKey", None)
                fallback.append(ck)
            await context.add_cookies(fallback)
            return True
        except Exception:
            return False
    finally:
        await _safe_detach_cdp_session(cdp)
        if created_temp and page is not None:
            try:
                await page.close()
            except Exception:
                pass


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

        raw_cookies = new_state.get("cookies") if isinstance(new_state.get("cookies"), list) else (prev_state.get("cookies") or [])
        payload = {
            "cookies": _deduplicate_cookie_list(raw_cookies),
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
            key
            for c in existing_cookies
            if (key := _cookie_dedup_key(c)) is not None
        }
    except Exception:
        existing_keys = set()

    now_ts = time.time()
    grouped_missing: Dict[Tuple[str, str, str], List[Dict[str, Any]]] = {}
    for raw in payload.get("cookies") or []:
        norm_key = _cookie_dedup_key(raw)
        if norm_key is None or norm_key in existing_keys:
            continue
        if raw.get("value") is None:
            continue
        exp = raw.get("expires")
        if isinstance(exp, (int, float)) and exp > 0 and exp <= now_ts:
            continue
        grouped_missing.setdefault(norm_key, []).append(raw)

    to_restore: List[Dict[str, Any]] = [
        param
        for items in grouped_missing.values()
        if (param := _build_cdp_cookie_param(_pick_winning_cookie(items))) is not None
    ]

    if to_restore:
        try:
            if await _cdp_set_cookies(context, to_restore):
                logger.info("Recovered %d missing persistent cookie(s) from %s", len(to_restore), state_file)
        except Exception as exc:
            logger.debug("recover_profile_storage_state restore warning: %s", exc)

    recovery_js = _build_local_storage_recovery_script(payload.get("origins") or [])
    if recovery_js:
        try:
            await context.add_init_script(recovery_js)
            logger.info("Installed localStorage crash-recovery fallback from %s", state_file)
        except Exception as exc:
            logger.debug("recover_profile_storage_state init script warning: %s", exc)


async def deduplicate_context_cookies(context: BrowserContext) -> None:
    """Remove stale host-only vs dot-domain duplicate cookies (e.g. phpBB sid mismatch causing invalid form tokens)."""
    if not context:
        return
    try:
        cookies = await context.cookies()
    except Exception:
        return
    grouped: Dict[Tuple[str, str, str], List[Dict[str, Any]]] = {}
    for c in cookies:
        key = _cookie_dedup_key(c)
        if key is not None:
            grouped.setdefault(key, []).append(c)

    conflicts: List[Tuple[str, str, str, Dict[str, Any]]] = []
    for (name, base_dom, path), items in grouped.items():
        if len(items) < 2:
            continue
        if not any(str(it.get("domain", "")).lower() == base_dom for it in items):
            continue
        if not any(str(it.get("domain", "")).startswith(".") for it in items):
            continue
        winner = _pick_winning_cookie(items)
        param = _build_cdp_cookie_param(winner)
        if param is not None:
            conflicts.append((name, base_dom, path, param))

    if not conflicts:
        return

    page = None
    created_temp = False
    cdp = None
    try:
        existing_pages = list(getattr(context, "pages", []) or [])
        page = existing_pages[0] if existing_pages else await context.new_page()
        created_temp = len(existing_pages) == 0
        cdp = await context.new_cdp_session(page)
        for name, base_dom, path, _param in conflicts:
            await cdp.send("Network.deleteCookies", {"name": name, "domain": base_dom, "path": path})
        await cdp.send("Storage.setCookies", {"cookies": [param for _, _, _, param in conflicts]})
        logger.info("Deduplicated %d conflicting host/domain cookie(s)", len(conflicts))
    except Exception as exc:
        logger.debug("deduplicate_context_cookies warning: %s", exc)
    finally:
        await _safe_detach_cdp_session(cdp)
        if created_temp and page is not None:
            try:
                await page.close()
            except Exception:
                pass


def _detect_chromium_ua(executable_path: Optional[str]) -> Optional[str]:
    """Detects installed Chromium major version for a consistent Desktop User-Agent."""
    if not executable_path:
        return None
    major: Optional[str] = None
    if _IS_WINDOWS:
        # On Windows, chrome.exe is a GUI subsystem binary (--version prints nothing to stdout).
        # Inspect sibling version directories (e.g. Application\134.0.6998.89) or parent path first.
        try:
            exe_dir = os.path.dirname(os.path.abspath(executable_path))
            ver_candidates: List[Tuple[int, str]] = []
            if os.path.isdir(exe_dir):
                for entry in os.listdir(exe_dir):
                    m = re.match(r"^(\d+)\.\d+\.\d+\.\d+$", entry)
                    if m:
                        ver_candidates.append((int(m.group(1)), m.group(1)))
            if ver_candidates:
                ver_candidates.sort(reverse=True)
                major = ver_candidates[0][1]
        except Exception:
            pass
        if not major:
            try:
                ps_cmd = f"(Get-Item '{executable_path.replace(chr(39), chr(39)*2)}').VersionInfo.ProductVersion"
                out = subprocess.check_output(
                    ["powershell", "-NoProfile", "-Command", ps_cmd],
                    text=True,
                    stderr=subprocess.DEVNULL,
                    timeout=3,
                    creationflags=_SUBPROC_NO_WINDOW,
                )
                m = re.search(r"\b(\d+)\.\d+\.\d+\.\d+\b", out)
                if m:
                    major = m.group(1)
            except Exception:
                pass
        if major:
            return (
                f"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                f"(KHTML, like Gecko) Chrome/{major}.0.0.0 Safari/537.36"
            )
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
            if platform.system() == "Darwin":
                return (
                    f"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
                    f"(KHTML, like Gecko) Chrome/{major}.0.0.0 Safari/537.36"
                )
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
# External Video Player HTTP Stream Relay (Plain-HTTP IPv4 Proxy for Amiga V-MPlayer)
# ============================================================================
_EXT_STREAM_REGISTRY: Dict[str, Dict[str, Any]] = {}
_EXT_STREAM_COUNTER: int = 0


def register_ext_video_stream(upstream_url: str, source_url: str = "") -> str:
    global _EXT_STREAM_COUNTER
    _EXT_STREAM_COUNTER += 1
    sid = str(_EXT_STREAM_COUNTER)
    # Keep registry bounded to latest 32 streams
    if len(_EXT_STREAM_REGISTRY) > 32:
        oldest_key = next(iter(_EXT_STREAM_REGISTRY))
        _EXT_STREAM_REGISTRY.pop(oldest_key, None)
    _EXT_STREAM_REGISTRY[sid] = {
        "upstream_url": upstream_url,
        "source_url": source_url or upstream_url,
        "created_at": time.monotonic(),
    }
    return sid


async def _handle_ext_video_stream_client(reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
    """Streams resolved YouTube / media video over plain HTTP IPv4 to AmigaOS 4 V-MPlayer / MPlayer / Emotion."""
    proc = None
    try:
        req_line_bytes = await asyncio.wait_for(reader.readline(), timeout=10.0)
        if not req_line_bytes:
            return
        req_line = req_line_bytes.decode("latin-1", errors="replace").strip()
        parts = req_line.split()
        if len(parts) < 2:
            return
        method = parts[0].upper()
        path = parts[1]

        headers: Dict[str, str] = {}
        while True:
            hline = await asyncio.wait_for(reader.readline(), timeout=5.0)
            if not hline or hline in (b"\r\n", b"\n"):
                break
            hstr = hline.decode("latin-1", errors="replace").strip()
            if ":" in hstr:
                hk, hv = hstr.split(":", 1)
                headers[hk.strip().lower()] = hv.strip()

        # Extract stream ID from /video/<id>.mp4
        m = re.search(r"/video/(\d+)", path)
        sid = m.group(1) if m else ""
        entry = _EXT_STREAM_REGISTRY.get(sid)
        if not entry and _EXT_STREAM_REGISTRY:
            # Fallback to most recently registered stream
            sid = list(_EXT_STREAM_REGISTRY.keys())[-1]
            entry = _EXT_STREAM_REGISTRY.get(sid)

        if not entry:
            writer.write(b"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
            await writer.drain()
            return

        upstream_url = entry["upstream_url"]
        range_hdr = headers.get("range", "")
        logger.info("External Player HTTP Relay %s %s (Range=%s) -> %s", method, path, range_hdr or "none", upstream_url[:100])

        is_hls = ".m3u8" in upstream_url.lower() or "manifest.googlevideo.com" in upstream_url.lower()
        if not is_hls:
            # Direct progressive MP4 relay with full HTTP Range support
            q = asyncio.Queue(maxsize=16)
            loop = asyncio.get_running_loop()
            stop_flag = threading.Event()

            def _upstream_worker():
                req_headers = {
                    "User-Agent": "com.google.android.youtube/19.29.37 (Linux; U; Android 11) gzip",
                    "Accept": "*/*",
                    "Connection": "keep-alive",
                }
                if range_hdr:
                    req_headers["Range"] = range_hdr
                req = urllib.request.Request(upstream_url, headers=req_headers, method="GET" if method != "HEAD" else "HEAD")
                try:
                    with urllib.request.urlopen(req, timeout=15.0) as resp:
                        status_code = getattr(resp, "status", 200) or 200
                        ctype = resp.headers.get("Content-Type") or "video/mp4"
                        clen = resp.headers.get("Content-Length")
                        crange = resp.headers.get("Content-Range")
                        meta = (status_code, ctype, clen, crange)
                        asyncio.run_coroutine_threadsafe(q.put(("meta", meta)), loop).result(timeout=5.0)
                        if method == "HEAD":
                            return
                        while not stop_flag.is_set():
                            chunk = resp.read(65536)
                            if not chunk:
                                break
                            asyncio.run_coroutine_threadsafe(q.put(("data", chunk)), loop).result(timeout=15.0)
                except Exception as exc:
                    try:
                        asyncio.run_coroutine_threadsafe(q.put(("err", str(exc))), loop).result(timeout=2.0)
                    except Exception:
                        pass
                finally:
                    try:
                        asyncio.run_coroutine_threadsafe(q.put(("eof", None)), loop).result(timeout=2.0)
                    except Exception:
                        pass

            worker_fut = loop.run_in_executor(None, _upstream_worker)
            try:
                first_kind, first_payload = await asyncio.wait_for(q.get(), timeout=15.0)
                if first_kind == "meta":
                    status_code, ctype, clen, crange = first_payload
                    reason = "Partial Content" if status_code == 206 else "OK"
                    resp_hdrs = [
                        f"HTTP/1.1 {status_code} {reason}",
                        f"Content-Type: {ctype}",
                        "Accept-Ranges: bytes",
                        "Connection: close",
                    ]
                    if clen:
                        resp_hdrs.append(f"Content-Length: {clen}")
                    if crange:
                        resp_hdrs.append(f"Content-Range: {crange}")
                    writer.write(("\r\n".join(resp_hdrs) + "\r\n\r\n").encode("latin-1"))
                    await writer.drain()

                    while True:
                        kind, payload = await q.get()
                        if kind == "data" and payload:
                            writer.write(payload)
                            await writer.drain()
                        else:
                            break
                    return
                else:
                    logger.warning("Direct upstream relay failed (%s), falling back to ffmpeg transcode", first_payload)
            finally:
                stop_flag.set()

        # Fallback / HLS: stream via ffmpeg as H.264/AAC MPEG-TS compatible with all AmigaOS 4 MPlayer builds
        ffmpeg_bin = shutil.which("ffmpeg")
        if not ffmpeg_bin:
            writer.write(b"HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
            await writer.drain()
            return

        writer.write(
            b"HTTP/1.1 200 OK\r\n"
            b"Content-Type: video/mp2t\r\n"
            b"Connection: close\r\n\r\n"
        )
        await writer.drain()
        if method == "HEAD":
            return

        proc = await asyncio.create_subprocess_exec(
            ffmpeg_bin,
            "-hide_banner",
            "-loglevel", "error",
            "-reconnect", "1",
            "-reconnect_streamed", "1",
            "-reconnect_delay_max", "4",
            "-i", upstream_url,
            "-c:v", "copy",
            "-c:a", "aac",
            "-b:a", "128k",
            "-f", "mpegts",
            "pipe:1",
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.DEVNULL,
        )
        assert proc.stdout is not None
        while True:
            chunk = await proc.stdout.read(65536)
            if not chunk:
                break
            writer.write(chunk)
            await writer.drain()
    except (ConnectionResetError, BrokenPipeError, asyncio.CancelledError):
        pass
    except Exception as exc:
        logger.debug("External video stream relay closed: %s", exc)
    finally:
        if proc is not None:
            try:
                proc.kill()
            except Exception:
                pass
        try:
            writer.close()
            await writer.wait_closed()
        except Exception:
            pass


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
        use_rustwright: bool = False,
        audio_device: str = "",
        tab_audio_bridge: Optional[SilentTabAudioBridge] = None,
        close_browser_on_exit: bool = True,
    ):
        self.reader = reader
        self.writer = writer
        self.browser = browser
        self.is_persistent_context: bool = isinstance(browser, _PERSISTENT_CONTEXT_TYPES)
        self.close_browser_on_exit: bool = bool(close_browser_on_exit)
        self._storage_saved_on_exit: bool = False
        self.engine: str = engine.lower()
        self.storage_state_file: str = storage_state_file or get_vortex_storage_state_path(self.engine)
        self._context_state: Dict[str, Any] = context_state if context_state is not None else {}
        self.browser_locale: str = normalize_bcp47_locale(browser_locale) or _SERVER_LOCALE
        self.locale_explicit: bool = bool(locale_explicit)
        self.use_rustwright: bool = bool(use_rustwright)
        self.audio_device: str = audio_device
        self.tab_audio_bridge: Optional[SilentTabAudioBridge] = tab_audio_bridge
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
        self.ext_play_auto_youtube: bool = True
        self._last_ext_play_time: float = 0.0
        self.last_hover_url: Optional[str] = None
        self.last_hover_time: float = 0.0
        self.hover_task: Optional[asyncio.Task] = None

        # Per-Tab Explicit Navigation History Stacks (immune to redirect loops & cross-tab mixing)
        self._tab_histories: Dict[Tuple[str, Any], Dict[str, Any]] = {}

        self.next_download_id: int = 1
        self.pending_downloads: Dict[int, asyncio.Future] = {}
        self._active_download_cancels: Dict[int, asyncio.Event] = {}
        self._browser_dl_ws: Any = None
        self._browser_dl_task: Optional[asyncio.Task] = None
        self._browser_dl_dir: str = ""
        self._browser_dl_cmd_id: int = 10
        self._cdp_downloads: Dict[str, Dict[str, Any]] = {}
        self._cdp_handled_guids: Set[str] = set()
        self._cdp_handled_keys: Dict[Tuple[str, str], float] = {}
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
        self._nav_task: Optional[asyncio.Task] = None
        self._tab_nav_tasks: Dict[Tuple[str, Any], asyncio.Task] = {}
        self._client_nav_received = asyncio.Event()
        self._err_aborted_counts: Dict[Tuple[str, Any], int] = {}
        self._wedged_pages: Set[Tuple[str, Any]] = set()
        self._last_download_time: float = 0.0
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
                sock.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
                if hasattr(socket, "TCP_KEEPIDLE"):
                    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPIDLE, 15)
                if hasattr(socket, "TCP_KEEPINTVL"):
                    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPINTVL, 5)
                if hasattr(socket, "TCP_KEEPCNT"):
                    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPCNT, 3)
        except Exception:
            pass

        # CDP screencast (push-based frame streaming, no per-frame request/response overhead)
        self.screencast_queue: Optional[asyncio.Queue] = None
        self.screencast_active: bool = False
        self._screencast_handler = None
        self.last_screencast_frame_time: float = 0.0
        self._video_eval_in_flight: bool = False
        self._cached_page_url: str = ""

        # Mouse click tracking for gesture stabilization & upload trigger
        self._mouse_down_x: int = -1
        self._mouse_down_y: int = -1
        self._mouse_down_btn: str = ""
        self._mouse_down_time: float = 0.0
        self._last_click_time: float = 0.0
        self._last_click_x: int = -1
        self._last_click_y: int = -1
        self._click_count: int = 1
        self._scrollbar_drag_offset: Optional[float] = None
        self._popup_adopt_lock = asyncio.Lock()
        self._adopting_popups: set = set()
        self.last_upload_intercept_time: float = 0.0
        self._server_clipboard: str = ""

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
                        # Listen for Page.fileChooserOpened, Page.windowOpen & unexpected close on DirectCDP
                        if (
                            self.cdp is direct
                            and self._tab_generation == gen
                            and self.page is target_page
                            and self.running
                        ):
                            self.cdp.on("Page.fileChooserOpened", self._on_cdp_file_chooser_opened)
                            self.cdp.on("Page.windowOpen", self._on_cdp_window_open)
                            self.cdp.on("Page.javascriptDialogOpening", self._on_cdp_js_dialog)
                            self.cdp.on(
                                "_disconnected",
                                lambda _=None, tp=target_page, g=gen: self._on_cdp_unexpected_disconnect(tp, g),
                            )
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

    async def _launch_external_player(self, raw_target: str) -> None:
        """Resolves a video URL (YouTube, Twitch, Mediathek, or direct <video>) into a plain-HTTP
        IPv4 LAN stream URL served by our built-in HTTP proxy (port 7779) and launches V-MPlayer on Amiga."""
        resolved_url = (raw_target or "").strip()
        try:
            if self.page:
                try:
                    dom_media = await self.page.evaluate("""() => {
                        let foundSrc = '';
                        document.querySelectorAll('video').forEach(v => {
                            try { v.pause(); } catch(e) {}
                            const s = v.currentSrc || v.src || '';
                            if (!foundSrc && s && (s.startsWith('http://') || s.startsWith('https://'))) {
                                foundSrc = s;
                            }
                        });
                        return foundSrc;
                    }""")
                    if dom_media and (not resolved_url or resolved_url == getattr(self, "_cached_page_url", "")):
                        resolved_url = dom_media
                except Exception:
                    pass

            # Prefer the virtualenv yt-dlp / python -m yt_dlp (cross-platform: Linux, Windows .exe, macOS)
            py_dir = os.path.dirname(sys.executable)
            venv_ytdlp_win = os.path.join(py_dir, "yt-dlp.exe")
            venv_ytdlp_unix = os.path.join(py_dir, "yt-dlp")
            ytdlp_cmd: List[str] = []
            if os.path.isfile(venv_ytdlp_win):
                ytdlp_cmd = [venv_ytdlp_win]
            elif os.path.isfile(venv_ytdlp_unix):
                ytdlp_cmd = [venv_ytdlp_unix]
            else:
                sys_ytdlp = shutil.which("yt-dlp")
                if sys_ytdlp:
                    ytdlp_cmd = [sys_ytdlp]
                else:
                    ytdlp_cmd = [sys.executable, "-m", "yt_dlp"]

            upstream_urls: List[str] = []
            if ytdlp_cmd and resolved_url.startswith(("http://", "https://")):
                proc = await asyncio.create_subprocess_exec(
                    *ytdlp_cmd,
                    "--no-playlist",
                    "--no-warnings",
                    "--extractor-args", "youtube:player_client=android,ios,tv,web",
                    "-f", "best[ext=mp4][vcodec^=avc1][acodec!=none]/18/best[ext=mp4]/best",
                    "-g",
                    resolved_url,
                    stdout=asyncio.subprocess.PIPE,
                    stderr=asyncio.subprocess.PIPE,
                )
                try:
                    stdout, _ = await asyncio.wait_for(proc.communicate(), timeout=8.0)
                    if proc.returncode == 0 and stdout:
                        upstream_urls = [
                            ln.strip()
                            for ln in stdout.decode("utf-8", errors="replace").splitlines()
                            if ln.strip().startswith(("http://", "https://"))
                        ]
                        if upstream_urls:
                            logger.info("Resolved external player stream via yt-dlp: %s", upstream_urls[0][:120])
                except asyncio.TimeoutError:
                    try:
                        proc.kill()
                    except Exception:
                        pass

            target_stream = upstream_urls[0] if upstream_urls else resolved_url
            if not target_stream:
                return

            # Register in the built-in plain-HTTP IPv4 stream relay (port 7779) so Amiga V-MPlayer
            # gets a short, clean http://<server-ip>:7779/video/<id>.mp4 URL without IPv6/TLS1.3/403 issues.
            server_ip = "127.0.0.1"
            try:
                sockname = self.writer.get_extra_info("sockname")
                if sockname and isinstance(sockname, (tuple, list)) and sockname[0]:
                    raw_ip = str(sockname[0])
                    if raw_ip.startswith("::ffff:"):
                        raw_ip = raw_ip[7:]
                    if raw_ip not in ("0.0.0.0", "::", ""):
                        server_ip = raw_ip
            except Exception:
                pass

            stream_port = int(os.environ.get("VORTEX_STREAM_PORT", "7779"))
            stream_id = register_ext_video_stream(target_stream, source_url=resolved_url)
            play_url = f"http://{server_ip}:{stream_port}/video/{stream_id}.mp4"
            logger.info("Sending @EXTPLAY_LAUNCH to Amiga: %s -> %s", resolved_url, play_url)
            self._send_nav_update(f"@EXTPLAY_LAUNCH:{play_url}", title="External Video")
        except Exception as e:
            logger.warning("Error in _launch_external_player('%s'): %s", raw_target, e)
            if resolved_url:
                self._send_nav_update(f"@EXTPLAY_LAUNCH:{resolved_url}", title="External Video")

    def _send_nav_update(self, url: str, tab_idx: int = -1, title: str = "") -> None:
        if not self.running or not url:
            return
        if not url.startswith("@") and (tab_idx < 0 or tab_idx == self.active_tab_idx):
            self._cached_page_url = url.lower()

        async def _do_send():
            nonlocal title
            t_idx = tab_idx
            if t_idx < 0:
                try:
                    found_idx = self._find_tab_index(self.page) if (self.page and self.tabs) else -1
                    t_idx = found_idx if found_idx >= 0 else self.active_tab_idx
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

    def _send_tab_sync(
        self,
        action: int,
        tab_idx: int,
        active_idx: int,
        make_active: bool = True,
        url: str = "",
        title: str = "",
    ) -> None:
        if not self.running:
            return
        url_str = (url or "").strip()
        if url_str.startswith("about:") and action != TAB_SYNC_ADD:
            url_str = ""
        url_bytes = url_str.encode("utf-8", errors="replace")[:511].ljust(512, b"\x00")
        title_bytes = (title or "").encode("utf-8", errors="replace")[:127].ljust(128, b"\x00")
        pkt = STRUCT_MSG_TAB_SYNC.pack(
            MSG_TAB_SYNC,
            int(action),
            1 if make_active else 0,
            max(0, int(tab_idx)),
            max(0, int(active_idx)),
            max(1, len(self.tabs)),
            url_bytes,
            title_bytes,
        )
        t = asyncio.create_task(self.send_packet(pkt))
        self.background_tasks.add(t)
        t.add_done_callback(self.background_tasks.discard)

    @staticmethod
    def _page_identity(page: Any) -> Tuple[str, Any]:
        raw = getattr(page, "_sync", page)
        tid = getattr(raw, "_target_id", None) or getattr(page, "_target_id", None)
        if tid:
            return ("target_id", str(tid))
        return ("obj", id(raw))

    def _find_tab_index(self, page: Any) -> int:
        if page is None:
            return -1
        try:
            return self.tabs.index(page)
        except ValueError:
            pass
        key = self._page_identity(page)
        for i, tab in enumerate(self.tabs):
            if self._page_identity(tab) == key:
                return i
        return -1

    async def _fetch_live_page_target_ids(self) -> Optional[set]:
        try:
            def _get():
                with urllib.request.urlopen(f"http://127.0.0.1:{self.cdp_port}/json/list", timeout=0.6) as r:
                    return json.loads(r.read().decode("utf-8", errors="replace"))
            targets = await asyncio.to_thread(_get)
            return {str(t["id"]) for t in targets if t.get("type") == "page" and t.get("id")}
        except Exception:
            return None

    async def _adopt_rustwright_popups(self) -> None:
        """Pump Rustwright's target discovery so popups/target=_blank opened during DirectCDP clicks are adopted."""
        if not self.running or not self.context or getattr(self, "_creating_tab", False):
            return
        async with self._popup_adopt_lock:
            if not self.running or not self.context or getattr(self, "_creating_tab", False):
                return
            try:
                live_ids = await self._fetch_live_page_target_ids() if self.engine == "chromium" else None
                known = {self._page_identity(t) for t in self.tabs}
                if live_ids is not None:
                    known_tid_strs = {v for kind, v in known if kind == "target_id"}
                    if live_ids.issubset(known_tid_strs):
                        return

                sync_ctx = getattr(self.context, "_sync", None)
                if self.use_rustwright and sync_ctx is not None and hasattr(sync_ctx, "_adopt_existing_pages"):
                    await asyncio.wait_for(asyncio.to_thread(sync_ctx._adopt_existing_pages), timeout=0.6)
                pages = list(getattr(self.context, "pages", []) or [])
                for p in pages:
                    pkey = self._page_identity(p)
                    if pkey not in known and pkey not in self._adopting_popups:
                        self._on_popup_page(p)
            except Exception as exc:
                logger.debug("Popup target adopt notice: %s", exc)

    def _on_cdp_unexpected_disconnect(self, target_page: Page, gen: int) -> None:
        if not self.running or self._switching_tab or self._tab_generation != gen:
            return

        async def _check_closed() -> None:
            await asyncio.sleep(0.12)
            if not self.running or self._switching_tab or self._tab_generation != gen:
                return
            live_ids = await self._fetch_live_page_target_ids()
            if live_ids is None:
                return
            for tab in list(self.tabs):
                kind, val = self._page_identity(tab)
                if kind == "target_id" and str(val) not in live_ids:
                    self._on_page_closed(tab)

        t = asyncio.create_task(_check_closed())
        self.background_tasks.add(t)
        t.add_done_callback(self.background_tasks.discard)

    def _on_cdp_window_open(self, params: Optional[Dict[str, Any]] = None) -> None:
        if not self.running or getattr(self, "_creating_tab", False):
            return

        async def _check() -> None:
            for delay in (0.05, 0.15, 0.35):
                await asyncio.sleep(delay)
                if not self.running or getattr(self, "_creating_tab", False):
                    return
                await self._adopt_rustwright_popups()

        t = asyncio.create_task(_check())
        self.background_tasks.add(t)
        t.add_done_callback(self.background_tasks.discard)

    def _on_cdp_js_dialog(self, params: Optional[Dict[str, Any]] = None) -> None:
        """Auto-accept beforeunload dialogs and dismiss blocking JS dialogs on DirectCDP."""
        if not self.running or not isinstance(self.cdp, DirectCDPSession) or not self.cdp.connected:
            return
        dtype = str((params or {}).get("type") or "").lower()
        accept = (dtype == "beforeunload")
        cdp_ref = self.cdp

        async def _reply() -> None:
            try:
                await asyncio.wait_for(
                    cdp_ref.send("Page.handleJavaScriptDialog", {"accept": accept}),
                    timeout=1.0,
                )
            except Exception:
                pass

        t = asyncio.create_task(_reply())
        self.background_tasks.add(t)
        t.add_done_callback(self.background_tasks.discard)

    def _on_dialog_event(self, dialog: Any) -> None:
        """Accept beforeunload prompts (prevents Rustwright net::ERR_ABORTED lockup) and dismiss modal alerts."""
        if dialog is None:
            return

        async def _handle_dlg() -> None:
            try:
                dtype = str(getattr(dialog, "type", "") or "").lower()
                if dtype == "beforeunload":
                    await dialog.accept()
                else:
                    await dialog.dismiss()
            except Exception:
                pass

        t = asyncio.create_task(_handle_dlg())
        self.background_tasks.add(t)
        t.add_done_callback(self.background_tasks.discard)

    @staticmethod
    def _is_page_closed(page: Any) -> bool:
        if page is None:
            return True
        try:
            return bool(page.is_closed())
        except Exception:
            return True

    def _cancel_nav_task(self, page: Any = None) -> None:
        if page is not None:
            pkey = self._page_identity(page)
            pt = self._tab_nav_tasks.pop(pkey, None)
            if pt is not None and not pt.done():
                pt.cancel()
            if self._nav_task is pt:
                self._nav_task = None
            return
        t = self._nav_task
        self._nav_task = None
        if t is not None and not t.done():
            t.cancel()

    def _get_tab_history(self, page: Any) -> Dict[str, Any]:
        pkey = self._page_identity(page)
        hist = self._tab_histories.get(pkey)
        if hist is None:
            hist = {
                "history": [],
                "times": [],
                "idx": -1,
                "jumping": False,
                "force_push": False,
            }
            self._tab_histories[pkey] = hist
        return hist

    def _record_tab_history(self, tab_page: Page, url: str) -> None:
        if tab_page is None:
            return
        url = (url or "").strip()
        if not url or url.startswith("about:") or url.startswith("javascript:"):
            return
        hist = self._get_tab_history(tab_page)
        now = time.monotonic()
        tidx = self._find_tab_index(tab_page)
        h_list: List[str] = hist["history"]
        h_times: List[float] = hist["times"]
        h_idx: int = hist["idx"]

        if hist["jumping"]:
            if 0 <= h_idx < len(h_list):
                h_list[h_idx] = url
                h_times[h_idx] = now
            return

        if 0 <= h_idx < len(h_list):
            if h_list[h_idx] == url:
                return
            force_push = bool(hist.get("force_push", False))
            hist["force_push"] = False
            # If the previous navigation occurred < 1.2s ago (and wasn't the portal or an explicit user URL),
            # treat this as an automatic HTTP/JS redirect and update the top entry in-place
            # so clicking Back doesn't get stuck in a redirect loop!
            elapsed = now - h_times[h_idx]
            prev_url = h_list[h_idx]
            if not force_push and elapsed < 1.2 and not is_startpage_url(prev_url, self.portal_url):
                h_list[h_idx] = url
                h_times[h_idx] = now
                logger.info("Tab[%d] history updated redirect in-place [%d]: %s", tidx, h_idx, url)
                return
        else:
            hist["force_push"] = False

        # Push new history entry (bounded to 200 entries per tab)
        h_list[:] = h_list[: h_idx + 1]
        h_times[:] = h_times[: h_idx + 1]
        if len(h_list) >= 200:
            overflow = len(h_list) - 199
            del h_list[:overflow]
            del h_times[:overflow]
        h_list.append(url)
        h_times.append(now)
        hist["idx"] = len(h_list) - 1
        logger.info("Tab[%d] history push [%d/%d]: %s", tidx, hist["idx"], len(h_list), url)

    async def _force_close_page(self, page: Any) -> None:
        if page is None:
            return
        pkey = self._page_identity(page)
        self._cancel_nav_task(page)
        self._err_aborted_counts.pop(pkey, None)
        self._wedged_pages.discard(pkey)
        self._tab_histories.pop(pkey, None)
        self._detach_page_listeners(page)
        if not self._is_page_closed(page):
            closed = False
            try:
                await asyncio.wait_for(page.close(run_before_unload=False), timeout=2.0)
                closed = True
            except TypeError:
                try:
                    await asyncio.wait_for(page.close(), timeout=2.0)
                    closed = True
                except Exception:
                    pass
            except Exception:
                pass
            if not closed and not self._is_page_closed(page):
                kind, tid = pkey
                if kind == "target_id" and isinstance(self.cdp, DirectCDPSession) and self.cdp.connected:
                    try:
                        await asyncio.wait_for(
                            self.cdp.send("Target.closeTarget", {"targetId": str(tid)}),
                            timeout=1.5,
                        )
                    except Exception:
                        pass

    async def _stop_page_loading(self, target_page: Page) -> None:
        if self._is_page_closed(target_page):
            return
        if (
            self.page is not None
            and self._page_identity(self.page) == self._page_identity(target_page)
            and isinstance(self.cdp, DirectCDPSession)
            and self.cdp.connected
        ):
            try:
                await asyncio.wait_for(self.cdp.send("Page.stopLoading"), timeout=0.6)
            except Exception:
                pass
        try:
            await asyncio.wait_for(
                target_page.evaluate("() => { try { window.onbeforeunload = null; } catch(e) {} try { window.stop(); } catch(e) {} }"),
                timeout=0.5,
            )
        except Exception:
            pass

    async def _replace_tab_page(self, tidx: int, reason: str = "wedged") -> Optional[Page]:
        """Replaces a wedged/closed Page with a fresh context.new_page() and rebinds CDP screencast."""
        if not self.running or not self.context:
            return None
        old_page = self.tabs[tidx] if (0 <= tidx < len(self.tabs)) else self.page
        was_active = (
            (tidx == self.active_tab_idx)
            or (
                old_page is not None
                and self.page is not None
                and self._page_identity(old_page) == self._page_identity(self.page)
            )
        )
        old_key = self._page_identity(old_page) if old_page is not None else None
        migrated_hist = None
        if old_key is not None:
            self._err_aborted_counts.pop(old_key, None)
            self._wedged_pages.discard(old_key)
            migrated_hist = self._tab_histories.pop(old_key, None)

        prev_creating = getattr(self, "_creating_tab", False)
        self._creating_tab = True
        try:
            new_page = await asyncio.wait_for(self.context.new_page(), timeout=5.0)
        except Exception as exc:
            if old_key is not None and migrated_hist is not None:
                self._tab_histories[old_key] = migrated_hist
            logger.warning("Failed spawning replacement Page for tab [%d] (%s): %s", tidx, reason, exc)
            return None
        finally:
            self._creating_tab = prev_creating

        if migrated_hist is not None:
            self._tab_histories[self._page_identity(new_page)] = migrated_hist

        try:
            await new_page.set_viewport_size({"width": self.width, "height": self.height})
        except Exception:
            pass
        self._setup_page_listeners(new_page)

        if 0 <= tidx < len(self.tabs):
            self.tabs[tidx] = new_page
        else:
            self.tabs.append(new_page)
            tidx = len(self.tabs) - 1

        if was_active:
            self._tab_generation += 1
            self._switching_tab = True
            try:
                await self._stop_screencast()
                await self._detach_cdp()
                self.page = new_page
                self.active_tab_idx = tidx
                self._video_element_playing = False
                self._last_video_eval_time = 0.0
                self.last_frame_hash = None
                self.force_full_frame = True
                try:
                    await new_page.bring_to_front()
                except Exception:
                    pass
                if self.engine == "chromium":
                    await self._attach_direct_cdp(new_page)
                    await self._start_screencast()
                else:
                    self.cdp = None
            finally:
                self._switching_tab = False
            await self._apply_dark_mode(new_page)
        else:
            await self._apply_dark_mode(new_page)

        if old_page is not None and self._page_identity(old_page) != self._page_identity(new_page):
            await self._force_close_page(old_page)

        logger.warning("Replaced %s tab [%d] with a fresh browser Page", reason, tidx)
        return new_page

    async def _safe_page_goto(
        self,
        target_page: Page,
        url: str,
        *,
        wait_until: str = "commit",
        timeout: int = 15000,
        allow_replace: bool = True,
    ) -> Page:
        """Navigates target_page resiliently, recovering from closed pages or repeated net::ERR_ABORTED."""
        tidx = self._find_tab_index(target_page)
        if tidx < 0:
            tidx = self.active_tab_idx

        if self._is_page_closed(target_page):
            if not allow_replace:
                raise RuntimeError("Target page is closed")
            fresh = await self._replace_tab_page(tidx, reason="closed")
            if fresh is None:
                raise RuntimeError("Could not replace closed page")
            target_page = fresh

        for attempt in range(2):
            pkey = self._page_identity(target_page)
            try:
                await target_page.goto(url, wait_until=wait_until, timeout=timeout)
                self._err_aborted_counts.pop(pkey, None)
                self._wedged_pages.discard(pkey)
                return target_page
            except asyncio.CancelledError:
                raise
            except Exception as exc:
                err_s = str(exc)
                is_download_abort = (
                    (time.monotonic() - self._last_download_time) < 1.5
                    or bool(self.pending_downloads)
                    or "Download is starting" in err_s
                )
                if is_download_abort:
                    raise

                is_dead = (
                    self._is_page_closed(target_page)
                    or "Session with given id not found" in err_s
                    or "Target closed" in err_s
                    or "has been closed" in err_s
                )
                if is_dead and allow_replace:
                    cur_tidx = self._find_tab_index(target_page)
                    if cur_tidx < 0:
                        if not self.running or not self.tabs or self.page is None or self._page_identity(self.page) != pkey:
                            raise
                        cur_tidx = self.active_tab_idx
                    fresh = await self._replace_tab_page(cur_tidx, reason="closed/detached")
                    if fresh is None:
                        raise
                    await fresh.goto(url, wait_until=wait_until, timeout=timeout)
                    fkey = self._page_identity(fresh)
                    self._err_aborted_counts.pop(fkey, None)
                    self._wedged_pages.discard(fkey)
                    return fresh

                if "ERR_ABORTED" in err_s and allow_replace:
                    cnt = self._err_aborted_counts.get(pkey, 0) + 1
                    self._err_aborted_counts[pkey] = cnt
                    cur_tidx = self._find_tab_index(target_page)
                    if cur_tidx < 0:
                        if not self.running or not self.tabs or self.page is None or self._page_identity(self.page) != pkey:
                            raise
                        cur_tidx = self.active_tab_idx
                    if cnt >= 2:
                        self._wedged_pages.add(pkey)
                        logger.warning(
                            "Page.goto hit net::ERR_ABORTED %d times on tab [%d]; replacing wedged Page for %s",
                            cnt, cur_tidx, url,
                        )
                        fresh = await self._replace_tab_page(cur_tidx, reason="ERR_ABORTED wedged")
                        if fresh is None:
                            raise
                        await fresh.goto(url, wait_until=wait_until, timeout=timeout)
                        fkey = self._page_identity(fresh)
                        self._err_aborted_counts.pop(fkey, None)
                        self._wedged_pages.discard(fkey)
                        return fresh
                    if attempt == 0:
                        logger.info(
                            "Page.goto aborted once on tab [%d] (%s); stopping pending load and retrying",
                            cur_tidx, url,
                        )
                        await self._stop_page_loading(target_page)
                        continue
                raise
        return target_page

    def _schedule_popup_adopt_check(self) -> None:
        if not (self.running and self.use_rustwright and self.engine == "chromium"):
            return

        async def _poll() -> None:
            for delay in (0.10, 0.30):
                await asyncio.sleep(delay)
                if not self.running or getattr(self, "_creating_tab", False):
                    return
                await self._adopt_rustwright_popups()

        t = asyncio.create_task(_poll())
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
        page.on("dialog", self._on_dialog_event)
        page.on("filechooser", self._on_filechooser_event)
        page.on("download", self._on_download_event)
        page.on("framenavigated", self._on_main_frame_navigated)
        page.on("load", self._on_page_loaded)
        page.on("domcontentloaded", self._on_dom_content_loaded)
        page.on("close", self._on_page_closed)
        page.on("crash", self._on_page_crashed)

    def _detach_page_listeners(self, page: Any) -> None:
        if page is None:
            return
        for ev, cb in (
            ("dialog", self._on_dialog_event),
            ("filechooser", self._on_filechooser_event),
            ("download", self._on_download_event),
            ("framenavigated", self._on_main_frame_navigated),
            ("load", self._on_page_loaded),
            ("domcontentloaded", self._on_dom_content_loaded),
            ("close", self._on_page_closed),
            ("crash", self._on_page_crashed),
        ):
            try:
                page.remove_listener(ev, cb)
            except Exception:
                pass

    def _on_page_crashed(self, *args) -> None:
        crashed_page = args[0] if args else self.page
        crashed_url = ""
        try:
            crashed_url = crashed_page.url if crashed_page else ""
        except Exception:
            pass
        logger.error("Browser tab crashed (%s) — recovering tab without dropping Amiga session", crashed_url or "unknown")
        if not self.running or not self.context:
            return

        async def _recover_crashed_tab() -> None:
            if not self.running or not self.context:
                return
            found = self._find_tab_index(crashed_page)
            idx = found if found >= 0 else self.active_tab_idx
            try:
                new_page = await self._replace_tab_page(idx, reason="crashed")
                if new_page is not None and self.page is not None and self._page_identity(self.page) == self._page_identity(new_page):
                    try:
                        await new_page.goto(self.portal_url, wait_until="domcontentloaded", timeout=8000)
                    except Exception:
                        pass
                    self._send_progress(100, "Tab wiederhergestellt")
                    self._send_nav_update(self.portal_url)
                    self._trigger_full_refresh(True)
            except Exception as rec_exc:
                logger.warning("Tab crash recovery warning: %s", rec_exc)

        t = asyncio.create_task(_recover_crashed_tab())
        self.background_tasks.add(t)
        t.add_done_callback(self.background_tasks.discard)

    def _on_page_closed(self, *args) -> None:
        closed_page = args[0] if args else None
        if closed_page is None:
            return
        self._detach_page_listeners(closed_page)
        ckey = self._page_identity(closed_page)
        self._err_aborted_counts.pop(ckey, None)
        self._wedged_pages.discard(ckey)
        self._tab_histories.pop(ckey, None)
        if not self.tabs:
            return
        idx = self._find_tab_index(closed_page)
        if idx < 0:
            return
        self.tabs.pop(idx)
        was_active = (self._page_identity(closed_page) == self._page_identity(self.page)) if self.page else False
        if idx < self.active_tab_idx:
            self.active_tab_idx -= 1
        elif self.active_tab_idx >= len(self.tabs):
            self.active_tab_idx = max(0, len(self.tabs) - 1)
        if was_active:
            if self.running and self.tabs:
                self.page = self.tabs[self.active_tab_idx]
                self._send_tab_sync(
                    TAB_SYNC_CLOSE,
                    idx,
                    self.active_tab_idx,
                    make_active=True,
                    url=(self.page.url or ""),
                )
                t = asyncio.create_task(self._activate_tab(self.active_tab_idx))
                self.background_tasks.add(t)
                t.add_done_callback(self.background_tasks.discard)
            else:
                self.page = None
                self._send_tab_sync(TAB_SYNC_CLOSE, idx, 0, make_active=True)
        elif self.running:
            self._send_tab_sync(TAB_SYNC_CLOSE, idx, self.active_tab_idx, make_active=False)

    def _on_page_loaded(self, *args) -> None:
        if not self.running:
            return
        sender = args[0] if args else self.page
        if sender is not None and (
            self.web_zoom_pct != 100
            or self.web_font_size != 16
            or self.web_font_family != "sans-serif"
        ):
            t_sty = asyncio.create_task(self._apply_web_styling(sender))
            self.background_tasks.add(t_sty)
            t_sty.add_done_callback(self.background_tasks.discard)
        if self.page and self._page_identity(sender) != self._page_identity(self.page):
            tidx = self._find_tab_index(sender)
            if tidx >= 0 and getattr(sender, "url", None):
                self._send_nav_update(sender.url, tab_idx=tidx)
            return
        self._send_progress(100, "Fertig")
        self._on_page_updated()
        if self.page and self.page.url:
            self._send_nav_update(self.page.url)
        self._schedule_storage_save(delay=2.0)

    def _on_dom_content_loaded(self, *args) -> None:
        if not self.running:
            return
        sender = args[0] if args else self.page
        if sender is not None and (
            self.web_zoom_pct != 100
            or self.web_font_size != 16
            or self.web_font_family != "sans-serif"
        ):
            t_sty = asyncio.create_task(self._apply_web_styling(sender))
            self.background_tasks.add(t_sty)
            t_sty.add_done_callback(self.background_tasks.discard)
        if self.page and self._page_identity(sender) != self._page_identity(self.page):
            tidx = self._find_tab_index(sender)
            if tidx >= 0 and getattr(sender, "url", None):
                self._send_nav_update(sender.url, tab_idx=tidx)
            return
        self._send_progress(75, "Erstelle Seite...")
        self._on_page_updated()
        if self.page and self.page.url:
            self._send_nav_update(self.page.url)

    def _on_main_frame_navigated(self, frame: Frame) -> None:
        if not self.running or frame is None:
            return
        # Ignore sub-frames (iframes)
        try:
            if getattr(frame, "parent_frame", None) is not None:
                return
        except Exception:
            pass

        tab_page: Optional[Page] = None
        try:
            fp = getattr(frame, "page", None)
            if fp is not None:
                tidx_fp = self._find_tab_index(fp)
                if tidx_fp >= 0:
                    tab_page = self.tabs[tidx_fp]
                elif self.page is not None and self._page_identity(fp) == self._page_identity(self.page):
                    tab_page = self.page
        except Exception:
            tab_page = None

        if tab_page is None:
            for p in list(self.tabs):
                try:
                    if p.main_frame == frame:
                        tab_page = p
                        break
                except Exception:
                    continue
        if tab_page is None and self.page is not None:
            try:
                if self.page.main_frame == frame:
                    tab_page = self.page
            except Exception:
                pass
        if tab_page is None:
            return
        try:
            if tab_page.main_frame != frame:
                return
        except Exception:
            pass

        url = (frame.url or "").strip()
        if not url or url.startswith("about:") or url.startswith("javascript:"):
            return

        self._record_tab_history(tab_page, url)

        is_active = (
            self.page is not None
            and self._page_identity(tab_page) == self._page_identity(self.page)
        )
        if not is_active:
            tidx = self._find_tab_index(tab_page)
            if tidx >= 0:
                self._send_nav_update(url, tab_idx=tidx)
            return

        now = time.monotonic()
        self.force_full_frame = True
        self.active_until = now + 2.0
        self.force_frame_event.set()
        self._schedule_delayed_frames()
        self._send_nav_update(url)
        self._schedule_storage_save(delay=3.0)

        if self.dark_mode == 1 and self.page:
            t = asyncio.create_task(self._apply_dark_mode(self.page))
            self.background_tasks.add(t)
            t.add_done_callback(self.background_tasks.discard)

    def _on_popup_page(self, new_page: Page) -> None:
        """Adopts target='_blank' links and window.open() popups as real tabs while preserving window.opener."""
        if not self.running or getattr(self, "_creating_tab", False):
            return
        if not self.page:
            return
        pkey = self._page_identity(new_page)
        if pkey == self._page_identity(self.page) or self._find_tab_index(new_page) >= 0:
            return
        if pkey in self._adopting_popups:
            return
        self._adopting_popups.add(pkey)

        async def _adopt_popup():
            try:
                if not self.running or getattr(self, "_creating_tab", False):
                    return
                if not self.page or self._find_tab_index(new_page) >= 0:
                    return
                # Wait briefly for initial navigation URL if blank (keep new_page alive for window.opener / document.write popups)
                popup_url = (new_page.url or "").strip()
                if not popup_url or popup_url == "about:blank":
                    for _ in range(10):
                        await asyncio.sleep(0.05)
                        if not self.running or getattr(self, "_creating_tab", False):
                            return
                        if self._find_tab_index(new_page) >= 0:
                            return
                        popup_url = (new_page.url or "").strip()
                        if popup_url and popup_url != "about:blank":
                            break
                if not self.running or getattr(self, "_creating_tab", False):
                    return
                if self._find_tab_index(new_page) >= 0:
                    return
                if popup_url.startswith("chrome:") or popup_url.startswith("resource:"):
                    try:
                        await new_page.close()
                    except Exception:
                        pass
                    return

                # Fallback if max client tabs (VB_MAX_TABS=16) reached
                if len(self.tabs) >= VB_MAX_TABS:
                    try:
                        await new_page.close()
                    except Exception:
                        pass
                    if popup_url and not popup_url.startswith("about:") and self.page and popup_url != self.page.url:
                        logger.info("Max tabs reached (%d); opening popup in current tab: %s", VB_MAX_TABS, popup_url)
                        await self._handle_navigation(popup_url)
                    return

                old_page = self.page
                self.tabs.append(new_page)
                new_idx = len(self.tabs) - 1
                self._setup_page_listeners(new_page)
                tab_url = "" if (not popup_url or popup_url.startswith("about:")) else popup_url
                if tab_url:
                    self._record_tab_history(new_page, tab_url)
                title = ""
                try:
                    title = (await asyncio.wait_for(new_page.title(), timeout=0.3) or "").strip()
                except Exception:
                    title = ""
                logger.info("Adopted popup/new-tab [%d]: %s", new_idx, tab_url or "Popup")
                self._send_tab_sync(TAB_SYNC_ADD, new_idx, new_idx, make_active=True, url=tab_url, title=title)
                await self._activate_tab(new_idx, old_page=old_page)
            except Exception as exc:
                logger.debug("Popup adoption error: %s", exc)
            finally:
                self._adopting_popups.discard(pkey)

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
                    os.path.expanduser("~/.config/vortexremotebrowser/twitch_cookies.json"),
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
                        os.path.expanduser("~/.config/vortexremotebrowser/twitch_token.txt"),
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

            # Pre-load Discord & extra site session cookies / Discord auth token from user profile dir
            try:
                cfg_dir = get_vortex_config_dir()
                for extra_json_name in ("discord_cookies.json", "extra_cookies.json"):
                    extra_candidates = list(dict.fromkeys([
                        os.path.join(cfg_dir, extra_json_name),
                        os.path.expanduser(f"~/.config/vortexremotebrowser/{extra_json_name}"),
                    ]))
                    for j_path in extra_candidates:
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
                                        "httpOnly": c.get("httpOnly", False),
                                    }
                                    if "sameSite" in c and c["sameSite"] in ("Strict", "Lax", "None"):
                                        ck["sameSite"] = c["sameSite"]
                                    clean_list.append(ck)
                                if clean_list:
                                    await self.context.add_cookies(clean_list)
                                    logger.info("Pre-loaded %d session cookies from %s", len(clean_list), j_path)
                                    break
                            except Exception as _dje:
                                logger.debug("Extra cookie load error (%s): %s", j_path, _dje)

                discord_tok_candidates = list(dict.fromkeys([
                    os.path.join(cfg_dir, "discord_token.txt"),
                    os.path.expanduser("~/.config/vortexremotebrowser/discord_token.txt"),
                ]))
                self._discord_preload_js = ""
                for dt_path in discord_tok_candidates:
                    if os.path.exists(dt_path):
                        with open(dt_path, "r", encoding="utf-8") as f:
                            raw_dt = f.read().strip()
                        if raw_dt:
                            if ":" in raw_dt and raw_dt.split(":", 1)[0].isdigit():
                                d_uid, d_tok = raw_dt.split(":", 1)
                            else:
                                d_uid, d_tok = "", raw_dt
                            d_tok = d_tok.strip().strip('"')
                            if d_tok:
                                self._discord_preload_js = (
                                    "try {\n"
                                    "  const h = (location && location.hostname) ? location.hostname.toLowerCase() : '';\n"
                                    "  if (h === 'discord.com' || h.endsWith('.discord.com') || h === 'discordapp.com' || h.endsWith('.discordapp.com')) {\n"
                                    f"    const tok = {json.dumps(d_tok)};\n"
                                    f"    const uid = {json.dumps(d_uid)};\n"
                                    "    const applyLs = (ls) => {\n"
                                    "      if (!ls) return;\n"
                                    "      ls.setItem('token', JSON.stringify(tok));\n"
                                    "      if (uid) {\n"
                                    "        ls.setItem('user_id_cache', JSON.stringify(uid));\n"
                                    "        ls.setItem('tokens', JSON.stringify({ [uid]: tok }));\n"
                                    "      }\n"
                                    "    };\n"
                                    "    try { applyLs(window.localStorage); } catch (e) {}\n"
                                    "    try {\n"
                                    "      const ifr = document.createElement('iframe');\n"
                                    "      ifr.style.display = 'none';\n"
                                    "      (document.documentElement || document.head || document.body).appendChild(ifr);\n"
                                    "      applyLs(ifr.contentWindow.localStorage);\n"
                                    "      ifr.remove();\n"
                                    "    } catch (e) {}\n"
                                    "  }\n"
                                    "} catch (e) {}\n"
                                )
                                logger.info("Pre-loaded Discord session token init script from %s", dt_path)
                                break
            except Exception as _de:
                self._discord_preload_js = ""
                logger.debug("Discord session preload error: %s", _de)

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
                """ + getattr(self, "_discord_preload_js", "") + """

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
                        },

                        update: function(x, y) {
                            if (this.startX < 0 || this.startY < 0) return;
                            let dist = Math.hypot(x - this.startX, y - this.startY);
                            if (dist < 4) return;
                            this.isDragging = true;

                            let el = document.elementFromPoint(this.startX, this.startY);
                            if (el && (el.tagName === 'INPUT' || el.tagName === 'TEXTAREA' || el.isContentEditable || el.tagName === 'IFRAME' || el.shadowRoot)) return;

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
                            let doc = document, win = window, el = document.activeElement;
                            for (let i = 0; i < 8 && el; i++) {
                                if (el.tagName === 'IFRAME') {
                                    try {
                                        const cd = el.contentDocument || (el.contentWindow && el.contentWindow.document);
                                        if (!cd) break;
                                        doc = cd;
                                        win = el.contentWindow || doc.defaultView || win;
                                        el = doc.activeElement;
                                        continue;
                                    } catch (e) { break; }
                                }
                                if (el.shadowRoot && el.shadowRoot.activeElement) {
                                    el = el.shadowRoot.activeElement;
                                    continue;
                                }
                                break;
                            }
                            let s = (win && win.getSelection) ? win.getSelection().toString() : (doc.getSelection ? doc.getSelection().toString() : '');
                            if (s && s.length > 0) return { dragged: dragged, text: s };

                            if (el && (el.tagName === 'TEXTAREA' || el.tagName === 'INPUT')) {
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
            try:
                if self.engine == "firefox":
                    eng_label = "Firefox (Playwright)"
                elif self.use_rustwright:
                    eng_label = "Chromium (Rustwright DirectCDP)"
                else:
                    eng_label = "Chromium (Playwright)"
                eng_bytes = eng_label.encode("utf-8", errors="replace")[:63].ljust(64, b"\x00")
                await self.send_packet(STRUCT_MSG_SERVER_INFO.pack(MSG_SERVER_INFO, eng_bytes))
            except Exception as _se:
                logger.debug("send MSG_SERVER_INFO warning: %s", _se)
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

            live_pages = [p for p in list(getattr(self.context, "pages", []) or []) if not self._is_page_closed(p)]
            if self.is_persistent_context and live_pages:
                self.page = live_pages[0]
                for stray in live_pages[1:]:
                    await self._force_close_page(stray)
                try:
                    await self.page.set_viewport_size({"width": self.width, "height": self.height})
                except Exception:
                    pass
            else:
                self.page = await self.context.new_page()
            try:
                await self.page.bring_to_front()
            except Exception:
                pass
            self.tabs = [self.page]
            self.active_tab_idx = 0
            self.context.on("page", self._on_popup_page)
            if self.engine == "chromium":
                await self._attach_direct_cdp(self.page)
                await self._start_browser_download_monitor()
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
            has_existing_page = bool(curr_url and not is_startpage_url(curr_url, self.portal_url))

            initial_url = self.start_url if self.start_url else self.portal_url
            logger.info("Initial viewport ready (%dx%d, has_existing_page=%s) — waiting briefly for client tab sync", self.width, self.height, has_existing_page)
            async def _init_nav():
                # Give the Amiga client a short window to send its saved tabs / start URL
                # so the static start page never overwrites restored tabs on startup.
                if not self.start_url:
                    try:
                        await asyncio.wait_for(self._client_nav_received.wait(), timeout=2.0)
                        logger.info("Client sent initial tab/URL sync; skipping default start page load")
                        return
                    except asyncio.TimeoutError:
                        pass
                    except asyncio.CancelledError:
                        return
                if not self.running or self._client_nav_received.is_set():
                    return
                if not has_existing_page and self.page is not None:
                    try:
                        await self._safe_page_goto(self.page, initial_url, wait_until="domcontentloaded", timeout=12000)
                    except asyncio.CancelledError:
                        return
                    except Exception as nav_exc:
                        logger.warning("Initial navigation warning (%s), falling back to start page", nav_exc)
                        if initial_url != self.portal_url and self.page is not None:
                            try:
                                await self._safe_page_goto(self.page, self.portal_url, wait_until="domcontentloaded", timeout=8000)
                            except Exception:
                                pass
                elif self.page is not None:
                    self._record_tab_history(self.page, self.page.url)
                    self._send_nav_update(self.page.url)
                self.active_until = time.monotonic() + 3.0
                self._trigger_full_refresh(True)
                self._schedule_delayed_frames()

            t_init = asyncio.create_task(_init_nav())
            self._nav_task = t_init
            self.background_tasks.add(t_init)
            t_init.add_done_callback(self.background_tasks.discard)

            done, pending = await asyncio.wait(
                tasks,
                return_when=asyncio.FIRST_COMPLETED,
            )
            self.running = False
            for t in pending:
                t.cancel()
            if pending:
                try:
                    await asyncio.wait_for(
                        asyncio.gather(*pending, return_exceptions=True),
                        timeout=2.0,
                    )
                except Exception:
                    pass
        except Exception as exc:
            logger.error("Session error: %s", exc)
        finally:
            try:
                self.running = False
                # Close client TCP transport immediately so socket never lingers in CLOSE-WAIT
                try:
                    self.writer.close()
                    await asyncio.wait_for(self.writer.wait_closed(), timeout=1.0)
                except Exception:
                    pass
                if self._h264_encoder is not None:
                    self._h264_encoder.stop()
                    self._h264_encoder = None
                try:
                    await asyncio.wait_for(self._stop_screencast(), timeout=1.5)
                except Exception:
                    pass
                try:
                    await asyncio.wait_for(self._detach_cdp(), timeout=1.5)
                except Exception:
                    pass
                try:
                    await asyncio.wait_for(self._stop_browser_download_monitor(), timeout=1.5)
                except Exception:
                    pass
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
                for ev in self._active_download_cancels.values():
                    ev.set()
                self._active_download_cancels.clear()
                for entry in self.pending_uploads.values():
                    if len(entry) >= 2 and entry[1] and not entry[1].done():
                        entry[1].cancel()
                self.pending_uploads.clear()
                self._cancel_nav_task()
                for pt in list(self._tab_nav_tasks.values()):
                    if not pt.done():
                        pt.cancel()
                self._tab_nav_tasks.clear()
                if self._pending_storage_save_task and not self._pending_storage_save_task.done():
                    self._pending_storage_save_task.cancel()
                self._pending_storage_save_task = None
                bg_list = list(self.background_tasks)
                for t in bg_list:
                    if not t.done():
                        t.cancel()
                if bg_list:
                    try:
                        await asyncio.wait_for(
                            asyncio.gather(*bg_list, return_exceptions=True),
                            timeout=1.0,
                        )
                    except Exception:
                        pass
                self.background_tasks.clear()
                if self._context_state.get("active_session") is self:
                    self._context_state["active_session"] = None
                if self.is_persistent_context:
                    try:
                        await asyncio.wait_for(self.save_storage_state(force=True), timeout=3.0)
                        self._storage_saved_on_exit = True
                    except Exception:
                        pass
                    if not self.close_browser_on_exit:
                        for tab in list(self.tabs[1:]):
                            await self._force_close_page(tab)
                        if self.context:
                            try:
                                tab0_id = self._page_identity(self.tabs[0]) if self.tabs else None
                                for stray in list(getattr(self.context, "pages", []) or []):
                                    if tab0_id is None or self._page_identity(stray) != tab0_id:
                                        await self._force_close_page(stray)
                            except Exception:
                                pass
                        if self.tabs:
                            tab0 = self.tabs[0]
                            tab0_clean = False
                            t0_key = self._page_identity(tab0)
                            if not self._is_page_closed(tab0) and t0_key not in self._wedged_pages:
                                try:
                                    await self._stop_page_loading(tab0)
                                    await asyncio.wait_for(
                                        tab0.goto("about:blank", wait_until="commit", timeout=3000),
                                        timeout=3.5,
                                    )
                                    tab0_clean = True
                                except Exception:
                                    tab0_clean = False
                            if not tab0_clean and self.context:
                                try:
                                    logger.info("Teardown: replacing wedged/unresettable tab[0] with fresh blank Page")
                                    fresh0 = await asyncio.wait_for(self.context.new_page(), timeout=3.0)
                                    await self._force_close_page(tab0)
                                    self.tabs[0] = fresh0
                                except Exception:
                                    pass
                elif self.context:
                    try:
                        await asyncio.wait_for(self.context.close(), timeout=3.0)
                    except Exception:
                        pass
                self.tabs.clear()
                self.page = None
                self.context = None
                self.browser = None
                self.cdp = None
                self._tab_histories.clear()
                self._err_aborted_counts.clear()
                self._wedged_pages.clear()
                self._cdp_downloads.clear()
                self._cdp_handled_guids.clear()
                self._cdp_handled_keys.clear()
                self._adopting_popups.clear()
                self._server_clipboard = ""
                self.prev_raw_buf = None
                self.last_frame_hash = None
                self._argb_buf = None
                self._rgb565_buf = None
                self._yuv_buf = None
                self._rect_buf = None
                self._pkt_buf = None
                self.screencast_queue = None
                self._screencast_handler = None
                if getattr(self, "_encode_executor", None) is not None:
                    enc_exec = self._encode_executor
                    self._encode_executor = None
                    if _FAST_PIXCONV is not None and hasattr(_FAST_PIXCONV, "fast_pixconv_release_tls"):
                        try:
                            fut = enc_exec.submit(_FAST_PIXCONV.fast_pixconv_release_tls)
                            await asyncio.wait_for(asyncio.wrap_future(fut), timeout=0.5)
                        except Exception:
                            pass
                    try:
                        enc_exec.shutdown(wait=False, cancel_futures=True)
                    except TypeError:
                        try:
                            enc_exec.shutdown(wait=False)
                        except Exception:
                            pass
                    except Exception:
                        pass
                _reclaim_python_memory()
                logger.info("AmigaOS 4 client session closed (%s)", peer)
            finally:
                self._closed_event.set()

    async def _safe_eval(self, script: str, arg: Any = None, timeout: float = 0.6, default: Any = None) -> Any:
        if not self.page or not self.running:
            return default
        if isinstance(self.cdp, DirectCDPSession) and self.cdp.connected:
            try:
                s = script.strip()
                if arg is not None:
                    expr = f"({s})({json.dumps(arg)})"
                elif s.startswith("()"):
                    expr = f"({s})()"
                else:
                    expr = s
                res = await asyncio.wait_for(
                    self.cdp.send(
                        "Runtime.evaluate",
                        {"expression": expr, "returnByValue": True, "awaitPromise": False},
                    ),
                    timeout=timeout,
                )
                if isinstance(res, dict) and "exceptionDetails" not in res:
                    val_obj = res.get("result")
                    if isinstance(val_obj, dict) and "value" in val_obj:
                        return val_obj["value"]
                    return default
            except Exception:
                return default
        try:
            coro = self.page.evaluate(script) if arg is None else self.page.evaluate(script, arg)
            return await asyncio.wait_for(coro, timeout=timeout)
        except Exception:
            return default

    async def _cdp_mouse(
        self,
        cdp_type: str,
        x: int,
        y: int,
        button: str = "none",
        click_count: int = 0,
        delta_y: float = 0.0,
        timeout: float = 0.25,
        buttons: Optional[int] = None,
    ) -> bool:
        """Dispatch mouse events directly via DirectCDP WebSocket, bypassing Rustwright PyO3/Tokio locks."""
        if not (isinstance(self.cdp, DirectCDPSession) and self.cdp.connected):
            return False
        btn_mask_map = {"left": 1, "right": 2, "middle": 4}
        if buttons is None:
            if cdp_type == "mousePressed" or (cdp_type == "mouseMoved" and button != "none"):
                buttons = btn_mask_map.get(button, 0)
            else:
                buttons = 0
        params: Dict[str, Any] = {
            "type": cdp_type,
            "x": float(x),
            "y": float(y),
            "button": button,
            "buttons": int(buttons),
        }
        if click_count > 0:
            params["clickCount"] = int(click_count)
        if cdp_type == "mouseWheel":
            params["deltaX"] = 0.0
            params["deltaY"] = float(delta_y)
        try:
            await asyncio.wait_for(self.cdp.send("Input.dispatchMouseEvent", params), timeout=timeout)
            return True
        except Exception:
            return False

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
    # Real-Time Audio Capture & Streaming Loop (PipeWire / macOS / Windows WASAPI -> Amiga AHI)
    # ------------------------------------------------------------------------
    async def _send_pcm_chunk(self, chunk: bytes, state: Dict[str, int]) -> None:
        if _FAST_PIXCONV is not None:
            if _FAST_PIXCONV.pcm_s16be_is_silent(chunk, len(chunk), 10) == 1:
                state["silent_streak"] += 1
                return
        else:
            if chunk == b"\x00" * len(chunk):
                state["silent_streak"] += 1
                return
        state["silent_streak"] = 0

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
        state["chunks_sent"] += 1
        if state["chunks_sent"] == 1 or state["chunks_sent"] % 50 == 0:
            logger.info("Streamed %d audio chunks to Amiga AHI (latest=%dB)", state["chunks_sent"], len(payload))

    async def _audio_stream_windows_wasapi(self) -> None:
        """Captures native Windows WASAPI loopback via PyAudioWPatch (zero virtual audio driver required)."""
        import array
        import pyaudiowpatch as pyaudio  # type: ignore

        loop = asyncio.get_running_loop()
        pcm_q: asyncio.Queue = asyncio.Queue(maxsize=32)
        stop_flag = threading.Event()
        resampler: Optional[subprocess.Popen] = None

        def _wasapi_worker() -> None:
            nonlocal resampler
            pa = None
            stream = None
            try:
                pa = pyaudio.PyAudio()
                dev = pa.get_default_wasapi_loopback()
                if not dev:
                    return
                src_rate = int(round(float(dev.get("defaultSampleRate", AUDIO_SAMPLE_RATE))))
                src_ch = max(1, int(dev.get("maxInputChannels", AUDIO_CHANNELS)))
                dev_name = str(dev.get("name", "WASAPI Loopback"))
                need_resample = (src_rate != AUDIO_SAMPLE_RATE or src_ch != AUDIO_CHANNELS)
                frames_per_buf = max(512, int(src_rate * 0.04))

                if need_resample:
                    resampler = subprocess.Popen(
                        [
                            "ffmpeg", "-loglevel", "error",
                            "-f", "s16le", "-ar", str(src_rate), "-ac", str(src_ch), "-i", "-",
                            "-ar", str(AUDIO_SAMPLE_RATE), "-ac", str(AUDIO_CHANNELS),
                            "-f", "s16be", "-",
                        ],
                        stdin=subprocess.PIPE,
                        stdout=subprocess.PIPE,
                        stderr=subprocess.DEVNULL,
                        bufsize=0,
                        creationflags=_SUBPROC_NO_WINDOW,
                    )

                    def _drain_resampled() -> None:
                        buf = bytearray()
                        try:
                            while not stop_flag.is_set() and resampler and resampler.stdout:
                                data = resampler.stdout.read(AUDIO_CHUNK_BYTES)
                                if not data:
                                    break
                                buf.extend(data)
                                while len(buf) >= AUDIO_CHUNK_BYTES:
                                    pkt = bytes(buf[:AUDIO_CHUNK_BYTES])
                                    del buf[:AUDIO_CHUNK_BYTES]
                                    def _put(b=pkt):
                                        if pcm_q.full():
                                            try:
                                                pcm_q.get_nowait()
                                            except asyncio.QueueEmpty:
                                                pass
                                        try:
                                            pcm_q.put_nowait(b)
                                        except asyncio.QueueFull:
                                            pass
                                    loop.call_soon_threadsafe(_put)
                        except Exception:
                            pass

                    threading.Thread(target=_drain_resampled, daemon=True, name="vb_wasapi_resample").start()

                logger.info(
                    "Windows WASAPI loopback active ('%s', %dHz %dch -> %dHz S16BE Stereo)",
                    dev_name, src_rate, src_ch, AUDIO_SAMPLE_RATE,
                )
                stream = pa.open(
                    format=pyaudio.paInt16,
                    channels=src_ch,
                    rate=src_rate,
                    input=True,
                    input_device_index=int(dev["index"]),
                    frames_per_buffer=frames_per_buf,
                )
                direct_buf = bytearray()
                while not stop_flag.is_set() and self.running:
                    raw = stream.read(frames_per_buf, exception_on_overflow=False)
                    if not raw:
                        continue
                    if need_resample and resampler and resampler.stdin:
                        try:
                            resampler.stdin.write(raw)
                            resampler.stdin.flush()
                        except Exception:
                            break
                    else:
                        # Already 44100Hz stereo S16LE -> fast in-place byteswap to S16BE
                        arr = array.array("h")
                        arr.frombytes(raw)
                        arr.byteswap()
                        direct_buf.extend(arr.tobytes())
                        while len(direct_buf) >= AUDIO_CHUNK_BYTES:
                            pkt = bytes(direct_buf[:AUDIO_CHUNK_BYTES])
                            del direct_buf[:AUDIO_CHUNK_BYTES]
                            def _put_direct(b=pkt):
                                if pcm_q.full():
                                    try:
                                        pcm_q.get_nowait()
                                    except asyncio.QueueEmpty:
                                        pass
                                try:
                                    pcm_q.put_nowait(b)
                                except asyncio.QueueFull:
                                    pass
                            loop.call_soon_threadsafe(_put_direct)
            except Exception as exc:
                logger.debug("Windows WASAPI loopback worker ended: %s", exc)
            finally:
                if stream is not None:
                    try:
                        stream.stop_stream()
                        stream.close()
                    except Exception:
                        pass
                if pa is not None:
                    try:
                        pa.terminate()
                    except Exception:
                        pass
                if resampler is not None:
                    try:
                        if resampler.stdin:
                            resampler.stdin.close()
                        resampler.terminate()
                    except Exception:
                        pass
                try:
                    loop.call_soon_threadsafe(pcm_q.put_nowait, None)
                except Exception:
                    pass

        worker = threading.Thread(target=_wasapi_worker, daemon=True, name="vb_wasapi_cap")
        worker.start()
        state = {"chunks_sent": 0, "silent_streak": 0}
        try:
            while self.running:
                chunk = await pcm_q.get()
                if chunk is None or not self.running:
                    break
                await self._send_pcm_chunk(chunk, state)
        finally:
            stop_flag.set()

    async def _audio_stream_tabcapture(self) -> bool:
        """Streams silent browser-internal MV3 TabCapture PCM directly to Amiga AHI.
        Returns True if the extension connected and streamed, False if fallback is needed."""
        bridge = self.tab_audio_bridge
        if bridge is None:
            return False
        pcm_q = bridge.attach_consumer(maxsize=24)
        try:
            if not bridge.connected_event.is_set():
                try:
                    await asyncio.wait_for(bridge.connected_event.wait(), timeout=4.5)
                except asyncio.TimeoutError:
                    logger.warning(
                        "SilentTabAudioBridge did not connect within 4.5s (branded Chrome/Edge may block --load-extension). "
                        "Install Playwright Chromium / Ungoogled-Chromium for silent Windows-speaker-free audio; falling back to OS audio capture."
                    )
                    return False
            if not self.running:
                return True
            logger.info(
                "Silent Browser TabCapture audio active (ws://127.0.0.1:%d -> %dHz S16BE Stereo; host speakers muted)",
                bridge.ws_port,
                AUDIO_SAMPLE_RATE,
            )
            state = {"chunks_sent": 0, "silent_streak": 0}
            while self.running:
                try:
                    chunk = await asyncio.wait_for(pcm_q.get(), timeout=1.0)
                except asyncio.TimeoutError:
                    continue
                if not self.running:
                    break
                await self._send_pcm_chunk(chunk, state)
            return True
        finally:
            bridge.detach_consumer(pcm_q)

    async def _audio_stream_loop(self) -> None:
        proc: Optional[asyncio.subprocess.Process] = None
        try:
            if self.tab_audio_bridge is not None:
                if await self._audio_stream_tabcapture():
                    return
                if not self.running:
                    return
            unmute_all_browser_sink_inputs()
            if _IS_WINDOWS:
                win_backend, win_dev = _select_windows_audio_backend(self.audio_device)
                if win_backend == "wasapi":
                    await self._audio_stream_windows_wasapi()
                    return
                if win_backend == "dshow" and win_dev:
                    proc = await asyncio.create_subprocess_exec(
                        "ffmpeg", "-loglevel", "error",
                        "-f", "dshow",
                        "-i", f"audio={win_dev}",
                        "-ar", str(AUDIO_SAMPLE_RATE),
                        "-ac", str(AUDIO_CHANNELS),
                        "-f", "s16be",
                        "-",
                        stdout=asyncio.subprocess.PIPE,
                        stderr=asyncio.subprocess.DEVNULL,
                        creationflags=_SUBPROC_NO_WINDOW,
                    )
                    logger.info(
                        "Windows DirectShow audio capture active ('%s' -> %dHz S16BE Stereo)",
                        win_dev,
                        AUDIO_SAMPLE_RATE,
                    )
                else:
                    return
            elif platform.system() == "Darwin":
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
            state = {"chunks_sent": 0, "silent_streak": 0}
            loop_iter = 0
            audio_loop = asyncio.get_running_loop()
            while self.running and proc.stdout:
                chunk = await proc.stdout.readexactly(AUDIO_CHUNK_BYTES)
                if not self.running:
                    break

                loop_iter += 1
                if loop_iter == 1 or (state["silent_streak"] >= 25 and loop_iter % 120 == 1):
                    await audio_loop.run_in_executor(None, unmute_all_browser_sink_inputs)

                await self._send_pcm_chunk(chunk, state)
        except asyncio.CancelledError:
            pass
        except asyncio.IncompleteReadError:
            pass
        except Exception as exc:
            logger.debug("Audio capture loop ended: %s", exc)
        finally:
            if proc is not None:
                try:
                    if proc.returncode is None:
                        proc.terminate()
                        await asyncio.wait_for(proc.wait(), timeout=1.0)
                except Exception:
                    try:
                        if proc.returncode is None:
                            proc.kill()
                        await asyncio.wait_for(proc.wait(), timeout=1.0)
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
        cur_url = self._cached_page_url if (not use_mjpeg and self.page) else ""
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
            if self.pending_downloads and any(not f.done() for f in self.pending_downloads.values()):
                await asyncio.sleep(0.04)
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
            cur_url = self._cached_page_url if self.page else ""
            is_video_site = _is_video_platform_url(cur_url)

            # 3. Query HTML5 <video> element playback state asynchronously every 0.6 seconds (non-overlapping)
            if self.page and not self._switching_tab and not self._video_eval_in_flight and (now_tick - self._last_video_eval_time) >= 0.6:
                self._last_video_eval_time = now_tick
                self._video_eval_in_flight = True
                target_page = self.page
                eval_gen = self._tab_generation
                is_vid_site = is_video_site
                async def _eval_video(tp=target_page, eg=eval_gen, vs=is_vid_site):
                    try:
                        if (
                            not tp
                            or not self.running
                            or self._switching_tab
                            or self._tab_generation != eg
                            or self.page is not tp
                        ):
                            return
                        check_js = """(isVideoSite) => {
                            try {
                                const vw = window.innerWidth || 1280;
                                const vh = window.innerHeight || 720;
                                const checkRoot = (root) => {
                                    if (!root || !root.querySelectorAll) return false;
                                    for (const v of root.querySelectorAll('video')) {
                                        if (!v.paused && !v.ended && v.readyState >= 2) {
                                            const r = v.getBoundingClientRect ? v.getBoundingClientRect() : null;
                                            if (!r) {
                                                if (isVideoSite) return true;
                                                continue;
                                            }
                                            if (isVideoSite) {
                                                if (r.width >= 32 && r.height >= 32) return true;
                                            } else {
                                                const visible = (r.bottom > 0 && r.top < vh && r.right > 0 && r.left < vw);
                                                const hasAudio = (!v.muted && v.volume > 0);
                                                if (visible && ((hasAudio && r.width >= 200 && r.height >= 120) || (r.width >= 640 && r.height >= 360))) {
                                                    return true;
                                                }
                                            }
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
                        if isinstance(self.cdp, DirectCDPSession) and self.cdp.connected:
                            is_playing = bool(await self._safe_eval(check_js, vs, timeout=0.35, default=False))
                        else:
                            max_frames = 6 if vs else 3
                            frames = list(getattr(tp, "frames", None) or [])[:max_frames]
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
                                    res = await asyncio.wait_for(frame.evaluate(check_js, vs), timeout=0.35)
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
                    finally:
                        self._video_eval_in_flight = False
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
                    try:
                        await self._set_screencast_quality(self.video_jpeg_quality)
                    except Exception:
                        pass
            elif self.is_video_mode:
                # Video ended/paused; wait 3.0s of static/quiet before restoring crisp high quality (e.g. q80)
                if (now_tick - self._last_video_state_time) >= 3.0:
                    self.is_video_mode = False
                    logger.info("Video playback ended/idle -> Restoring standard browsing mode")
                    try:
                        await self._set_screencast_quality(self.base_jpeg_quality)
                    except Exception:
                        pass
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
            try:
                await self._capture_and_send_frame(force=forced)
            except Exception as frame_exc:
                logger.debug("Frame capture warning (session kept alive): %s", frame_exc)
                await asyncio.sleep(0.05)

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
                                    if (!foundA && el.closest) {
                                        const ytCard = el.closest('ytd-thumbnail, yt-thumbnail-view-model, ytd-rich-item-renderer, ytd-video-renderer, ytd-compact-video-renderer, ytd-grid-video-renderer, ytd-reel-item-renderer, yt-lockup-view-model, ytm-video-with-context-renderer, ytm-media-item');
                                        if (ytCard && ytCard.querySelector) {
                                            const ya = ytCard.querySelector('a#thumbnail[href*="/watch"], a#thumbnail[href*="/shorts/"], a#video-title-link[href], a[href*="/watch?v="], a[href*="/shorts/"]');
                                            if (ya) foundA = ya;
                                        }
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
                            let isEditable = false;
                            try {
                                let targetEl = document.elementFromPoint(x, y);
                                let targetDoc = document;
                                let hostFrame = null;
                                let fx = x, fy = y;
                                for (let i = 0; i < 6 && targetEl; i++) {
                                    if (targetEl.shadowRoot && targetEl.shadowRoot.elementFromPoint) {
                                        const inner = targetEl.shadowRoot.elementFromPoint(fx, fy);
                                        if (inner && inner !== targetEl) { targetEl = inner; continue; }
                                    }
                                    if (targetEl.tagName === 'IFRAME') {
                                        const cd = targetEl.contentDocument || (targetEl.contentWindow && targetEl.contentWindow.document);
                                        if (!cd) break;
                                        const r = targetEl.getBoundingClientRect();
                                        fx -= r.left; fy -= r.top;
                                        hostFrame = targetEl;
                                        targetDoc = cd;
                                        targetEl = cd.elementFromPoint(fx, fy);
                                        continue;
                                    }
                                    break;
                                }
                                if (targetEl) {
                                    const tag = (targetEl.tagName || '').toUpperCase();
                                    if (tag === 'INPUT' || tag === 'TEXTAREA' || targetEl.isContentEditable || (targetDoc && targetDoc.designMode === 'on')) {
                                        isEditable = true;
                                        const needsFocus = (hostFrame && document.activeElement !== hostFrame) || (targetDoc.activeElement !== targetEl);
                                        if (needsFocus) {
                                            if (hostFrame && typeof hostFrame.focus === 'function') {
                                                hostFrame.focus({ preventScroll: true });
                                            }
                                            if (typeof targetEl.focus === 'function') {
                                                targetEl.focus({ preventScroll: true });
                                            }
                                            if ((targetEl.isContentEditable || (targetDoc && targetDoc.designMode === 'on')) && targetDoc.getSelection) {
                                                const sel = targetDoc.getSelection();
                                                if (sel && sel.rangeCount === 0) {
                                                    const r = targetDoc.caretRangeFromPoint ? targetDoc.caretRangeFromPoint(fx, fy) : null;
                                                    if (r) {
                                                        sel.addRange(r);
                                                    } else {
                                                        const nr = targetDoc.createRange();
                                                        nr.selectNodeContents(targetEl);
                                                        nr.collapse(false);
                                                        sel.addRange(nr);
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            } catch (e) {}

                            return { linkHref, linkLabel, mSrc, isEditable };
                        }""",
                        [cx, cy],
                    ),
                    timeout=1.0,
                )
                is_editable = False
                if isinstance(info, dict):
                    link_url = str(info.get("linkHref") or "")
                    link_text = str(info.get("linkLabel") or "")
                    media_url = str(info.get("mSrc") or "")
                    is_editable = bool(info.get("isEditable", False))
            except Exception as exc:
                is_editable = False
                logger.warning("Context menu target inspection warning at (%d, %d): %s", cx, cy, exc)

            if not is_editable and not link_url and self.last_hover_url:
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
                elif accepted == 0:
                    cancel_ev = self._active_download_cancels.get(download_id)
                    if cancel_ev is not None:
                        logger.info("Mid-transfer cancel requested by Amiga for download id=%d", download_id)
                        cancel_ev.set()

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
                        try:
                            clip_text = clip_bytes.decode("utf-8")
                        except UnicodeDecodeError:
                            clip_text = clip_bytes.decode("latin-1", errors="replace")
                        logger.info("Received MSG_CLIPBOARD from Amiga (%d chars): %r", len(clip_text), clip_text[:60])
                        self._server_clipboard = clip_text
                        if self.page:
                            await self._paste_text_into_active(clip_text)
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
                    await self._safe_eval(
                        f"(() => {{ const se = document.scrollingElement || document.documentElement || document.body; if (se) window.scrollTo(0, Math.max(0, (se.scrollHeight - window.innerHeight) * {ratio:.4f})); }})()"
                    )
                else:
                    if not await self._cdp_mouse("mouseMoved", cx, cy, "none", 0):
                        await asyncio.wait_for(self.page.mouse.click(cx, cy, button=btn), timeout=1.5)
                    else:
                        await self._cdp_mouse("mousePressed", cx, cy, btn, 1)
                        await self._cdp_mouse("mouseReleased", cx, cy, btn, 1)
                    if btn == "left":
                        self._schedule_storage_save(delay=3.5)
                    if btn in ("left", "middle"):
                        self._schedule_popup_adopt_check()
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
                self._scrollbar_drag_offset = None

                if btn == "right":
                    await self._send_native_context_menu(cx, cy)
                    return

                sb_handled = False
                if btn == "left" and cx >= (self.width - 20):
                    sb_res = await self._safe_eval(
                        """([cy]) => {
                            const se = document.scrollingElement || document.documentElement || document.body;
                            if (!se) return { scrollable: false };
                            const sh = se.scrollHeight || 0;
                            const ch = window.innerHeight || document.documentElement.clientHeight || 0;
                            if (sh <= ch + 1) return { scrollable: false };
                            const maxScroll = sh - ch;
                            const thumbH = Math.max(20, Math.round(ch * (ch / sh)));
                            const trackSpan = Math.max(1, ch - thumbH);
                            const thumbTop = (se.scrollTop / Math.max(1, maxScroll)) * trackSpan;
                            let grabOffset;
                            if (cy >= thumbTop - 4 && cy <= thumbTop + thumbH + 4) {
                                grabOffset = cy - thumbTop;
                            } else {
                                grabOffset = thumbH * 0.5;
                                const newTop = Math.max(0, Math.min(trackSpan, cy - grabOffset));
                                window.scrollTo({ top: (newTop / trackSpan) * maxScroll, left: se.scrollLeft || 0, behavior: 'instant' });
                            }
                            return { scrollable: true, grabOffset: grabOffset };
                        }""",
                        [cy],
                        timeout=0.35,
                        default=None,
                    )
                    if isinstance(sb_res, dict) and sb_res.get("scrollable"):
                        self._scrollbar_drag_offset = float(sb_res.get("grabOffset", 0.0))
                        sb_handled = True

                if not sb_handled:
                    if btn == "left":
                        await self._safe_eval("([x, y]) => window.__vortex_selection && window.__vortex_selection.start(x, y)", [cx, cy], timeout=0.4)
                    if not await self._cdp_mouse("mouseMoved", cx, cy, "none", 0):
                        await asyncio.wait_for(self.page.mouse.move(cx, cy), timeout=1.0)
                        await asyncio.wait_for(self.page.mouse.down(button=btn, click_count=self._click_count), timeout=1.0)
                    else:
                        await self._cdp_mouse("mousePressed", cx, cy, btn, self._click_count)
                self._trigger_full_refresh(True)
                self._schedule_delayed_frames()

            elif event_type == INPUT_MOUSE_UP:
                btn = "left"
                if key_code == 2:
                    btn = "middle"
                elif key_code == 3:
                    btn = "right"
                logger.info("INPUT_MOUSE_UP (%s) at (%d, %d)", btn, cx, cy)

                if btn == "left" and self._scrollbar_drag_offset is not None:
                    self._scrollbar_drag_offset = None
                    self._mouse_down_btn = ""
                    self._trigger_full_refresh(True)
                    self._schedule_delayed_frames()
                    return

                if btn != "right":
                    down_x = self._mouse_down_x if self._mouse_down_x >= 0 else cx
                    down_y = self._mouse_down_y if self._mouse_down_y >= 0 else cy
                    dist = ((cx - down_x) ** 2 + (cy - down_y) ** 2) ** 0.5

                    dragged = False
                    sel_text = ""
                    if btn == "left":
                        res = await self._safe_eval("([x, y]) => window.__vortex_selection ? window.__vortex_selection.finish(x, y) : {dragged: false, text: ''}", [cx, cy], timeout=0.4)
                        if res and isinstance(res, dict):
                            dragged = bool(res.get("dragged", False))
                            sel_text = res.get("text", "") or ""

                    # If user dragged, release with click_count=1 to finalize the freeform selection
                    if dist > 4 or dragged:
                        self._click_count = 1

                    # Automatic YouTube video click -> External Player (V-MPlayer) interception
                    yt_intercept_url = ""
                    if (
                        btn == "left"
                        and dist <= 10
                        and not dragged
                        and self.ext_play_auto_youtube
                        and (time.monotonic() - self._last_ext_play_time) >= 1.2
                    ):
                        yt_hit = await self._safe_eval(
                            """([x, y]) => {
                                let el = document.elementFromPoint(x, y);
                                if (!el) return '';
                                while (el && el.shadowRoot && el.shadowRoot.elementFromPoint) {
                                    const inner = el.shadowRoot.elementFromPoint(x, y);
                                    if (!inner || inner === el) break;
                                    el = inner;
                                }
                                const elem = el.nodeType === 1 ? el : el.parentElement;
                                if (!elem || !elem.closest) return '';

                                // Never intercept search inputs, header/masthead, menus, dialogs, or player control bar
                                if (elem.closest('input, textarea, select, [contenteditable="true"], ytd-masthead, #masthead-container, ytd-searchbox, #search-form, tp-yt-app-drawer, #guide, ytd-menu-popup-renderer, tp-yt-paper-dialog, .ytp-chrome-bottom, .ytp-settings-menu, ytd-comments, #comments')) {
                                    return '';
                                }
                                // Ignore subscribe/like/share/action buttons that are not play buttons or thumbnails
                                const btnEl = elem.closest('button, [role="button"]');
                                if (btnEl && !btnEl.classList.contains('ytp-large-play-button') && !btnEl.closest('ytd-thumbnail, yt-thumbnail-view-model')) {
                                    return '';
                                }

                                const isYtVideoUrl = (u) => {
                                    if (!u) return false;
                                    try {
                                        const parsed = new URL(u, document.baseURI);
                                        const h = parsed.hostname.toLowerCase();
                                        const p = parsed.pathname;
                                        if (h === 'youtu.be' && p.length > 1) return true;
                                        if (h === 'youtube.com' || h.endsWith('.youtube.com') || h === 'youtube-nocookie.com' || h.endsWith('.youtube-nocookie.com')) {
                                            if (p === '/watch' && parsed.searchParams.has('v')) return true;
                                            if (p.startsWith('/shorts/') || p.startsWith('/embed/') || p.startsWith('/live/')) return true;
                                        }
                                    } catch (e) {}
                                    return false;
                                };

                                let targetHref = '';
                                const a = elem.closest('a[href]');
                                if (a) {
                                    const raw = (typeof a.href === 'string' && a.href) ? a.href : (a.getAttribute('href') || '');
                                    try {
                                        const abs = new URL(raw, document.baseURI).href;
                                        if (isYtVideoUrl(abs)) targetHref = abs;
                                    } catch (e) {}
                                }

                                if (!targetHref) {
                                    const thumbCard = elem.closest('ytd-thumbnail, yt-thumbnail-view-model, ytd-reel-item-renderer, ytm-media-item, .yt-lockup-view-model__content-image');
                                    if (thumbCard) {
                                        const card = thumbCard.closest('ytd-rich-item-renderer, ytd-video-renderer, ytd-compact-video-renderer, ytd-grid-video-renderer, ytd-reel-item-renderer, yt-lockup-view-model, ytm-video-with-context-renderer') || thumbCard;
                                        const ya = card.querySelector ? card.querySelector('a#thumbnail[href], a[href*="/watch?v="], a[href*="/shorts/"], a#video-title-link[href]') : null;
                                        if (ya) {
                                            try {
                                                const abs = new URL(ya.getAttribute('href') || ya.href, document.baseURI).href;
                                                if (isYtVideoUrl(abs)) targetHref = abs;
                                            } catch (e) {}
                                        }
                                    }
                                }

                                // Also detect clicking the main video surface / big play button on an already open YouTube watch/shorts page
                                if (!targetHref && isYtVideoUrl(location.href)) {
                                    if (elem.closest('.html5-video-player, video.html5-main-video, .ytp-cued-thumbnail-overlay, .ytp-large-play-button, #player-container')) {
                                        targetHref = location.href;
                                    }
                                }

                                if (targetHref) {
                                    const swallow = (ev) => {
                                        try {
                                            ev.preventDefault();
                                            ev.stopPropagation();
                                            ev.stopImmediatePropagation();
                                        } catch (e) {}
                                    };
                                    window.addEventListener('click', swallow, { capture: true, once: true });
                                    window.addEventListener('mouseup', swallow, { capture: true, once: true });
                                    window.addEventListener('pointerup', swallow, { capture: true, once: true });
                                    setTimeout(() => {
                                        window.removeEventListener('click', swallow, { capture: true });
                                        window.removeEventListener('mouseup', swallow, { capture: true });
                                        window.removeEventListener('pointerup', swallow, { capture: true });
                                    }, 400);
                                    try {
                                        document.querySelectorAll('video').forEach(v => { try { v.pause(); } catch (e) {} });
                                    } catch (e) {}
                                    return targetHref;
                                }
                                return '';
                            }""",
                            [cx, cy],
                            timeout=0.45,
                            default="",
                        )
                        if isinstance(yt_hit, str) and yt_hit.startswith(("http://", "https://")):
                            yt_intercept_url = yt_hit
                            self._last_ext_play_time = time.monotonic()

                    if cx != down_x or cy != down_y:
                        if not await self._cdp_mouse("mouseMoved", cx, cy, btn, 0):
                            await asyncio.wait_for(self.page.mouse.move(cx, cy), timeout=1.0)
                    if not await self._cdp_mouse("mouseReleased", cx, cy, btn, self._click_count):
                        await asyncio.wait_for(self.page.mouse.up(button=btn, click_count=self._click_count), timeout=1.0)

                    self._mouse_down_btn = ""

                    if yt_intercept_url:
                        logger.info("YouTube video click detected at (%d, %d) -> External Player: %s", cx, cy, yt_intercept_url)
                        asyncio.create_task(self._launch_external_player(yt_intercept_url))
                        self._trigger_full_refresh(True)
                        return

                    # Universal check: only if stationary click AND not dragged AND no upload is pending/recent
                    now_up = time.monotonic()
                    if btn == "left" and dist <= 10 and not dragged and not self.pending_uploads and (now_up - self.last_upload_intercept_time) >= 3.0:
                        try:
                            upload_hit = await self._safe_eval("""([x, y]) => {
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
                            }""", [down_x, down_y], timeout=0.35, default=False)
                            if upload_hit:
                                logger.info("Fallback upload click triggered at (%d, %d)", down_x, down_y)
                                self.last_upload_intercept_time = time.monotonic()
                        except Exception as e:
                            logger.info("Upload element check notice: %s", e)

                    if btn in ("left", "middle") and dist <= 10:
                        self._schedule_popup_adopt_check()

                    if btn == "left":
                        if sel_text and len(sel_text.strip()) > 0:
                            logger.info("Selected text (%d chars): %r", len(sel_text), sel_text[:60])
                            self._server_clipboard = sel_text
                            await self.send_clipboard_to_amiga(sel_text)
                        else:
                            try:
                                sel = await self._safe_eval(self._JS_GET_DEEP_SELECTION, timeout=0.35, default="")
                                if sel and len(sel.strip()) > 0:
                                    logger.info("Selected text (%d chars): %r", len(sel), sel[:60])
                                    self._server_clipboard = sel
                                    await self.send_clipboard_to_amiga(sel)
                            except Exception:
                                pass
                        self._schedule_storage_save(delay=3.5)
                self._trigger_full_refresh(True)
                self._schedule_delayed_frames()

            elif event_type == INPUT_MOUSE_MOVE:
                try:
                    if self._mouse_down_btn == "left":
                        if self._scrollbar_drag_offset is not None:
                            await self._safe_eval(
                                """([cy, grabOffset]) => {
                                    const se = document.scrollingElement || document.documentElement || document.body;
                                    if (!se) return;
                                    const sh = se.scrollHeight || 0;
                                    const ch = window.innerHeight || document.documentElement.clientHeight || 0;
                                    if (sh <= ch + 1) return;
                                    const maxScroll = sh - ch;
                                    const thumbH = Math.max(20, Math.round(ch * (ch / sh)));
                                    const trackSpan = Math.max(1, ch - thumbH);
                                    const newTop = Math.max(0, Math.min(trackSpan, cy - grabOffset));
                                    window.scrollTo({ top: (newTop / trackSpan) * maxScroll, left: se.scrollLeft || 0, behavior: 'instant' });
                                }""",
                                [cy, self._scrollbar_drag_offset],
                                timeout=0.25,
                            )
                            self._trigger_full_refresh(False)
                        else:
                            # If user moves mouse with button down, update selection directly via caretRangeFromPoint
                            dist = ((cx - self._mouse_down_x) ** 2 + (cy - self._mouse_down_y) ** 2) ** 0.5
                            if dist > 4:
                                self._click_count = 1
                                await self._safe_eval("([x, y]) => window.__vortex_selection && window.__vortex_selection.update(x, y)", [cx, cy], timeout=0.25)
                            if not await self._cdp_mouse("mouseMoved", cx, cy, "left", 0, timeout=0.2):
                                await asyncio.wait_for(self.page.mouse.move(cx, cy), timeout=0.2)
                            self._trigger_full_refresh(False)
                    else:
                        if not await self._cdp_mouse("mouseMoved", cx, cy, "none", 0, timeout=0.15):
                            await asyncio.wait_for(self.page.mouse.move(cx, cy), timeout=0.15)
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
                scrolled = bool(
                    await self._safe_eval(
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
                        timeout=0.4,
                        default=False,
                    )
                )
                if not scrolled:
                    try:
                        if not await self._cdp_mouse("mouseWheel", cx, cy, "none", 0, delta_y=float(dy), timeout=0.4):
                            await asyncio.wait_for(self.page.mouse.move(cx, cy), timeout=0.2)
                            await asyncio.wait_for(self.page.mouse.wheel(0, float(dy)), timeout=0.4)
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

    _JS_GET_DEEP_SELECTION = """() => {
        let doc = document, win = window, el = document.activeElement;
        for (let i = 0; i < 8 && el; i++) {
            if (el.tagName === 'IFRAME') {
                try {
                    const cd = el.contentDocument || (el.contentWindow && el.contentWindow.document);
                    if (!cd) break;
                    doc = cd;
                    win = el.contentWindow || doc.defaultView || win;
                    el = doc.activeElement;
                    continue;
                } catch (e) { break; }
            }
            if (el.shadowRoot && el.shadowRoot.activeElement) {
                el = el.shadowRoot.activeElement;
                continue;
            }
            break;
        }
        const s = (win && win.getSelection) ? win.getSelection().toString() : (doc.getSelection ? doc.getSelection().toString() : '');
        if (s && s.length > 0) return s;
        if (el && (el.tagName === 'TEXTAREA' || el.tagName === 'INPUT')) {
            try {
                return el.value.substring(el.selectionStart, el.selectionEnd);
            } catch (e) {}
        }
        return '';
    }"""

    _JS_GET_ACTIVE_TEXT = """() => {
        let doc = document, el = document.activeElement;
        for (let i = 0; i < 8 && el; i++) {
            if (el.tagName === 'IFRAME') {
                try {
                    const cd = el.contentDocument || (el.contentWindow && el.contentWindow.document);
                    if (!cd) break;
                    doc = cd;
                    el = doc.activeElement;
                    continue;
                } catch (e) { break; }
            }
            if (el.shadowRoot && el.shadowRoot.activeElement) {
                el = el.shadowRoot.activeElement;
                continue;
            }
            break;
        }
        if (!el) return null;
        if (el.tagName === 'TEXTAREA' || el.tagName === 'INPUT') return el.value;
        return el.innerText;
    }"""

    _JS_INSERT_TEXT_DEEP = """([text]) => {
        let doc = document, el = document.activeElement;
        for (let i = 0; i < 8 && el; i++) {
            if (el.tagName === 'IFRAME') {
                try {
                    const cd = el.contentDocument || (el.contentWindow && el.contentWindow.document);
                    if (!cd) break;
                    doc = cd;
                    el = doc.activeElement;
                    continue;
                } catch (e) { break; }
            }
            if (el.shadowRoot && el.shadowRoot.activeElement) {
                el = el.shadowRoot.activeElement;
                continue;
            }
            break;
        }
        if (!el) return false;
        const tag = (el.tagName || '').toUpperCase();
        if (tag === 'TEXTAREA' || tag === 'INPUT') {
            try {
                const s = typeof el.selectionStart === 'number' ? el.selectionStart : el.value.length;
                const e = typeof el.selectionEnd === 'number' ? el.selectionEnd : s;
                if (typeof el.setRangeText === 'function') {
                    el.setRangeText(text, s, e, 'end');
                } else {
                    el.value = el.value.slice(0, s) + text + el.value.slice(e);
                    el.selectionStart = el.selectionEnd = s + text.length;
                }
                el.dispatchEvent(new Event('input', { bubbles: true }));
                el.dispatchEvent(new Event('change', { bubbles: true }));
                return true;
            } catch (err) {}
        }
        if (el.isContentEditable || (doc && doc.designMode === 'on')) {
            try {
                const sel = doc.getSelection ? doc.getSelection() : null;
                if (sel && sel.rangeCount === 0) {
                    const nr = doc.createRange();
                    nr.selectNodeContents(el);
                    nr.collapse(false);
                    sel.addRange(nr);
                }
                if (doc.execCommand && doc.execCommand('insertText', false, text)) {
                    return true;
                }
            } catch (err) {}
        }
        return false;
    }"""

    async def _paste_text_into_active(self, clip_text: str) -> None:
        if not self.page or not clip_text:
            return
        before_text = await self._safe_eval(self._JS_GET_ACTIVE_TEXT, timeout=0.25, default=None)
        inserted = False
        if isinstance(self.cdp, DirectCDPSession) and self.cdp.connected:
            try:
                await asyncio.wait_for(
                    self.cdp.send("Input.insertText", {"text": clip_text}),
                    timeout=1.0,
                )
                inserted = True
            except Exception:
                inserted = False
        if not inserted:
            try:
                await asyncio.wait_for(self.page.keyboard.insert_text(clip_text), timeout=1.5)
                inserted = True
            except Exception:
                inserted = False
        if before_text is not None:
            after_text = await self._safe_eval(self._JS_GET_ACTIVE_TEXT, timeout=0.25, default=None)
            if after_text == before_text:
                await self._safe_eval(self._JS_INSERT_TEXT_DEEP, [clip_text], timeout=0.4, default=False)
        self._trigger_full_refresh(True)
        self._schedule_delayed_frames()

    _CDP_KEY_DEFS = {
        "a": ("a", "KeyA", 65),
        "c": ("c", "KeyC", 67),
        "v": ("v", "KeyV", 86),
        "x": ("x", "KeyX", 88),
        "y": ("y", "KeyY", 89),
        "z": ("z", "KeyZ", 90),
        "ArrowLeft":  ("ArrowLeft",  "ArrowLeft",  37),
        "ArrowUp":    ("ArrowUp",    "ArrowUp",    38),
        "ArrowRight": ("ArrowRight", "ArrowRight", 39),
        "ArrowDown":  ("ArrowDown",  "ArrowDown",  40),
        "Home":       ("Home",       "Home",       36),
        "End":        ("End",        "End",        35),
    }

    async def _cdp_dispatch_key(
        self,
        key_id: str,
        modifiers: int = 0,
        command: Optional[str] = None,
        timeout: float = 0.6,
    ) -> bool:
        """Dispatch key shortcut directly via DirectCDP WebSocket (supports editing commands in iframes)."""
        if not (isinstance(self.cdp, DirectCDPSession) and self.cdp.connected):
            return False
        kdef = self._CDP_KEY_DEFS.get(key_id)
        if not kdef:
            return False
        key_val, code_val, vk = kdef
        down_params: Dict[str, Any] = {
            "type": "rawKeyDown",
            "modifiers": int(modifiers),
            "key": key_val,
            "code": code_val,
            "windowsVirtualKeyCode": int(vk),
            "nativeVirtualKeyCode": int(vk),
        }
        if command:
            down_params["commands"] = [command]
        up_params: Dict[str, Any] = {
            "type": "keyUp",
            "modifiers": int(modifiers),
            "key": key_val,
            "code": code_val,
            "windowsVirtualKeyCode": int(vk),
            "nativeVirtualKeyCode": int(vk),
        }
        try:
            await asyncio.wait_for(self.cdp.send("Input.dispatchKeyEvent", down_params), timeout=timeout)
            await asyncio.wait_for(self.cdp.send("Input.dispatchKeyEvent", up_params), timeout=timeout)
            return True
        except Exception:
            return False

    async def _scroll_if_not_editing(self, dy: int, absolute: Optional[str] = None) -> bool:
        if not self.page:
            return False
        try:
            is_editing = await self._safe_eval(
                """() => {
                    let doc = document, el = document.activeElement;
                    for (let i = 0; i < 8 && el; i++) {
                        if (el.tagName === 'IFRAME') {
                            try {
                                const cd = el.contentDocument || (el.contentWindow && el.contentWindow.document);
                                if (!cd) break;
                                doc = cd;
                                el = doc.activeElement;
                                continue;
                            } catch (e) { break; }
                        }
                        if (el.shadowRoot && el.shadowRoot.activeElement) {
                            el = el.shadowRoot.activeElement;
                            continue;
                        }
                        break;
                    }
                    if (doc && doc.designMode === 'on') return true;
                    if (!el) return false;
                    const tag = (el.tagName || '').toUpperCase();
                    return (tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT' || el.isContentEditable);
                }""",
                timeout=0.5,
                default=False,
            )
            if is_editing:
                return False
            if absolute == "top":
                await self._safe_eval("window.scrollTo({ top: 0, left: 0, behavior: 'instant' })", timeout=0.5)
            elif absolute == "bottom":
                await self._safe_eval("window.scrollTo({ top: document.documentElement.scrollHeight, left: 0, behavior: 'instant' })", timeout=0.5)
            else:
                await self._safe_eval(f"window.scrollBy({{ top: {int(dy)}, left: 0, behavior: 'instant' }})", timeout=0.5)
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
            elif text_or_url == "Shift+Tab":
                await asyncio.wait_for(self.page.keyboard.press("Shift+Tab"), timeout=1.5)
                return
            elif text_or_url in ("Shift+ArrowLeft", "Shift+ArrowRight", "Shift+ArrowUp", "Shift+ArrowDown", "Shift+Home", "Shift+End"):
                base_k = text_or_url.split("+", 1)[1]
                if not await self._cdp_dispatch_key(base_k, modifiers=8):
                    await asyncio.wait_for(self.page.keyboard.press(text_or_url), timeout=1.5)
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
                await asyncio.wait_for(self.page.keyboard.press(text_or_url), timeout=1.5)
                return
            elif text_or_url in ("Copy", "Control+c", "Control+C", "Cut", "Control+x", "Control+X") or key_code in (3, 24):
                is_cut = text_or_url in ("Cut", "Control+x", "Control+X") or key_code == 24
                try:
                    sel = await self._safe_eval(self._JS_GET_DEEP_SELECTION, timeout=0.35, default="")
                    if sel and len(sel) > 0:
                        logger.info("Copying selection (%d chars) to Amiga clipboard", len(sel))
                        self._server_clipboard = sel
                        await self.send_clipboard_to_amiga(sel)
                except Exception:
                    pass
                kchar, kcmd, kcombo = ("x", "cut", "Control+x") if is_cut else ("c", "copy", "Control+c")
                if not await self._cdp_dispatch_key(kchar, modifiers=2, command=kcmd):
                    await asyncio.wait_for(self.page.keyboard.press(kcombo), timeout=1.5)
                return
            elif text_or_url in ("Paste", "Control+v", "Control+V") or key_code == 22:
                before_text = None
                if self._server_clipboard:
                    before_text = await self._safe_eval(self._JS_GET_ACTIVE_TEXT, timeout=0.25, default=None)
                if not await self._cdp_dispatch_key("v", modifiers=2, command="paste"):
                    await asyncio.wait_for(self.page.keyboard.press("Control+v"), timeout=1.5)
                if self._server_clipboard and before_text is not None:
                    after_text = await self._safe_eval(self._JS_GET_ACTIVE_TEXT, timeout=0.25, default=None)
                    if after_text == before_text:
                        await self._paste_text_into_active(self._server_clipboard)
                return
            elif text_or_url in ("Control+a", "Control+A", "Control+z", "Control+Z", "Control+y", "Control+Y") or key_code in (1, 25, 26):
                ctrl_map = {1: ("a", "selectAll"), 25: ("y", "redo"), 26: ("z", "undo")}
                if text_or_url.lower().startswith("control+"):
                    kchar = text_or_url[-1].lower()
                    kcmd = {"a": "selectAll", "y": "redo", "z": "undo"}.get(kchar, "selectAll")
                else:
                    kchar, kcmd = ctrl_map.get(key_code, ("a", "selectAll"))
                if not await self._cdp_dispatch_key(kchar, modifiers=2, command=kcmd):
                    await asyncio.wait_for(self.page.keyboard.press(f"Control+{kchar}"), timeout=1.5)
                return
            else:
                await asyncio.wait_for(self.page.keyboard.type(text_or_url), timeout=1.5)
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
        # Do NOT cancel in-flight background navigations on other tabs (e.g. during startup tab restore)
        target_page = self.tabs[tidx]
        if self._is_page_closed(target_page):
            fresh = await self._replace_tab_page(tidx, reason="closed on switch")
            if fresh is None:
                return
            if (
                self.active_tab_idx == tidx
                and self.page is not None
                and self._page_identity(self.page) == self._page_identity(fresh)
            ):
                self._cached_page_url = (fresh.url or "").lower()
                self._trigger_full_refresh(True)
                self._send_nav_update(fresh.url or self.portal_url, tab_idx=tidx)
                self._schedule_delayed_frames()
                return
            target_page = fresh
        self._tab_generation += 1
        self._switching_tab = True
        try:
            if old_page and self._page_identity(old_page) != self._page_identity(target_page):
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
            self._cached_page_url = (cur_page.url or "").lower()
            self._send_nav_update(cur_page.url or "", tab_idx=tidx)
        self._schedule_delayed_frames()

        async def _post_switch_tasks() -> None:
            p = cur_page
            if not p or self.page is not p or not self.running:
                return
            need_refresh = False
            if getattr(p, "_vb_applied_dark_mode", None) != self.dark_mode:
                await self._apply_dark_mode(p)
                try:
                    setattr(p, "_vb_applied_dark_mode", self.dark_mode)
                except Exception:
                    pass
                need_refresh = True
            elif getattr(p, "_vb_applied_zoom", 100) != self.web_zoom_pct:
                await self._apply_web_styling(p)
                need_refresh = True
            if need_refresh and self.page is p:
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

            if self.cdp is not None and self.engine == "chromium":
                try:
                    await self.cdp.send(
                        "Page.setFontSizes",
                        {
                            "fontSizes": {
                                "standard": fsize,
                                "fixed": max(10, fsize - 2),
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

            zoom_css_val = "" if base_zoom == 100 else f"{base_zoom}%"
            js = f"""
            (() => {{
                try {{
                    if (document.documentElement) {{
                        document.documentElement.style.zoom = '{zoom_css_val}';
                        void document.documentElement.offsetHeight;
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
            try:
                setattr(target_page, "_vb_applied_zoom", base_zoom)
            except Exception:
                pass
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

        if cmd.upper().startswith("@EXTPLAY_AUTO:"):
            val = cmd.split(":", 1)[1].strip().lower()
            self.ext_play_auto_youtube = val not in ("0", "false", "off", "no")
            logger.info("Client set automatic external player for YouTube clicks: %s", self.ext_play_auto_youtube)
            return

        if cmd.startswith("@EXTPLAY:"):
            raw_target = cmd.split(":", 1)[1].strip()
            asyncio.create_task(self._launch_external_player(raw_target))
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
                self.last_frame_hash = None
                self.force_full_frame = True
                self._trigger_full_refresh(True)
                self._schedule_delayed_frames()
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
            self._client_nav_received.set()
            parts = cmd.split(":", 1)
            target = parts[1].strip() if len(parts) > 1 else ""
            if is_startpage_url(target, self.portal_url):
                target = self.portal_url
            elif not (target.startswith("http://") or target.startswith("https://") or target.startswith("file://")):
                target = "https://" + target
            if self.context:
                # Do NOT cancel other tabs' background navigations (e.g. Tab 0 restoring on startup)
                try:
                    self._creating_tab = True
                    self._tab_generation += 1
                    self._switching_tab = True
                    old_page = self.page
                    if old_page and not self._is_page_closed(old_page):
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
                    self._switching_tab = False
                    await self._apply_dark_mode(new_page)
                    self._send_progress(20, f"Lade {target}...")
                    self._get_tab_history(new_page)["force_push"] = True
                    new_page = await self._safe_page_goto(new_page, target, wait_until="commit", timeout=15000)
                    nav_u = (new_page.url or "").strip()
                    if not nav_u or nav_u.startswith("about:"):
                        nav_u = target
                    self._record_tab_history(new_page, nav_u)
                    self._send_nav_update(nav_u)
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
            self._client_nav_received.set()
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
                    target_tab = self.tabs[tidx]
                    same_tab = (
                        tidx == self.active_tab_idx
                        and self.page is not None
                        and self._page_identity(self.page) == self._page_identity(target_tab)
                        and not self._is_page_closed(target_tab)
                        and cdp_ok
                    )
                    if same_tab:
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
                    self._cancel_nav_task(p_to_close)
                    was_active = (
                        self.page is not None
                        and self._page_identity(p_to_close) == self._page_identity(self.page)
                    )
                    if was_active:
                        self._tab_generation += 1
                        self._switching_tab = True
                        try:
                            await self._stop_screencast()
                            await self._detach_cdp()
                        except Exception:
                            self._switching_tab = False
                    await self._force_close_page(p_to_close)
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

        self._client_nav_received.set()
        cur_hist = self._get_tab_history(self.page) if self.page else {"history": [], "idx": -1}
        logger.info(
            "INPUT_GOTO_URL: %s (tab[%d] history idx=%d/%d)",
            cmd,
            self.active_tab_idx,
            cur_hist["idx"],
            len(cur_hist["history"]),
        )
        self._cancel_nav_task(self.page)
        self._trigger_full_refresh()
        self._schedule_delayed_frames()

        nav_page_init = self.page
        nav_page_key = self._page_identity(nav_page_init) if nav_page_init is not None else None
        nav_tab_gen = self._tab_generation

        async def _nav_bg():
            nav_page = nav_page_init
            resolved_target = ""
            cmd_upper = cmd.upper()
            try:
                if nav_page is None:
                    return
                self._send_progress(25, f"Lade {cmd[:30]}...")
                if cmd_upper == "@BACK":
                    hist = self._get_tab_history(nav_page)
                    if hist["idx"] > 0:
                        hist["idx"] -= 1
                        target_url = hist["history"][hist["idx"]]
                        resolved_target = target_url
                        logger.info(
                            "Navigating tab[%d] @BACK to history[%d]: %s",
                            self._find_tab_index(nav_page),
                            hist["idx"],
                            target_url,
                        )
                        hist["jumping"] = True
                        try:
                            nav_page = await self._safe_page_goto(nav_page, target_url, wait_until="commit", timeout=12000)
                        finally:
                            hist["jumping"] = False
                    elif not is_startpage_url(nav_page.url or "", self.portal_url):
                        resolved_target = self.portal_url
                        logger.info(
                            "Navigating tab[%d] @BACK fallback to portal: %s",
                            self._find_tab_index(nav_page),
                            self.portal_url,
                        )
                        if not hist["history"] or hist["history"][0] != self.portal_url:
                            hist["history"].insert(0, self.portal_url)
                            hist["times"].insert(0, time.monotonic())
                        hist["idx"] = 0
                        hist["jumping"] = True
                        try:
                            nav_page = await self._safe_page_goto(nav_page, self.portal_url, wait_until="commit", timeout=10000)
                        finally:
                            hist["jumping"] = False
                elif cmd_upper == "@FORWARD":
                    hist = self._get_tab_history(nav_page)
                    if hist["idx"] + 1 < len(hist["history"]):
                        hist["idx"] += 1
                        target_url = hist["history"][hist["idx"]]
                        resolved_target = target_url
                        logger.info(
                            "Navigating tab[%d] @FORWARD to history[%d]: %s",
                            self._find_tab_index(nav_page),
                            hist["idx"],
                            target_url,
                        )
                        hist["jumping"] = True
                        try:
                            nav_page = await self._safe_page_goto(nav_page, target_url, wait_until="commit", timeout=12000)
                        finally:
                            hist["jumping"] = False
                elif cmd_upper == "@RELOAD":
                    try:
                        if self._is_page_closed(nav_page):
                            raise RuntimeError("Target page is closed")
                        await nav_page.reload(wait_until="commit", timeout=15000)
                    except asyncio.CancelledError:
                        raise
                    except Exception:
                        reload_url = (nav_page.url or "") if not self._is_page_closed(nav_page) else ""
                        if not reload_url or reload_url.startswith("about:"):
                            reload_url = self._cached_page_url or self.portal_url
                        resolved_target = reload_url
                        nav_page = await self._safe_page_goto(nav_page, reload_url, wait_until="commit", timeout=15000)
                elif cmd_upper == "@STOP":
                    await self._stop_page_loading(nav_page)
                elif cmd_upper == "@HOME":
                    resolved_target = self.portal_url
                    self._get_tab_history(nav_page)["force_push"] = True
                    nav_page = await self._safe_page_goto(nav_page, self.portal_url, wait_until="commit", timeout=10000)
                else:
                    target = cmd
                    if is_startpage_url(target, self.portal_url):
                        logger.info("Redirecting startpage request '%s' to '%s'", target, self.portal_url)
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
                    resolved_target = target
                    self._get_tab_history(nav_page)["force_push"] = True
                    nav_page = await self._safe_page_goto(nav_page, target, wait_until="commit", timeout=15000)
            except asyncio.CancelledError:
                return
            except Exception as exc:
                logger.warning("Navigation warning for '%s': %s", cmd, exc)
            finally:
                if self._nav_task is asyncio.current_task():
                    self._nav_task = None
                if nav_page_key is not None and self._tab_nav_tasks.get(nav_page_key) is asyncio.current_task():
                    self._tab_nav_tasks.pop(nav_page_key, None)
                cur_u = ""
                try:
                    cur_u = (nav_page.url or "").strip() if (nav_page is not None and not self._is_page_closed(nav_page)) else ""
                except Exception:
                    cur_u = ""
                if (not cur_u or cur_u.startswith("about:")) and resolved_target:
                    cur_u = resolved_target
                if nav_page is not None and cur_u and cmd_upper not in ("@BACK", "@FORWARD", "@STOP", "@RELOAD"):
                    self._record_tab_history(nav_page, cur_u)
                still_active = (
                    self.running
                    and self.page is not None
                    and nav_page is not None
                    and (
                        self._page_identity(self.page) == self._page_identity(nav_page)
                        or self._tab_generation == nav_tab_gen
                    )
                )
                if still_active:
                    self._send_progress(100, "Fertig")
                    if cur_u:
                        self._send_nav_update(cur_u)
                    self.active_until = time.monotonic() + 3.0
                    self._trigger_full_refresh(True)
                    self._schedule_delayed_frames()

        t = asyncio.create_task(_nav_bg())
        self._nav_task = t
        if nav_page_key is not None:
            self._tab_nav_tasks[nav_page_key] = t
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
    # Instant Browser-CDP Download Interceptor & Live Streamer (Server -> Amiga)
    # ------------------------------------------------------------------------
    async def _start_browser_download_monitor(self) -> None:
        """Subscribes to Chromium Browser.downloadWillBegin / Browser.downloadProgress via
        a browser-level CDP WebSocket so the Amiga ASL requester pops up within milliseconds
        of clicking any download link, instead of waiting for Chromium to finish downloading
        the entire file first."""
        if self.engine != "chromium" or not _HAS_WEBSOCKETS or websockets is None:
            return
        await self._stop_browser_download_monitor()
        try:
            def _get_browser_ws_url() -> Optional[str]:
                with urllib.request.urlopen(f"http://127.0.0.1:{self.cdp_port}/json/version", timeout=2.0) as resp:
                    info = json.loads(resp.read().decode("utf-8", errors="ignore"))
                    return info.get("webSocketDebuggerUrl")

            ws_url = await asyncio.get_running_loop().run_in_executor(None, _get_browser_ws_url)
            if not ws_url:
                return
            dl_dir = tempfile.mkdtemp(prefix="vb_cdp_dl_")
            ws = await websockets.connect(ws_url, max_size=4 * 1024 * 1024, open_timeout=3.0)
            self._browser_dl_dir = dl_dir
            self._browser_dl_ws = ws
            self._browser_dl_cmd_id = 1
            await ws.send(json.dumps({
                "id": 1,
                "method": "Browser.setDownloadBehavior",
                "params": {
                    "behavior": "allowAndName",
                    "downloadPath": dl_dir,
                    "eventsEnabled": True,
                },
            }))

            async def _reader_loop() -> None:
                try:
                    async for raw in ws:
                        if not self.running:
                            break
                        try:
                            msg = json.loads(raw)
                        except Exception:
                            continue
                        meth = msg.get("method")
                        if not meth:
                            continue
                        params = msg.get("params") or {}
                        if meth == "Browser.downloadWillBegin":
                            guid = str(params.get("guid") or "")
                            if not guid or guid in self._cdp_downloads:
                                continue
                            self._last_download_time = time.monotonic()
                            url = str(params.get("url") or "")
                            suggested = str(params.get("suggestedFilename") or "").strip()
                            if not suggested and url:
                                parsed = urlparse(url)
                                suggested = unquote(os.path.basename(parsed.path)).strip()
                            entry = {
                                "guid": guid,
                                "url": url,
                                "suggested": suggested,
                                "total_bytes": 0,
                                "received_bytes": 0,
                                "state": "inProgress",
                                "file_path": "",
                                "got_total_event": asyncio.Event(),
                                "progress_event": asyncio.Event(),
                                "done_event": asyncio.Event(),
                            }
                            self._cdp_downloads[guid] = entry
                            self._cdp_handled_guids.add(guid)
                            self._cdp_handled_keys[(url, suggested)] = time.monotonic()
                            t = asyncio.create_task(self._process_cdp_download(entry))
                            self.background_tasks.add(t)
                            t.add_done_callback(self.background_tasks.discard)
                        elif meth == "Browser.downloadProgress":
                            guid = str(params.get("guid") or "")
                            entry = self._cdp_downloads.get(guid)
                            if not entry:
                                continue
                            tb = params.get("totalBytes")
                            if isinstance(tb, (int, float)) and tb > 0:
                                entry["total_bytes"] = int(tb)
                            rb = params.get("receivedBytes")
                            if isinstance(rb, (int, float)) and rb >= 0:
                                entry["received_bytes"] = int(rb)
                            fp = params.get("filePath")
                            if fp:
                                entry["file_path"] = str(fp)
                            st = str(params.get("state") or "inProgress")
                            entry["state"] = st
                            if entry["total_bytes"] > 0 or st in ("completed", "canceled"):
                                entry["got_total_event"].set()
                            entry["progress_event"].set()
                            if st in ("completed", "canceled"):
                                entry["done_event"].set()
                except asyncio.CancelledError:
                    pass
                except Exception as exc:
                    logger.debug("Browser CDP download monitor loop ended: %s", exc)

            self._browser_dl_task = asyncio.create_task(_reader_loop())
            logger.info("Instant Browser-CDP download monitor active (dir=%s)", dl_dir)
        except Exception as exc:
            logger.debug("Browser-CDP download monitor unavailable (%s); using Playwright fallback", exc)
            await self._stop_browser_download_monitor()

    async def _stop_browser_download_monitor(self) -> None:
        task = self._browser_dl_task
        self._browser_dl_task = None
        if task is not None and not task.done():
            task.cancel()
            try:
                await task
            except Exception:
                pass
        ws = self._browser_dl_ws
        self._browser_dl_ws = None
        if ws is not None:
            try:
                await ws.close()
            except Exception:
                pass
        for entry in self._cdp_downloads.values():
            entry["done_event"].set()
            entry["got_total_event"].set()
            entry["progress_event"].set()
        self._cdp_downloads.clear()
        dl_dir = self._browser_dl_dir
        self._browser_dl_dir = ""
        if dl_dir and os.path.isdir(dl_dir):
            try:
                shutil.rmtree(dl_dir, ignore_errors=True)
            except Exception:
                pass

    async def _cdp_cancel_download(self, guid: str) -> None:
        ws = self._browser_dl_ws
        if ws is None or not guid:
            return
        try:
            self._browser_dl_cmd_id += 1
            await ws.send(json.dumps({
                "id": self._browser_dl_cmd_id,
                "method": "Browser.cancelDownload",
                "params": {"guid": guid},
            }))
        except Exception:
            pass

    async def _process_cdp_download(self, entry: Dict[str, Any]) -> None:
        """Immediately opens the Amiga ASL FileRequester upon Browser.downloadWillBegin,
        then live-streams chunks as Chromium writes them (or cancels Chromium if rejected)."""
        guid = entry["guid"]
        download_id = self.next_download_id
        self.next_download_id += 1
        suggested = entry["suggested"] or f"download_{download_id}.bin"
        dl_dir = self._browser_dl_dir
        cr_path = os.path.join(dl_dir, f"{guid}.crdownload") if dl_dir else ""
        final_path = os.path.join(dl_dir, guid) if dl_dir else ""
        cancel_ev = asyncio.Event()
        self._active_download_cancels[download_id] = cancel_ev

        try:
            # Wait briefly (max 150ms) for the first Browser.downloadProgress event carrying Content-Length
            try:
                await asyncio.wait_for(entry["got_total_event"].wait(), timeout=0.15)
            except asyncio.TimeoutError:
                pass

            total_size = int(entry.get("total_bytes") or 0)
            if total_size == 0 and entry.get("state") == "completed":
                fp = entry.get("file_path") or final_path
                if fp and os.path.isfile(fp):
                    try:
                        total_size = os.path.getsize(fp)
                    except Exception:
                        pass

            logger.info(
                "Instant Download id=%d ready for Amiga ASL: '%s' (%d bytes, state=%s)",
                download_id, suggested, total_size, entry.get("state"),
            )
            self._send_progress(100, f"Download: {suggested[:40]}")

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
                await self._cdp_cancel_download(guid)
                return

            try:
                accepted = await asyncio.wait_for(resp_future, timeout=300.0)
            finally:
                self.pending_downloads.pop(download_id, None)

            if not accepted:
                logger.info("Download id=%d rejected/cancelled by Amiga user; cancelling browser transfer.", download_id)
                await self._cdp_cancel_download(guid)
                return

            # Stream chunks live from the growing .crdownload file -> final completed file!
            sent_bytes = 0
            f_handle = None
            try:
                while self.running and not cancel_ev.is_set():
                    state = entry.get("state", "inProgress")
                    target_final = entry.get("file_path") or final_path

                    # Determine readable source file
                    if _IS_WINDOWS:
                        # On Windows avoid holding an open handle across awaits so Chromium can rename .crdownload -> final
                        src_path = target_final if (target_final and os.path.isfile(target_final)) else (cr_path if (cr_path and os.path.isfile(cr_path)) else "")
                        chunk = b""
                        if src_path:
                            try:
                                with open(src_path, "rb") as wf:
                                    wf.seek(sent_bytes)
                                    chunk = wf.read(DOWNLOAD_CHUNK_SIZE)
                            except OSError:
                                chunk = b""
                    else:
                        if f_handle is None:
                            open_path = target_final if (target_final and os.path.isfile(target_final)) else (cr_path if (cr_path and os.path.isfile(cr_path)) else "")
                            if open_path:
                                try:
                                    f_handle = open(open_path, "rb")
                                    if sent_bytes > 0:
                                        f_handle.seek(sent_bytes)
                                except OSError:
                                    f_handle = None
                        chunk = f_handle.read(DOWNLOAD_CHUNK_SIZE) if f_handle is not None else b""

                    if chunk:
                        chunk_hdr = STRUCT_MSG_DL_CHUNK.pack(MSG_DOWNLOAD_CHUNK, download_id, len(chunk))
                        if not await self.send_packet(chunk_hdr + chunk):
                            await self._cdp_cancel_download(guid)
                            return
                        sent_bytes += len(chunk)
                        await asyncio.sleep(0.001)
                        continue

                    # Caught up to current EOF on disk
                    if state == "completed":
                        # Verify final renamed file has no unread tail bytes
                        if f_handle is not None:
                            try:
                                f_handle.close()
                            except Exception:
                                pass
                            f_handle = None
                        if target_final and os.path.isfile(target_final):
                            final_size = os.path.getsize(target_final)
                            if sent_bytes < final_size:
                                with open(target_final, "rb") as tf:
                                    tf.seek(sent_bytes)
                                    while self.running and not cancel_ev.is_set():
                                        tail = tf.read(DOWNLOAD_CHUNK_SIZE)
                                        if not tail:
                                            break
                                        chunk_hdr = STRUCT_MSG_DL_CHUNK.pack(MSG_DOWNLOAD_CHUNK, download_id, len(tail))
                                        if not await self.send_packet(chunk_hdr + tail):
                                            return
                                        sent_bytes += len(tail)
                                        await asyncio.sleep(0.001)
                        break

                    if state == "canceled":
                        logger.warning("Browser download id=%d was canceled before completion (%d bytes sent).", download_id, sent_bytes)
                        return

                    # Wait for more bytes from Chromium (or poll every 50ms since Chromium batches progress events)
                    entry["progress_event"].clear()
                    try:
                        await asyncio.wait_for(entry["progress_event"].wait(), timeout=0.05)
                    except asyncio.TimeoutError:
                        pass
            finally:
                if f_handle is not None:
                    try:
                        f_handle.close()
                    except Exception:
                        pass

            if cancel_ev.is_set():
                await self._cdp_cancel_download(guid)
                logger.info("Download id=%d stopped mid-transfer by Amiga user (%d bytes sent).", download_id, sent_bytes)
                return

            comp_pkt = STRUCT_MSG_DL_COMPLETE.pack(MSG_DOWNLOAD_COMPLETE, download_id)
            await self.send_packet(comp_pkt)
            logger.info("Instant Download id=%d completed (%d bytes streamed live to Amiga).", download_id, sent_bytes)
        except asyncio.TimeoutError:
            logger.warning("Download id=%d timed out waiting for Amiga ASL response.", download_id)
            await self._cdp_cancel_download(guid)
        except asyncio.CancelledError:
            await self._cdp_cancel_download(guid)
            raise
        except Exception as exc:
            logger.error("Error in instant CDP download id=%d: %s", download_id, exc)
        finally:
            self.pending_downloads.pop(download_id, None)
            self._active_download_cancels.pop(download_id, None)
            self._cdp_downloads.pop(guid, None)
            for p in (entry.get("file_path") or "", final_path, cr_path):
                if p and os.path.isfile(p):
                    try:
                        os.remove(p)
                    except Exception:
                        pass

    def _on_download_event(self, download: Download) -> None:
        self._last_download_time = time.monotonic()
        # Skip duplicates already intercepted instantaneously by the Browser-CDP download monitor
        try:
            raw_sync = getattr(download, "_sync", download)
            dl_path = str(getattr(raw_sync, "_path", "") or "")
            if dl_path and os.path.basename(dl_path) in self._cdp_handled_guids:
                logger.debug("Skipping duplicate page.on(download) already handled via Browser-CDP (%s)", os.path.basename(dl_path))
                return
            key = (str(getattr(download, "url", "") or ""), str(getattr(download, "suggested_filename", "") or "").strip())
            ts = self._cdp_handled_keys.get(key)
            if ts is not None and (time.monotonic() - ts) < 600.0:
                logger.debug("Skipping duplicate page.on(download) for %s (already handled via Browser-CDP)", key[1])
                return
        except Exception:
            pass
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

        if url.startswith("/") and self.page and self.page.url:
            url = urljoin(self.page.url, url)

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
            try:
                accepted = await asyncio.wait_for(resp_future, timeout=300.0)
            finally:
                self.pending_downloads.pop(download_id, None)
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
        """Fallback download handler (Firefox / non-CDP): opens the Amiga ASL requester
        immediately while saving the browser download concurrently in the background."""
        download_id = self.next_download_id
        self.next_download_id += 1

        suggested = download.suggested_filename or f"download_{download_id}.bin"
        logger.info("Intercepted browser download id=%d: '%s' (url=%s)", download_id, suggested, download.url)

        tmp_fd, tmp_path = tempfile.mkstemp(prefix=f"vb_dl_{download_id}_")
        os.close(tmp_fd)

        save_task = asyncio.create_task(download.save_as(tmp_path))
        try:
            # Open Amiga ASL immediately while download.save_as runs in background
            loop = asyncio.get_running_loop()
            resp_future = loop.create_future()
            self.pending_downloads[download_id] = resp_future

            fname_bytes = suggested.encode("utf-8", errors="replace")[:255]
            fname_padded = fname_bytes.ljust(256, b"\x00")
            total_size = 0
            if save_task.done() and not save_task.exception() and os.path.isfile(tmp_path):
                total_size = os.path.getsize(tmp_path)

            start_pkt = STRUCT_MSG_DL_START.pack(
                MSG_DOWNLOAD_START,
                download_id,
                total_size,
                fname_padded,
            )
            if not await self.send_packet(start_pkt):
                save_task.cancel()
                return

            try:
                accepted = await asyncio.wait_for(resp_future, timeout=300.0)
            finally:
                self.pending_downloads.pop(download_id, None)

            if not accepted:
                logger.info("Download id=%d rejected/cancelled by Amiga user.", download_id)
                try:
                    await download.cancel()
                except Exception:
                    pass
                save_task.cancel()
                return

            await save_task
            total_size = os.path.getsize(tmp_path)
            logger.info("Download id=%d accepted! Streaming %d bytes in 64KB chunks...", download_id, total_size)
            sent_bytes = 0
            with open(tmp_path, "rb") as f:
                while self.running:
                    chunk = f.read(DOWNLOAD_CHUNK_SIZE)
                    if not chunk:
                        break
                    chunk_hdr = STRUCT_MSG_DL_CHUNK.pack(MSG_DOWNLOAD_CHUNK, download_id, len(chunk))
                    if not await self.send_packet(chunk_hdr + chunk):
                        return
                    sent_bytes += len(chunk)
                    await asyncio.sleep(0.002)

            comp_pkt = STRUCT_MSG_DL_COMPLETE.pack(MSG_DOWNLOAD_COMPLETE, download_id)
            await self.send_packet(comp_pkt)
            logger.info("Download id=%d completed (%d bytes streamed to Amiga).", download_id, sent_bytes)
        except asyncio.TimeoutError:
            logger.warning("Download id=%d timed out waiting for Amiga ASL response.", download_id)
            save_task.cancel()
        except Exception as exc:
            logger.error("Error processing browser download id=%d: %s", download_id, exc)
            if not save_task.done():
                save_task.cancel()
        finally:
            self.pending_downloads.pop(download_id, None)
            try:
                if os.path.exists(tmp_path):
                    os.remove(tmp_path)
            except Exception:
                pass

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

            try:
                accepted = await asyncio.wait_for(resp_future, timeout=300.0)
            finally:
                self.pending_downloads.pop(download_id, None)
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

    # 1. Inspect /proc/cpuinfo on Linux or platform.processor() on Windows/macOS
    if _IS_WINDOWS:
        proc_str = (platform.processor() or os.environ.get("PROCESSOR_IDENTIFIER", "")).strip()
        if proc_str:
            profile["cpu_model"] = proc_str
            low_p = proc_str.lower()
            if "intel" in low_p:
                profile["cpu_vendor"] = "Intel"
            elif "amd" in low_p:
                profile["cpu_vendor"] = "AMD"
        profile["has_dri"] = True
        profile["drm_drivers"] = ["d3d11"]
    elif os.path.exists("/proc/cpuinfo"):
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

    if _IS_WINDOWS:
        return [
            "--enable-gpu",
            "--ignore-gpu-blocklist",
            "--enable-gpu-rasterization",
            "--use-gl=angle",
            "--use-angle=d3d11",
            "--enable-unsafe-swiftshader",
            "--enable-features=CanvasOopRasterization",
        ], True

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
            return ["--enable-gpu", "--use-gl=angle", "--use-angle=gles-egl", "--disable-accelerated-video-decode", "--enable-unsafe-swiftshader"], has_dri
        return ["--disable-gpu", "--disable-software-rasterizer"], False

    # Under Linux --ozone-platform=headless, Chromium's VaapiVideoDecoder + --enable-zero-copy
    # crashes gpu-process on H.264 video playback (after 3 crashes Chromium falls back to
    # --use-gl=disabled, breaking WebGL 1 & WebGL 2). Keeping hardware ANGLE GPU rasterization
    # & WebGL 1/2 active while disabling in-browser VA-API video decode prevents GPU crashes
    # (FFmpeg H.264 stream encoding still uses /dev/dri VA-API independently).
    enable_chrome_vaapi = (os.environ.get("VORTEX_ENABLE_CHROME_VAAPI", "0").strip() == "1")
    gpu_flags = [
        "--enable-gpu",
        "--ignore-gpu-blocklist",
        "--enable-gpu-rasterization",
        "--enable-unsafe-swiftshader",
    ]
    if not enable_chrome_vaapi:
        gpu_flags.append("--disable-accelerated-video-decode")

    if hw_profile.get("is_rpi"):
        # Raspberry Pi (Broadcom VC4 / V3D GPU): ANGLE on OpenGL ES / EGL (no VA-API on Pi)
        gpu_flags.extend([
            "--use-gl=angle",
            "--use-angle=gles-egl",
            "--enable-features=CanvasOopRasterization",
        ])
    elif hw_profile.get("has_intel_gpu") and not hw_profile.get("has_amd_gpu") and not hw_profile.get("has_nvidia_gpu"):
        # Intel GPU: ANGLE GLES/EGL backend (WebGL 1/2 + hardware rasterization)
        feats = "CanvasOopRasterization,VaapiVideoDecoder,VaapiVideoEncoder" if enable_chrome_vaapi else "CanvasOopRasterization"
        gpu_flags.extend([
            "--use-gl=angle",
            "--use-angle=gles-egl",
            f"--enable-features={feats}",
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
            # Modern AMD amdgpu DRM: ANGLE Vulkan (RADV) for WebGL 1/2 & GPU rasterization
            feats = "CanvasOopRasterization,Vulkan,VaapiVideoDecoder" if enable_chrome_vaapi else "CanvasOopRasterization"
            gpu_flags.extend([
                "--use-gl=angle",
                "--use-angle=vulkan",
                f"--enable-features={feats}",
            ])
    else:
        # Generic Mesa / NVIDIA / ARM GPU: ANGLE GLES/EGL
        feats = "CanvasOopRasterization,VaapiVideoDecoder" if enable_chrome_vaapi else "CanvasOopRasterization"
        gpu_flags.extend([
            "--use-gl=angle",
            "--use-angle=gles-egl",
            f"--enable-features={feats}",
        ])

    return gpu_flags, has_dri


def _is_pid_alive(pid: int) -> bool:
    if pid <= 0:
        return False
    if _IS_WINDOWS:
        try:
            PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
            STILL_ACTIVE = 259
            h = ctypes.windll.kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, int(pid))
            if not h:
                return False
            try:
                code = ctypes.c_ulong(0)
                if ctypes.windll.kernel32.GetExitCodeProcess(h, ctypes.byref(code)):
                    return code.value == STILL_ACTIVE
                return False
            finally:
                ctypes.windll.kernel32.CloseHandle(h)
        except Exception:
            return False
    return os.path.exists(f"/proc/{pid}")


def _wait_pids_exit(pids: List[int], timeout: float) -> List[int]:
    """Polls until all PIDs exit or timeout elapses; returns remaining alive PIDs."""
    deadline = time.monotonic() + timeout
    remaining = list(pids)
    while remaining and time.monotonic() < deadline:
        remaining = [pid for pid in remaining if _is_pid_alive(pid)]
        if not remaining:
            break
        time.sleep(0.1)
    return [pid for pid in remaining if _is_pid_alive(pid)]


def _find_proc_pids_matching(predicate) -> List[int]:
    if _IS_WINDOWS or not os.path.isdir("/proc"):
        return []
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


def _reap_child_zombies() -> None:
    """Reaps any terminated direct child processes on POSIX so zero <defunct> zombies remain."""
    if _IS_WINDOWS or not hasattr(os, "waitpid"):
        return
    for _ in range(64):
        try:
            pid, _ = os.waitpid(-1, os.WNOHANG)
            if pid <= 0:
                break
        except ChildProcessError:
            break
        except Exception:
            break


def _get_direct_child_pids(parent_pid: int) -> List[int]:
    """Returns live direct child PIDs of parent_pid on Linux via /proc."""
    if _IS_WINDOWS or not os.path.isdir("/proc"):
        return []
    children: List[int] = []
    try:
        entries = os.listdir("/proc")
    except Exception:
        return children
    ppid_prefix = f"PPid:\t{parent_pid}\n"
    for entry in entries:
        if not entry.isdigit():
            continue
        pid = int(entry)
        if pid == parent_pid:
            continue
        try:
            with open(f"/proc/{pid}/status", "r", encoding="utf-8", errors="ignore") as f:
                st = f.read(1024)
            if ppid_prefix in st:
                children.append(pid)
        except Exception:
            continue
    return children


def _cleanup_browser_processes_and_locks(profile_dir: str, engine: str = "chromium") -> None:
    """Terminates any lingering browser, crashpad, Playwright driver, ffmpeg, or parec processes and clears lock files."""
    _reap_child_zombies()
    sig_kill = getattr(signal, "SIGKILL", signal.SIGTERM)

    if not _IS_WINDOWS and os.path.isdir("/proc"):
        real_profile = os.path.realpath(profile_dir)
        user_data_flags = {f"--user-data-dir={profile_dir}", f"--user-data-dir={real_profile}"}
        crashpad_pids: Set[int] = set()

        def _matches_profile_proc(pid: int, argv: List[str], main_only: bool) -> bool:
            matched = False
            if any(a in user_data_flags for a in argv):
                matched = True
            elif engine == "firefox" and (profile_dir in argv or real_profile in argv):
                matched = True
            if not matched:
                return False
            for a in argv:
                if a.startswith("--crashpad-handler-pid="):
                    val = a.split("=", 1)[1].strip()
                    if val.isdigit():
                        crashpad_pids.add(int(val))
            if main_only and any(a.startswith("--type=") for a in argv):
                return False
            return True

        # 1. Gracefully stop main browser processes bound to our profile first
        main_pids = _find_proc_pids_matching(lambda pid, argv: _matches_profile_proc(pid, argv, True))
        if main_pids:
            logger.info("Stopping lingering browser main process(es) %s for %s...", main_pids, profile_dir)
            for pid in main_pids:
                try:
                    os.kill(pid, signal.SIGTERM)
                except Exception:
                    pass
            _wait_pids_exit(main_pids, timeout=2.0)

        # 2. Collect all remaining profile processes, referenced crashpad handlers, and direct leaked helper children
        stray_pids: List[int] = _find_proc_pids_matching(lambda pid, argv: _matches_profile_proc(pid, argv, False))
        for cpid in list(crashpad_pids):
            if _is_pid_alive(cpid) and cpid not in stray_pids:
                stray_pids.append(cpid)
            for gchild in _get_direct_child_pids(cpid):
                if gchild not in stray_pids:
                    stray_pids.append(gchild)

        helper_names = ("chromium", "chrome", "firefox", "ffmpeg", "parec", "node", "chrome_crashpad_handler")
        for cpid in _get_direct_child_pids(os.getpid()):
            if cpid in stray_pids:
                continue
            try:
                with open(f"/proc/{cpid}/cmdline", "rb") as f:
                    raw = f.read()
                if not raw:
                    continue
                argv0 = raw.split(b"\x00", 1)[0].decode("utf-8", errors="ignore")
                base = os.path.basename(argv0).lower()
                if any(h in base for h in helper_names):
                    stray_pids.append(cpid)
            except Exception:
                continue

        if stray_pids:
            logger.info("Cleaning up remaining browser/helper process(es): %s", stray_pids)
            for pid in stray_pids:
                try:
                    os.kill(pid, signal.SIGTERM)
                except Exception:
                    pass
            remaining = _wait_pids_exit(stray_pids, timeout=1.5)
            for pid in remaining:
                try:
                    os.kill(pid, sig_kill)
                except Exception:
                    pass
            if remaining:
                _wait_pids_exit(remaining, timeout=0.5)

        # 3. If no Chromium process remains for this UID at all, clean any orphaned chrome_crashpad_handler daemons
        my_uid = os.getuid() if hasattr(os, "getuid") else -1
        if my_uid >= 0:
            def _same_uid(pid: int) -> bool:
                try:
                    return os.stat(f"/proc/{pid}").st_uid == my_uid
                except Exception:
                    return False

            any_user_chrome = _find_proc_pids_matching(
                lambda pid, argv: _same_uid(pid)
                and os.path.basename(argv[0]).lower() in ("chromium", "chrome", "ungoogled-chromium", "chromium-browser")
            )
            if not any_user_chrome:
                orphan_cp = _find_proc_pids_matching(
                    lambda pid, argv: _same_uid(pid)
                    and "chrome_crashpad_handler" in os.path.basename(argv[0]).lower()
                )
                for pid in orphan_cp:
                    try:
                        os.kill(pid, signal.SIGTERM)
                    except Exception:
                        pass
                if orphan_cp:
                    rem_cp = _wait_pids_exit(orphan_cp, timeout=0.8)
                    for pid in rem_cp:
                        try:
                            os.kill(pid, sig_kill)
                        except Exception:
                            pass

        _reap_child_zombies()

    # Clean stale Chromium/Firefox lock files
    for lock_name in ("SingletonLock", "SingletonCookie", "SingletonSocket", "lockfile", "parent.lock", ".parentlock"):
        lp = os.path.join(profile_dir, lock_name)
        if os.path.lexists(lp):
            try:
                os.remove(lp)
            except Exception:
                pass


def _cleanup_existing_server_and_locks(port: int, profile_dir: str, config_dir: str) -> None:
    """Gracefully stops older matching server.py or orphaned browser locks without corrupting SQLite."""
    pid_file = os.path.join(config_dir, "server.pid")
    other_pids: List[int] = []
    sig_kill = getattr(signal, "SIGKILL", signal.SIGTERM)

    if os.path.isfile(pid_file):
        try:
            with open(pid_file, "r", encoding="utf-8") as f:
                recorded_pid = int(f.read().strip())
            if recorded_pid != os.getpid() and _is_pid_alive(recorded_pid):
                if _IS_WINDOWS:
                    other_pids.append(recorded_pid)
                else:
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
        logger.info("Gracefully stopping previous VortexRemoteBrowser server instance(s) %s...", other_pids)
        for pid in other_pids:
            try:
                if _IS_WINDOWS:
                    subprocess.run(
                        ["taskkill", "/PID", str(pid), "/T", "/F"],
                        stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL,
                        timeout=4,
                        creationflags=_SUBPROC_NO_WINDOW,
                    )
                else:
                    os.kill(pid, signal.SIGTERM)
            except Exception:
                pass
        remaining = _wait_pids_exit(other_pids, timeout=5.0)
        for pid in remaining:
            logger.warning("Previous server PID %d did not exit after SIGTERM timeout; forcing exit", pid)
            try:
                os.kill(pid, sig_kill)
            except Exception:
                pass
        if remaining:
            _wait_pids_exit(remaining, timeout=1.0)

    _cleanup_browser_processes_and_locks(profile_dir)


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


async def _close_browser_gracefully(browser: BrowserContext, use_rustwright: bool, cdp_port: int = 9222) -> None:
    """Sends CDP Browser.close when needed and waits cleanly so SQLite/LevelDB flush to disk."""
    if not browser:
        return
    if use_rustwright and _HAS_WEBSOCKETS and websockets is not None:
        try:
            def _fetch_ws_url() -> Optional[str]:
                with urllib.request.urlopen(f"http://127.0.0.1:{cdp_port}/json/version", timeout=1.5) as resp:
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
        await asyncio.wait_for(browser.close(), timeout=6.0)
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
        user_tag = str(os.getuid()) if hasattr(os, "getuid") else re.sub(r"[^a-zA-Z0-9_-]", "_", getpass.getuser() or "user")
        fallback_dir = os.path.join(tempfile.gettempdir(), f"vortexremotebrowser_{engine}_profile_{user_tag}")
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
    win_prev_sig_handlers: Dict[int, Any] = {}
    for sig in (signal.SIGTERM, signal.SIGINT):
        try:
            loop.add_signal_handler(sig, stop_event.set)
            registered_signals.append(sig)
        except NotImplementedError:
            try:
                def _win_sig_handler(_signum, _frame):
                    loop.call_soon_threadsafe(stop_event.set)
                win_prev_sig_handlers[sig] = signal.getsignal(sig)
                signal.signal(sig, _win_sig_handler)
            except Exception:
                pass
    if _IS_WINDOWS and hasattr(signal, "SIGBREAK"):
        try:
            sig_brk = signal.SIGBREAK
            def _win_brk_handler(_signum, _frame):
                loop.call_soon_threadsafe(stop_event.set)
            win_prev_sig_handlers[sig_brk] = signal.getsignal(sig_brk)
            signal.signal(sig_brk, _win_brk_handler)
        except Exception:
            pass

    if getattr(args, "exit_on_stdin_eof", False):
        def _watch_stdin_eof() -> None:
            try:
                while True:
                    chunk = sys.stdin.buffer.read(1) if hasattr(sys.stdin, "buffer") else sys.stdin.read(1)
                    if not chunk:
                        break
            except Exception:
                pass
            try:
                loop.call_soon_threadsafe(stop_event.set)
            except Exception:
                pass

        threading.Thread(target=_watch_stdin_eof, daemon=True, name="vb_stdin_eof").start()

    audio_device = getattr(args, "audio_device", "") or ""
    tab_audio_bridge: Optional[SilentTabAudioBridge] = None
    if getattr(args, "no_audio", False):
        audio_ready = False
        logger.info("Audio streaming disabled by --no-audio / VORTEX_NO_AUDIO.")
    else:
        if _should_use_tabcapture_audio(engine, audio_device, no_audio=False):
            bridge = SilentTabAudioBridge()
            if await bridge.start():
                tab_audio_bridge = bridge
        audio_ready = ensure_audio_sink(audio_device, tabcapture_active=(tab_audio_bridge is not None))
    portal_url = resolve_startpage_url()

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
    if audio_ready and tab_audio_bridge is None and platform.system() == "Linux":
        browser_env["PULSE_SINK"] = AUDIO_SINK_NAME

    cdp_port = 9222 if args.port == 7788 else (9222 + (args.port % 1000))
    if engine == "chromium":
        custom_bin = getattr(args, "browser_bin", None)
        full_chrome = find_full_chromium_executable(custom_bin)
        enable_exts = getattr(args, "enable_extensions", False)
        all_exts = list(find_all_extension_paths()) if enable_exts else []
        if tab_audio_bridge is not None and tab_audio_bridge.ext_dir:
            all_exts.append(tab_audio_bridge.ext_dir)

        # Clean up stale SingletonLock/Socket/Cookie from prior crashes or killed processes
        for lock_file in ("SingletonLock", "SingletonCookie", "SingletonSocket", "lockfile"):
            lp = os.path.join(profile_dir, lock_file)
            if os.path.exists(lp) or os.path.islink(lp):
                try:
                    os.unlink(lp)
                    logger.info("Cleaned up stale lock file: %s", lp)
                except Exception as e:
                    logger.warning("Could not remove lock file %s: %s", lp, e)

        if hasattr(os, "getuid") and os.getuid() == 0:
            logger.warning("=" * 68)
            logger.warning(" HINWEIS: Der VortexRemoteBrowser Server laeuft als ROOT (UID 0)!")
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
        js_heap_mb = 512 if is_low_mem else 1536
        renderer_limit = 2 if is_low_mem else 4

        if hw.get("is_low_power") or hw.get("cpu_cores", 4) <= 4:
            raster_threads = 2
            logger.info("Low-power CPU detected: Adjusted raster threads to 2 (preserves cores for stream encoding)")
        else:
            raster_threads = 4

        if platform.system() == "Linux":
            ozone_flags = ["--ozone-platform=headless"]
        elif _IS_WINDOWS and os.environ.get("VORTEX_SHOW_BROWSER", "0") != "1":
            # Full Chromium headless=new on Windows: supports extensions, D3D11 GPU & WASAPI audio with zero visible window
            ozone_flags = ["--headless=new"]
        else:
            ozone_flags = []
        launch_args = _merge_chromium_feature_flags(ozone_flags + [
            f"--lang={server_locale}",
            f"--remote-debugging-port={cdp_port}",
            "--remote-allow-origins=*",
            "--no-sandbox",
            "--disable-dev-shm-usage",
            "--disable-crash-reporter",
            "--disable-breakpad",
            "--font-render-hinting=medium",
            "--enable-font-antialiasing",
            "--enable-smooth-scrolling",
            "--autoplay-policy=no-user-gesture-required",
            "--disable-blink-features=AutomationControlled",
            f"--num-raster-threads={raster_threads}",
            "--enable-low-res-tiling",
            f"--js-flags=--max-old-space-size={js_heap_mb}",
            f"--renderer-process-limit={renderer_limit}",
            "--process-per-site",
            "--disk-cache-size=67108864",
            "--media-cache-size=33554432",
            "--disable-features=Translate,OptimizationHints,MediaRouter,DialMediaRouteProvider",
        ] + gpu_flags)
        if all_exts:
            ext_list = ",".join(all_exts)
            logger.info("Enabling unpacked Chrome extensions (%d): %s", len(all_exts), ext_list)
            launch_args.append(f"--disable-extensions-except={ext_list}")
            launch_args.append(f"--load-extension={ext_list}")
            if tab_audio_bridge is not None:
                launch_args.append(tab_audio_bridge.allowlist_flag)
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

    full_chrome = full_chrome if engine == "chromium" else None
    all_exts = all_exts if engine == "chromium" else []
    launch_args = launch_args if engine == "chromium" else []
    gpu_flags = gpu_flags if engine == "chromium" else []
    raster_threads = raster_threads if engine == "chromium" else 2
    js_heap_mb = js_heap_mb if engine == "chromium" else 1024
    renderer_limit = renderer_limit if engine == "chromium" else 4

    async def _launch_browser_runtime() -> Tuple[Any, BrowserContext, Dict[str, Any]]:
        """Launches a fresh browser instance on-demand when an Amiga client connects."""
        await loop.run_in_executor(None, _cleanup_browser_processes_and_locks, profile_dir, engine)
        local_needs_recovery = check_profile_needs_recovery(profile_dir, state_file, engine, is_fallback_profile)
        pw_cm = pw_factory()
        pw = await pw_cm.__aenter__()
        try:
            if engine == "firefox":
                logger.info("Launching Playwright Firefox persistent context at %s...", profile_dir)
                firefox_user_agent = (
                    "Mozilla/5.0 (X11; Linux x86_64; rv:134.0) Gecko/20100101 Firefox/134.0"
                )
                br = await pw.firefox.launch_persistent_context(
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
                }
                if use_rustwright:
                    # Avoid Rustwright calling Emulation.setLocaleOverride per popup (--process-per-site shares renderers)
                    launch_kwargs["extra_http_headers"] = {"Accept-Language": build_accept_language(server_locale)}
                else:
                    launch_kwargs["locale"] = server_locale
                detected_ua = _detect_chromium_ua(full_chrome)
                if detected_ua:
                    launch_kwargs["user_agent"] = detected_ua
                br = await pw.chromium.launch_persistent_context(**launch_kwargs)
            else:
                logger.info("Launching Headless Chromium fallback persistent context at %s...", profile_dir)
                fallback_args = _merge_chromium_feature_flags([
                    f"--lang={server_locale}",
                    f"--remote-debugging-port={cdp_port}",
                    "--remote-allow-origins=*",
                    "--no-sandbox",
                    "--disable-dev-shm-usage",
                    "--disable-crash-reporter",
                    "--disable-breakpad",
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
                    if tab_audio_bridge is not None:
                        fallback_args.append(tab_audio_bridge.allowlist_flag)
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
                }
                if use_rustwright:
                    fallback_kwargs["extra_http_headers"] = {"Accept-Language": build_accept_language(server_locale)}
                else:
                    fallback_kwargs["locale"] = server_locale
                detected_ua = _detect_chromium_ua(getattr(pw.chromium, "executable_path", None))
                if detected_ua:
                    fallback_kwargs["user_agent"] = detected_ua
                br = await pw.chromium.launch_persistent_context(**fallback_kwargs)

            if local_needs_recovery:
                await recover_profile_storage_state(br, state_file)
            await deduplicate_context_cookies(br)

            ctx_state: Dict[str, Any] = {
                "scripts_initialized": False,
                "active_session": None,
            }
            return pw_cm, br, ctx_state
        except Exception:
            try:
                await asyncio.wait_for(pw_cm.__aexit__(None, None, None), timeout=3.0)
            except Exception:
                pass
            await loop.run_in_executor(None, _cleanup_browser_processes_and_locks, profile_dir, engine)
            raise

    async def _teardown_browser_runtime(
        pw_cm: Any,
        br: Optional[BrowserContext],
        skip_storage_export: bool = False,
    ) -> None:
        """Completely closes the browser, driver, and any lingering processes and returns RAM to OS."""
        logger.info("Client disconnected: shutting down browser instance and reclaiming memory...")
        if br is not None and not skip_storage_export:
            try:
                await asyncio.wait_for(export_merged_storage_state(br, state_file), timeout=4.0)
            except Exception:
                pass
        if br is not None:
            try:
                await _close_browser_gracefully(br, use_rustwright, cdp_port=cdp_port)
            except Exception as exc:
                logger.debug("Browser close notice: %s", exc)
        if pw_cm is not None:
            try:
                await asyncio.wait_for(pw_cm.__aexit__(None, None, None), timeout=4.0)
            except Exception as exc:
                logger.debug("Playwright/Rustwright exit notice: %s", exc)
        br = None
        pw_cm = None
        await loop.run_in_executor(None, _cleanup_browser_processes_and_locks, profile_dir, engine)
        _reclaim_python_memory()
        logger.info("Browser instance fully stopped; server is now idle with zero browser processes.")

    try:
        global cpu_gov_global
        cpu_gov = CPUGovernorManager(enabled=not getattr(args, "no_cpu_governor", False))
        cpu_gov_global = cpu_gov

        active_session: Optional[AmigaClientSession] = None
        browser_lock = asyncio.Lock()

        async def handle_client(reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
            nonlocal active_session
            # Immediately ask any previous active session to stop before waiting on browser_lock
            prev_sess = active_session
            if prev_sess is not None:
                logger.info("New client connected: closing previous session before launching fresh browser")
                prev_sess.running = False
                try:
                    prev_sess.writer.close()
                except Exception:
                    pass

            await cpu_gov.client_connected()
            pw_cm = None
            browser: Optional[BrowserContext] = None
            session: Optional[AmigaClientSession] = None
            try:
                async with browser_lock:
                    if not getattr(reader, "_buffer", None):
                        await asyncio.sleep(0.05)
                    if reader.at_eof() or stop_event.is_set():
                        try:
                            writer.close()
                            await asyncio.wait_for(writer.wait_closed(), timeout=0.5)
                        except Exception:
                            pass
                        return

                    # Send immediate status hint to Amiga while browser starts on-demand
                    try:
                        msg_b = "Starte Browser...".encode("latin-1", errors="replace")[:63].ljust(64, b"\x00")
                        writer.write(STRUCT_MSG_PROGRESS.pack(MSG_PAGE_PROGRESS, 10, msg_b))
                        await asyncio.wait_for(writer.drain(), timeout=1.0)
                    except Exception:
                        pass

                    try:
                        pw_cm, browser, context_state = await _launch_browser_runtime()
                    except Exception as launch_exc:
                        logger.error("Failed to launch browser on client connect: %s", launch_exc)
                        try:
                            writer.close()
                            await asyncio.wait_for(writer.wait_closed(), timeout=0.5)
                        except Exception:
                            pass
                        return

                    if reader.at_eof() or stop_event.is_set():
                        await _teardown_browser_runtime(pw_cm, browser, skip_storage_export=True)
                        pw_cm = None
                        browser = None
                        try:
                            writer.close()
                            await asyncio.wait_for(writer.wait_closed(), timeout=0.5)
                        except Exception:
                            pass
                        return

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
                        use_rustwright=use_rustwright,
                        audio_device=audio_device,
                        tab_audio_bridge=tab_audio_bridge,
                        close_browser_on_exit=True,
                    )
                    session.cdp_port = cdp_port
                    active_session = session

                    storage_already_saved = False
                    try:
                        await session.run()
                    finally:
                        storage_already_saved = bool(getattr(session, "_storage_saved_on_exit", False))
                        if active_session is session:
                            active_session = None
                        session = None
                        context_state = None
                        await _teardown_browser_runtime(
                            pw_cm,
                            browser,
                            skip_storage_export=storage_already_saved,
                        )
                        pw_cm = None
                        browser = None
                        _reclaim_python_memory()
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

        stream_port = int(os.environ.get("VORTEX_STREAM_PORT", "7779"))
        try:
            srv_stream = await asyncio.start_server(_handle_ext_video_stream_client, args.host, stream_port)
            extra_servers.append(srv_stream)
            logger.info("External Video Player HTTP Relay listening on %s:%d", args.host, stream_port)
        except OSError as exc:
            logger.warning("Could not bind external video player HTTP relay port %d: %s", stream_port, exc)

        all_socks = list(server.sockets or [])
        for s in extra_servers:
            all_socks.extend(s.sockets or [])
        addrs = ", ".join(str(sock.getsockname()) for sock in all_socks)
        if audio_ready:
            if tab_audio_bridge is not None:
                audio_desc = "ON (Silent Browser TabCapture -> AHI; Host Speakers Muted)"
            elif _IS_WINDOWS:
                audio_desc = "ON (Windows WASAPI/DirectShow -> AHI)"
            elif platform.system() == "Darwin":
                audio_desc = "ON (macOS AVFoundation -> AHI)"
            else:
                audio_desc = "ON (Full Browser -> PipeWire -> AHI)"
        else:
            audio_desc = "OFF"
        logger.info(
            "VortexRemoteBrowser Server ready on %s (On-Demand Browser) | Engine: %s | Viewport: %dx%d (1:1 Auto-Sync) | Format: %s | Codec: LZ4+DirtySlice | Audio: %s",
            addrs,
            engine.upper(),
            args.width,
            args.height,
            "YUV420P" if pixel_fmt == FMT_YUV420P else ("RGB565" if pixel_fmt == FMT_RGB565 else "ARGB32"),
            audio_desc,
        )
        _reclaim_python_memory()

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
            logger.info("Shutting down VortexRemoteBrowser Server cleanly...")
            sess = active_session
            if sess is not None:
                sess.running = False
                try:
                    sess.writer.close()
                except Exception:
                    pass
            # Wait briefly for any active handle_client() to finish browser teardown cleanly
            try:
                await asyncio.wait_for(browser_lock.acquire(), timeout=8.0)
                browser_lock.release()
            except Exception:
                pass
            cpu_gov.restore()
            for s in extra_servers:
                s.close()
                await s.wait_closed()
            await loop.run_in_executor(None, _cleanup_browser_processes_and_locks, profile_dir, engine)
            _reclaim_python_memory()
    finally:
        for sig in registered_signals:
            try:
                loop.remove_signal_handler(sig)
            except Exception:
                pass
        for sig, prev_h in win_prev_sig_handlers.items():
            try:
                signal.signal(sig, prev_h)
            except Exception:
                pass
        if tab_audio_bridge is not None:
            await tab_audio_bridge.stop()
        _remove_server_pid(pid_file)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="VortexRemoteBrowser Universal Companion Server (Linux / Raspberry Pi / macOS / Windows)")
    parser.add_argument("--host", default="0.0.0.0", help="Bind address (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=7788, help="TCP port (default: 7788, plus 9757 for LAN)")
    parser.add_argument("--portal-port", type=int, default=0, help=argparse.SUPPRESS)
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
        "--no-audio",
        action="store_true",
        default=os.environ.get("VORTEX_NO_AUDIO", "").strip().lower() in ("1", "true", "yes"),
        help="Disable audio streaming [env: VORTEX_NO_AUDIO=1]",
    )
    parser.add_argument(
        "--audio-device",
        default=os.environ.get("VORTEX_AUDIO_DEVICE", ""),
        help="Optional audio backend/device override: 'tabcapture' (silent browser-only audio, Windows default), 'wasapi' (Windows system loopback), or DirectShow device name [env: VORTEX_AUDIO_DEVICE]",
    )
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
    parser.add_argument(
        "--exit-on-stdin-eof",
        action="store_true",
        default=False,
        help=argparse.SUPPRESS,
    )
    return parser.parse_args()


if __name__ == "__main__":
    try:
        asyncio.run(run_server(parse_args()))
    except KeyboardInterrupt:
        logger.info("VortexRemoteBrowser Server stopped.")
