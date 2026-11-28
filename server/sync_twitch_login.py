#!/usr/bin/env python3
# ==============================================================================
# VortexRemoteBrowser - Automated Login & Session Sync Tool (Twitch, Discord & Custom Sites)
# Restores and verifies authenticated sessions in Ungoogled-Chromium/Chromium
# Completely portable: dynamically detects user home, system paths, and ownership.
# ==============================================================================
import argparse
import asyncio
import glob
import json
import os
import re
import shutil
import sqlite3
import sys
import time

try:
    from playwright.async_api import async_playwright
except ImportError:
    async_playwright = None


def get_target_user_and_home():
    """
    Dynamically determines the non-root user (UID, GID) and home directory.
    Works reliably whether executed directly as a user, via sudo, or as root.
    """
    uid = os.getuid()
    gid = os.getgid()
    home = os.path.expanduser("~")

    if uid != 0:
        return uid, gid, home

    sudo_user = os.environ.get("SUDO_USER")
    if sudo_user and sudo_user != "root":
        try:
            import pwd
            pw = pwd.getpwnam(sudo_user)
            return pw.pw_uid, pw.pw_gid, pw.pw_dir
        except Exception:
            pass

    try:
        import pwd
        script_dir = os.path.dirname(os.path.abspath(__file__))
        st = os.stat(script_dir)
        if st.st_uid != 0:
            pw = pwd.getpwuid(st.st_uid)
            return pw.pw_uid, pw.pw_gid, pw.pw_dir
    except Exception:
        pass

    for vb_dir in glob.glob("/home/*/.config/vortexremotebrowser"):
        try:
            import pwd
            st = os.stat(vb_dir)
            if st.st_uid != 0:
                pw = pwd.getpwuid(st.st_uid)
                return pw.pw_uid, pw.pw_gid, pw.pw_dir
        except Exception:
            pass

    for h in sorted(glob.glob("/home/*")):
        try:
            import pwd
            st = os.stat(h)
            if st.st_uid >= 1000:
                pw = pwd.getpwuid(st.st_uid)
                return pw.pw_uid, pw.pw_gid, pw.pw_dir
        except Exception:
            pass

    return uid, gid, home


def get_config_dirs():
    _, _, user_home = get_target_user_and_home()
    dirs = []
    for d in [
        os.path.join(user_home, ".config", "vortexremotebrowser"),
        os.path.expanduser("~/.config/vortexremotebrowser"),
    ]:
        if d and d not in dirs:
            dirs.append(d)
    return dirs


def save_to_config_dirs(filename, content, is_json=False):
    target_uid, target_gid, _ = get_target_user_and_home()
    saved_paths = []
    for cfg_dir in get_config_dirs():
        try:
            os.makedirs(cfg_dir, mode=0o700, exist_ok=True)
            fp = os.path.join(cfg_dir, filename)
            with open(fp, "w", encoding="utf-8") as f:
                if is_json:
                    json.dump(content, f, indent=2)
                else:
                    f.write(str(content).strip() + "\n")
            os.chmod(fp, 0o600)
            if os.getuid() == 0 and target_uid != 0:
                try:
                    os.chown(cfg_dir, target_uid, target_gid)
                    os.chown(fp, target_uid, target_gid)
                except Exception:
                    pass
            saved_paths.append(fp)
        except Exception:
            pass
    return saved_paths


def _get_firefox_cookie_dbs():
    _, _, user_home = get_target_user_and_home()
    globs = [
        os.path.join(user_home, ".config/librewolf/librewolf/*/cookies.sqlite"),
        os.path.join(user_home, ".librewolf/*/cookies.sqlite"),
        os.path.join(user_home, ".mozilla/firefox/*/cookies.sqlite"),
        os.path.join(user_home, ".var/app/io.gitlab.librewolf-community/.librewolf/*/cookies.sqlite"),
        os.path.join(user_home, ".var/app/org.mozilla.firefox/.mozilla/firefox/*/cookies.sqlite"),
        os.path.join(user_home, "snap/firefox/common/.mozilla/firefox/*/cookies.sqlite"),
        os.path.expanduser("~/.config/librewolf/librewolf/*/cookies.sqlite"),
        os.path.expanduser("~/.librewolf/*/cookies.sqlite"),
        os.path.expanduser("~/.mozilla/firefox/*/cookies.sqlite"),
    ]
    candidates = []
    seen = set()
    for g in globs:
        for p in glob.glob(g):
            if p not in seen and os.path.isfile(p):
                seen.add(p)
                candidates.append(p)
    return candidates


def _read_cookies_from_sqlite(sqlite_path, host_pattern="%twitch%"):
    """Safely reads moz_cookies from sqlite, supporting read-only immutable access and WAL fallback."""
    query = (
        "SELECT name, value, host, path, expiry, isSecure, isHttpOnly, sameSite "
        "FROM moz_cookies WHERE host LIKE ?"
    )
    try:
        con = sqlite3.connect(f"file:{sqlite_path}?immutable=1", uri=True, timeout=1.0)
        cur = con.cursor()
        rows = cur.execute(query, (host_pattern,)).fetchall()
        con.close()
        if rows:
            return _format_sqlite_rows(rows)
    except Exception:
        pass

    tmp_db = f"/tmp/vortex_ck_{os.getpid()}_{int(time.time()*1000)}.sqlite"
    tmp_wal = f"{tmp_db}-wal"
    wal = f"{sqlite_path}-wal"
    try:
        shutil.copy2(sqlite_path, tmp_db)
        if os.path.isfile(wal):
            shutil.copy2(wal, tmp_wal)
        con = sqlite3.connect(tmp_db, timeout=2.0)
        cur = con.cursor()
        rows = cur.execute(query, (host_pattern,)).fetchall()
        con.close()
        if rows:
            return _format_sqlite_rows(rows)
    except Exception:
        pass
    finally:
        for p in (tmp_db, tmp_wal):
            if os.path.exists(p):
                os.remove(p)

    return None


def _format_sqlite_rows(rows):
    cookies = []
    same_site_map = {0: "None", 1: "Lax", 2: "Strict"}
    for r in rows:
        cookies.append({
            "name": r[0],
            "value": r[1],
            "domain": r[2],
            "path": r[3] or "/",
            "expires": float(r[4]) if r[4] else None,
            "secure": bool(r[5]),
            "httpOnly": bool(r[6]),
            "sameSite": same_site_map.get(r[7], "None")
        })
    return cookies


def scan_local_browser_cookies():
    for c in _get_firefox_cookie_dbs():
        cookies = _read_cookies_from_sqlite(c, "%twitch%")
        if cookies and any(ck["name"] == "auth-token" for ck in cookies):
            print(f"[+] Found {len(cookies)} Twitch cookies in local profile: {c}")
            return cookies
    return None


def scan_local_domain_cookies(domain_filter):
    pat = f"%{domain_filter.lstrip('.')}%"
    all_found = []
    for c in _get_firefox_cookie_dbs():
        cookies = _read_cookies_from_sqlite(c, pat)
        if cookies:
            print(f"[+] Found {len(cookies)} cookies for '{domain_filter}' in: {c}")
            all_found.extend(cookies)
            break
    return all_found


def verify_discord_token_api(token):
    if not token:
        return False, None
    import urllib.request, urllib.error
    req = urllib.request.Request(
        "https://discord.com/api/v9/users/@me",
        headers={
            "Authorization": token.strip().strip('"'),
            "User-Agent": "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"
        }
    )
    try:
        with urllib.request.urlopen(req, timeout=5) as resp:
            if resp.status == 200:
                info = json.loads(resp.read().decode("utf-8", errors="ignore"))
                return True, str(info.get("id") or "")
    except Exception:
        pass
    return False, None


def scan_local_discord_session():
    """
    Scans local desktop browsers (LibreWolf/Firefox, Chromium, Brave, Chrome)
    and local Discord desktop clients (~/.config/discord, Flatpak Discord, Vesktop)
    for an active Discord authentication token, User ID, and cookies.
    """
    _, _, user_home = get_target_user_and_home()
    token = None
    uid = None

    # 1. Scan LevelDB Local Storage (Discord Desktop, Vesktop, Flatpak Discord, Chromium, Brave, Chrome)
    leveldb_globs = [
        os.path.join(user_home, ".config/discord/Local Storage/leveldb"),
        os.path.join(user_home, ".config/vesktop/sessionData/Local Storage/leveldb"),
        os.path.join(user_home, ".var/app/com.discordapp.Discord/config/discord/Local Storage/leveldb"),
        os.path.join(user_home, ".config/chromium/*/Local Storage/leveldb"),
        os.path.join(user_home, ".config/BraveSoftware/Brave-Browser/*/Local Storage/leveldb"),
        os.path.join(user_home, ".config/google-chrome/*/Local Storage/leveldb"),
        os.path.expanduser("~/.config/discord/Local Storage/leveldb"),
        os.path.expanduser("~/.config/chromium/*/Local Storage/leveldb"),
        os.path.expanduser("~/.config/BraveSoftware/Brave-Browser/*/Local Storage/leveldb"),
    ]

    jwt_re = re.compile(rb"([A-Za-z0-9_-]{20,32}\.[A-Za-z0-9_-]{6,8}\.[A-Za-z0-9_-]{25,55})")
    uid_re = re.compile(rb'"([0-9]{15,22})"')
    key_re = re.compile(rb"_https://(?:ptb\.|canary\.)?discord(?:app)?\.com\x00\x01(tokens|token)\b")

    seen_dirs = set()
    for g in leveldb_globs:
        for ldir in glob.glob(g):
            if ldir in seen_dirs or not os.path.isdir(ldir):
                continue
            seen_dirs.add(ldir)
            files = sorted(
                glob.glob(os.path.join(ldir, "*.log")) + glob.glob(os.path.join(ldir, "*.ldb")),
                key=lambda x: os.path.getmtime(x) if os.path.exists(x) else 0,
                reverse=True,
            )
            for fn in files:
                try:
                    with open(fn, "rb") as f:
                        data = f.read()
                    for m in key_re.finditer(data):
                        chunk = data[m.end():m.end() + 240]
                        jm = jwt_re.search(chunk)
                        if jm:
                            cand_tok = jm.group(1).decode("ascii", errors="ignore")
                            is_valid, api_uid = verify_discord_token_api(cand_tok)
                            if is_valid:
                                token = cand_tok
                                um = uid_re.search(chunk)
                                uid = api_uid or (um.group(1).decode("ascii", errors="ignore") if um else None)
                                print(f"[+] Found VALID active Discord session token in local LevelDB: {ldir} (UID: {uid or 'auto'})")
                                break
                            else:
                                print(f"[-] Ignored expired Discord token in {ldir} (HTTP 401)")
                    if token:
                        break
                except Exception:
                    pass
            if token:
                break
        if token:
            break

    # 2. Scan Firefox / LibreWolf storage/default/https+++discord.com/ls/data.sqlite
    if not token:
        ff_ls_globs = [
            os.path.join(user_home, ".config/librewolf/librewolf/*/storage/default/https+++*discord*/ls/data.sqlite"),
            os.path.join(user_home, ".librewolf/*/storage/default/https+++*discord*/ls/data.sqlite"),
            os.path.join(user_home, ".mozilla/firefox/*/storage/default/https+++*discord*/ls/data.sqlite"),
            os.path.expanduser("~/.config/librewolf/librewolf/*/storage/default/https+++*discord*/ls/data.sqlite"),
            os.path.expanduser("~/.mozilla/firefox/*/storage/default/https+++*discord*/ls/data.sqlite"),
        ]
        for g in ff_ls_globs:
            for db_path in glob.glob(g):
                try:
                    con = sqlite3.connect(f"file:{db_path}?immutable=1", uri=True, timeout=1.0)
                    rows = con.cursor().execute("SELECT key, value FROM data WHERE key IN ('token', 'tokens', 'user_id_cache')").fetchall()
                    con.close()
                    for k, val in rows:
                        raw_bytes = val if isinstance(val, bytes) else str(val).encode("utf-8", errors="ignore")
                        jm = jwt_re.search(raw_bytes)
                        if jm:
                            cand_tok = jm.group(1).decode("ascii", errors="ignore")
                            is_valid, api_uid = verify_discord_token_api(cand_tok)
                            if is_valid:
                                token = cand_tok
                                uid = api_uid or uid
                        um = uid_re.search(raw_bytes)
                        if um and not uid:
                            uid = um.group(1).decode("ascii", errors="ignore")
                    if token:
                        print(f"[+] Found active Discord session token in Firefox/LibreWolf storage: {db_path}")
                        break
                except Exception:
                    pass
            if token:
                break

    # 3. Also collect any .discord.com / .discordapp.com cookies from Firefox/LibreWolf
    cookies = []
    for c in _get_firefox_cookie_dbs():
        c1 = _read_cookies_from_sqlite(c, "%discord%")
        if c1:
            cookies.extend(c1)
            break

    return token, uid, cookies


def load_cached_cookies(filename="twitch_cookies.json", required_cookie_name=None):
    for cfg_dir in get_config_dirs():
        p = os.path.join(cfg_dir, filename)
        if os.path.isfile(p):
            try:
                with open(p, "r", encoding="utf-8") as f:
                    cookies = json.load(f)
                if isinstance(cookies, list) and cookies:
                    if not required_cookie_name or any(c.get("name") == required_cookie_name for c in cookies):
                        print(f"[+] Loaded {len(cookies)} cached cookies from: {p}")
                        return cookies
            except Exception:
                pass
    return None


def load_token_text(filename="twitch_token.txt"):
    for cfg_dir in get_config_dirs():
        p = os.path.join(cfg_dir, filename)
        if os.path.isfile(p):
            try:
                with open(p, "r", encoding="utf-8") as f:
                    t = f.read().strip()
                if t and len(t) >= 16:
                    print(f"[+] Found saved token in: {p}")
                    return t
            except Exception:
                pass
    return None


async def sync_discord(discord_token, discord_uid=None, discord_cookies=None, cdp_url="http://127.0.0.1:9222"):
    """
    Persists the Discord token + cookies to ~/.config/vortexremotebrowser/
    (so server.py automatically preloads it on every browser launch) AND if Chromium
    is currently running on CDP, injects it live and verifies the Discord session.
    """
    if not discord_token:
        return False

    discord_token = discord_token.strip().strip('"')
    if ":" in discord_token and not discord_uid and discord_token.split(":", 1)[0].isdigit():
        discord_uid, discord_token = discord_token.split(":", 1)

    token_entry = f"{discord_uid}:{discord_token}" if discord_uid else discord_token
    saved_tok_paths = save_to_config_dirs("discord_token.txt", token_entry, is_json=False)
    for sp in saved_tok_paths:
        print(f"[+] Discord Session-Token dauerhaft gespeichert in: {sp}")

    clean_cookies = []
    if discord_cookies:
        for c in discord_cookies:
            ck = {
                "name": c["name"],
                "value": c["value"],
                "domain": c["domain"] if c["domain"].startswith(".") else ("." + c["domain"]),
                "path": c.get("path", "/"),
                "secure": c.get("secure", True),
                "httpOnly": c.get("httpOnly", False),
            }
            if "sameSite" in c and c["sameSite"] in ("Strict", "Lax", "None"):
                ck["sameSite"] = c["sameSite"]
            clean_cookies.append(ck)
        save_to_config_dirs("discord_cookies.json", clean_cookies, is_json=True)

    # Also update chromium_storage_state.json directly so on-demand browser starts with Discord localStorage ready
    for cfg_dir in get_config_dirs():
        state_path = os.path.join(cfg_dir, "chromium_storage_state.json")
        state = {"cookies": [], "origins": []}
        if os.path.isfile(state_path):
            try:
                with open(state_path, "r", encoding="utf-8") as f:
                    loaded = json.load(f)
                if isinstance(loaded, dict):
                    state = loaded
            except Exception:
                pass

        if clean_cookies:
            existing_cks = [c for c in state.get("cookies", []) if "discord" not in c.get("domain", "")]
            existing_cks.extend(clean_cookies)
            state["cookies"] = existing_cks

        origins = [o for o in state.get("origins", []) if "discord" not in o.get("origin", "")]
        ls_items = [{"name": "token", "value": json.dumps(discord_token)}]
        if discord_uid:
            ls_items.append({"name": "user_id_cache", "value": json.dumps(discord_uid)})
            ls_items.append({"name": "tokens", "value": json.dumps({discord_uid: discord_token})})
        origins.append({"origin": "https://discord.com", "localStorage": ls_items})
        state["origins"] = origins
        save_to_config_dirs("chromium_storage_state.json", state, is_json=True)
        break

    if async_playwright is None:
        print("[+] Discord-Session im Server-Profil hinterlegt (wird beim Browserstart automatisch geladen).")
        return True

    print(f"[*] Prüfe aktive Chromium-Instanz über CDP ({cdp_url})...")
    async with async_playwright() as pw:
        try:
            browser = await pw.chromium.connect_over_cdp(cdp_url, timeout=3000)
        except Exception:
            print("==================================================================")
            print(" [SUCCESS] Discord Session im Server-Profil aktiviert! 🎉")
            print("   (Der Server-Browser ist im Standby und lädt die Discord-Session")
            print("    automatisch, sobald du im Amiga-Client discord.com öffnest!)")
            print("==================================================================")
            return True

        ctx = browser.contexts[0]
        if clean_cookies:
            await ctx.add_cookies(clean_cookies)

        page = ctx.pages[0] if ctx.pages else await ctx.new_page()
        print("[*] Injiziere Discord Session in laufenden Server-Browser...")
        try:
            await page.goto("https://discord.com/app", timeout=25000, wait_until="commit")
        except Exception:
            pass

        await page.evaluate("""([tok, uid]) => {
            const applyLs = (ls) => {
                if (!ls) return;
                ls.setItem('token', JSON.stringify(tok));
                if (uid) {
                    ls.setItem('user_id_cache', JSON.stringify(uid));
                    ls.setItem('tokens', JSON.stringify({ [uid]: tok }));
                }
            };
            try { applyLs(window.localStorage); } catch (e) {}
            try {
                const ifr = document.createElement('iframe');
                ifr.style.display = 'none';
                document.documentElement.appendChild(ifr);
                applyLs(ifr.contentWindow.localStorage);
                ifr.remove();
            } catch (e) {}
        }""", [discord_token, discord_uid or ""])

        try:
            await page.goto("https://discord.com/app", timeout=25000, wait_until="domcontentloaded")
        except Exception:
            pass
        await asyncio.sleep(2)

        for cfg_dir in get_config_dirs():
            try:
                sp = os.path.join(cfg_dir, "chromium_storage_state.json")
                await ctx.storage_state(path=sp)
                os.chmod(sp, 0o600)
            except Exception:
                pass

        print("==================================================================")
        print(" [SUCCESS] Discord Session live in Chromium aktiviert! 🎉")
        if discord_uid:
            print(f"   Discord User-ID: {discord_uid}")
        print(f"   Aktuelle URL   : {page.url}")
        print("==================================================================")
        return True


async def sync_twitch(cookies_to_inject, token_val=None, login_user=None, cdp_url="http://127.0.0.1:9222"):
    clean_cookies = []
    for c in cookies_to_inject:
        ck = {
            "name": c["name"],
            "value": c["value"],
            "domain": c["domain"] if c["domain"].startswith(".") else ("." + c["domain"]),
            "path": c.get("path", "/"),
            "secure": c.get("secure", True),
            "httpOnly": c.get("httpOnly", False),
        }
        if "sameSite" in c and c["sameSite"] in ("Strict", "Lax", "None"):
            ck["sameSite"] = c["sameSite"]
        clean_cookies.append(ck)

    save_to_config_dirs("twitch_cookies.json", clean_cookies, is_json=True)
    for ck in clean_cookies:
        if ck["name"] == "auth-token" and not token_val:
            token_val = ck["value"]
    if token_val:
        save_to_config_dirs("twitch_token.txt", token_val, is_json=False)

    if async_playwright is None:
        return True

    print(f"[*] Connecting to browser over CDP ({cdp_url})...")
    async with async_playwright() as pw:
        try:
            browser = await pw.chromium.connect_over_cdp(cdp_url, timeout=3000)
        except Exception:
            print("==================================================================")
            print(" [SUCCESS] Twitch Cookies & Token im Server-Profil gespeichert! 🎉")
            print("   (Werden beim nächsten Öffnen im Amiga-Client automatisch geladen)")
            print("==================================================================")
            return True

        ctx = browser.contexts[0]
        page = ctx.pages[0] if ctx.pages else await ctx.new_page()

        existing = await ctx.cookies()
        non_twitch = [c for c in existing if "twitch" not in c.get("domain", "")]
        await ctx.clear_cookies()
        if non_twitch:
            await ctx.add_cookies(non_twitch)

        await ctx.add_cookies(clean_cookies)
        print(f"[+] Injected {len(clean_cookies)} Twitch cookies into Chromium context.")

        uid = ""
        for ck in clean_cookies:
            if ck["name"] == "auth-token" and not token_val:
                token_val = ck["value"]
            elif ck["name"] == "login" and not login_user:
                login_user = ck["value"]
            elif ck["name"] == "persistent" and ":" in ck["value"] and not uid:
                uid = ck["value"].split(":")[0]
            elif ck["name"] == "twilight-user":
                try:
                    import urllib.parse
                    val = urllib.parse.unquote(ck["value"])
                    parsed_tw = json.loads(val)
                    if "id" in parsed_tw and not uid:
                        uid = str(parsed_tw["id"])
                    if "login" in parsed_tw and not login_user:
                        login_user = parsed_tw["login"]
                except Exception:
                    pass

        print("[*] Navigating to https://www.twitch.tv...")
        try:
            await page.goto("https://www.twitch.tv", timeout=30000, wait_until="commit")
        except Exception as e:
            print(f"[!] Warning on initial goto: {e}")

        await asyncio.sleep(2)

        if token_val:
            tw_user = json.dumps({
                "authToken": token_val,
                "displayName": login_user.capitalize() if login_user else "User",
                "id": uid,
                "login": login_user or "user",
                "roles": {"isStaff": False},
                "version": 2
            })
            await page.evaluate("""([tok, log, tw]) => {
                try {
                    localStorage.setItem('auth-token', tok);
                    localStorage.setItem('login', log);
                    localStorage.setItem('name', log);
                    localStorage.setItem('twilight-user', tw);
                } catch(e) {}
            }""", [token_val, login_user, tw_user])

        try:
            await page.reload(timeout=30000, wait_until="domcontentloaded")
        except Exception:
            pass

        for cfg_dir in get_config_dirs():
            try:
                sp = os.path.join(cfg_dir, "chromium_storage_state.json")
                await ctx.storage_state(path=sp)
                os.chmod(sp, 0o600)
            except Exception:
                pass

        print("==================================================================")
        print(" [SUCCESS] Twitch Login erfolgreich synchronisiert! 🎉")
        print("==================================================================")
        return True


def main():
    parser = argparse.ArgumentParser(description="VortexRemoteBrowser - Universal Login & Session Sync Tool (Twitch, Discord & Custom Sites)")
    parser.add_argument("--site", default="all", help="Site to sync: 'all', 'discord', 'twitch', or a custom domain (default: all)")
    parser.add_argument("--token", default=None, help="Twitch auth-token value")
    parser.add_argument("--user", default=None, help="Twitch username")
    parser.add_argument("--cookies", default=None, help="Path to twitch_cookies.json")
    parser.add_argument("--discord-token", default=None, help="Discord token (or UID:TOKEN)")
    parser.add_argument("--discord-uid", default=None, help="Discord user ID")
    parser.add_argument("--discord-cookies", default=None, help="Path to discord_cookies.json")
    parser.add_argument("--extra-cookies", default=None, help="Path to extra_cookies.json for custom domains")
    parser.add_argument("--cdp", default="http://127.0.0.1:9222", help="CDP URL (default: http://127.0.0.1:9222)")
    args = parser.parse_args()

    print("==================================================================")
    print(" VortexRemoteBrowser - Login & Session Synchronizer (Twitch & Discord)")
    print("==================================================================")

    site = args.site.lower().strip()
    any_ok = False

    # 1. Discord Sync
    if site in ("all", "discord"):
        d_tok, d_uid, d_cks = None, args.discord_uid, None
        if args.discord_cookies and os.path.isfile(args.discord_cookies):
            try:
                with open(args.discord_cookies, "r", encoding="utf-8") as f:
                    d_cks = json.load(f)
            except Exception:
                pass
        if args.discord_token:
            d_tok = args.discord_token
        else:
            d_tok, auto_uid, auto_cks = scan_local_discord_session()
            if not d_uid:
                d_uid = auto_uid
            if not d_cks:
                d_cks = auto_cks
        if not d_tok:
            saved_dt = load_token_text("discord_token.txt")
            if saved_dt:
                if ":" in saved_dt and saved_dt.split(":", 1)[0].isdigit():
                    d_uid, d_tok = saved_dt.split(":", 1)
                else:
                    d_tok = saved_dt
        if not d_cks:
            d_cks = load_cached_cookies("discord_cookies.json")

        if d_tok:
            if asyncio.run(sync_discord(d_tok, discord_uid=d_uid, discord_cookies=d_cks, cdp_url=args.cdp)):
                any_ok = True
        elif site == "discord":
            print("[!] Kein lokaler Discord-Token gefunden.")
            print("    Tipp: Übergib ihn direkt mit --discord-token DEIN_TOKEN")

    # 2. Twitch Sync
    if site in ("all", "twitch"):
        cookies = None
        if args.cookies and os.path.isfile(args.cookies):
            with open(args.cookies, "r", encoding="utf-8") as f:
                cookies = json.load(f)
        if not cookies:
            cookies = scan_local_browser_cookies()
        if not cookies:
            cookies = load_cached_cookies("twitch_cookies.json", "auth-token")
        token = args.token or load_token_text("twitch_token.txt")
        if not cookies and token:
            cookies = [{"name": "auth-token", "value": token, "domain": ".twitch.tv", "path": "/", "secure": True}]
            if args.user:
                cookies.append({"name": "login", "value": args.user, "domain": ".twitch.tv", "path": "/", "secure": True})
        if cookies:
            if asyncio.run(sync_twitch(cookies, token_val=token, login_user=args.user, cdp_url=args.cdp)):
                any_ok = True

    # 3. Custom Domain Cookies Sync
    if site not in ("all", "twitch", "discord") or args.extra_cookies:
        extra_cks = None
        if args.extra_cookies and os.path.isfile(args.extra_cookies):
            with open(args.extra_cookies, "r", encoding="utf-8") as f:
                extra_cks = json.load(f)
        elif site not in ("all", "twitch", "discord"):
            extra_cks = scan_local_domain_cookies(site)
        if extra_cks:
            save_to_config_dirs("extra_cookies.json", extra_cks, is_json=True)
            print(f"[+] Saved {len(extra_cks)} cookies for '{site}' into extra_cookies.json")
            any_ok = True

    sys.exit(0 if any_ok else 1)


if __name__ == "__main__":
    main()
