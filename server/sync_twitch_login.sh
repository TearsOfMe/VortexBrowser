#!/usr/bin/env bash
# ==============================================================================
# VortexRemoteBrowser - Universal Login & Session Synchronizer (Twitch, Discord & Web)
# Usage:
#   ./sync_twitch_login.sh [--server IP] [--site all|discord|twitch|DOMAIN]
#                          [--discord-token TOKEN] [--token TWITCH_TOKEN]
#   ./sync_twitch_login.sh --restore-pc
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVER_IP="${VORTEX_SERVER_IP:-192.168.178.2}"
SITE="all"
TOKEN=""
TW_USER=""
DISCORD_TOKEN=""
DISCORD_UID=""
RESTORE_PC=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --server|--host)
            SERVER_IP="$2"
            shift 2
            ;;
        --site|--domain)
            SITE="$2"
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
        --discord-token)
            DISCORD_TOKEN="$2"
            shift 2
            ;;
        --discord-uid)
            DISCORD_UID="$2"
            shift 2
            ;;
        --restore-pc)
            RESTORE_PC=1
            shift
            ;;
        --help|-h)
            echo "VortexRemoteBrowser Session Synchronizer (Twitch, Discord & Custom Domains)"
            echo "Usage:"
            echo "  ./sync_twitch_login.sh [--server IP] [--site all|discord|twitch|DOMAIN]"
            echo "  ./sync_twitch_login.sh [--server IP] --discord-token TOKEN"
            echo "  ./sync_twitch_login.sh [--server IP] --token TWITCH_TOKEN [--user USER]"
            echo "  ./sync_twitch_login.sh --restore-pc"
            exit 0
            ;;
        *)
            shift
            ;;
    esac
done

echo "=================================================================="
echo " VortexRemoteBrowser - Login & Session Synchronizer (Twitch & Discord)"
echo " Target Server: ${SERVER_IP} | Site: ${SITE}"
echo "=================================================================="

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

# Check if we are running directly on the target server
MY_IPS=$(hostname -I 2>/dev/null || echo "")
IS_LOCAL=0
if [[ "$SERVER_IP" == "127.0.0.1" || "$SERVER_IP" == "localhost" ]] || [[ "$MY_IPS" =~ "$SERVER_IP" ]]; then
    IS_LOCAL=1
fi

if [ "$IS_LOCAL" -eq 1 ] && [ "$RESTORE_PC" -eq 0 ]; then
    echo "[*] Running locally on server..."
    cd "$SCRIPT_DIR"
    if [ -d ".venv" ]; then
        PYTHON=".venv/bin/python"
    elif [ -d "server/.venv" ]; then
        PYTHON="server/.venv/bin/python"
    else
        PYTHON="python3"
    fi
    ARGS=("--site" "$SITE")
    [ -n "$TOKEN" ] && ARGS+=("--token" "$TOKEN")
    [ -n "$TW_USER" ] && ARGS+=("--user" "$TW_USER")
    [ -n "$DISCORD_TOKEN" ] && ARGS+=("--discord-token" "$DISCORD_TOKEN")
    [ -n "$DISCORD_UID" ] && ARGS+=("--discord-uid" "$DISCORD_UID")
    exec "$PYTHON" "$LOCAL_PY_SCRIPT" "${ARGS[@]}"
fi

echo "[*] Checking connectivity to remote server ${SERVER_IP}..."
if ! ping -c 1 -W 2 "$SERVER_IP" >/dev/null 2>&1; then
    echo "[ERROR] Server ${SERVER_IP} ist nicht erreichbar!"
    exit 1
fi

# 1. Extract local Twitch cookies, Discord token/cookies, and custom domain cookies on the workstation
TMP_TWITCH_CK="$(mktemp)"
TMP_DISCORD_CK="$(mktemp)"
TMP_DISCORD_TOK="$(mktemp)"
TMP_EXTRA_CK="$(mktemp)"
chmod 600 "$TMP_TWITCH_CK" "$TMP_DISCORD_CK" "$TMP_DISCORD_TOK" "$TMP_EXTRA_CK"
trap 'rm -f "$TMP_TWITCH_CK" "$TMP_DISCORD_CK" "$TMP_DISCORD_TOK" "$TMP_EXTRA_CK"' EXIT

python3 - "$LOCAL_PY_SCRIPT" "$SITE" "$TMP_TWITCH_CK" "$TMP_DISCORD_CK" "$TMP_DISCORD_TOK" "$TMP_EXTRA_CK" "$DISCORD_TOKEN" "$DISCORD_UID" <<'PYEOF'
import importlib.util, json, os, sys

script_path, site, out_tw_ck, out_dc_ck, out_dc_tok, out_ex_ck, cli_dc_tok, cli_dc_uid = sys.argv[1:9]
spec = importlib.util.spec_from_file_location("sync_mod", script_path)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

if site in ("all", "twitch"):
    tw_cks = mod.scan_local_browser_cookies()
    if tw_cks:
        with open(out_tw_ck, "w", encoding="utf-8") as f:
            json.dump(tw_cks, f, indent=2)

if site in ("all", "discord"):
    d_tok, d_uid, d_cks = mod.scan_local_discord_session()
    if cli_dc_tok:
        d_tok = cli_dc_tok
    if cli_dc_uid:
        d_uid = cli_dc_uid
    if d_tok:
        entry = f"{d_uid}:{d_tok}" if d_uid else d_tok
        with open(out_dc_tok, "w", encoding="utf-8") as f:
            f.write(entry + "\n")
    if d_cks:
        with open(out_dc_ck, "w", encoding="utf-8") as f:
            json.dump(d_cks, f, indent=2)

if site not in ("all", "twitch", "discord"):
    ex_cks = mod.scan_local_domain_cookies(site)
    if ex_cks:
        with open(out_ex_ck, "w", encoding="utf-8") as f:
            json.dump(ex_cks, f, indent=2)
PYEOF

# 2. Detect remote server directory & Python environment
echo "[*] Detecting remote server installation and Python environment..."
REMOTE_INFO=$(ssh -o ConnectTimeout=5 "root@${SERVER_IP}" '
    PID_DIR=$(pgrep -f "server.py" | head -n1 | xargs -I{} readlink -f /proc/{}/cwd 2>/dev/null || true)
    if [ -n "$PID_DIR" ] && [ -f "$PID_DIR/server.py" ]; then
        DIR="$PID_DIR"
    elif [ -d "$(systemctl show vortexremotebrowser -p WorkingDirectory --value 2>/dev/null || true)" ]; then
        DIR="$(systemctl show vortexremotebrowser -p WorkingDirectory --value 2>/dev/null)"
    else
        for d in /home/*/Server-* /home/*/vortexremotebrowser* /opt/vortexremotebrowser* /root/Server-*; do
            if [ -f "$d/server.py" ]; then
                DIR="$d"
                break
            fi
        done
    fi
    DIR="${DIR:-/home/tears/Server-Linux}"
    PY="python3"
    if [ -x "${DIR}/.venv/bin/python" ]; then
        PY="${DIR}/.venv/bin/python"
    elif [ -x "/root/.venv/bin/python" ]; then
        PY="/root/.venv/bin/python"
    fi
    SVC_USER=$(stat -c "%U" "${DIR}/server.py" 2>/dev/null || echo "root")
    SVC_HOME=$(getent passwd "$SVC_USER" | cut -d: -f6)
    [ -n "$SVC_HOME" ] || SVC_HOME="/root"
    CFG="${SVC_HOME}/.config/vortexremotebrowser"
    mkdir -p "$CFG"
    chown "$SVC_USER:$SVC_USER" "${SVC_HOME}/.config" "$CFG" 2>/dev/null || true
    chmod 700 "$CFG"
    echo "${DIR}|${PY}|${CFG}|${SVC_USER}"
')

IFS='|' read -r REMOTE_DIR REMOTE_PY REMOTE_CFG_DIR REMOTE_SVC_USER <<< "$REMOTE_INFO"
echo "[+] Remote server directory : ${REMOTE_DIR}"
echo "[+] Remote config directory : ${REMOTE_CFG_DIR} (User: ${REMOTE_SVC_USER})"

REMOTE_ARGS=("--site" "$SITE")

if [ -s "$TMP_TWITCH_CK" ]; then
    echo "[*] Uploading local Twitch cookies to ${SERVER_IP}..."
    scp -q "$TMP_TWITCH_CK" "root@${SERVER_IP}:${REMOTE_CFG_DIR}/twitch_cookies.json"
    ssh "root@${SERVER_IP}" "chmod 600 '${REMOTE_CFG_DIR}/twitch_cookies.json' && chown '${REMOTE_SVC_USER}:${REMOTE_SVC_USER}' '${REMOTE_CFG_DIR}/twitch_cookies.json'"
    REMOTE_ARGS+=("--cookies" "${REMOTE_CFG_DIR}/twitch_cookies.json")
fi

if [ -s "$TMP_DISCORD_CK" ]; then
    echo "[*] Uploading local Discord cookies to ${SERVER_IP}..."
    scp -q "$TMP_DISCORD_CK" "root@${SERVER_IP}:${REMOTE_CFG_DIR}/discord_cookies.json"
    ssh "root@${SERVER_IP}" "chmod 600 '${REMOTE_CFG_DIR}/discord_cookies.json' && chown '${REMOTE_SVC_USER}:${REMOTE_SVC_USER}' '${REMOTE_CFG_DIR}/discord_cookies.json'"
    REMOTE_ARGS+=("--discord-cookies" "${REMOTE_CFG_DIR}/discord_cookies.json")
fi

if [ -s "$TMP_DISCORD_TOK" ]; then
    echo "[*] Uploading local Discord session token to ${SERVER_IP}..."
    scp -q "$TMP_DISCORD_TOK" "root@${SERVER_IP}:${REMOTE_CFG_DIR}/discord_token.txt"
    ssh "root@${SERVER_IP}" "chmod 600 '${REMOTE_CFG_DIR}/discord_token.txt' && chown '${REMOTE_SVC_USER}:${REMOTE_SVC_USER}' '${REMOTE_CFG_DIR}/discord_token.txt'"
fi

if [ -s "$TMP_EXTRA_CK" ]; then
    echo "[*] Uploading custom domain cookies (${SITE}) to ${SERVER_IP}..."
    scp -q "$TMP_EXTRA_CK" "root@${SERVER_IP}:${REMOTE_CFG_DIR}/extra_cookies.json"
    ssh "root@${SERVER_IP}" "chmod 600 '${REMOTE_CFG_DIR}/extra_cookies.json' && chown '${REMOTE_SVC_USER}:${REMOTE_SVC_USER}' '${REMOTE_CFG_DIR}/extra_cookies.json'"
    REMOTE_ARGS+=("--extra-cookies" "${REMOTE_CFG_DIR}/extra_cookies.json")
fi

[ -n "$TOKEN" ] && REMOTE_ARGS+=("--token" "$TOKEN")
[ -n "$TW_USER" ] && REMOTE_ARGS+=("--user" "$TW_USER")

# 3. Upload updated sync_twitch_login.py and execute on remote server
scp -q "$LOCAL_PY_SCRIPT" "root@${SERVER_IP}:${REMOTE_DIR}/sync_twitch_login.py"
ssh "root@${SERVER_IP}" "chown '${REMOTE_SVC_USER}:${REMOTE_SVC_USER}' '${REMOTE_DIR}/sync_twitch_login.py' && chmod +x '${REMOTE_DIR}/sync_twitch_login.py'"

REMOTE_CMD="${REMOTE_PY} '${REMOTE_DIR}/sync_twitch_login.py'"
for arg in "${REMOTE_ARGS[@]}"; do
    REMOTE_CMD="${REMOTE_CMD} '${arg}'"
done

ssh "root@${SERVER_IP}" "$REMOTE_CMD"
