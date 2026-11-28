# ==============================================================================
# VortexRemoteBrowser Native Windows 10/11 (x64) Companion Server Installer
# - Installs/verifies Python 3.10+ venv, Rustwright, Playwright, PyAudioWPatch
# - Checks/installs FFmpeg (NVENC / AMF / QSV / Media Foundation / libx264)
# - Detects Chromium/Chrome/Edge or installs Playwright Chromium
# - Verifies prebuilt native fast_pixconv.dll / fast_pixconv_avx2.dll
# - Configures Windows Defender Firewall (TCP 7788 & 9757, Private network)
# ==============================================================================
[CmdletBinding()]
param(
    [switch]$InstallPlaywrightChromium,
    [switch]$UseSystemBrowser,
    [switch]$InstallAutostart,
    [switch]$SkipFirewall,
    [switch]$SkipFFmpeg
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ScriptDir

Write-Host "============================================================================" -ForegroundColor Cyan
Write-Host " VortexRemoteBrowser Native Windows 10/11 Companion Server Setup (x64)" -ForegroundColor Cyan
Write-Host "============================================================================" -ForegroundColor Cyan
Write-Host ""

function Find-Python {
    foreach ($cand in @(
        @{ cmd = "py"; args = @("-3.12") },
        @{ cmd = "py"; args = @("-3.11") },
        @{ cmd = "py"; args = @("-3") },
        @{ cmd = "python"; args = @() },
        @{ cmd = "python3"; args = @() }
    )) {
        try {
            $exe = Get-Command $cand.cmd -ErrorAction SilentlyContinue
            if (-not $exe) { continue }
            $out = & $cand.cmd @($cand.args + @("-c", "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}|{sys.executable}')")) 2>$null
            if ($LASTEXITCODE -eq 0 -and $out) {
                $parts = ($out | Select-Object -First 1).Trim().Split("|")
                if ($parts.Length -eq 2) {
                    $ver = [Version]$parts[0]
                    if ($ver -ge [Version]"3.10" -and (Test-Path $parts[1])) {
                        return $parts[1]
                    }
                }
            }
        } catch {}
    }
    return $null
}

# 1. Python 3.10+
Write-Host "[1/6] Checking Python 3.10+..." -ForegroundColor Yellow
$PythonExe = Find-Python
if (-not $PythonExe) {
    if (Get-Command winget -ErrorAction SilentlyContinue) {
        Write-Host "  Python 3.10+ not found. Installing Python 3.12 via winget..." -ForegroundColor Cyan
        winget install -e --id Python.Python.3.12 --accept-package-agreements --accept-source-agreements
        $env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path", "User")
        $PythonExe = Find-Python
    }
    if (-not $PythonExe) {
        Write-Host "ERROR: Python 3.10+ is required. Install from https://www.python.org/downloads/ (check 'Add python.exe to PATH') and re-run." -ForegroundColor Red
        exit 1
    }
}
Write-Host "  Using Python: $PythonExe" -ForegroundColor Green

# 2. Virtual Environment & Python Packages
Write-Host "[2/6] Creating virtual environment (.venv) and installing Python dependencies..." -ForegroundColor Yellow
$VenvDir = Join-Path $ScriptDir ".venv"
$VenvPy  = Join-Path $VenvDir "Scripts\python.exe"
if (-not (Test-Path $VenvPy)) {
    & $PythonExe -m venv $VenvDir
}
& $VenvPy -m pip install --upgrade pip wheel
& $VenvPy -m pip install -r (Join-Path $ScriptDir "requirements.txt")
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: pip install failed." -ForegroundColor Red
    exit 1
}

# 3. FFmpeg (H.264 NVENC/AMF/QSV/MF/libx264 & audio resampling)
if (-not $SkipFFmpeg) {
    Write-Host "[3/6] Checking FFmpeg..." -ForegroundColor Yellow
    if (-not (Get-Command ffmpeg -ErrorAction SilentlyContinue)) {
        if (Get-Command winget -ErrorAction SilentlyContinue) {
            Write-Host "  FFmpeg not found. Installing Gyan.FFmpeg via winget..." -ForegroundColor Cyan
            try {
                winget install -e --id Gyan.FFmpeg --accept-package-agreements --accept-source-agreements
                $env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path", "User")
            } catch {
                Write-Host "  WARNING: FFmpeg auto-install skipped. H.264 video & audio resampling require ffmpeg.exe on PATH." -ForegroundColor DarkYellow
            }
        } else {
            Write-Host "  WARNING: ffmpeg.exe not found on PATH. Install FFmpeg (e.g. 'winget install Gyan.FFmpeg') for H.264 & audio." -ForegroundColor DarkYellow
        }
    } else {
        Write-Host "  FFmpeg found: $((Get-Command ffmpeg).Source)" -ForegroundColor Green
    }
} else {
    Write-Host "[3/6] FFmpeg check skipped." -ForegroundColor DarkGray
}

# 4. Chromium / Browser Check
# Prefer Ungoogled-Chromium or Playwright Chromium ("Chrome for Testing") because
# Google Chrome 137+ disables --load-extension in branded builds, which is required
# for Silent Amiga-Only TabCapture Audio (host speakers muted) and uBlock Origin.
Write-Host "[4/6] Checking Chromium browser (with --load-extension support)..." -ForegroundColor Yellow
$UnbrandedFound = $null
foreach ($b in @(
    "$env:LOCALAPPDATA\Chromium\Application\chrome.exe",
    "$env:PROGRAMFILES\Chromium\Application\chrome.exe"
)) {
    if ($b -and (Test-Path $b)) {
        $UnbrandedFound = $b
        break
    }
}
if (-not $UnbrandedFound) {
    $pwCands = @(
        Get-ChildItem "$env:LOCALAPPDATA\ms-playwright\chromium-*\chrome-win64\chrome.exe" -ErrorAction SilentlyContinue
        Get-ChildItem "$env:LOCALAPPDATA\ms-playwright\chromium-*\chrome-win\chrome.exe" -ErrorAction SilentlyContinue
    ) | Sort-Object FullName -Descending
    if ($pwCands -and $pwCands.Count -gt 0) {
        $UnbrandedFound = $pwCands[0].FullName
    }
}

if ($InstallPlaywrightChromium -or ((-not $UnbrandedFound) -and (-not $UseSystemBrowser))) {
    Write-Host "  Installing Playwright Chromium (Chrome for Testing — enables Silent Windows Audio & uBlock Origin)..." -ForegroundColor Cyan
    & $VenvPy -m playwright install chromium
} elseif ($UnbrandedFound) {
    Write-Host "  Detected Chromium with full extension support: $UnbrandedFound" -ForegroundColor Green
} else {
    Write-Host "  -UseSystemBrowser specified: will use system Chrome/Edge/Brave if installed." -ForegroundColor DarkYellow
    Write-Host "  (Note: Chrome 137+ blocks unpacked extensions; re-run without -UseSystemBrowser if Windows speakers still play audio.)" -ForegroundColor DarkGray
}

# 5. Verify native SIMD DLL & server scripts
Write-Host "[5/6] Verifying native SIMD DLL, server.py & tray launcher..." -ForegroundColor Yellow
& $VenvPy -m py_compile (Join-Path $ScriptDir "server.py") (Join-Path $ScriptDir "run_windows_tray.pyw")
& $VenvPy -c @"
import ctypes, os
base = r'$ScriptDir'
if hasattr(os, 'add_dll_directory'):
    os.add_dll_directory(base)
has_avx2 = bool(ctypes.windll.kernel32.IsProcessorFeaturePresent(40))
cands = ['fast_pixconv_avx2.dll', 'fast_pixconv.dll'] if has_avx2 else ['fast_pixconv.dll']
loaded = None
for c in cands:
    p = os.path.join(base, c)
    if os.path.isfile(p):
        lib = ctypes.CDLL(p)
        if hasattr(lib, 'encode_jpeg_to_frame_packet'):
            loaded = c
            break
print('  Native SIMD DLL:', loaded or 'NOT FOUND (Pillow fallback)', '| AVX2 CPU:', has_avx2)
try:
    import pyaudiowpatch as pa
    p = pa.PyAudio()
    dev = p.get_default_wasapi_loopback()
    p.terminate()
    print('  Windows WASAPI Loopback Audio:', dev.get('name') if dev else 'Not available')
except Exception as e:
    print('  Windows WASAPI Loopback Audio: unavailable ->', e)
"@

# 6. Windows Defender Firewall rule for TCP 7788 & 9757
if (-not $SkipFirewall) {
    Write-Host "[6/6] Checking Windows Firewall rule (TCP 7788, 9757)..." -ForegroundColor Yellow
    $isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    $RuleName = "VortexRemoteBrowser Amiga Companion Server (TCP 7788,9757)"
    $existing = Get-NetFirewallRule -DisplayName $RuleName -ErrorAction SilentlyContinue
    if ($existing) {
        Write-Host "  Firewall rule already exists: $RuleName" -ForegroundColor Green
    } elseif ($isAdmin) {
        New-NetFirewallRule -DisplayName $RuleName -Direction Inbound -Protocol TCP -LocalPort 7788,9757 -Action Allow -Profile Private | Out-Null
        Write-Host "  Created inbound Private-network firewall rule for TCP 7788 & 9757." -ForegroundColor Green
    } else {
        Write-Host "  Not running as Administrator - run this once in an Admin PowerShell so your Amiga can connect over LAN:" -ForegroundColor DarkYellow
        Write-Host "    New-NetFirewallRule -DisplayName '$RuleName' -Direction Inbound -Protocol TCP -LocalPort 7788,9757 -Action Allow -Profile Private" -ForegroundColor White
    }
}

if ($InstallAutostart) {
    try {
        $StartupDir = [Environment]::GetFolderPath("Startup")
        $ShortcutPath = Join-Path $StartupDir "VortexRemoteBrowser Server.lnk"
        $WshShell = New-Object -ComObject WScript.Shell
        $Shortcut = $WshShell.CreateShortcut($ShortcutPath)
        $Shortcut.TargetPath = "$env:SystemRoot\System32\wscript.exe"
        $Shortcut.Arguments = "`"$(Join-Path $ScriptDir 'run_windows_hidden.vbs')`""
        $Shortcut.WorkingDirectory = $ScriptDir
        $Shortcut.Description = "VortexRemoteBrowser Amiga Companion Server (Background Tray)"
        $Shortcut.Save()
        Write-Host "  Created Windows Startup autostart shortcut: $ShortcutPath" -ForegroundColor Green
    } catch {
        Write-Host "  WARNING: Could not create Startup shortcut: $_" -ForegroundColor DarkYellow
    }
}

# Print local LAN IPv4 addresses
$LanIPs = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
    Where-Object { $_.IPAddress -notlike "127.*" -and $_.IPAddress -notlike "169.254.*" -and $_.PrefixOrigin -ne "WellKnown" } |
    Select-Object -ExpandProperty IPAddress -Unique

Write-Host ""
Write-Host "============================================================================" -ForegroundColor Green
Write-Host " VortexRemoteBrowser Windows Server installation complete!" -ForegroundColor Green
Write-Host " Start silently (background + tray): double-click run_windows_hidden.vbs" -ForegroundColor Green
Write-Host " Start with console window:          double-click run_windows.bat" -ForegroundColor Green
Write-Host " Stop background server:             double-click stop_windows.bat" -ForegroundColor Green
Write-Host " Audio mode:                         Silent Browser TabCapture (Amiga only, PC speakers muted)" -ForegroundColor Cyan
if ($LanIPs) {
    Write-Host " Your Windows LAN IP(s) for AmigaOS 4:  $($LanIPs -join ', ')" -ForegroundColor Cyan
}
Write-Host " Ports:                              7788 (Primary) & 9757 (LAN Secondary)" -ForegroundColor Green
Write-Host " Tip: Re-run with -InstallAutostart to launch silently when Windows starts." -ForegroundColor DarkGray
Write-Host "============================================================================" -ForegroundColor Green
