#!/usr/bin/env python3
# ==============================================================================
# VortexBrowser - Automated Twitch Login & Session Sync Tool
# Restores and verifies Twitch authenticated session in Ungoogled-Chromium/Chromium
# Completely portable: dynamically detects user home, system paths, and ownership.
# ==============================================================================
import argparse
import asyncio
import glob
import json
import os
import shutil
import sqlite3
import sys
import time

try:
    from playwright.async_api import async_playwright
except ImportError:
    print("[ERROR] Playwright not found in Python environment. Install with: pip install playwright")
    sys.exit(1)


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

    # Running as root: detect actual non-root user
    # 1. Check SUDO_USER environment variable
    sudo_user = os.environ.get("SUDO_USER")
    if sudo_user and sudo_user != "root":
        try:
            import pwd
            pw = pwd.getpwnam(sudo_user)
            return pw.pw_uid, pw.pw_gid, pw.pw_dir
        except Exception:
            pass

    # 2. Check the owner of the script directory
    try:
        import pwd
        script_dir = os.path.dirname(os.path.abspath(__file__))
        st = os.stat(script_dir)
        if st.st_uid != 0:
            pw = pwd.getpwuid(st.st_uid)
            return pw.pw_uid, pw.pw_gid, pw.pw_dir
    except Exception:
        pass

    # 3. Check existing /home/* directories containing .config/vortexbrowse
    for vb_dir in glob.glob("/home/*/.config/vortexbrowse"):
        try:
            import pwd
            st = os.stat(vb_dir)
            if st.st_uid != 0:
                pw = pwd.getpwuid(st.st_uid)
                return pw.pw_uid, pw.pw_gid, pw.pw_dir
        except Exception:
            pass

    # 4. Check any normal user home under /home (UID >= 1000)
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


def scan_local_browser_cookies():
    """
    Scans local browser profiles (LibreWolf, Firefox, Flatpaks, Snap)
    for active Twitch cookies using safe SQLite URI access.
    """
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

    for c in candidates:
        cookies = _read_cookies_from_sqlite(c)
        if cookies and any(ck["name"] == "auth-token" for ck in cookies):
            print(f"[+] Found {len(cookies)} Twitch cookies in local profile: {c}")
            return cookies
    return None


def _read_cookies_from_sqlite(sqlite_path):
    """Safely reads moz_cookies from sqlite, supporting read-only immutable access and WAL fallback."""
    # Method 1: Direct immutable URI read (safe even if browser is actively running and locked)
    try:
        con = sqlite3.connect(f"file:{sqlite_path}?immutable=1", uri=True, timeout=1.0)
        cur = con.cursor()
        rows = cur.execute(
            "SELECT name, value, host, path, expiry, isSecure, isHttpOnly, sameSite "
            "FROM moz_cookies WHERE host LIKE '%twitch%'"
        ).fetchall()
        con.close()
        if rows:
            return _format_sqlite_rows(rows)
    except Exception:
        pass

    # Method 2: Copy to temporary file with WAL
    tmp_db = f"/tmp/vortex_tw_{os.getpid()}_{int(time.time()*1000)}.sqlite"
    tmp_wal = f"{tmp_db}-wal"
    wal = f"{sqlite_path}-wal"
    try:
        shutil.copy2(sqlite_path, tmp_db)
        if os.path.isfile(wal):
            shutil.copy2(wal, tmp_wal)
        con = sqlite3.connect(tmp_db, timeout=2.0)
        cur = con.cursor()
        rows = cur.execute(
            "SELECT name, value, host, path, expiry, isSecure, isHttpOnly, sameSite "
            "FROM moz_cookies WHERE host LIKE '%twitch%'"
        ).fetchall()
        con.close()
        if rows:
            return _format_sqlite_rows(rows)
    except Exception:
        pass
    finally:
        if os.path.exists(tmp_db):
            os.remove(tmp_db)
        if os.path.exists(tmp_wal):
            os.remove(tmp_wal)

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


def load_cached_cookies(search_paths=None):
    """Loads saved Twitch cookies from JSON backup files using dynamic system paths."""
    if search_paths is None:
        script_dir = os.path.dirname(os.path.abspath(__file__))
        _, _, user_home = get_target_user_and_home()
        search_paths = [
            os.path.join(script_dir, "twitch_cookies.json"),
            os.path.join(user_home, ".config", "vortexbrowse", "twitch_cookies.json"),
            os.path.expanduser("~/.config/vortexbrowse/twitch_cookies.json"),
            "/tmp/twitch_cookies.json",
        ]
        # Also check all /home/*/Server-*/twitch_cookies.json
        search_paths.extend(glob.glob("/home/*/Server-*/twitch_cookies.json"))
        # De-duplicate while preserving order
        seen = set()
        search_paths = [p for p in search_paths if not (p in seen or seen.add(p))]

    for p in search_paths:
        if os.path.isfile(p):
            try:
                with open(p, "r", encoding="utf-8") as f:
                    cookies = json.load(f)
                if isinstance(cookies, list) and any(c.get("name") == "auth-token" for c in cookies):
                    print(f"[+] Loaded {len(cookies)} cached Twitch cookies from: {p}")
                    return cookies
            except Exception:
                pass
    return None


def load_token_text():
    """Loads plain auth-token string using dynamic system paths."""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    _, _, user_home = get_target_user_and_home()
    search_paths = [
        os.path.join(script_dir, "twitch_token.txt"),
        os.path.join(user_home, ".config", "vortexbrowse", "twitch_token.txt"),
        os.path.expanduser("~/.config/vortexbrowse/twitch_token.txt"),
        "/tmp/twitch_token.txt",
    ]
    search_paths.extend(glob.glob("/home/*/Server-*/twitch_token.txt"))
    seen = set()
    search_paths = [p for p in search_paths if not (p in seen or seen.add(p))]

    for p in search_paths:
        if os.path.isfile(p):
            try:
                with open(p, "r", encoding="utf-8") as f:
                    t = f.read().strip()
                if t and len(t) >= 16:
                    print(f"[+] Found saved Twitch auth-token in: {p}")
                    return t
            except Exception:
                pass
    return None


async def sync_twitch(cookies_to_inject, token_val=None, login_user=None, cdp_url="http://127.0.0.1:9222"):
    print(f"[*] Connecting to browser over CDP ({cdp_url})...")
    async with async_playwright() as pw:
        try:
            browser = await pw.chromium.connect_over_cdp(cdp_url)
        except Exception as e:
            print(f"[ERROR] Could not connect to Chromium CDP on {cdp_url}: {e}")
            print("        Make sure VortexBrowser server is running.")
            return False

        ctx = browser.contexts[0]
        page = ctx.pages[0] if ctx.pages else await ctx.new_page()

        # Clean cookie format for Playwright
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

        # Clear existing stale twitch cookies
        existing = await ctx.cookies()
        non_twitch = [c for c in existing if "twitch" not in c.get("domain", "")]
        await ctx.clear_cookies()
        if non_twitch:
            await ctx.add_cookies(non_twitch)

        await ctx.add_cookies(clean_cookies)
        print(f"[+] Injected {len(clean_cookies)} cookies into Chromium context.")

        # Determine token, user & user_id dynamically from cookies
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

        # Set localStorage state
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

        print("[*] Reloading Twitch to apply session...")
        try:
            await page.reload(timeout=30000, wait_until="domcontentloaded")
        except Exception:
            pass

        print("[*] Verifying login state...")
        verified = False
        user_avatar = ""
        user_name = ""

        for attempt in range(10):
            await asyncio.sleep(1)
            login_btn = await page.evaluate("() => !!document.querySelector('button[data-a-target=\"login-button\"]')")
            user_menu = await page.evaluate("() => !!document.querySelector('button[data-a-target=\"user-menu-toggle\"]')")

            if not login_btn and user_menu:
                verified = True
                user_avatar = await page.evaluate("""() => {
                    const img = document.querySelector('button[data-a-target="user-menu-toggle"] img');
                    return img ? img.src : '';
                }""")
                user_name = await page.evaluate("""() => {
                    const span = document.querySelector('button[data-a-target="user-menu-toggle"] [data-a-target="user-name"]');
                    return span ? span.innerText : '';
                }""")
                break

        if verified:
            print("==================================================================")
            print(" [SUCCESS] Twitch Login erfolgreich verifiziert! 🎉")
            if login_user or user_name:
                print(f"   Benutzer: {user_name or login_user}")
            if user_avatar:
                print(f"   Profilbild: {user_avatar[:70]}...")
            print("==================================================================")

            # Persist full storage state to disk across dynamically discovered locations
            script_dir = os.path.dirname(os.path.abspath(__file__))
            target_uid, target_gid, user_home = get_target_user_and_home()

            target_dirs = []
            c1 = os.path.join(user_home, ".config", "vortexbrowse")
            c2 = script_dir
            c3 = os.path.expanduser("~/.config/vortexbrowse")
            for d in [c1, c2, c3]:
                if d and d not in target_dirs:
                    target_dirs.append(d)

            for cfg_dir in target_dirs:
                try:
                    os.makedirs(cfg_dir, exist_ok=True)
                except Exception:
                    continue

                storage_path = os.path.join(cfg_dir, "chromium_storage_state.json")
                try:
                    await ctx.storage_state(path=storage_path)
                    print(f"[+] Browser-Status dauerhaft gesichert in: {storage_path}")
                except Exception:
                    pass

                cookie_backup = os.path.join(cfg_dir, "twitch_cookies.json")
                try:
                    with open(cookie_backup, "w", encoding="utf-8") as f:
                        json.dump(clean_cookies, f, indent=2)
                    print(f"[+] Cookies-Backup gesichert in: {cookie_backup}")
                except Exception:
                    pass

                if token_val:
                    token_backup = os.path.join(cfg_dir, "twitch_token.txt")
                    try:
                        with open(token_backup, "w", encoding="utf-8") as f:
                            f.write(token_val + "\n")
                    except Exception:
                        pass

                # Fix file ownership dynamically for non-root target user
                if os.getuid() == 0 and target_uid != 0:
                    for fn in ["chromium_storage_state.json", "twitch_cookies.json", "twitch_token.txt"]:
                        fp = os.path.join(cfg_dir, fn)
                        if os.path.exists(fp):
                            try:
                                os.chown(fp, target_uid, target_gid)
                            except Exception:
                                pass

            return True
        else:
            print("[FAILED] Twitch zeigt immer noch den Anmelde-Button an.")
            print("         Moeglicherweise ist der auth-token abgelaufen.")
            return False


def main():
    parser = argparse.ArgumentParser(description="VortexBrowser - Twitch Login & Session Sync Tool")
    parser.add_argument("--token", default=None, help="Twitch auth-token value")
    parser.add_argument("--user", default=None, help="Twitch username (default: auto-detected from session cookies)")
    parser.add_argument("--cookies", default=None, help="Path to twitch_cookies.json")
    parser.add_argument("--cdp", default="http://127.0.0.1:9222", help="CDP URL (default: http://127.0.0.1:9222)")
    args = parser.parse_args()

    print("==================================================================")
    print(" VortexBrowser - Twitch Automatic Login & Session Synchronizer")
    print("==================================================================")

    cookies = None
    if args.cookies and os.path.isfile(args.cookies):
        with open(args.cookies, "r", encoding="utf-8") as f:
            cookies = json.load(f)

    if not cookies:
        cookies = scan_local_browser_cookies()

    if not cookies:
        cookies = load_cached_cookies()

    token = args.token or load_token_text()

    if not cookies and token:
        # Build minimal cookie set from token
        cookies = [
            {"name": "auth-token", "value": token, "domain": ".twitch.tv", "path": "/", "secure": True},
        ]
        if args.user:
            cookies.append({"name": "login", "value": args.user, "domain": ".twitch.tv", "path": "/", "secure": True})
            cookies.append({"name": "name", "value": args.user, "domain": ".twitch.tv", "path": "/", "secure": True})

    if not cookies:
        print("[!] Keine gespeicherten Twitch-Cookies oder Token gefunden.")
        print("    Bitte logge dich z.B. in LibreWolf auf twitch.tv ein oder gib dein auth-token an.")
        try:
            token_in = input("Twitch auth-token eingeben (oder Enter zum Abbrechen): ").strip()
            if token_in:
                token = token_in
                cookies = [
                    {"name": "auth-token", "value": token, "domain": ".twitch.tv", "path": "/", "secure": True},
                ]
                if args.user:
                    cookies.append({"name": "login", "value": args.user, "domain": ".twitch.tv", "path": "/", "secure": True})
                    cookies.append({"name": "name", "value": args.user, "domain": ".twitch.tv", "path": "/", "secure": True})
            else:
                sys.exit(1)
        except (KeyboardInterrupt, EOFError):
            sys.exit(1)

    ok = asyncio.run(sync_twitch(cookies, token_val=token, login_user=args.user, cdp_url=args.cdp))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
