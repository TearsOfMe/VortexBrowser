#!/usr/bin/env python3
# ==============================================================================
# VortexBrowser - Automated Twitch Login & Session Sync Tool
# Restores and verifies Twitch authenticated session in Ungoogled-Chromium/Chromium
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


def scan_local_browser_cookies():
    """Scans local LibreWolf and Firefox profiles for active Twitch cookies."""
    candidates = (
        glob.glob(os.path.expanduser("~/.config/librewolf/librewolf/*/cookies.sqlite")) +
        glob.glob(os.path.expanduser("~/.mozilla/firefox/*/cookies.sqlite"))
    )
    for c in candidates:
        if not os.path.isfile(c):
            continue
        tmp_db = f"/tmp/vortex_tw_{os.getpid()}.sqlite"
        tmp_wal = f"{tmp_db}-wal"
        wal = f"{c}-wal"
        try:
            shutil.copy2(c, tmp_db)
            if os.path.isfile(wal):
                shutil.copy2(wal, tmp_wal)
            con = sqlite3.connect(tmp_db)
            cur = con.cursor()
            rows = cur.execute(
                "SELECT name, value, host, path, expiry, isSecure, isHttpOnly, sameSite "
                "FROM moz_cookies WHERE host LIKE '%twitch%'"
            ).fetchall()
            if rows:
                cookies = []
                for r in rows:
                    same_site_map = {0: "None", 1: "Lax", 2: "Strict"}
                    ss = same_site_map.get(r[7], "None")
                    cookies.append({
                        "name": r[0],
                        "value": r[1],
                        "domain": r[2],
                        "path": r[3] or "/",
                        "expires": float(r[4]) if r[4] else None,
                        "secure": bool(r[5]),
                        "httpOnly": bool(r[6]),
                        "sameSite": ss
                    })
                # Check if auth-token is present
                if any(ck["name"] == "auth-token" for ck in cookies):
                    print(f"[+] Found {len(cookies)} Twitch cookies in local profile: {c}")
                    return cookies
        except Exception:
            pass
        finally:
            if os.path.exists(tmp_db):
                os.remove(tmp_db)
            if os.path.exists(tmp_wal):
                os.remove(tmp_wal)
    return None


def load_cached_cookies(search_paths=None):
    """Loads saved Twitch cookies from JSON backup files."""
    if search_paths is None:
        search_paths = [
            os.path.join(os.path.dirname(os.path.abspath(__file__)), "twitch_cookies.json"),
            os.path.expanduser("~/.config/vortexbrowse/twitch_cookies.json"),
            "/home/tears/Server-CachyOS-EliteDesk/twitch_cookies.json",
            "/tmp/twitch_cookies.json",
        ]
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
    """Loads plain auth-token string if available."""
    search_paths = [
        os.path.join(os.path.dirname(os.path.abspath(__file__)), "twitch_token.txt"),
        os.path.expanduser("~/.config/vortexbrowse/twitch_token.txt"),
        "/home/tears/Server-CachyOS-EliteDesk/twitch_token.txt",
    ]
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
        # Give Twitch React SPA up to 10 seconds to mount auth headers
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
            print(f"   Benutzer: {login_user}")
            if user_avatar:
                print(f"   Profilbild: {user_avatar[:70]}...")
            print("==================================================================")

            # Persist full storage state to disk across relevant directories
            target_dirs = [
                os.path.expanduser("~/.config/vortexbrowse"),
                "$HOME/.config/vortexbrowse",
                "/home/tears/Server-CachyOS-EliteDesk",
            ]
            for cfg_dir in target_dirs:
                if not os.path.exists(os.path.dirname(cfg_dir)):
                    continue
                os.makedirs(cfg_dir, exist_ok=True)
                storage_path = os.path.join(cfg_dir, "chromium_storage_state.json")
                try:
                    await ctx.storage_state(path=storage_path)
                    print(f"[+] Browser-Status dauerhaft gesichert in: {storage_path}")
                except Exception as se:
                    pass

                cookie_backup = os.path.join(cfg_dir, "twitch_cookies.json")
                try:
                    with open(cookie_backup, "w", encoding="utf-8") as f:
                        json.dump(clean_cookies, f, indent=2)
                    print(f"[+] Cookies-Backup gesichert in: {cookie_backup}")
                except Exception as ce:
                    pass

                if token_val:
                    token_backup = os.path.join(cfg_dir, "twitch_token.txt")
                    try:
                        with open(token_backup, "w", encoding="utf-8") as f:
                            f.write(token_val + "\n")
                    except Exception:
                        pass

                # Fix file ownership for user tears if run as root
                if os.getuid() == 0:
                    try:
                        import pwd
                        pw = pwd.getpwnam("tears")
                        for fn in ["chromium_storage_state.json", "twitch_cookies.json", "twitch_token.txt"]:
                            fp = os.path.join(cfg_dir, fn)
                            if os.path.exists(fp):
                                os.chown(fp, pw.pw_uid, pw.pw_gid)
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
