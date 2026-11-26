#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""
VortexBrowser Native Windows Invisible Background & System Tray Launcher
========================================================================
Runs server.py completely invisibly in the background (zero console window)
with an optional native Win32 system tray icon (zero external dependencies).

Usage:
  - Double-click run_windows_hidden.vbs  (recommended, zero console flash)
  - Or run via pythonw.exe:
      .venv\Scripts\pythonw.exe run_windows_tray.pyw [extra server.py args]
  - Headless without tray icon:
      .venv\Scripts\pythonw.exe run_windows_tray.pyw --no-tray
  - Stop a running background instance cleanly:
      .venv\Scripts\python.exe run_windows_tray.pyw --stop
"""

from __future__ import annotations

import ctypes
import math
import os
import platform
import signal
import socket
import subprocess
import sys
import time
from typing import List, Optional, Tuple

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
SERVER_PY = os.path.join(SCRIPT_DIR, "server.py")
TRAY_CLASS_NAME = "VortexBrowserTrayClass"
TRAY_WINDOW_TITLE = "VortexBrowser Companion Server"
MUTEX_NAME = "Local\\VortexBrowserServerTrayMutex"


def _get_config_dir() -> str:
    env_dir = os.environ.get("VORTEX_PROFILE_DIR", "").strip()
    if env_dir:
        d = os.path.abspath(os.path.expanduser(env_dir))
    elif platform.system() == "Windows":
        appdata = os.environ.get("APPDATA") or os.path.expanduser("~\\AppData\\Roaming")
        d = os.path.join(appdata, "VortexBrowser")
    else:
        d = os.path.expanduser("~/.config/vortexbrowse")
    os.makedirs(d, exist_ok=True)
    return d


def _get_lan_ip() -> str:
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            s.connect(("192.0.2.1", 80))
            ip = s.getsockname()[0]
            if ip and not ip.startswith("127."):
                return ip
        finally:
            s.close()
    except Exception:
        pass
    try:
        ip = socket.gethostbyname(socket.gethostname())
        if ip and not ip.startswith("127."):
            return ip
    except Exception:
        pass
    return "127.0.0.1"


def _find_python_exe() -> str:
    venv_py = os.path.join(SCRIPT_DIR, ".venv", "Scripts", "python.exe")
    if os.path.isfile(venv_py):
        return venv_py
    exe = sys.executable or "python.exe"
    if exe.lower().endswith("pythonw.exe"):
        cand = exe[:-len("pythonw.exe")] + "python.exe"
        if os.path.isfile(cand):
            return cand
    return exe


def _build_server_cmd(extra_args: List[str]) -> Tuple[List[str], int]:
    py_exe = _find_python_exe()
    port = int(os.environ.get("VORTEX_PORT", "7788") or "7788")
    if extra_args:
        for i, a in enumerate(extra_args):
            if a == "--port" and i + 1 < len(extra_args):
                try:
                    port = int(extra_args[i + 1])
                except ValueError:
                    pass
            elif a.startswith("--port="):
                try:
                    port = int(a.split("=", 1)[1])
                except ValueError:
                    pass
        cmd = [py_exe, SERVER_PY, *extra_args]
    else:
        width = os.environ.get("VORTEX_WIDTH", "1920") or "1920"
        height = os.environ.get("VORTEX_HEIGHT", "1080") or "1080"
        fps = os.environ.get("VORTEX_FPS", "30") or "30"
        bitrate = os.environ.get("VORTEX_VIDEO_BITRATE", "10000") or "10000"
        quality = os.environ.get("VORTEX_JPEG_QUALITY", "85") or "85"
        engine = os.environ.get("VORTEX_ENGINE", "chromium") or "chromium"
        log_level = os.environ.get("VORTEX_LOG_LEVEL", "INFO") or "INFO"
        cmd = [
            py_exe,
            SERVER_PY,
            "--host", "0.0.0.0",
            "--port", str(port),
            "--width", width,
            "--height", height,
            "--fps", fps,
            "--format", "yuv420p",
            "--video-bitrate", bitrate,
            "--jpeg-quality", quality,
            "--codec", "auto",
            "--engine", engine,
            "--log-level", log_level,
        ]
    if "--exit-on-stdin-eof" not in cmd:
        cmd.append("--exit-on-stdin-eof")
    return cmd, port


def _open_log_file(log_path: str):
    try:
        if os.path.isfile(log_path) and os.path.getsize(log_path) > 2 * 1024 * 1024:
            old_path = log_path + ".old"
            try:
                if os.path.exists(old_path):
                    os.unlink(old_path)
                os.replace(log_path, old_path)
            except Exception:
                pass
    except Exception:
        pass
    fp = open(log_path, "a", encoding="utf-8", errors="replace", buffering=1)
    fp.write(f"\n=== VortexBrowser Background Server Launch ({time.strftime('%Y-%m-%d %H:%M:%S')}) ===\n")
    fp.flush()
    return fp


def _write_pid(path: str) -> None:
    try:
        with open(path, "w", encoding="utf-8") as f:
            f.write(str(os.getpid()))
    except Exception:
        pass


def _read_pid(path: str) -> Optional[int]:
    try:
        with open(path, "r", encoding="utf-8") as f:
            val = int(f.read().strip())
            return val if val > 0 else None
    except Exception:
        return None


def _remove_file_quiet(path: str) -> None:
    try:
        if os.path.exists(path):
            os.unlink(path)
    except Exception:
        pass


def _is_pid_running_windows(pid: int) -> bool:
    if pid <= 0 or platform.system() != "Windows":
        return False
    SYNCHRONIZE = 0x00100000
    PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
    STILL_ACTIVE = 259
    k32 = ctypes.windll.kernel32
    k32.OpenProcess.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32]
    k32.OpenProcess.restype = ctypes.c_void_p
    k32.GetExitCodeProcess.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint32)]
    k32.GetExitCodeProcess.restype = ctypes.c_int
    k32.CloseHandle.argtypes = [ctypes.c_void_p]
    k32.CloseHandle.restype = ctypes.c_int
    h = k32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, 0, pid)
    if not h:
        return False
    try:
        code = ctypes.c_uint32(0)
        if k32.GetExitCodeProcess(h, ctypes.byref(code)):
            return code.value == STILL_ACTIVE
        return False
    finally:
        k32.CloseHandle(h)


def _stop_running_instance() -> int:
    cfg_dir = _get_config_dir()
    tray_pid_file = os.path.join(cfg_dir, "tray.pid")
    srv_pid_file = os.path.join(cfg_dir, "server.pid")
    stopped_any = False

    if platform.system() == "Windows":
        user32 = ctypes.windll.user32
        user32.FindWindowW.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p]
        user32.FindWindowW.restype = ctypes.c_void_p
        user32.PostMessageW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t]
        user32.PostMessageW.restype = ctypes.c_int
        WM_CLOSE = 0x0010
        hwnd = user32.FindWindowW(TRAY_CLASS_NAME, None)
        if hwnd:
            print("[VortexBrowser] Sending clean shutdown signal to background tray server...")
            user32.PostMessageW(hwnd, WM_CLOSE, 0, 0)
            stopped_any = True
            deadline = time.monotonic() + 8.0
            while time.monotonic() < deadline:
                if not user32.FindWindowW(TRAY_CLASS_NAME, None):
                    spid = _read_pid(srv_pid_file)
                    if not spid or not _is_pid_running_windows(spid):
                        break
                time.sleep(0.2)

        for label, pfile in (("server", srv_pid_file), ("tray", tray_pid_file)):
            pid = _read_pid(pfile)
            if pid and _is_pid_running_windows(pid):
                print(f"[VortexBrowser] Stopping remaining {label} process (PID {pid})...")
                stopped_any = True
                try:
                    subprocess.run(
                        ["taskkill", "/PID", str(pid), "/T", "/F"],
                        stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL,
                        timeout=6,
                        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
                    )
                except Exception:
                    pass
            _remove_file_quiet(pfile)

    if stopped_any:
        print("[VortexBrowser] Background server stopped.")
    else:
        print("[VortexBrowser] No running VortexBrowser background server was found.")
    return 0


class ServerProcessController:
    def __init__(self, extra_args: List[str], cfg_dir: str) -> None:
        self.extra_args = extra_args
        self.cfg_dir = cfg_dir
        self.log_path = os.path.join(cfg_dir, "server.log")
        self.cmd, self.port = _build_server_cmd(extra_args)
        self.proc: Optional[subprocess.Popen] = None
        self.log_fp = None
        self.stopping = False
        self.restart_count = 0
        self.last_start_time = 0.0

    def start(self) -> bool:
        self.stopping = False
        if self.proc is not None and self.proc.poll() is None:
            return True
        self._close_log()
        self.log_fp = _open_log_file(self.log_path)
        env = dict(os.environ)
        env["PYTHONUNBUFFERED"] = "1"
        flags = 0
        if platform.system() == "Windows":
            flags = getattr(subprocess, "CREATE_NO_WINDOW", 0) | getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0)
        try:
            self.proc = subprocess.Popen(
                self.cmd,
                cwd=SCRIPT_DIR,
                stdin=subprocess.PIPE,
                stdout=self.log_fp,
                stderr=subprocess.STDOUT,
                env=env,
                creationflags=flags,
            )
            self.last_start_time = time.monotonic()
            return True
        except Exception as exc:
            try:
                self.log_fp.write(f"[TrayLauncher] Failed to launch server.py: {exc}\n")
                self.log_fp.flush()
            except Exception:
                pass
            self._close_log()
            return False

    def stop(self, timeout: float = 6.0) -> None:
        self.stopping = True
        p = self.proc
        self.proc = None
        if p is not None and p.poll() is None:
            # 1. Close stdin pipe -> triggers clean graceful shutdown via --exit-on-stdin-eof
            try:
                if p.stdin:
                    p.stdin.close()
            except Exception:
                pass
            # 2. Also send CTRL_BREAK_EVENT as secondary graceful signal on Windows
            if platform.system() == "Windows" and hasattr(signal, "CTRL_BREAK_EVENT"):
                try:
                    p.send_signal(signal.CTRL_BREAK_EVENT)
                except Exception:
                    pass
            try:
                p.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                try:
                    p.terminate()
                    p.wait(timeout=2.0)
                except Exception:
                    pass
                if p.poll() is None:
                    if platform.system() == "Windows":
                        try:
                            subprocess.run(
                                ["taskkill", "/PID", str(p.pid), "/T", "/F"],
                                stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL,
                                timeout=4,
                                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
                            )
                        except Exception:
                            pass
                    try:
                        p.kill()
                    except Exception:
                        pass
        self._close_log()

    def restart(self) -> bool:
        self.stop(timeout=5.0)
        time.sleep(0.3)
        self.restart_count = 0
        return self.start()

    def check_health(self) -> str:
        if self.stopping:
            return "Stopping"
        if self.proc is None:
            return "Stopped"
        rc = self.proc.poll()
        if rc is None:
            if time.monotonic() - self.last_start_time > 30.0:
                self.restart_count = 0
            return "Running"
        # Unexpected exit — attempt bounded auto-restart
        if self.restart_count < 3:
            self.restart_count += 1
            if self.log_fp:
                try:
                    self.log_fp.write(
                        f"[TrayLauncher] server.py exited unexpectedly (code={rc}); "
                        f"auto-restarting ({self.restart_count}/3)...\n"
                    )
                    self.log_fp.flush()
                except Exception:
                    pass
            time.sleep(1.0)
            if self.start():
                return f"Restarted ({self.restart_count}/3)"
        return f"Exited (code {rc})"

    def _close_log(self) -> None:
        if self.log_fp is not None:
            try:
                self.log_fp.flush()
                self.log_fp.close()
            except Exception:
                pass
            self.log_fp = None


def _run_headless_windows(ctrl: ServerProcessController, tray_pid_file: str) -> int:
    _write_pid(tray_pid_file)
    try:
        if not ctrl.start():
            return 1
        while True:
            time.sleep(1.0)
            if ctrl.proc is None:
                break
            rc = ctrl.proc.poll()
            if rc is not None:
                if ctrl.restart_count < 3 and not ctrl.stopping:
                    ctrl.check_health()
                    continue
                return rc
    except KeyboardInterrupt:
        pass
    finally:
        ctrl.stop()
        _remove_file_quiet(tray_pid_file)
    return 0


def _run_tray_windows(ctrl: ServerProcessController, tray_pid_file: str) -> int:
    from ctypes import wintypes

    LRESULT = ctypes.c_ssize_t
    WPARAM = ctypes.c_size_t
    LPARAM = ctypes.c_ssize_t
    WNDPROC = ctypes.WINFUNCTYPE(LRESULT, wintypes.HWND, wintypes.UINT, WPARAM, LPARAM)

    user32 = ctypes.windll.user32
    kernel32 = ctypes.windll.kernel32
    shell32 = ctypes.windll.shell32
    gdi32 = ctypes.windll.gdi32

    # Explicit 64-bit-safe argtypes & restypes
    kernel32.CreateMutexW.argtypes = [ctypes.c_void_p, wintypes.BOOL, wintypes.LPCWSTR]
    kernel32.CreateMutexW.restype = wintypes.HANDLE
    kernel32.GetLastError.argtypes = []
    kernel32.GetLastError.restype = wintypes.DWORD
    kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel32.CloseHandle.restype = wintypes.BOOL
    kernel32.GetModuleHandleW.argtypes = [wintypes.LPCWSTR]
    kernel32.GetModuleHandleW.restype = wintypes.HMODULE

    user32.DefWindowProcW.argtypes = [wintypes.HWND, wintypes.UINT, WPARAM, LPARAM]
    user32.DefWindowProcW.restype = LRESULT
    user32.DestroyWindow.argtypes = [wintypes.HWND]
    user32.DestroyWindow.restype = wintypes.BOOL
    user32.PostQuitMessage.argtypes = [ctypes.c_int]
    user32.PostQuitMessage.restype = None
    user32.GetMessageW.argtypes = [ctypes.POINTER(wintypes.MSG), wintypes.HWND, wintypes.UINT, wintypes.UINT]
    user32.GetMessageW.restype = wintypes.BOOL
    user32.TranslateMessage.argtypes = [ctypes.POINTER(wintypes.MSG)]
    user32.TranslateMessage.restype = wintypes.BOOL
    user32.DispatchMessageW.argtypes = [ctypes.POINTER(wintypes.MSG)]
    user32.DispatchMessageW.restype = LRESULT
    user32.SetTimer.argtypes = [wintypes.HWND, ctypes.c_size_t, wintypes.UINT, ctypes.c_void_p]
    user32.SetTimer.restype = ctypes.c_size_t
    user32.KillTimer.argtypes = [wintypes.HWND, ctypes.c_size_t]
    user32.KillTimer.restype = wintypes.BOOL
    user32.LoadIconW.argtypes = [wintypes.HINSTANCE, ctypes.c_void_p]
    user32.LoadIconW.restype = wintypes.HICON
    user32.DestroyIcon.argtypes = [wintypes.HICON]
    user32.DestroyIcon.restype = wintypes.BOOL
    user32.CreatePopupMenu.argtypes = []
    user32.CreatePopupMenu.restype = wintypes.HMENU
    user32.AppendMenuW.argtypes = [wintypes.HMENU, wintypes.UINT, ctypes.c_size_t, wintypes.LPCWSTR]
    user32.AppendMenuW.restype = wintypes.BOOL
    user32.TrackPopupMenu.argtypes = [
        wintypes.HMENU, wintypes.UINT, ctypes.c_int, ctypes.c_int,
        ctypes.c_int, wintypes.HWND, ctypes.c_void_p,
    ]
    user32.TrackPopupMenu.restype = wintypes.BOOL
    user32.DestroyMenu.argtypes = [wintypes.HMENU]
    user32.DestroyMenu.restype = wintypes.BOOL
    user32.GetCursorPos.argtypes = [ctypes.POINTER(wintypes.POINT)]
    user32.GetCursorPos.restype = wintypes.BOOL
    user32.SetForegroundWindow.argtypes = [wintypes.HWND]
    user32.SetForegroundWindow.restype = wintypes.BOOL
    user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, WPARAM, LPARAM]
    user32.PostMessageW.restype = wintypes.BOOL
    user32.MessageBoxW.argtypes = [wintypes.HWND, wintypes.LPCWSTR, wintypes.LPCWSTR, wintypes.UINT]
    user32.MessageBoxW.restype = ctypes.c_int

    # Single-instance guard
    ERROR_ALREADY_EXISTS = 183
    hmutex = kernel32.CreateMutexW(None, False, MUTEX_NAME)
    if hmutex and kernel32.GetLastError() == ERROR_ALREADY_EXISTS:
        kernel32.CloseHandle(hmutex)
        user32.MessageBoxW(
            None,
            "VortexBrowser Server is already running in the system tray.",
            "VortexBrowser Server",
            0x40,  # MB_ICONINFORMATION
        )
        return 0

    class WNDCLASSEXW(ctypes.Structure):
        _fields_ = [
            ("cbSize", wintypes.UINT),
            ("style", wintypes.UINT),
            ("lpfnWndProc", WNDPROC),
            ("cbClsExtra", ctypes.c_int),
            ("cbWndExtra", ctypes.c_int),
            ("hInstance", wintypes.HINSTANCE),
            ("hIcon", wintypes.HICON),
            ("hCursor", wintypes.HCURSOR),
            ("hbrBackground", wintypes.HBRUSH),
            ("lpszMenuName", wintypes.LPCWSTR),
            ("lpszClassName", wintypes.LPCWSTR),
            ("hIconSm", wintypes.HICON),
        ]

    class NOTIFYICONDATAW(ctypes.Structure):
        _fields_ = [
            ("cbSize", wintypes.DWORD),
            ("hWnd", wintypes.HWND),
            ("uID", wintypes.UINT),
            ("uFlags", wintypes.UINT),
            ("uCallbackMessage", wintypes.UINT),
            ("hIcon", wintypes.HICON),
            ("szTip", wintypes.WCHAR * 128),
            ("dwState", wintypes.DWORD),
            ("dwStateMask", wintypes.DWORD),
            ("szInfo", wintypes.WCHAR * 256),
            ("uVersion", wintypes.UINT),
            ("szInfoTitle", wintypes.WCHAR * 64),
            ("dwInfoFlags", wintypes.DWORD),
        ]

    class BITMAPINFOHEADER(ctypes.Structure):
        _fields_ = [
            ("biSize", wintypes.DWORD),
            ("biWidth", wintypes.LONG),
            ("biHeight", wintypes.LONG),
            ("biPlanes", wintypes.WORD),
            ("biBitCount", wintypes.WORD),
            ("biCompression", wintypes.DWORD),
            ("biSizeImage", wintypes.DWORD),
            ("biXPelsPerMeter", wintypes.LONG),
            ("biYPelsPerMeter", wintypes.LONG),
            ("biClrUsed", wintypes.DWORD),
            ("biClrImportant", wintypes.DWORD),
        ]

    class ICONINFO(ctypes.Structure):
        _fields_ = [
            ("fIcon", wintypes.BOOL),
            ("xHotspot", wintypes.DWORD),
            ("yHotspot", wintypes.DWORD),
            ("hbmMask", wintypes.HBITMAP),
            ("hbmColor", wintypes.HBITMAP),
        ]

    user32.RegisterClassExW.argtypes = [ctypes.POINTER(WNDCLASSEXW)]
    user32.RegisterClassExW.restype = wintypes.ATOM
    user32.CreateWindowExW.argtypes = [
        wintypes.DWORD, wintypes.LPCWSTR, wintypes.LPCWSTR, wintypes.DWORD,
        ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
        wintypes.HWND, wintypes.HMENU, wintypes.HINSTANCE, ctypes.c_void_p,
    ]
    user32.CreateWindowExW.restype = wintypes.HWND
    user32.CreateIconIndirect.argtypes = [ctypes.POINTER(ICONINFO)]
    user32.CreateIconIndirect.restype = wintypes.HICON
    shell32.Shell_NotifyIconW.argtypes = [wintypes.DWORD, ctypes.POINTER(NOTIFYICONDATAW)]
    shell32.Shell_NotifyIconW.restype = wintypes.BOOL
    gdi32.CreateDIBSection.argtypes = [
        wintypes.HDC, ctypes.POINTER(BITMAPINFOHEADER), wintypes.UINT,
        ctypes.POINTER(ctypes.c_void_p), wintypes.HANDLE, wintypes.DWORD,
    ]
    gdi32.CreateDIBSection.restype = wintypes.HBITMAP
    gdi32.CreateBitmap.argtypes = [ctypes.c_int, ctypes.c_int, wintypes.UINT, wintypes.UINT, ctypes.c_void_p]
    gdi32.CreateBitmap.restype = wintypes.HBITMAP
    gdi32.DeleteObject.argtypes = [wintypes.HGDIOBJ]
    gdi32.DeleteObject.restype = wintypes.BOOL

    def _create_vortex_hicon() -> Tuple[int, bool]:
        """Generates a crisp 16x16 blue-cyan Vortex orb HICON in memory (no external .ico needed)."""
        try:
            size = 16
            bmi = BITMAPINFOHEADER()
            bmi.biSize = ctypes.sizeof(BITMAPINFOHEADER)
            bmi.biWidth = size
            bmi.biHeight = -size  # top-down DIB
            bmi.biPlanes = 1
            bmi.biBitCount = 32
            bmi.biCompression = 0  # BI_RGB
            bits_ptr = ctypes.c_void_p()
            hbm_color = gdi32.CreateDIBSection(None, ctypes.byref(bmi), 0, ctypes.byref(bits_ptr), None, 0)
            if not hbm_color or not bits_ptr.value:
                raise RuntimeError("CreateDIBSection failed")
            buf = bytearray(size * size * 4)
            cx = (size - 1) / 2.0
            cy = (size - 1) / 2.0
            for y in range(size):
                for x in range(size):
                    dx = (x - cx) / 7.2
                    dy = (y - cy) / 7.2
                    r = math.hypot(dx, dy)
                    idx = (y * size + x) * 4
                    if r <= 1.0:
                        ang = math.atan2(dy, dx)
                        swirl = 0.5 + 0.5 * math.sin(ang * 2.0 + r * 5.0)
                        edge = max(0.0, min(1.0, (1.0 - r) * 6.0))
                        alpha = int(255 * edge)
                        # Glossy cyan/blue Vortex palette (BGRA)
                        red = int(20 + 60 * swirl * (1.0 - r * 0.4))
                        green = int(150 + 95 * (1.0 - r * 0.5) * (0.6 + 0.4 * swirl))
                        blue = int(215 + 40 * (1.0 - r * 0.3))
                        buf[idx + 0] = min(255, blue)
                        buf[idx + 1] = min(255, green)
                        buf[idx + 2] = min(255, red)
                        buf[idx + 3] = alpha
            ctypes.memmove(bits_ptr.value, bytes(buf), len(buf))
            hbm_mask = gdi32.CreateBitmap(size, size, 1, 1, None)
            ii = ICONINFO()
            ii.fIcon = True
            ii.hbmMask = hbm_mask
            ii.hbmColor = hbm_color
            hicon = user32.CreateIconIndirect(ctypes.byref(ii))
            gdi32.DeleteObject(hbm_color)
            if hbm_mask:
                gdi32.DeleteObject(hbm_mask)
            if hicon:
                return hicon, True
        except Exception:
            pass
        IDI_APPLICATION = ctypes.c_void_p(32512)
        return user32.LoadIconW(None, IDI_APPLICATION), False

    WM_DESTROY = 0x0002
    WM_CLOSE = 0x0010
    WM_QUERYENDSESSION = 0x0011
    WM_ENDSESSION = 0x0016
    WM_COMMAND = 0x0111
    WM_TIMER = 0x0113
    WM_APP = 0x8000
    WM_TRAY = WM_APP + 1
    WM_LBUTTONDBLCLK = 0x0203
    WM_RBUTTONUP = 0x0205
    WM_CONTEXTMENU = 0x007B

    NIM_ADD = 0x00000000
    NIM_MODIFY = 0x00000001
    NIM_DELETE = 0x00000002
    NIF_MESSAGE = 0x00000001
    NIF_ICON = 0x00000002
    NIF_TIP = 0x00000004
    NIF_INFO = 0x00000010
    NIIF_INFO = 0x00000001

    MF_STRING = 0x0000
    MF_GRAYED = 0x0001
    MF_SEPARATOR = 0x0800
    TPM_RIGHTBUTTON = 0x0002
    TPM_BOTTOMALIGN = 0x0020

    ID_STATUS = 1001
    ID_OPEN_LOG = 1002
    ID_OPEN_DIR = 1003
    ID_RESTART = 1004
    ID_EXIT = 1005
    TIMER_HEALTH = 1

    lan_ip = _get_lan_ip()
    hicon, owns_icon = _create_vortex_hicon()
    state = {"hwnd": None, "status": "Starting", "nid_added": False}

    def _make_nid(tip: str, balloon_title: str = "", balloon_msg: str = "") -> NOTIFYICONDATAW:
        nid = NOTIFYICONDATAW()
        nid.cbSize = ctypes.sizeof(NOTIFYICONDATAW)
        nid.hWnd = state["hwnd"]
        nid.uID = 1
        nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP
        nid.uCallbackMessage = WM_TRAY
        nid.hIcon = hicon
        nid.szTip = tip[:127]
        if balloon_msg:
            nid.uFlags |= NIF_INFO
            nid.szInfoTitle = balloon_title[:63]
            nid.szInfo = balloon_msg[:255]
            nid.dwInfoFlags = NIIF_INFO
        return nid

    def _update_tooltip(status_text: str) -> None:
        state["status"] = status_text
        if not state["nid_added"] or not state["hwnd"]:
            return
        tip = f"VortexBrowser Server ({lan_ip}:{ctrl.port}) - {status_text}"
        nid = _make_nid(tip)
        shell32.Shell_NotifyIconW(NIM_MODIFY, ctypes.byref(nid))

    def _open_log() -> None:
        try:
            if not os.path.exists(ctrl.log_path):
                with open(ctrl.log_path, "a", encoding="utf-8"):
                    pass
            subprocess.Popen(["notepad.exe", ctrl.log_path])
        except Exception:
            pass

    def _open_cfg_folder() -> None:
        try:
            subprocess.Popen(["explorer.exe", ctrl.cfg_dir])
        except Exception:
            pass

    def _show_menu(hwnd: int) -> None:
        hmenu = user32.CreatePopupMenu()
        if not hmenu:
            return
        try:
            user32.AppendMenuW(hmenu, MF_STRING | MF_GRAYED, ID_STATUS, f"VortexBrowser ({lan_ip}:{ctrl.port}) [{state['status']}]")
            user32.AppendMenuW(hmenu, MF_SEPARATOR, 0, None)
            user32.AppendMenuW(hmenu, MF_STRING, ID_OPEN_LOG, "Open Server Log")
            user32.AppendMenuW(hmenu, MF_STRING, ID_OPEN_DIR, "Open Config / Profile Folder")
            user32.AppendMenuW(hmenu, MF_STRING, ID_RESTART, "Restart Server")
            user32.AppendMenuW(hmenu, MF_SEPARATOR, 0, None)
            user32.AppendMenuW(hmenu, MF_STRING, ID_EXIT, "Stop && Exit")
            pt = wintypes.POINT()
            user32.GetCursorPos(ctypes.byref(pt))
            user32.SetForegroundWindow(hwnd)
            user32.TrackPopupMenu(hmenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, None)
            user32.PostMessageW(hwnd, 0, 0, 0)
        finally:
            user32.DestroyMenu(hmenu)

    def _wnd_proc(hwnd: int, msg: int, wparam: int, lparam: int) -> int:
        if msg == WM_TRAY:
            ev = lparam & 0xFFFF
            if ev in (WM_RBUTTONUP, WM_CONTEXTMENU):
                _show_menu(hwnd)
                return 0
            if ev == WM_LBUTTONDBLCLK:
                _open_log()
                return 0
        elif msg == WM_COMMAND:
            cmd_id = wparam & 0xFFFF
            if cmd_id == ID_OPEN_LOG:
                _open_log()
            elif cmd_id == ID_OPEN_DIR:
                _open_cfg_folder()
            elif cmd_id == ID_RESTART:
                _update_tooltip("Restarting...")
                ok = ctrl.restart()
                _update_tooltip("Running" if ok else "Restart Failed")
            elif cmd_id == ID_EXIT:
                user32.DestroyWindow(hwnd)
            return 0
        elif msg == WM_TIMER and wparam == TIMER_HEALTH:
            st = ctrl.check_health()
            if st != state["status"]:
                _update_tooltip(st)
            return 0
        elif msg == WM_QUERYENDSESSION:
            return 1
        elif msg in (WM_CLOSE, WM_ENDSESSION):
            user32.DestroyWindow(hwnd)
            return 0
        elif msg == WM_DESTROY:
            user32.KillTimer(hwnd, TIMER_HEALTH)
            ctrl.stop(timeout=6.0)
            if state["nid_added"]:
                nid = _make_nid("")
                shell32.Shell_NotifyIconW(NIM_DELETE, ctypes.byref(nid))
                state["nid_added"] = False
            user32.PostQuitMessage(0)
            return 0
        return user32.DefWindowProcW(hwnd, msg, wparam, lparam)

    wnd_proc_cb = WNDPROC(_wnd_proc)
    hinst = kernel32.GetModuleHandleW(None)
    wc = WNDCLASSEXW()
    wc.cbSize = ctypes.sizeof(WNDCLASSEXW)
    wc.lpfnWndProc = wnd_proc_cb
    wc.hInstance = hinst
    wc.hIcon = hicon
    wc.hIconSm = hicon
    wc.lpszClassName = TRAY_CLASS_NAME
    user32.RegisterClassExW(ctypes.byref(wc))

    hwnd = user32.CreateWindowExW(
        0, TRAY_CLASS_NAME, TRAY_WINDOW_TITLE, 0,
        0, 0, 0, 0, None, None, hinst, None,
    )
    if not hwnd:
        if hmutex:
            kernel32.CloseHandle(hmutex)
        return _run_headless_windows(ctrl, tray_pid_file)

    state["hwnd"] = hwnd
    _write_pid(tray_pid_file)

    try:
        ok = ctrl.start()
        state["status"] = "Running" if ok else "Failed to Start"
        nid = _make_nid(
            f"VortexBrowser Server ({lan_ip}:{ctrl.port}) - {state['status']}",
            "VortexBrowser Server",
            f"Running silently in background on {lan_ip}:{ctrl.port} (Amiga Audio: Silent TabCapture)",
        )
        if shell32.Shell_NotifyIconW(NIM_ADD, ctypes.byref(nid)):
            state["nid_added"] = True
        user32.SetTimer(hwnd, TIMER_HEALTH, 2500, None)

        msg = wintypes.MSG()
        while user32.GetMessageW(ctypes.byref(msg), None, 0, 0) > 0:
            user32.TranslateMessage(ctypes.byref(msg))
            user32.DispatchMessageW(ctypes.byref(msg))
    finally:
        ctrl.stop(timeout=5.0)
        if state["nid_added"]:
            nid = _make_nid("")
            shell32.Shell_NotifyIconW(NIM_DELETE, ctypes.byref(nid))
        if owns_icon and hicon:
            user32.DestroyIcon(hicon)
        _remove_file_quiet(tray_pid_file)
        if hmutex:
            kernel32.CloseHandle(hmutex)
    return 0


def main() -> int:
    argv = list(sys.argv[1:])
    if "--stop" in argv:
        return _stop_running_instance()

    no_tray = False
    extra_args: List[str] = []
    for a in argv:
        if a == "--no-tray":
            no_tray = True
        else:
            extra_args.append(a)

    cfg_dir = _get_config_dir()
    tray_pid_file = os.path.join(cfg_dir, "tray.pid")
    ctrl = ServerProcessController(extra_args=extra_args, cfg_dir=cfg_dir)

    if platform.system() != "Windows" or no_tray:
        return _run_headless_windows(ctrl, tray_pid_file)
    return _run_tray_windows(ctrl, tray_pid_file)


if __name__ == "__main__":
    raise SystemExit(main())
