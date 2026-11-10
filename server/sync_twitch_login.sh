#!/usr/bin/env bash
# ==============================================================================
# VortexBrowser - Twitch Automatic Login & Session Synchronizer
# Usage:
#   ./sync_twitch_login.sh [--server IP] [--token TOKEN] [--user USER]
#   ./sync_twitch_login.sh --restore-pc
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVER_IP="${VORTEX_SERVER_IP:-127.0.0.1}"
TOKEN=""
TW_USER=""
RESTORE_PC=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --server|--host)
            SERVER_IP="$2"
            shift 2
            ;;
        --token)
            TOKEN="$2"
            shift 2
            ;;
        --user)
            TW_USER="$2"
            shift 2
            ;;
        --restore-pc)
            RESTORE_PC=1
            shift
            ;;
        --help|-h)
            echo "VortexBrowser Twitch Synchronizer"
            echo "Usage:"
            echo "  ./sync_twitch_login.sh [--server IP] [--token TOKEN] [--user USER]"
            echo "  ./sync_twitch_login.sh --restore-pc   (Restore login into PC LibreWolf/Firefox)"
            exit 0
            ;;
        *)
            shift
            ;;
    esac
done

echo "=================================================================="
echo " VortexBrowser - Twitch Automatic Login & Session Synchronizer"
echo " Target Server: ${SERVER_IP}"
echo "=================================================================="

# Function to locate local Python script
find_local_script() {
    for cand in \
        "${SCRIPT_DIR}/server/sync_twitch_login.py" \
        "${SCRIPT_DIR}/sync_twitch_login.py"; do
        if [ -f "$cand" ]; then
            echo "$cand"
            return
        fi
    done
    echo ""
}

LOCAL_PY_SCRIPT=$(find_local_script)
if [ -z "$LOCAL_PY_SCRIPT" ]; then
    echo "[ERROR] sync_twitch_login.py could not be found in ${SCRIPT_DIR}!"
    exit 1
fi

# Restore PC option
if [ "$RESTORE_PC" -eq 1 ]; then
    echo "[*] Abrufen des aktiven Twitch-Tokens..."
    FETCHED_TOKEN="$TOKEN"
    if [ -z "$FETCHED_TOKEN" ]; then
        if ssh "root@${SERVER_IP}" "test -f ~/.config/vortexbrowse/twitch_token.txt" 2>/dev/null; then
            FETCHED_TOKEN=$(ssh "root@${SERVER_IP}" "cat ~/.config/vortexbrowse/twitch_token.txt" 2>/dev/null || true)
        elif [ -f "${HOME}/.config/vortexbrowse/twitch_token.txt" ]; then
            FETCHED_TOKEN=$(cat "${HOME}/.config/vortexbrowse/twitch_token.txt")
        fi
    fi

    if [ -n "$FETCHED_TOKEN" ]; then
        echo ""
        # Try direct sqlite injection if browser is closed
        INJECTED=$(python3 -c "
import glob, os, sqlite3, time, sys

token = sys.argv[1]
globs = [
    os.path.expanduser('~/.config/librewolf/librewolf/*/cookies.sqlite'),
    os.path.expanduser('~/.librewolf/*/cookies.sqlite'),
    os.path.expanduser('~/.mozilla/firefox/*/cookies.sqlite'),
    os.path.expanduser('~/.var/app/io.gitlab.librewolf-community/.librewolf/*/cookies.sqlite'),
    os.path.expanduser('~/.var/app/org.mozilla.firefox/.mozilla/firefox/*/cookies.sqlite'),
]

candidates = []
for g in globs:
    for p in glob.glob(g):
        if os.path.isfile(p) and p not in candidates:
            candidates.append(p)

success = False
for c in candidates:
    try:
        con = sqlite3.connect(c, timeout=0.5)
        now_us = int(time.time() * 1000000)
        exp = int(time.time() + 31536000)
        con.execute('''
            INSERT OR REPLACE INTO moz_cookies
            (originAttributes, name, value, host, path, expiry, lastAccessed, creationTime, isSecure, isHttpOnly, inBrowserElement, sameSite, schemeMap, isPartitionedAttributeSet, updateTime)
            VALUES ('', 'auth-token', ?, '.twitch.tv', '/', ?, ?, ?, 1, 0, 0, 0, 2, 0, ?)
        ''', (token, exp, now_us, now_us, now_us))
        con.commit()
        con.close()
        print(f'[+] auth-token direkt in Datenbank geschrieben: {c}')
        success = True
    except Exception:
        pass

if success:
    print('DIRECT_OK')
" "$FETCHED_TOKEN" 2>/dev/null || true)

        echo "=================================================================="
        echo " [SOFORT-LOGIN IM PC-BROWSER / LIBREWOLF]"
        echo "=================================================================="
        echo " Dein aktiver Twitch auth-token lautet:"
        echo "   ${FETCHED_TOKEN}"
        echo ""

        if echo "$INJECTED" | grep -q "DIRECT_OK"; then
            echo "[OK] Cookie wurde direkt in das Profil geschrieben!"
            echo "     Starte LibreWolf neu oder oeffne https://www.twitch.tv"
        else
            echo " Da LibreWolf gerade geoeffnet ist (Datenbank gesperrt) und Twitch"
            echo " das Anmeldeformular in LibreWolf mit Anti-Bot-Checks blockiert:"
            echo ""
            echo " 1. Oeffne in LibreWolf: https://www.twitch.tv"
            echo " 2. Druecke F12 (Entwicklerwerkzeuge) und klicke auf 'Konsole'"
            echo " 3. Fuege diese Zeile ein und druecke Enter:"
            echo ""
            echo "document.cookie = 'auth-token=${FETCHED_TOKEN}; path=/; domain=.twitch.tv; secure; max-age=31536000'; location.reload();"
            echo ""
            echo " Der Browser laedt neu und du bist sofort wieder eingeloggt!"
            echo " (Alternativ: LibreWolf schliessen und './sync_twitch_login.sh --restore-pc' erneut ausfuehren)"
        fi
        echo "=================================================================="
        exit 0
    else
        echo "[!] Kein gespeicherter Token gefunden. Bitte zuerst auf dem Server synchronisieren."
        exit 1
    fi
fi

# Check if we are running directly on the server
MY_IPS=$(hostname -I 2>/dev/null || echo "")
IS_LOCAL=0
if [[ "$SERVER_IP" == "127.0.0.1" || "$SERVER_IP" == "localhost" ]] || [[ "$MY_IPS" =~ "$SERVER_IP" ]]; then
    IS_LOCAL=1
fi

if [ "$IS_LOCAL" -eq 1 ]; then
    echo "[*] Running locally on server..."
    cd "$SCRIPT_DIR"
    if [ -d ".venv" ]; then
        PYTHON=".venv/bin/python"
    elif [ -d "server/.venv" ]; then
        PYTHON="server/.venv/bin/python"
    else
        PYTHON="python3"
    fi
    ARGS=()
    if [ -n "$TOKEN" ]; then
        ARGS+=("--token" "$TOKEN")
    fi
    if [ -n "$TW_USER" ]; then
        ARGS+=("--user" "$TW_USER")
    fi
    exec "$PYTHON" "$LOCAL_PY_SCRIPT" "${ARGS[@]}"
fi

# We are on the workstation/client machine connecting to remote server
echo "[*] Checking connectivity to remote server ${SERVER_IP}..."
if ! ping -c 1 -W 2 "$SERVER_IP" >/dev/null 2>&1; then
    echo "[ERROR] Server ${SERVER_IP} ist nicht erreichbar!"
    exit 1
fi

# 1. Check if local LibreWolf/Firefox has active cookies to upload
echo "[*] Checking local browser profiles for fresh Twitch cookies..."
python3 -c "
import glob, os, sqlite3, json

globs = [
    os.path.expanduser('~/.config/librewolf/librewolf/*/cookies.sqlite'),
    os.path.expanduser('~/.librewolf/*/cookies.sqlite'),
    os.path.expanduser('~/.mozilla/firefox/*/cookies.sqlite'),
    os.path.expanduser('~/.var/app/io.gitlab.librewolf-community/.librewolf/*/cookies.sqlite'),
    os.path.expanduser('~/.var/app/org.mozilla.firefox/.mozilla/firefox/*/cookies.sqlite'),
]

candidates = []
for g in globs:
    for p in glob.glob(g):
        if os.path.isfile(p) and p not in candidates:
            candidates.append(p)

found = False
for c in candidates:
    try:
        con = sqlite3.connect(f'file:{c}?immutable=1', uri=True, timeout=1.0)
        rows = con.cursor().execute('SELECT name, value, host, path, expiry, isSecure, isHttpOnly, sameSite FROM moz_cookies WHERE host LIKE \"%twitch%\";').fetchall()
        con.close()
        if rows and any(r[0] == 'auth-token' for r in rows):
            same_site_map = {0: 'None', 1: 'Lax', 2: 'Strict'}
            cookies = [{'name': r[0], 'value': r[1], 'domain': r[2], 'path': r[3] or '/', 'expires': float(r[4]) if r[4] else None, 'secure': bool(r[5]), 'httpOnly': bool(r[6]), 'sameSite': same_site_map.get(r[7], 'None')} for r in rows]
            with open('/tmp/fresh_twitch_cookies.json', 'w', encoding='utf-8') as f:
                json.dump(cookies, f, indent=2)
            print(f'[+] Frische Cookies aus lokalem Profil gefunden: {c} ({len(cookies)} Cookies)')
            found = True
            break
    except Exception:
        pass

if not found:
    print('[-] Keine aktiven Cookies im lokalen Browser gefunden (nutze Server-Cache oder --token).')
"

# 2. Dynamically determine remote server directory & Python environment
echo "[*] Detecting remote server installation and Python environment..."
REMOTE_INFO=$(ssh -o ConnectTimeout=5 "root@${SERVER_IP}" '
    # 1. Process directory of currently running server.py
    PID_DIR=$(pgrep -f "server.py" | head -n1 | xargs -I{} readlink -f /proc/{}/cwd 2>/dev/null || true)
    if [ -n "$PID_DIR" ] && [ -f "$PID_DIR/server.py" ]; then
        DIR="$PID_DIR"
    elif [ -d "$(systemctl show vortexbrowser -p WorkingDirectory --value 2>/dev/null || true)" ]; then
        DIR="$(systemctl show vortexbrowser -p WorkingDirectory --value 2>/dev/null)"
    else
        for d in /home/*/Server-* /home/*/vortexbrowse* /opt/vortexbrowse* /root/Server-*; do
            if [ -f "$d/server.py" ]; then
                DIR="$d"
                break
            fi
        done
    fi
    DIR="${DIR:-/tmp}"
    PY="python3"
    if [ -x "${DIR}/.venv/bin/python" ]; then
        PY="${DIR}/.venv/bin/python"
    elif [ -x "/root/.venv/bin/python" ]; then
        PY="/root/.venv/bin/python"
    fi
    echo "${DIR}|${PY}"
')

REMOTE_DIR="${REMOTE_INFO%%|*}"
REMOTE_PY="${REMOTE_INFO##*|}"

if [ -z "$REMOTE_DIR" ] || [ "$REMOTE_DIR" = "/tmp" ]; then
    echo "[!] Remote server directory not detected from process; searching..."
    REMOTE_DIR=$(ssh "root@${SERVER_IP}" 'find /home -maxdepth 3 -name "server.py" -exec dirname {} \; 2>/dev/null | head -n 1 || echo "/tmp"')
fi

echo "[+] Remote server directory : ${REMOTE_DIR}"
echo "[+] Remote Python binary    : ${REMOTE_PY}"

# Upload fresh cookies if extracted
if [ -f "/tmp/fresh_twitch_cookies.json" ]; then
    echo "[*] Uploading fresh Twitch cookies to ${SERVER_IP}..."
    scp /tmp/fresh_twitch_cookies.json "root@${SERVER_IP}:/tmp/twitch_cookies.json" >/dev/null 2>&1 || true
    scp /tmp/fresh_twitch_cookies.json "root@${SERVER_IP}:${REMOTE_DIR}/twitch_cookies.json" >/dev/null 2>&1 || true
    rm -f /tmp/fresh_twitch_cookies.json
fi

# 3. Synchronize sync_twitch_login.py to server
echo "[*] Synchronizing sync script to remote server..."
scp "$LOCAL_PY_SCRIPT" "root@${SERVER_IP}:${REMOTE_DIR}/sync_twitch_login.py" >/dev/null

# 4. Execute login synchronization on server
echo "[*] Executing login synchronization on server..."
REMOTE_CMD="${REMOTE_PY} '${REMOTE_DIR}/sync_twitch_login.py'"
if [ -n "$TOKEN" ]; then
    REMOTE_CMD="${REMOTE_CMD} --token '${TOKEN}'"
fi
if [ -n "$TW_USER" ]; then
    REMOTE_CMD="${REMOTE_CMD} --user '${TW_USER}'"
fi

ssh -t "root@${SERVER_IP}" "$REMOTE_CMD"
