#!/usr/bin/env bash
# ==============================================================================
# VortexBrowser - Twitch Automatic Login & Session Synchronizer
# Usage: ./sync_twitch_login.sh [--server IP] [--token TOKEN]
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVER_IP="${VORTEX_SERVER_IP:-192.168.178.2}"
TOKEN=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --server)
            SERVER_IP="$2"
            shift 2
            ;;
        --token)
            TOKEN="$2"
            shift 2
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
    else
        PYTHON="python3"
    fi
    ARGS=()
    if [ -n "$TOKEN" ]; then
        ARGS+=("--token" "$TOKEN")
    fi
    exec "$PYTHON" cachyos-elitedesk-server/sync_twitch_login.py "${ARGS[@]}"
fi

# We are on the workstation/client machine connecting to remote server
echo "[*] Checking connectivity to remote server ${SERVER_IP}..."
if ! ping -c 1 -W 2 "$SERVER_IP" >/dev/null 2>&1; then
    echo "[ERROR] Server ${SERVER_IP} ist nicht erreichbar!"
    exit 1
fi

# 1. Check if local LibreWolf has active cookies to upload
echo "[*] Checking local LibreWolf profile for fresh Twitch cookies..."
python3 -c "
import glob, os, sqlite3, shutil, json

candidates = glob.glob(os.path.expanduser('~/.config/librewolf/librewolf/*/cookies.sqlite'))
found = False
for c in candidates:
    wal = c + '-wal'
    tmp = '/tmp/local_lw.sqlite'
    tmp_wal = '/tmp/local_lw.sqlite-wal'
    try:
        shutil.copy2(c, tmp)
        if os.path.exists(wal): shutil.copy2(wal, tmp_wal)
        con = sqlite3.connect(tmp)
        rows = con.cursor().execute('SELECT name, value, host, path, expiry, isSecure, isHttpOnly, sameSite FROM moz_cookies WHERE host LIKE \"%twitch%\";').fetchall()
        if rows and any(r[0] == 'auth-token' for r in rows):
            cookies = [{'name': r[0], 'value': r[1], 'domain': r[2], 'path': r[3] or '/', 'expires': float(r[4]) if r[4] else None, 'secure': bool(r[5]), 'httpOnly': bool(r[6]), 'sameSite': {0:'None',1:'Lax',2:'Strict'}.get(r[7],'None')} for r in rows]
            with open('/tmp/fresh_twitch_cookies.json', 'w', encoding='utf-8') as f:
                json.dump(cookies, f, indent=2)
            print(f'[+] Frische Cookies aus lokalem LibreWolf gefunden ({len(cookies)} Cookies)!')
            found = True
            break
    except Exception:
        pass
    finally:
        if os.path.exists(tmp): os.remove(tmp)
        if os.path.exists(tmp_wal): os.remove(tmp_wal)
if not found:
    print('[-] Keine aktiven Cookies im lokalen LibreWolf-Profil gefunden (Browser geschlossen oder nicht angemeldet).')
"

if [ -f "/tmp/fresh_twitch_cookies.json" ]; then
    echo "[*] Uploading fresh Twitch cookies to ${SERVER_IP}..."
    scp /tmp/fresh_twitch_cookies.json "root@${SERVER_IP}:/tmp/twitch_cookies.json" >/dev/null 2>&1 || true
    rm -f /tmp/fresh_twitch_cookies.json
fi

# 2. Deploy script to server and execute
echo "[*] Synchronizing sync script to server..."
scp "${SCRIPT_DIR}/cachyos-elitedesk-server/sync_twitch_login.py" "root@${SERVER_IP}:/home/tears/Server-CachyOS-EliteDesk/sync_twitch_login.py" >/dev/null

echo "[*] Executing login synchronization on server..."
REMOTE_CMD="python3 /home/tears/Server-CachyOS-EliteDesk/sync_twitch_login.py"
if [ -n "$TOKEN" ]; then
    REMOTE_CMD="${REMOTE_CMD} --token ${TOKEN}"
fi

ssh "root@${SERVER_IP}" "$REMOTE_CMD"
