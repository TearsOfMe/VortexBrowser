#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <proto/utility.h>
#include <proto/chooser.h>
#include <proto/bsdsocket.h>
#include <proto/popupmenu.h>
#include <proto/keymap.h>
#include <proto/icon.h>
#include <proto/wb.h>
#include <workbench/workbench.h>

#include <classes/window.h>
#include <classes/popupmenu.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/string.h>
#include <gadgets/space.h>
#include <gadgets/chooser.h>
#include <gadgets/fuelgauge.h>
#include <gadgets/clicktab.h>
#include <images/bevel.h>
#include <intuition/gui.h>
#include <intuition/gadgetclass.h>
#include <intuition/icclass.h>
#include <intuition/pointerclass.h>
#include <devices/inputevent.h>
#include <libraries/keymap.h>
#include <sys/select.h>
#include <sys/time.h>

#include <proto/clicktab.h>

#include <images/bitmap.h>
#include <images/label.h>
#include <libraries/gadtools.h>
#include <graphics/gfx.h>
#include <graphics/rpattr.h>
#include <graphics/blitattr.h>
#include <graphics/modeid.h>
#include <graphics/displayinfo.h>
#include <graphics/layers.h>
#include <graphics/view.h>

#include "gui.h"
#include "prefs.h"
#include "net.h"
#include "renderer.h"
#include "favorites.h"
#include "gui_prefs.h"
#include "gui_passwords.h"
#include "gui_download.h"
#include "clipboard.h"
#include "locale_strings.h"

extern struct SocketIFace *ISocket;
extern void app_sync_hidden_state(VortexApp *app, BOOL hidden);

static void send_amiga_clipboard_paste(VortexApp *app)
{
    if (!app) return;
    char *clip_str = clipboard_read_text();
    if (clip_str && clip_str[0] != '\0') {
        size_t src_len  = strlen(clip_str);
        size_t utf8_cap = src_len * 4 + 4;
        char *utf8_buf  = (char *)malloc(utf8_cap);
        if (utf8_buf) {
            vb_local_to_utf8(clip_str, utf8_buf, utf8_cap);
            net_send_clipboard(app, utf8_buf);
            free(utf8_buf);
        } else {
            net_send_clipboard(app, clip_str);
        }
        app_set_status("Aus Zwischenablage eingef\xFCgt (%u Zeichen)", (unsigned)src_len);
        free(clip_str);
    } else {
        if (clip_str) free(clip_str);
        net_send_key(app, INPUT_KEY_DOWN, 22, "Paste");
    }
}

enum {
    SHORTCUT_SRC_VANILLA = 1,
    SHORTCUT_SRC_RAWKEY  = 2,
    SHORTCUT_SRC_MENU    = 3
};

static char resolve_rawkey_base_char(uint16 rawkey)
{
    uint16 base = rawkey & 0x7F;
    if (IKeymap) {
        struct InputEvent ie;
        char kbuf[8];
        memset(&ie, 0, sizeof(ie));
        ie.ie_Class         = IECLASS_RAWKEY;
        ie.ie_SubClass      = 0;
        ie.ie_Code          = base;
        ie.ie_Qualifier     = 0;
        ie.ie_Prev1DownCode = 0x80; /* Per keymap.doc: 0x80 stops dead-key scan */
        ie.ie_Prev1DownQual = 0;
        if (IKeymap->MapRawKey(&ie, (STRPTR)kbuf, sizeof(kbuf) - 1, NULL) == 1) {
            return (char)tolower((unsigned char)kbuf[0]);
        }
    }
    switch (base) {
        case 0x20: return 'a';
        case 0x33: return 'c';
        case 0x34: return 'v';
        case 0x32: return 'x';
        case 0x15: return 'y';
        case 0x31: return 'z';
        default:   return '\0';
    }
}

static BOOL dispatch_edit_shortcut(VortexApp *app, char ch, int source)
{
    static char    s_last_ch  = '\0';
    static int     s_last_src = 0;
    static int64_t s_last_us  = 0;

    if (!app) return FALSE;
    char lc = (char)tolower((unsigned char)ch);
    if (lc != 'a' && lc != 'c' && lc != 'v' && lc != 'x' && lc != 'y' && lc != 'z') {
        return FALSE;
    }

    struct timeval tv;
    gettimeofday(&tv, NULL);
    int64_t now_us = (int64_t)tv.tv_sec * 1000000LL + (int64_t)tv.tv_usec;
    if (lc == s_last_ch && source != s_last_src && (now_us - s_last_us) < 80000LL) {
        /* Same keystroke delivered by multiple IDCMP classes (RAWKEY + VANILLAKEY/MENUPICK) */
        return TRUE;
    }
    s_last_ch  = lc;
    s_last_src = source;
    s_last_us  = now_us;

    switch (lc) {
        case 'a': net_send_key(app, INPUT_KEY_DOWN, 0x01, "Control+a"); break;
        case 'c': net_send_key(app, INPUT_KEY_DOWN, 0x03, "Copy");      break;
        case 'v': send_amiga_clipboard_paste(app);                      break;
        case 'x': net_send_key(app, INPUT_KEY_DOWN, 0x18, "Cut");       break;
        case 'y': net_send_key(app, INPUT_KEY_DOWN, 0x19, "Control+y"); break;
        case 'z': net_send_key(app, INPUT_KEY_DOWN, 0x1A, "Control+z"); break;
        default:  return FALSE;
    }
    return TRUE;
}

static BOOL dispatch_zoom_shortcut(VortexApp *app, int dir, int source)
{
    static int     s_last_dir = 99;
    static int     s_last_src = 0;
    static int64_t s_last_us  = 0;

    if (!app) return FALSE;

    struct timeval tv;
    gettimeofday(&tv, NULL);
    int64_t now_us = (int64_t)tv.tv_sec * 1000000LL + (int64_t)tv.tv_usec;
    if (dir == s_last_dir && source != s_last_src && (now_us - s_last_us) < 80000LL) {
        /* Same keystroke delivered by both RAWKEY and MENUPICK */
        return TRUE;
    }
    s_last_dir = dir;
    s_last_src = source;
    s_last_us  = now_us;

    static const uint16_t k_zoom_steps[] = {
        50, 67, 75, 80, 90, 100, 110, 125, 150, 175, 200, 250, 300
    };
    const int n_steps = (int)(sizeof(k_zoom_steps) / sizeof(k_zoom_steps[0]));
    uint16_t cur = app->prefs.font.web_zoom_pct ? app->prefs.font.web_zoom_pct : 100;
    uint16_t next_zoom = cur;

    if (dir == 0) {
        next_zoom = 100;
        app->prefs.font.web_font_size = 16;
    } else if (dir > 0) {
        next_zoom = k_zoom_steps[n_steps - 1];
        for (int i = 0; i < n_steps; i++) {
            if (k_zoom_steps[i] > cur) {
                next_zoom = k_zoom_steps[i];
                break;
            }
        }
    } else {
        next_zoom = k_zoom_steps[0];
        for (int i = n_steps - 1; i >= 0; i--) {
            if (k_zoom_steps[i] < cur) {
                next_zoom = k_zoom_steps[i];
                break;
            }
        }
    }

    app->prefs.font.web_zoom_pct = next_zoom;
    prefs_save(app, TRUE);
    if (app->connected) {
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "@ZOOM:%u", (unsigned)next_zoom);
        net_send_url(app, cmd);
        if (dir == 0) {
            net_send_url(app, "@FONTSIZE:16");
        }
    }
    if (dir == 0) {
        app_set_status(GetVString(MSG_STATUS_FONT_RESET));
    } else {
        app_set_status("Zoom: %u%%", (unsigned)next_zoom);
    }
    return TRUE;
}

static struct DiskObject *load_app_icon(void)
{
    if (!IIcon) return NULL;

    struct DiskObject *dobj = NULL;
#if defined(__ALTIVEC__)
    dobj = IIcon->GetDiskObject("PROGDIR:VortexRemoteBrowser-AltiVec");
    if (!dobj) dobj = IIcon->GetDiskObject("PROGDIR:VortexRemoteBrowser");
#else
    dobj = IIcon->GetDiskObject("PROGDIR:VortexRemoteBrowser");
    if (!dobj) dobj = IIcon->GetDiskObject("PROGDIR:VortexRemoteBrowser-AltiVec");
#endif
    if (dobj) {
        dobj->do_CurrentX = NO_ICON_POSITION;
        dobj->do_CurrentY = NO_ICON_POSITION;
    }
    return dobj;
}

static void handle_menu_command(VortexApp *app, uint32 item_id);
static void open_main_menu_popup(VortexApp *app, int16_t mx, int16_t my);
static void open_quicklink_popup(VortexApp *app, int qidx, int16_t mx, int16_t my);

/* Amiga Standard Pull-Down Menu definition */
static struct NewMenu s_main_menu[] = {
    { NM_TITLE, (STRPTR)"Projekt",                                     NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Neu (Startseite)",                            NULL,        0, 0, (APTR)MID_PROJ_NEW },
    { NM_ITEM,  (STRPTR)"URL öffnen...",                              (STRPTR)"O", 0, 0, (APTR)MID_PROJ_OPEN },
    { NM_ITEM,  (STRPTR)"Neuer Tab",                                   (STRPTR)"T", 0, 0, (APTR)MID_TAB_NEW },
    { NM_ITEM,  (STRPTR)"Aktuellen Tab schließen",                    (STRPTR)"W", 0, 0, (APTR)MID_TAB_CLOSE },
    { NM_ITEM,  NM_BARLABEL,                                           NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Passwort-Manager...",                         (STRPTR)"K", 0, 0, (APTR)MID_PROJ_PASSWORDS },
    { NM_ITEM,  (STRPTR)"Download-Manager...",                         (STRPTR)"D", 0, 0, (APTR)MID_PROJ_DOWNLOADS },
    { NM_ITEM,  NM_BARLABEL,                                           NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Server konfigurieren...",                     (STRPTR)"1", 0, 0, (APTR)MID_SET_SERVER_CUSTOM },
    { NM_ITEM,  (STRPTR)"Mit Server verbinden",                        (STRPTR)"2", 0, 0, (APTR)MID_PROJ_CONNECT },
    { NM_ITEM,  (STRPTR)"Verbindung trennen",                          NULL,        0, 0, (APTR)MID_PROJ_DISCONNECT },
    { NM_ITEM,  NM_BARLABEL,                                           NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Einstellungen...",                            (STRPTR)"E", 0, 0, (APTR)MID_SETTINGS },
    { NM_ITEM,  (STRPTR)"Über VortexRemoteBrowser...",                      (STRPTR)"?", 0, 0, (APTR)MID_PROJ_ABOUT },
    { NM_ITEM,  NM_BARLABEL,                                           NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Beenden",                                     (STRPTR)"Q", 0, 0, (APTR)MID_PROJ_QUIT },

    { NM_TITLE, (STRPTR)"Bearbeiten",                                  NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Ausschneiden",                                (STRPTR)"X", 0, 0, (APTR)MID_EDIT_CUT },
    { NM_ITEM,  (STRPTR)"Kopieren",                                    (STRPTR)"C", 0, 0, (APTR)MID_EDIT_COPY },
    { NM_ITEM,  (STRPTR)"Einfügen",                                   (STRPTR)"V", 0, 0, (APTR)MID_EDIT_PASTE },

    { NM_TITLE, (STRPTR)"Ansicht",                                     NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Stream: 1:1 Nativ (Fenster-Größe)",         NULL,        0, 0, (APTR)MID_STREAM_RES_NATIVE },
    { NM_ITEM,  (STRPTR)"Stream: 720p HD (1280x720) [30-50 FPS]",      NULL,        0, 0, (APTR)MID_STREAM_RES_720P },
    { NM_ITEM,  (STRPTR)"Stream: 540p qHD (960x540) [Turbo]",          NULL,        0, 0, (APTR)MID_STREAM_RES_540P },
    { NM_ITEM,  (STRPTR)"Stream: 1080p FHD (1920x1080)",               NULL,        0, 0, (APTR)MID_STREAM_RES_1080P },
    { NM_ITEM,  NM_BARLABEL,                                           NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"FPS: 15 FPS (Stromsparend / Langsame CPU)",   NULL,        0, 0, (APTR)MID_FPS_15 },
    { NM_ITEM,  (STRPTR)"FPS: 25 FPS (Standard / PAL flüssig)",       NULL,        0, 0, (APTR)MID_FPS_25 },
    { NM_ITEM,  (STRPTR)"FPS: 30 FPS (Empfohlen für YouTube / NTSC)",  NULL,        0, 0, (APTR)MID_FPS_30 },
    { NM_ITEM,  (STRPTR)"FPS: 50 FPS (PAL High-Speed)",                NULL,        0, 0, (APTR)MID_FPS_50 },
    { NM_ITEM,  (STRPTR)"FPS: 60 FPS (Maximal / Gaming / PiStorm)",    NULL,        0, 0, (APTR)MID_FPS_60 },
    { NM_ITEM,  NM_BARLABEL,                                           NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Video-Stream: Auto (H.264 via va.library)",   NULL,        0, 0, (APTR)MID_STREAM_VIDEO_AUTO },
    { NM_ITEM,  (STRPTR)"Video-Stream: Nur Dirty-Rect (LZ4)",          NULL,        0, 0, (APTR)MID_STREAM_VIDEO_LZ4 },
    { NM_ITEM,  (STRPTR)"Video-Stream: Nur H.264 (Dauerhaft)",         NULL,        0, 0, (APTR)MID_STREAM_VIDEO_H264 },
    { NM_ITEM,  NM_BARLABEL,                                           NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Dunkles Theme (Dark Mode) an/aus",            (STRPTR)"U", 0, 0, (APTR)MID_TOGGLE_DARK_THEME },
    { NM_ITEM,  NM_BARLABEL,                                           NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Vollbild / Bildschirm umschalten (F11)",      NULL,        0, 0, (APTR)MID_TOGGLE_SCREEN },
    { NM_ITEM,  (STRPTR)"Statusleiste umschalten",                     NULL,        0, 0, (APTR)MID_TOGGLE_STATUSBAR },
    { NM_ITEM,  (STRPTR)"Fenster fixieren (Snapshot)",                 NULL,        0, 0, (APTR)MID_WINDOW_SNAPSHOT },
    { NM_ITEM,  (STRPTR)"Fensterposition zurücksetzen",                NULL,        0, 0, (APTR)MID_WINDOW_UNSNAPSHOT },
    { NM_ITEM,  NM_BARLABEL,                                           NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Schrift vergrößern (+)",                        (STRPTR)"+", 0, 0, (APTR)MID_FONT_LARGER },
    { NM_ITEM,  (STRPTR)"Schrift verkleinern (-)",                         (STRPTR)"-", 0, 0, (APTR)MID_FONT_SMALLER },
    { NM_ITEM,  (STRPTR)"Standard-Schriftgröße (16px)",              (STRPTR)"0", 0, 0, (APTR)MID_FONT_RESET },

    { NM_TITLE, (STRPTR)"Navigation",                                  NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Zurück",                                     (STRPTR)"B", 0, 0, (APTR)MID_NAV_BACK },
    { NM_ITEM,  (STRPTR)"Vorwärts",                                   (STRPTR)"F", 0, 0, (APTR)MID_NAV_FORWARD },
    { NM_ITEM,  (STRPTR)"Neu laden",                                   (STRPTR)"R", 0, 0, (APTR)MID_NAV_RELOAD },
    { NM_ITEM,  (STRPTR)"Stopp",                                       (STRPTR)"S", 0, 0, (APTR)MID_NAV_STOP },
    { NM_ITEM,  (STRPTR)"Startseite",                                  (STRPTR)"H", 0, 0, (APTR)MID_NAV_HOME },

    { NM_TITLE, (STRPTR)"Lesezeichen",                                 NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Lesezeichen hinzufügen",                     (STRPTR)"A", 0, 0, (APTR)MID_FAV_ADD },
    { NM_ITEM,  (STRPTR)"Zu Schnellwahl hinzufügen",                  NULL,        0, 0, (APTR)MID_QUICK_ADD_CURRENT },
    { NM_ITEM,  NM_BARLABEL,                                           NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Amiga-News.de",                               NULL,        0, 0, (APTR)MID_FAV_AMIGANEWS },
    { NM_ITEM,  (STRPTR)"OS4Depot.net",                                NULL,        0, 0, (APTR)MID_FAV_OS4DEPOT },
    { NM_ITEM,  (STRPTR)"Amigans.net",                                 NULL,        0, 0, (APTR)MID_FAV_AMIGANS },
    { NM_ITEM,  (STRPTR)"Aminet.net",                                  NULL,        0, 0, (APTR)MID_FAV_AMINET },
    { NM_ITEM,  (STRPTR)"YouTube",                                     NULL,        0, 0, (APTR)MID_FAV_YOUTUBE },
    { NM_ITEM,  (STRPTR)"Wikipedia",                                   NULL,        0, 0, (APTR)MID_FAV_WIKIPEDIA },

    { NM_END,   NULL,                                                  NULL,        0, 0, NULL }
};

static void gui_localize_menu(void)
{
    static BOOL s_menu_localized = FALSE;
    int title_idx = 0;
    if (s_menu_localized) return;
    s_menu_localized = TRUE;

    for (int i = 0; s_main_menu[i].nm_Type != NM_END; i++) {
        if (s_main_menu[i].nm_Type == NM_TITLE) {
            switch (title_idx++) {
                case 0: s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_PROJECT); break;
                case 1: s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_EDIT); break;
                case 2: s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_VIEW); break;
                case 3: s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_NAVIGATION); break;
                case 4: s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_BOOKMARKS); break;
                default: break;
            }
        } else if (s_main_menu[i].nm_Type == NM_ITEM) {
            uint32 mid = (uint32)s_main_menu[i].nm_UserData;
            switch (mid) {
                case MID_PROJ_NEW:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_NEW_HOME); break;
                case MID_PROJ_OPEN:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_OPEN_URL); break;
                case MID_TAB_NEW:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_TAB_NEW); break;
                case MID_TAB_CLOSE:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_TAB_CLOSE); break;
                case MID_PROJ_PASSWORDS:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_PASSWORDS); break;
                case MID_PROJ_DOWNLOADS:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_DOWNLOADS); break;
                case MID_SET_SERVER_CUSTOM:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_SERVER_CFG); break;
                case MID_PROJ_CONNECT:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_SERVER_CONNECT); break;
                case MID_PROJ_DISCONNECT:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_SERVER_DISCONNECT); break;
                case MID_SETTINGS:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_PREFERENCES); break;
                case MID_PROJ_ABOUT:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_ABOUT); break;
                case MID_PROJ_QUIT:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_QUIT); break;
                case MID_EDIT_CUT:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_CUT); break;
                case MID_EDIT_COPY:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_COPY); break;
                case MID_EDIT_PASTE:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_PASTE); break;
                case MID_STREAM_RES_NATIVE:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_STREAM_RES_NATIVE); break;
                case MID_STREAM_RES_720P:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_STREAM_RES_720P); break;
                case MID_STREAM_RES_540P:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_STREAM_RES_540P); break;
                case MID_STREAM_RES_1080P:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_STREAM_RES_1080P); break;
                case MID_FPS_15:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_FPS_15); break;
                case MID_FPS_25:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_FPS_25); break;
                case MID_FPS_30:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_FPS_30); break;
                case MID_FPS_50:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_FPS_50); break;
                case MID_FPS_60:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_FPS_60); break;
                case MID_STREAM_VIDEO_AUTO:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_VIDEO_AUTO); break;
                case MID_STREAM_VIDEO_LZ4:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_VIDEO_LZ4); break;
                case MID_STREAM_VIDEO_H264:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_VIDEO_H264); break;
                case MID_TOGGLE_DARK_THEME:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_TOGGLE_DARK); break;
                case MID_TOGGLE_SCREEN:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_TOGGLE_SCREEN); break;
                case MID_TOGGLE_STATUSBAR:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_TOGGLE_STATUSBAR); break;
                case MID_WINDOW_SNAPSHOT:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_WIN_SNAPSHOT); break;
                case MID_WINDOW_UNSNAPSHOT:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_WIN_UNSNAPSHOT); break;
                case MID_FONT_LARGER:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_ZOOM_IN); break;
                case MID_FONT_SMALLER:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_ZOOM_OUT); break;
                case MID_FONT_RESET:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_ZOOM_RESET); break;
                case MID_NAV_BACK:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_BACK); break;
                case MID_NAV_FORWARD:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_FORWARD); break;
                case MID_NAV_RELOAD:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_RELOAD); break;
                case MID_NAV_STOP:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_STOP); break;
                case MID_NAV_HOME:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_HOME); break;
                case MID_FAV_ADD:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_ADD_BOOKMARK); break;
                case MID_QUICK_ADD_CURRENT:
                    s_main_menu[i].nm_Label = (STRPTR)GetVString(MSG_MENU_ADD_QUICKLINK); break;
                default:
                    break;
            }
        }
    }
}

#define MAX_AISS_IMAGES 32
static Object *s_aiss_images[MAX_AISS_IMAGES];
static char   *s_aiss_paths[MAX_AISS_IMAGES * 3];
static int     s_num_aiss_images = 0;
static int     s_num_aiss_paths  = 0;

static struct Image *load_aiss_image(const char *const candidates[], int num_candidates, struct Screen *screen)
{
    if (!screen || !candidates || num_candidates <= 0) {
        return NULL;
    }

    APTR prev_win = IDOS->SetProcWindow((APTR)-1);
    BPTR tb_dir = IDOS->Lock("TBIMAGES:", SHARED_LOCK);
    IDOS->SetProcWindow(prev_win);

    if (tb_dir == ZERO) {
        return NULL;
    }
    IDOS->UnLock(tb_dir);

    for (int i = 0; i < num_candidates; i++) {
        const char *cand = candidates[i];
        if (!cand || cand[0] == '\0') {
            continue;
        }

        char path[128];
        char path_s[128];
        char path_g[128];

        snprintf(path,   sizeof(path),   "TBIMAGES:%s", cand);
        snprintf(path_s, sizeof(path_s), "TBIMAGES:%s_s", cand);
        snprintf(path_g, sizeof(path_g), "TBIMAGES:%s_g", cand);

        BPTR lk = IDOS->Lock(path, SHARED_LOCK);
        if (lk == ZERO) {
            continue; /* File does not exist, try next candidate */
        }
        IDOS->UnLock(lk);

        BOOL has_s = FALSE;
        BPTR lk_s = IDOS->Lock(path_s, SHARED_LOCK);
        if (lk_s != ZERO) {
            IDOS->UnLock(lk_s);
            has_s = TRUE;
        }

        BOOL has_g = FALSE;
        BPTR lk_g = IDOS->Lock(path_g, SHARED_LOCK);
        if (lk_g != ZERO) {
            IDOS->UnLock(lk_g);
            has_g = TRUE;
        }

        /* Persistent string copies so bitmap.image pointer remains permanently valid */
        char *p_norm = strdup(path);
        char *p_sel  = has_s ? strdup(path_s) : NULL;
        char *p_dis  = has_g ? strdup(path_g) : NULL;

        if (s_num_aiss_paths < (MAX_AISS_IMAGES * 3) - 3) {
            if (p_norm) s_aiss_paths[s_num_aiss_paths++] = p_norm;
            if (p_sel)  s_aiss_paths[s_num_aiss_paths++] = p_sel;
            if (p_dis)  s_aiss_paths[s_num_aiss_paths++] = p_dis;
        }

        struct Image *img = (struct Image *)IIntuition->NewObject(NULL, "bitmap.image",
            BITMAP_SourceFile,                              (Tag)p_norm,
            has_s ? BITMAP_SelectSourceFile : TAG_IGNORE,   (Tag)p_sel,
            has_g ? BITMAP_DisabledSourceFile : TAG_IGNORE, (Tag)p_dis,
            BITMAP_Screen,                                  (Tag)screen,
            BITMAP_Masking,                                 TRUE,
            TAG_DONE);

        if (img) {
            if (s_num_aiss_images < MAX_AISS_IMAGES) {
                s_aiss_images[s_num_aiss_images++] = (Object *)img;
            }
            return img;
        }
    }

    return NULL;
}

static Object        *s_tab_close_image = NULL;
static struct BitMap *s_tab_close_bm[3] = { NULL, NULL, NULL }; /* 0=normal, 1=selected, 2=disabled */

static void gui_free_tab_close_image(void)
{
    if (s_tab_close_image) {
        IIntuition->DisposeObject(s_tab_close_image);
        s_tab_close_image = NULL;
    }
    for (int i = 0; i < 3; i++) {
        if (s_tab_close_bm[i]) {
            IGraphics->FreeBitMap(s_tab_close_bm[i]);
            s_tab_close_bm[i] = NULL;
        }
    }
}

static struct BitMap *make_tab_close_bitmap(struct Screen *scr, int state)
{
    const int w = 13;
    const int h = 13;

    struct BitMap *bm = IGraphics->AllocBitMapTags(
        w, h, 32,
        BMATags_PixelFormat, PIXF_A8R8G8B8,
        BMATags_Displayable, TRUE,
        scr ? BMATags_Friend : TAG_IGNORE, (Tag)(scr ? scr->RastPort.BitMap : NULL),
        TAG_DONE);
    if (!bm && scr) {
        bm = IGraphics->AllocBitMapTags(
            w, h, 32,
            BMATags_PixelFormat, PIXF_A8R8G8B8,
            BMATags_Displayable, TRUE,
            TAG_DONE);
    }
    if (!bm) {
        return NULL;
    }

    uint32_t pixels[13 * 13];
    uint8_t top_r, top_g, top_b, bot_r, bot_g, bot_b;
    uint32_t border_argb, x_argb;
    uint8_t x_r, x_g, x_b;

    if (state == 1) {
        /* Pressed / selected */
        top_r = 0x9E; top_g = 0x1C; top_b = 0x16;
        bot_r = 0x74; bot_g = 0x10; bot_b = 0x0C;
        border_argb = 0xFF4E0806U;
        x_r = 0xF0; x_g = 0xE8; x_b = 0xE8;
    } else if (state == 2) {
        /* Inactive tab / disabled look */
        top_r = 0x8E; top_g = 0x92; top_b = 0x9A;
        bot_r = 0x6C; bot_g = 0x70; bot_b = 0x78;
        border_argb = 0xFF50545CU;
        x_r = 0xE0; x_g = 0xE4; x_b = 0xEA;
    } else {
        /* Normal OWB-style red close gadget */
        top_r = 0xDF; top_g = 0x40; top_b = 0x38;
        bot_r = 0xA8; bot_g = 0x1E; bot_b = 0x18;
        border_argb = 0xFF78120EU;
        x_r = 0xFF; x_g = 0xFF; x_b = 0xFF;
    }
    x_argb = (0xFFU << 24) | ((uint32_t)x_r << 16) | ((uint32_t)x_g << 8) | (uint32_t)x_b;

    for (int y = 0; y < h; y++) {
        int dy = y - 1;
        if (dy < 0) dy = 0;
        if (dy > 10) dy = 10;
        uint8_t bg_r = (uint8_t)(top_r + ((int)(bot_r - top_r) * dy) / 10);
        uint8_t bg_g = (uint8_t)(top_g + ((int)(bot_g - top_g) * dy) / 10);
        uint8_t bg_b = (uint8_t)(top_b + ((int)(bot_b - top_b) * dy) / 10);

        for (int x = 0; x < w; x++) {
            uint32_t px;
            if ((x == 0 || x == 12) && (y == 0 || y == 12)) {
                px = 0x00000000U;
            } else if (x == 0 || x == 12 || y == 0 || y == 12 ||
                       ((x == 1 || x == 11) && (y == 1 || y == 11))) {
                px = border_argb;
            } else {
                px = (0xFFU << 24) | ((uint32_t)bg_r << 16) | ((uint32_t)bg_g << 8) | (uint32_t)bg_b;
                if (x >= 3 && x <= 9 && y >= 3 && y <= 9) {
                    int d1 = abs(x - y);
                    int d2 = abs((x + y) - 12);
                    if (d1 == 0 || d2 == 0) {
                        px = x_argb;
                    } else if (d1 == 1 || d2 == 1) {
                        uint8_t mr = (uint8_t)((bg_r * 3 + x_r * 2) / 5);
                        uint8_t mg = (uint8_t)((bg_g * 3 + x_g * 2) / 5);
                        uint8_t mb = (uint8_t)((bg_b * 3 + x_b * 2) / 5);
                        px = (0xFFU << 24) | ((uint32_t)mr << 16) | ((uint32_t)mg << 8) | (uint32_t)mb;
                    }
                }
            }
            pixels[y * w + x] = px;
        }
    }

    APTR base_addr = NULL;
    uint32 dst_bpr = 0;
    APTR lock = IGraphics->LockBitMapTags(
        bm,
        LBM_BaseAddress, &base_addr,
        LBM_BytesPerRow, &dst_bpr,
        TAG_DONE);

    if (!lock || !base_addr || dst_bpr < (uint32)(w * 4)) {
        if (lock) {
            IGraphics->UnlockBitMap(lock);
        }
        IGraphics->FreeBitMap(bm);
        return NULL;
    }

    uint8_t *dst = (uint8_t *)base_addr;
    for (int y = 0; y < h; y++) {
        memcpy(dst + (size_t)y * dst_bpr, &pixels[y * w], (size_t)w * 4U);
    }
    IGraphics->UnlockBitMap(lock);
    return bm;
}

static Object *gui_create_tab_close_image(struct Screen *scr)
{
    gui_free_tab_close_image();

    s_tab_close_bm[0] = make_tab_close_bitmap(scr, 0);
    s_tab_close_bm[1] = make_tab_close_bitmap(scr, 1);
    s_tab_close_bm[2] = make_tab_close_bitmap(scr, 2);

    if (!s_tab_close_bm[0]) {
        gui_free_tab_close_image();
        return NULL;
    }

    s_tab_close_image = IIntuition->NewObject(NULL, "bitmap.image",
        BITMAP_BitMap,                                             (Tag)s_tab_close_bm[0],
        s_tab_close_bm[1] ? BITMAP_SelectBitMap : TAG_IGNORE,      (Tag)s_tab_close_bm[1],
        s_tab_close_bm[2] ? BITMAP_DisabledBitMap : TAG_IGNORE,    (Tag)s_tab_close_bm[2],
        BITMAP_Width,                                              13,
        BITMAP_Height,                                             13,
        BITMAP_Screen,                                             (Tag)scr,
        BITMAP_Masking,                                            TRUE,
        BITMAP_HasAlpha,                                           TRUE,
        TAG_DONE);

    if (!s_tab_close_image) {
        gui_free_tab_close_image();
    }
    return s_tab_close_image;
}

/* ------------------------------------------------------------------------
 * AmigaOS 4 Dark Theme Pens & Dynamic BackFill Hook
 * ------------------------------------------------------------------------ */
typedef struct {
    BOOL             allocated;
    struct ColorMap *cm;
    int32            bg_pen;
    int32            btn_bg_pen;
    int32            btn_sel_pen;
    int32            txt_pen;
    int32            inp_bg_pen;
    int32            gauge_fill_pen;
    int32            raw_bg;
    int32            raw_btn_bg;
    int32            raw_btn_sel;
    int32            raw_txt;
    int32            raw_inp_bg;
    int32            raw_gauge_fill;
} DarkPens;

static DarkPens s_dark_pens = { FALSE, NULL, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
static struct Hook s_dark_backfill_hook;
static struct Hook s_space_dark_render_hook;
static struct Hook s_dark_winbar_active_hook;
static struct Hook s_dark_winbar_inact_hook;
static struct Hook s_dark_screenbar_hook;

static BOOL s_orig_gui_attrs_saved = FALSE;
static struct Screen *s_orig_gui_screen = NULL;
static ULONG s_orig_winbar_active_hook = 0;
static ULONG s_orig_winbar_inact_hook = 0;
static ULONG s_orig_winborder_active_hook = 0;
static ULONG s_orig_winborder_inact_hook = 0;
static ULONG s_orig_screenbar_hook = 0;
static ULONG s_orig_tab_title_hook = 0;
static ULONG s_orig_tab_active_hook = 0;
static ULONG s_orig_tab_inact_hook = 0;
static ULONG s_orig_winborder_look = 0;
static ULONG s_orig_winbar_thick = 0;
static ULONG s_orig_winframe_thick = 0;
static ULONG s_orig_clicktab_look = 0;

static uint32 dark_backfill_hook_func(struct Hook *hook, struct RastPort *rp, struct BackFillMessage *msg)
{
    (void)hook;
    if (!rp || !msg) return 0;

    struct RastPort crp = *rp;
    crp.Layer = NULL;
    uint32 fill_pen = (s_dark_pens.allocated && s_dark_pens.bg_pen >= 0) ? (uint32)s_dark_pens.bg_pen : 1;
    IGraphics->SetAPen(&crp, fill_pen);
    IGraphics->RectFill(&crp, msg->Bounds.MinX, msg->Bounds.MinY, msg->Bounds.MaxX, msg->Bounds.MaxY);
    return 0;
}

static uint32 space_dark_render_hook_func(struct Hook *hook, APTR obj, struct gpRender *msg)
{
    (void)hook;
    if (!obj || !msg || !msg->gpr_RPort || !s_dark_pens.allocated) return 0;
    struct RastPort *rp = msg->gpr_RPort;
    struct Gadget *g = (struct Gadget *)obj;
    uint32 fill_pen = (s_dark_pens.bg_pen >= 0) ? (uint32)s_dark_pens.bg_pen : 1;
    IGraphics->SetAPen(rp, fill_pen);
    IGraphics->RectFill(rp, g->LeftEdge, g->TopEdge, g->LeftEdge + g->Width - 1, g->TopEdge + g->Height - 1);
    return 0;
}

static uint32 dark_winbar_active_hook_func(struct Hook *hook, struct RastPort *rp, struct BackFillMessage *msg)
{
    (void)hook;
    if (!rp || !msg) return 0;

    struct RastPort crp = *rp;
    crp.Layer = NULL;
    uint32 fill_pen = (s_dark_pens.allocated && s_dark_pens.btn_bg_pen >= 0) ? (uint32)s_dark_pens.btn_bg_pen
                    : ((s_dark_pens.allocated && s_dark_pens.bg_pen >= 0) ? (uint32)s_dark_pens.bg_pen : 1);
    IGraphics->SetAPen(&crp, fill_pen);
    IGraphics->RectFill(&crp, msg->Bounds.MinX, msg->Bounds.MinY, msg->Bounds.MaxX, msg->Bounds.MaxY);
    return 0;
}

static uint32 dark_winbar_inact_hook_func(struct Hook *hook, struct RastPort *rp, struct BackFillMessage *msg)
{
    (void)hook;
    if (!rp || !msg) return 0;

    struct RastPort crp = *rp;
    crp.Layer = NULL;
    uint32 fill_pen = (s_dark_pens.allocated && s_dark_pens.bg_pen >= 0) ? (uint32)s_dark_pens.bg_pen : 1;
    IGraphics->SetAPen(&crp, fill_pen);
    IGraphics->RectFill(&crp, msg->Bounds.MinX, msg->Bounds.MinY, msg->Bounds.MaxX, msg->Bounds.MaxY);
    return 0;
}

static uint32 dark_screenbar_hook_func(struct Hook *hook, struct RastPort *rp, struct BackFillMessage *msg)
{
    (void)hook;
    if (!rp || !msg) return 0;

    struct RastPort crp = *rp;
    crp.Layer = NULL;
    uint32 fill_pen = (s_dark_pens.allocated && s_dark_pens.inp_bg_pen >= 0) ? (uint32)s_dark_pens.inp_bg_pen : 1;
    IGraphics->SetAPen(&crp, fill_pen);
    IGraphics->RectFill(&crp, msg->Bounds.MinX, msg->Bounds.MinY, msg->Bounds.MaxX, msg->Bounds.MaxY);
    return 0;
}

static struct Hook s_post_refresh_hook;
static BOOL s_in_draw_titlebar = FALSE;

void gui_draw_dark_titlebar(VortexApp *app)
{
    if (!app || !app->win || app->prefs.dark_theme != 1) return;
    if (s_in_draw_titlebar) return; /* Prevent re-entrancy / recursive layer lock freezes */
    s_in_draw_titlebar = TRUE;

    struct Window *win = app->win;
    struct RastPort *rp = win->RPort;
    if (!rp || !s_dark_pens.allocated) {
        s_in_draw_titlebar = FALSE;
        return;
    }

    int top_h = win->BorderTop;
    int win_w = win->Width;
    int win_h = win->Height;
    if (top_h <= 2 || win_w <= 20) {
        s_in_draw_titlebar = FALSE;
        return;
    }

    BOOL active = (win->Flags & WFLG_WINDOWACTIVE) != 0;

    uint32 bar_pen  = (active && s_dark_pens.btn_bg_pen >= 0) ? (uint32)s_dark_pens.btn_bg_pen
                     : ((s_dark_pens.bg_pen >= 0) ? (uint32)s_dark_pens.bg_pen : 1);
    uint32 edge_pen = (s_dark_pens.btn_sel_pen >= 0) ? (uint32)s_dark_pens.btn_sel_pen : bar_pen;
    uint32 dark_pen = (s_dark_pens.inp_bg_pen >= 0) ? (uint32)s_dark_pens.inp_bg_pen : 1;
    uint32 text_pen = active ? ((s_dark_pens.txt_pen >= 0) ? (uint32)s_dark_pens.txt_pen : 2)
                             : ((s_dark_pens.btn_sel_pen >= 0) ? (uint32)s_dark_pens.btn_sel_pen : 1);

    /* 1. Fill entire top border / title bar */
    IGraphics->SetAPen(rp, bar_pen);
    IGraphics->RectFill(rp, 0, 0, win_w - 1, top_h - 1);

    /* 2. Top and bottom accent lines */
    IGraphics->SetAPen(rp, edge_pen);
    IGraphics->Move(rp, 0, 0);
    IGraphics->Draw(rp, win_w - 1, 0);

    IGraphics->SetAPen(rp, dark_pen);
    IGraphics->Move(rp, 0, top_h - 1);
    IGraphics->Draw(rp, win_w - 1, top_h - 1);

    /* Also darken the left, right, and bottom window borders if present */
    if (win->BorderLeft > 0) {
        IGraphics->SetAPen(rp, bar_pen);
        IGraphics->RectFill(rp, 0, top_h, win->BorderLeft - 1, win_h - 1);
    }
    if (win->BorderRight > 0) {
        IGraphics->SetAPen(rp, bar_pen);
        IGraphics->RectFill(rp, win_w - win->BorderRight, top_h, win_w - 1, win_h - 1);
    }
    if (win->BorderBottom > 0) {
        IGraphics->SetAPen(rp, bar_pen);
        IGraphics->RectFill(rp, 0, win_h - win->BorderBottom, win_w - 1, win_h - 1);
    }

    /* 3. Close button [X] at the left */
    int btn_h = top_h - 6;
    if (btn_h < 12) btn_h = 12;
    if (btn_h > 18) btn_h = 18;
    int btn_w = btn_h;
    int btn_x = 5;
    int btn_y = (top_h - btn_h) / 2;

    /* Close button box */
    IGraphics->SetAPen(rp, dark_pen);
    IGraphics->RectFill(rp, btn_x, btn_y, btn_x + btn_w - 1, btn_y + btn_h - 1);
    IGraphics->SetAPen(rp, edge_pen);
    IGraphics->Move(rp, btn_x, btn_y);
    IGraphics->Draw(rp, btn_x + btn_w - 1, btn_y);
    IGraphics->Draw(rp, btn_x + btn_w - 1, btn_y + btn_h - 1);
    IGraphics->Draw(rp, btn_x, btn_y + btn_h - 1);
    IGraphics->Draw(rp, btn_x, btn_y);

    /* Close cross 'X' */
    IGraphics->SetAPen(rp, text_pen);
    int pad = 3;
    IGraphics->Move(rp, btn_x + pad, btn_y + pad);
    IGraphics->Draw(rp, btn_x + btn_w - 1 - pad, btn_y + btn_h - 1 - pad);
    IGraphics->Move(rp, btn_x + btn_w - 1 - pad, btn_y + pad);
    IGraphics->Draw(rp, btn_x + pad, btn_y + btn_h - 1 - pad);

    /* 4. Right side gadgets: Depth, Iconify (aligned to real Intuition titlebar gadgets when present) */
    int depth_x = win_w - 5 - btn_w;
    int depth_w = btn_w;
    int iconify_x = depth_x - (btn_w + 4);
    int iconify_w = btn_w;
    int ry = btn_y;
    for (struct Gadget *g = win->FirstGadget; g != NULL; g = g->NextGadget) {
        int gx = g->LeftEdge;
        if (g->Flags & GFLG_RELRIGHT) {
            gx += win_w;
        }
        int gw = g->Width;
        if (gw >= 8 && gw <= 48 && gx > win_w / 2 && gx + gw <= win_w) {
            if ((g->GadgetType & GTYP_SYSTYPEMASK) == GTYP_WDEPTH) {
                depth_x = gx;
                depth_w = gw;
            } else if (g->GadgetID == 0xFFFE) {
                iconify_x = gx;
                iconify_w = gw;
            }
        }
    }

    /* Depth gadget */
    IGraphics->SetAPen(rp, dark_pen);
    IGraphics->RectFill(rp, depth_x, ry, depth_x + depth_w - 1, ry + btn_h - 1);
    IGraphics->SetAPen(rp, edge_pen);
    IGraphics->Move(rp, depth_x, ry);
    IGraphics->Draw(rp, depth_x + depth_w - 1, ry);
    IGraphics->Draw(rp, depth_x + depth_w - 1, ry + btn_h - 1);
    IGraphics->Draw(rp, depth_x, ry + btn_h - 1);
    IGraphics->Draw(rp, depth_x, ry);
    IGraphics->SetAPen(rp, text_pen);
    IGraphics->Move(rp, depth_x + 3, ry + 3);
    IGraphics->Draw(rp, depth_x + depth_w - 4, ry + 3);
    IGraphics->Draw(rp, depth_x + depth_w - 4, ry + btn_h - 4);
    IGraphics->Draw(rp, depth_x + 3, ry + btn_h - 4);
    IGraphics->Draw(rp, depth_x + 3, ry + 3);

    /* Iconify gadget [_] */
    int rx = iconify_x;
    IGraphics->SetAPen(rp, dark_pen);
    IGraphics->RectFill(rp, rx, ry, rx + iconify_w - 1, ry + btn_h - 1);
    IGraphics->SetAPen(rp, edge_pen);
    IGraphics->Move(rp, rx, ry);
    IGraphics->Draw(rp, rx + iconify_w - 1, ry);
    IGraphics->Draw(rp, rx + iconify_w - 1, ry + btn_h - 1);
    IGraphics->Draw(rp, rx, ry + btn_h - 1);
    IGraphics->Draw(rp, rx, ry);
    IGraphics->SetAPen(rp, text_pen);
    IGraphics->Move(rp, rx + 3, ry + btn_h - 4);
    IGraphics->Draw(rp, rx + iconify_w - 4, ry + btn_h - 4);

    /* 5. Window Title Text */
    int title_x = btn_x + btn_w + 8;
    int title_max_x = rx - 8;
    int avail_txt_w = title_max_x - title_x;

    if (avail_txt_w > 20 && app->win_title[0] != '\0') {
        struct TextFont *tf = rp->Font;
        int font_h = tf ? tf->tf_YSize : 12;
        int base_y = tf ? tf->tf_Baseline : (font_h - 2);
        int txt_y = (top_h - font_h) / 2 + base_y;

        char title_buf[512];
        snprintf(title_buf, sizeof(title_buf), "%s", app->win_title);

        if (IGraphics->TextLength(rp, title_buf, strlen(title_buf)) > avail_txt_w) {
            size_t tlen = strlen(title_buf);
            while (tlen > 4 && IGraphics->TextLength(rp, title_buf, tlen) > avail_txt_w - 16) {
                tlen--;
            }
            title_buf[tlen] = '\0';
            strcat(title_buf, "...");
        }

        IGraphics->SetDrMd(rp, JAM1);
        IGraphics->SetAPen(rp, text_pen);
        IGraphics->Move(rp, title_x, txt_y);
        IGraphics->Text(rp, title_buf, strlen(title_buf));
    }

    s_in_draw_titlebar = FALSE;
}

static uint32 post_refresh_hook_func(struct Hook *hook, APTR win_obj, struct Gadget *gadget)
{
    (void)win_obj;
    (void)gadget;
    VortexApp *app = (VortexApp *)hook->h_Data;
    if (app && app->prefs.dark_theme == 1) {
        gui_draw_dark_titlebar(app);
    }
    return 0;
}

static void gui_alloc_dark_pens(struct Screen *scr)
{
    if (s_dark_pens.allocated || !scr) return;

    memset(&s_dark_pens, 0, sizeof(s_dark_pens));
    s_dark_pens.bg_pen         = -1;
    s_dark_pens.btn_bg_pen     = -1;
    s_dark_pens.btn_sel_pen    = -1;
    s_dark_pens.txt_pen        = -1;
    s_dark_pens.inp_bg_pen     = -1;
    s_dark_pens.gauge_fill_pen = -1;
    s_dark_pens.raw_bg         = -1;
    s_dark_pens.raw_btn_bg     = -1;
    s_dark_pens.raw_btn_sel    = -1;
    s_dark_pens.raw_txt        = -1;
    s_dark_pens.raw_inp_bg     = -1;
    s_dark_pens.raw_gauge_fill = -1;

    struct ColorMap *cm = scr->ViewPort.ColorMap;
    s_dark_pens.cm = cm;

    if (cm && IGraphics) {
        s_dark_pens.raw_bg = IGraphics->ObtainBestPen(cm,
            0x22222222UL, 0x24242424UL, 0x28282828UL,
            OBP_Precision, PRECISION_GUI,
            TAG_DONE);

        s_dark_pens.raw_btn_bg = IGraphics->ObtainBestPen(cm,
            0x34343434UL, 0x38383838UL, 0x3F3F3F3FUL,
            OBP_Precision, PRECISION_GUI,
            TAG_DONE);

        s_dark_pens.raw_btn_sel = IGraphics->ObtainBestPen(cm,
            0x4A4A4A4AUL, 0x50505050UL, 0x5B5B5B5BUL,
            OBP_Precision, PRECISION_GUI,
            TAG_DONE);

        s_dark_pens.raw_txt = IGraphics->ObtainBestPen(cm,
            0xE4E4E4E4UL, 0xE6E6E6E6UL, 0xEBEBEBEBUL,
            OBP_Precision, PRECISION_GUI,
            TAG_DONE);

        s_dark_pens.raw_inp_bg = IGraphics->ObtainBestPen(cm,
            0x18181818UL, 0x19191919UL, 0x1C1C1C1CUL,
            OBP_Precision, PRECISION_GUI,
            TAG_DONE);

        s_dark_pens.raw_gauge_fill = IGraphics->ObtainBestPen(cm,
            0x3A3A3A3AUL, 0x44444444UL, 0x54545454UL,
            OBP_Precision, PRECISION_IMAGE,
            TAG_DONE);
    }

    s_dark_pens.bg_pen         = (s_dark_pens.raw_bg >= 0)         ? s_dark_pens.raw_bg         : 1;
    s_dark_pens.btn_bg_pen     = (s_dark_pens.raw_btn_bg >= 0)     ? s_dark_pens.raw_btn_bg     : 1;
    s_dark_pens.btn_sel_pen    = (s_dark_pens.raw_btn_sel >= 0)    ? s_dark_pens.raw_btn_sel    : 2;
    s_dark_pens.txt_pen        = (s_dark_pens.raw_txt >= 0)        ? s_dark_pens.raw_txt        : 2;
    s_dark_pens.inp_bg_pen     = (s_dark_pens.raw_inp_bg >= 0)     ? s_dark_pens.raw_inp_bg     : 1;
    s_dark_pens.gauge_fill_pen = (s_dark_pens.raw_gauge_fill >= 0) ? s_dark_pens.raw_gauge_fill : s_dark_pens.btn_sel_pen;

    memset(&s_dark_backfill_hook, 0, sizeof(struct Hook));
    s_dark_backfill_hook.h_Entry = (HOOKFUNC)dark_backfill_hook_func;

    memset(&s_space_dark_render_hook, 0, sizeof(struct Hook));
    s_space_dark_render_hook.h_Entry = (HOOKFUNC)space_dark_render_hook_func;

    memset(&s_dark_winbar_active_hook, 0, sizeof(struct Hook));
    s_dark_winbar_active_hook.h_Entry = (HOOKFUNC)dark_winbar_active_hook_func;

    memset(&s_dark_winbar_inact_hook, 0, sizeof(struct Hook));
    s_dark_winbar_inact_hook.h_Entry = (HOOKFUNC)dark_winbar_inact_hook_func;

    memset(&s_dark_screenbar_hook, 0, sizeof(struct Hook));
    s_dark_screenbar_hook.h_Entry = (HOOKFUNC)dark_screenbar_hook_func;

    s_dark_pens.allocated = TRUE;
}

static void gui_free_dark_pens(void)
{
    if (!s_dark_pens.allocated) return;

    if (s_dark_pens.cm && IGraphics) {
        if (s_dark_pens.raw_bg >= 0)         IGraphics->ReleasePen(s_dark_pens.cm, s_dark_pens.raw_bg);
        if (s_dark_pens.raw_btn_bg >= 0)     IGraphics->ReleasePen(s_dark_pens.cm, s_dark_pens.raw_btn_bg);
        if (s_dark_pens.raw_btn_sel >= 0)    IGraphics->ReleasePen(s_dark_pens.cm, s_dark_pens.raw_btn_sel);
        if (s_dark_pens.raw_txt >= 0)        IGraphics->ReleasePen(s_dark_pens.cm, s_dark_pens.raw_txt);
        if (s_dark_pens.raw_inp_bg >= 0)     IGraphics->ReleasePen(s_dark_pens.cm, s_dark_pens.raw_inp_bg);
        if (s_dark_pens.raw_gauge_fill >= 0) IGraphics->ReleasePen(s_dark_pens.cm, s_dark_pens.raw_gauge_fill);
    }

    s_dark_pens.allocated = FALSE;
    s_dark_pens.cm = NULL;
    s_dark_pens.bg_pen         = -1;
    s_dark_pens.btn_bg_pen     = -1;
    s_dark_pens.btn_sel_pen    = -1;
    s_dark_pens.txt_pen        = -1;
    s_dark_pens.inp_bg_pen     = -1;
    s_dark_pens.gauge_fill_pen = -1;
    s_dark_pens.raw_bg         = -1;
    s_dark_pens.raw_btn_bg     = -1;
    s_dark_pens.raw_btn_sel    = -1;
    s_dark_pens.raw_txt        = -1;
    s_dark_pens.raw_inp_bg     = -1;
    s_dark_pens.raw_gauge_fill = -1;
}

static Object *create_toolbar_button(uint32 gid, const char *const candidates[], int num_candidates,
                                     const char *fallback_text, const char *hint, struct Screen *scr)
{
    struct Image *img = load_aiss_image(candidates, num_candidates, scr);
    if (img) {
        return IIntuition->NewObject(NULL, "button.gadget",
            GA_ID,        gid,
            GA_RelVerify, TRUE,
            GA_Image,     (Object *)img,
            GA_HintInfo,  hint,
            s_dark_pens.allocated ? BUTTON_BackgroundPen : TAG_IGNORE, (uint32)s_dark_pens.btn_bg_pen,
            s_dark_pens.allocated ? BUTTON_TextPen : TAG_IGNORE,       (uint32)s_dark_pens.txt_pen,
            s_dark_pens.allocated ? BUTTON_FillPen : TAG_IGNORE,       (uint32)s_dark_pens.btn_sel_pen,
            s_dark_pens.allocated ? BUTTON_FillTextPen : TAG_IGNORE,   (uint32)s_dark_pens.txt_pen,
            TAG_DONE);
    } else {
        return IIntuition->NewObject(NULL, "button.gadget",
            GA_ID,        gid,
            GA_RelVerify, TRUE,
            GA_Text,      fallback_text,
            GA_HintInfo,  hint,
            s_dark_pens.allocated ? BUTTON_BackgroundPen : TAG_IGNORE, (uint32)s_dark_pens.btn_bg_pen,
            s_dark_pens.allocated ? BUTTON_TextPen : TAG_IGNORE,       (uint32)s_dark_pens.txt_pen,
            s_dark_pens.allocated ? BUTTON_FillPen : TAG_IGNORE,       (uint32)s_dark_pens.btn_sel_pen,
            s_dark_pens.allocated ? BUTTON_FillTextPen : TAG_IGNORE,   (uint32)s_dark_pens.txt_pen,
            TAG_DONE);
    }
}

static struct Image *load_quicklink_image(const char *icon_name, const char *url, struct Screen *screen)
{
    if (!screen) return NULL;

    char domain_cand[64];
    domain_cand[0] = '\0';
    if (url && url[0] != '\0') {
        const char *p = url;
        if (strncmp(p, "http://", 7) == 0) p += 7;
        else if (strncmp(p, "https://", 8) == 0) p += 8;
        if (strncmp(p, "www.", 4) == 0) p += 4;
        else if (strncmp(p, "m.", 2) == 0) p += 2;
        else if (strncmp(p, "de.", 3) == 0) p += 3;
        size_t di = 0;
        while (*p && *p != '.' && *p != '/' && *p != ':' && di < sizeof(domain_cand) - 1) {
            domain_cand[di++] = *p++;
        }
        domain_cand[di] = '\0';
    }

    const char *candidates[12];
    int num_c = 0;
    if (icon_name && icon_name[0] != '\0') candidates[num_c++] = icon_name;
    if (domain_cand[0] != '\0')            candidates[num_c++] = domain_cand;
    candidates[num_c++] = "bookmark";
    candidates[num_c++] = "favorite";
    candidates[num_c++] = "globe";
    candidates[num_c++] = "drawer";
    candidates[num_c++] = "link";

    static int s_has_progdir_icons = -1;
    if (s_has_progdir_icons == -1 && IDOS) {
        BPTR dirlk = IDOS->Lock("PROGDIR:icons", SHARED_LOCK);
        if (dirlk != ZERO) {
            s_has_progdir_icons = 1;
            IDOS->UnLock(dirlk);
        } else {
            s_has_progdir_icons = 0;
        }
    }

    const char *dir_prefixes[2];
    int num_prefixes = 0;
    if (s_has_progdir_icons == 1) {
        dir_prefixes[num_prefixes++] = "PROGDIR:icons/";
    }
    dir_prefixes[num_prefixes++] = "TBIMAGES:";

    APTR prev_win = IDOS ? IDOS->SetProcWindow((APTR)-1) : NULL;

    for (int i = 0; i < num_c; i++) {
        const char *cand = candidates[i];
        if (!cand || cand[0] == '\0') continue;

        char path[160];
        BPTR lk = ZERO;
        for (int d = 0; d < num_prefixes; d++) {
            snprintf(path, sizeof(path), "%s%s.png", dir_prefixes[d], cand);
            lk = IDOS->Lock(path, SHARED_LOCK);
            if (lk != ZERO) break;
            snprintf(path, sizeof(path), "%s%s", dir_prefixes[d], cand);
            lk = IDOS->Lock(path, SHARED_LOCK);
            if (lk != ZERO) break;
        }

        if (lk == ZERO) {
            continue;
        }
        IDOS->UnLock(lk);
        if (IDOS) IDOS->SetProcWindow(prev_win);

        char *p_copy = strdup(path);
        if (s_num_aiss_paths < (MAX_AISS_IMAGES * 3) - 1 && p_copy) {
            s_aiss_paths[s_num_aiss_paths++] = p_copy;
        }

        struct Image *img = (struct Image *)IIntuition->NewObject(NULL, "bitmap.image",
            BITMAP_SourceFile, (Tag)p_copy,
            BITMAP_Screen,     (Tag)screen,
            BITMAP_Masking,    TRUE,
            TAG_DONE);

        if (img) {
            if (s_num_aiss_images < MAX_AISS_IMAGES) {
                s_aiss_images[s_num_aiss_images++] = (Object *)img;
            }
            return img;
        }
        prev_win = IDOS ? IDOS->SetProcWindow((APTR)-1) : NULL;
    }
    if (IDOS) IDOS->SetProcWindow(prev_win);
    return NULL;
}

static struct Image *load_favicon_image(const char *url, struct Screen *screen)
{
    return load_quicklink_image(NULL, url, screen);
}

static char s_last_favicon_url[VB_MAX_URL_LEN] = { 0 };

static void gui_update_url_favicon(VortexApp *app, const char *url)
{
    if (!app || !app->gad_url_favicon || !app->win) return;

    if (url && s_last_favicon_url[0] && strcmp(url, s_last_favicon_url) == 0) {
        return;
    }

    struct Screen *scr = (app->win && app->win->WScreen) ? app->win->WScreen : app->custom_screen;
    struct Image *img = load_favicon_image(url, scr);
    if (img) {
        if (url) {
            strncpy(s_last_favicon_url, url, sizeof(s_last_favicon_url) - 1);
            s_last_favicon_url[sizeof(s_last_favicon_url) - 1] = '\0';
        }
        IIntuition->SetGadgetAttrs((struct Gadget *)app->gad_url_favicon, app->win, NULL,
                                   GA_Image, (Object *)img,
                                   TAG_DONE);
        IIntuition->RefreshGList((struct Gadget *)app->gad_url_favicon, app->win, NULL, 1);
    }
}

static char s_quick_btn_labels[VB_MAX_QUICKLINKS][80];
static char s_quick_btn_hints[VB_MAX_QUICKLINKS][256];

static Object *create_quicklink_button(uint32 gid, const char *label, const char *url, const char *icon_name, struct Screen *scr)
{
    (void)icon_name;
    (void)scr;
    int idx = (int)(gid - GID_QUICK_BASE);
    if (idx < 0 || idx >= VB_MAX_QUICKLINKS) idx = 0;

    /* Clean horizontal button padding for text */
    snprintf(s_quick_btn_labels[idx], sizeof(s_quick_btn_labels[idx]), "  %s  ", label ? label : "");
    snprintf(s_quick_btn_hints[idx], sizeof(s_quick_btn_hints[idx]), "%s", url ? url : "");

    return IIntuition->NewObject(NULL, "button.gadget",
        GA_ID,                 gid,
        GA_RelVerify,          TRUE,
        GA_Text,               (Tag)s_quick_btn_labels[idx],
        BUTTON_DomainString,   (Tag)s_quick_btn_labels[idx],
        BUTTON_BevelStyle,     BVS_THIN,
        BUTTON_Justification,  BCJ_CENTER,
        BUTTON_HorizAlignment, BALIGN_CENTER,
        BUTTON_VertAlignment,  BALIGN_CENTER,
        GA_HintInfo,           (Tag)s_quick_btn_hints[idx],
        s_dark_pens.allocated ? BUTTON_BackgroundPen : TAG_IGNORE, (uint32)s_dark_pens.btn_bg_pen,
        s_dark_pens.allocated ? BUTTON_TextPen : TAG_IGNORE,       (uint32)s_dark_pens.txt_pen,
        s_dark_pens.allocated ? BUTTON_FillPen : TAG_IGNORE,       (uint32)s_dark_pens.btn_sel_pen,
        s_dark_pens.allocated ? BUTTON_FillTextPen : TAG_IGNORE,   (uint32)s_dark_pens.txt_pen,
        TAG_DONE);
}

static struct Hook s_render_hook;
static struct Hook s_idcmp_hook;

static uint32 ctx_menu_hook_func(struct Hook *h, Object *item, APTR reserved)
{
    (void)reserved;
    uint32 *out_id = (uint32 *)h->h_Data;
    uint32 id_val  = 0;
    if (out_id && item && IIntuition->GetAttr(PMIA_ID, item, &id_val)) {
        *out_id = id_val;
    }
    return 0;
}

/* Deduplication and state for mouse buttons so IDCMPHook + WMHI_MOUSEBUTTONS don't double-fire */
static uint32 s_last_click_secs    = 0;
static uint32 s_last_click_micros  = 0;
static uint16 s_last_click_btn     = 0;
static BOOL   s_last_click_is_down = FALSE;
static BOOL   s_mouse_lmb_down     = FALSE;
static BOOL   s_got_extended_wheel = FALSE;
static int32  s_wheel_accum_dy     = 0;
static int16  s_wheel_win_x        = 640;
static int16  s_wheel_win_y        = 360;
static BOOL   s_current_rmb_trap   = FALSE;

void gui_set_rmb_trap(VortexApp *app, BOOL trap)
{
    if (!app || !app->win) return;
    if (trap != s_current_rmb_trap) {
        s_current_rmb_trap = trap;
        IIntuition->SetWindowAttrs(app->win,
            WA_RMBTrap, trap,
            TAG_DONE);
        if (trap) {
            app->win->Flags |= WFLG_RMBTRAP;
        } else {
            app->win->Flags &= ~WFLG_RMBTRAP;
        }
    }
}

static void dispatch_viewport_mouse_btn(VortexApp *app, int16_t win_x, int16_t win_y, uint16_t btn, BOOL is_down,
                                        uint32 secs, uint32 micros)
{
    if (!app) {
        return;
    }

    int hit_ql = -1;
    for (int i = 0; i < app->prefs.num_quick_links && i < VB_MAX_QUICKLINKS; i++) {
        struct Gadget *g = (struct Gadget *)app->gad_quick_btn[i];
        if (g && win_x >= g->LeftEdge && win_x < g->LeftEdge + g->Width &&
                 win_y >= g->TopEdge  && win_y < g->TopEdge  + g->Height) {
            hit_ql = i;
            break;
        }
    }

    if (!app->connected) {
        if ((btn == 3 || btn == 2) && is_down) {
            if (hit_ql >= 0) {
                if (btn == 2) {
                    gui_tab_add(app, app->prefs.quick_links[hit_ql].label[0] ? app->prefs.quick_links[hit_ql].label : NULL,
                                (char *)app->prefs.quick_links[hit_ql].url, TRUE);
                } else {
                    open_quicklink_popup(app, hit_ql, win_x, win_y);
                    gui_set_rmb_trap(app, FALSE);
                }
            }
        }
        return;
    }

    int16_t rx = app->last_remote_x, ry = app->last_remote_y;
    if (!renderer_map_window_to_remote(app, win_x, win_y, &rx, &ry)) {
        if (!is_down && (s_mouse_lmb_down || app->mouse_lmb_down) && btn == 1) {
            s_mouse_lmb_down = FALSE;
            app->mouse_lmb_down = FALSE;
            net_send_mouse_button(app, rx, ry, 1, FALSE);
        }
        if ((btn == 3 || btn == 2) && is_down) {
            if (hit_ql >= 0) {
                if (btn == 2) {
                    gui_tab_add(app, app->prefs.quick_links[hit_ql].label[0] ? app->prefs.quick_links[hit_ql].label : NULL,
                                (char *)app->prefs.quick_links[hit_ql].url, TRUE);
                } else {
                    open_quicklink_popup(app, hit_ql, win_x, win_y);
                    gui_set_rmb_trap(app, FALSE);
                }
            }
        }
        return;
    }

    /* Deduplicate if both IDCMPHook and WM_HANDLEINPUT report the same click within 50ms */
    if (secs != 0 || micros != 0) {
        int32 diff_s  = (int32)secs - (int32)s_last_click_secs;
        int32 diff_us = diff_s * 1000000 + ((int32)micros - (int32)s_last_click_micros);
        if (diff_us >= 0 && diff_us < 50000 && is_down == s_last_click_is_down && btn == s_last_click_btn) {
            return;
        }
        s_last_click_secs    = secs;
        s_last_click_micros  = micros;
        s_last_click_btn     = btn;
        s_last_click_is_down = is_down;
    }

    if (btn == 3) {
        if (is_down) {
            gui_set_rmb_trap(app, TRUE);
            memset(&app->ctx_menu, 0, sizeof(MsgContextMenu));
            app->ctx_menu_ready   = FALSE;
            app->ctx_menu_pending = TRUE;
            app_set_status(GetVString(MSG_STATUS_CTX_OPENING), (int)rx, (int)ry);
            net_send_click(app, rx, ry, 3);
        }
        return;
    }

    if (btn == 1) {
        s_mouse_lmb_down = is_down;
        app->mouse_lmb_down = is_down;
        app->last_remote_x = rx;
        app->last_remote_y = ry;
        if (app->win) {
            IIntuition->ReportMouse(TRUE, app->win);
        }
    }

    net_send_mouse_button(app, rx, ry, btn, is_down);
}

/* SPACE_RenderHook callback: redraws the remote browser frame on window resize/refresh */
static uint32 space_render_func(struct Hook *hook, Object *space_obj, struct gpRender *gpr)
{
    VortexApp *app = (VortexApp *)hook->h_Data;
    if (!app || !gpr || !gpr->gpr_RPort) {
        return 0;
    }

    struct IBox *area = NULL;
    struct IBox fallback_area;
    IIntuition->GetAttr(SPACE_AreaBox, space_obj, (uint32 *)&area);
    if ((!area || area->Width <= 2 || area->Height <= 2) && app->win) {
        fallback_area.Left   = app->win->BorderLeft;
        fallback_area.Top    = app->win->BorderTop + 56;
        fallback_area.Width  = app->win->Width - app->win->BorderLeft - app->win->BorderRight;
        fallback_area.Height = app->win->Height - app->win->BorderTop - app->win->BorderBottom - 60;
        if (app->prefs.show_status_bar) fallback_area.Height -= 24;
        if (fallback_area.Width > 2 && fallback_area.Height > 2) {
            area = &fallback_area;
        }
    }
    if (area && area->Width > 2 && area->Height > 2) {
        if (app->renderer.has_frame && (app->renderer.src_bm[0] || app->renderer.decoded_va_surface)) {
            /* NEVER call LockLayer / vaPutSurface / DoHookClipRects / AllocBitMap inside GM_RENDER
             * (SPACE_RenderHook): Intuition already holds ObtainGIRPort/Layer locks during window
             * resizing, so locking layers here deadlocks Workbench. Defer redraw to main loop. */
            app->renderer.dirty    = TRUE;
            app->renderer.dirty_x1 = 0;
            app->renderer.dirty_y1 = 0;
            app->renderer.dirty_x2 = 32767;
            app->renderer.dirty_y2 = 32767;
        } else {
            uint32 pen = (app->prefs.dark_theme == 1) ? ((s_dark_pens.allocated && s_dark_pens.bg_pen >= 0) ? (uint32)s_dark_pens.bg_pen : 1) : 0;
            IGraphics->SetAPen(gpr->gpr_RPort, pen);
            IGraphics->RectFill(gpr->gpr_RPort, area->Left, area->Top, area->Left + area->Width - 1, area->Top + area->Height - 1);
        }
    }
    return 0;
}

/* WINDOW_IDCMPHook callback: captures mouse buttons, mouse wheel, and window geometry inside viewport */
static uint32 idcmp_hook_func(struct Hook *hook, Object *win_obj, struct IntuiMessage *imsg)
{
    (void)win_obj;
    VortexApp *app = (VortexApp *)hook->h_Data;
    if (!app || !imsg) {
        return 0;
    }

    if (imsg->Class == IDCMP_CHANGEWINDOW || imsg->Class == IDCMP_ACTIVEWINDOW || imsg->Class == IDCMP_INACTIVEWINDOW) {
        if (imsg->Class == IDCMP_CHANGEWINDOW) {
            prefs_capture_window_box(app);
        }
        if (app->prefs.dark_theme == 1) {
            gui_draw_dark_titlebar(app);
        }
        return 0;
    }

    if (!app->connected && imsg->Class != IDCMP_MOUSEBUTTONS) {
        return 0;
    }

    switch (imsg->Class) {
        case IDCMP_MOUSEBUTTONS: {
            if (imsg->Code == SELECTDOWN) {
                dispatch_viewport_mouse_btn(app, imsg->MouseX, imsg->MouseY, 1, TRUE, imsg->Seconds, imsg->Micros);
            } else if (imsg->Code == SELECTUP) {
                dispatch_viewport_mouse_btn(app, imsg->MouseX, imsg->MouseY, 1, FALSE, imsg->Seconds, imsg->Micros);
            } else if (imsg->Code == MIDDLEDOWN) {
                dispatch_viewport_mouse_btn(app, imsg->MouseX, imsg->MouseY, 2, TRUE, imsg->Seconds, imsg->Micros);
            } else if (imsg->Code == MIDDLEUP) {
                dispatch_viewport_mouse_btn(app, imsg->MouseX, imsg->MouseY, 2, FALSE, imsg->Seconds, imsg->Micros);
            } else if (imsg->Code == MENUDOWN) {
                dispatch_viewport_mouse_btn(app, imsg->MouseX, imsg->MouseY, 3, TRUE, imsg->Seconds, imsg->Micros);
            }
            break;
        }

        case IDCMP_EXTENDEDMOUSE: {
            /* Mouse wheel handling:
             * Convert IntuiWheelData->WheelY (<0 = RAWKEY_NM_WHEEL_UP, >0 = RAWKEY_NM_WHEEL_DOWN)
             * into 3 lines * Scrollbar::pixelsPerLineStep() (3 * 40 = 120px) when inside browser view. */
            if (imsg->Code & IMSGCODE_INTUIWHEELDATA) {
                struct IntuiWheelData *wd = (struct IntuiWheelData *)imsg->IAddress;
                int16_t rx = 0, ry = 0;
                if (wd && wd->WheelY != 0 &&
                    renderer_map_window_to_remote(app, imsg->MouseX, imsg->MouseY, &rx, &ry)) {
                    s_got_extended_wheel = TRUE;
                    BOOL shift_held = (imsg->Qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0;
                    int32 base_step = shift_held ? 360 : 120;
                    int32 step = (wd->WheelY < 0) ? -base_step : base_step;

                    /* If user reversed wheel direction, reset accumulator immediately */
                    if ((step > 0 && s_wheel_accum_dy < 0) || (step < 0 && s_wheel_accum_dy > 0)) {
                        s_wheel_accum_dy = 0;
                    }
                    s_wheel_accum_dy += step;
                    int32 max_accum = shift_held ? 720 : 480;
                    if (s_wheel_accum_dy >  max_accum) s_wheel_accum_dy =  max_accum;
                    if (s_wheel_accum_dy < -max_accum) s_wheel_accum_dy = -max_accum;
                    s_wheel_win_x = imsg->MouseX;
                    s_wheel_win_y = imsg->MouseY;
                }
            }
            break;
        }

        case IDCMP_MOUSEMOVE: {
            app->pending_win_mouse_x = imsg->MouseX;
            app->pending_win_mouse_y = imsg->MouseY;
            app->pending_mouse_move  = TRUE;
            if (app->mouse_sig_mask && app->main_task) {
                IExec->Signal(app->main_task, app->mouse_sig_mask);
            }

            int16_t rx = 0, ry = 0;
            BOOL in_viewport = renderer_map_window_to_remote(app, imsg->MouseX, imsg->MouseY, &rx, &ry);
            BOOL in_quicklink = FALSE;
            for (int i = 0; i < app->prefs.num_quick_links && i < VB_MAX_QUICKLINKS; i++) {
                struct Gadget *g = (struct Gadget *)app->gad_quick_btn[i];
                if (g && imsg->MouseX >= g->LeftEdge && imsg->MouseX < g->LeftEdge + g->Width &&
                         imsg->MouseY >= g->TopEdge  && imsg->MouseY < g->TopEdge  + g->Height) {
                    in_quicklink = TRUE;
                    break;
                }
            }
            gui_set_rmb_trap(app, in_viewport || in_quicklink);
            break;
        }

        default:
            break;
    }

    return 0;
}

static void get_string_gadget_text(Object *gad_obj, char *out_buf, size_t out_cap)
{
    if (!out_buf || out_cap == 0) {
        return;
    }
    out_buf[0] = '\0';
    if (!gad_obj) {
        return;
    }

    const char *src = NULL;

    /* 1. Read live Intuition StringInfo->Buffer first so clicking 'Los' without pressing
     *    Return inside string.gadget still reads the newly typed text immediately! */
    struct Gadget *gad = (struct Gadget *)gad_obj;
    if (gad->SpecialInfo) {
        struct StringInfo *si = (struct StringInfo *)gad->SpecialInfo;
        if (si->Buffer && si->Buffer[0] != '\0') {
            src = (const char *)si->Buffer;
        }
    }

    /* 2. Fallback to BOOPSI STRINGA_TextVal */
    if (!src || src[0] == '\0') {
        STRPTR str_val = NULL;
        IIntuition->GetAttr(STRINGA_TextVal, gad_obj, (uint32 *)&str_val);
        if (str_val && str_val[0] != '\0') {
            src = (const char *)str_val;
        }
    }

    if (!src) {
        return;
    }

    while (*src == ' ' || *src == '\t' || *src == '\r' || *src == '\n') {
        src++;
    }
    strncpy(out_buf, src, out_cap - 1);
    out_buf[out_cap - 1] = '\0';

    size_t len = strlen(out_buf);
    while (len > 0 && (out_buf[len - 1] == ' ' || out_buf[len - 1] == '\t' ||
                       out_buf[len - 1] == '\r' || out_buf[len - 1] == '\n')) {
        out_buf[--len] = '\0';
    }
}

static BOOL is_url_or_command(const char *str)
{
    if (!str || !*str) return FALSE;
    while (*str == ' ' || *str == '\t') str++;
    if (*str == '\0') return FALSE;

    /* Vortex Browser internal commands start with @ or / */
    if (str[0] == '@' || str[0] == '/') return TRUE;

    /* Protocol schemes */
    if (strncmp(str, "http://", 7) == 0 ||
        strncmp(str, "https://", 8) == 0 ||
        strncmp(str, "ftp://", 6) == 0 ||
        strncmp(str, "file://", 7) == 0 ||
        strncmp(str, "about:", 6) == 0) {
        return TRUE;
    }

    /* If it contains spaces, treat as search query */
    if (strchr(str, ' ')) return FALSE;

    /* If it contains a dot and no spaces (e.g. google.de, aminet.net, os4depot.net), treat as URL */
    if (strchr(str, '.')) return TRUE;

    return FALSE;
}

static void build_search_url(VortexApp *app, const char *query, char *out_url, size_t out_cap)
{
    if (!out_url || out_cap == 0) return;
    out_url[0] = '\0';
    if (!query) return;

    char enc_buf[512];
    size_t ei = 0;
    for (size_t si = 0; query[si] != '\0' && ei + 4 < sizeof(enc_buf); si++) {
        unsigned char c = (unsigned char)query[si];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            enc_buf[ei++] = (char)c;
        } else if (c == ' ') {
            enc_buf[ei++] = '+';
        } else {
            snprintf(&enc_buf[ei], 4, "%%%02X", (unsigned)c);
            ei += 3;
        }
    }
    enc_buf[ei] = '\0';

    uint8_t se = (app && app->prefs.search_engine < NUM_SEARCH_ENGINES) ? app->prefs.search_engine : 0;
    snprintf(out_url, out_cap, g_search_engines[se].query_url_fmt, enc_buf);
}

static void parse_host_port(const char *input, char *out_host, size_t host_cap, uint16_t *out_port)
{
    if (!input || input[0] == '\0') {
        return;
    }
    const char *colon = strchr(input, ':');
    if (colon) {
        size_t hlen = (size_t)(colon - input);
        if (hlen >= host_cap) hlen = host_cap - 1;
        memcpy(out_host, input, hlen);
        out_host[hlen] = '\0';
        int p = atoi(colon + 1);
        if (p > 0 && p <= 65535) {
            *out_port = (uint16_t)p;
        }
    } else {
        snprintf(out_host, host_cap, "%s", input);
    }
}

void prompt_server_address(VortexApp *app)
{
    if (!app) return;

    char current_addr[160];
    const char *cur_h = app->server_host[0] ? app->server_host :
                       (app->prefs.server_host[0] ? app->prefs.server_host : "");
    uint16_t cur_p    = app->server_port ? app->server_port :
                       (app->prefs.server_port ? app->prefs.server_port : DEFAULT_SERVER_PORT);
    if (cur_h[0] != '\0') {
        snprintf(current_addr, sizeof(current_addr), "%s:%u", cur_h, (unsigned)cur_p);
    } else {
        snprintf(current_addr, sizeof(current_addr), "127.0.0.1:%u", (unsigned)cur_p);
    }

    Object *gad_addr = IIntuition->NewObject(NULL, "string.gadget",
        GA_ID,            1,
        GA_RelVerify,     TRUE,
        GA_TabCycle,      FALSE,
        GA_HintInfo,      GetVString(MSG_SRV_DLG_HINT),
        STRINGA_MaxChars, 128,
        STRINGA_TextVal,  current_addr,
        TAG_DONE);

    Object *btn_ok = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID,        2,
        GA_RelVerify, TRUE,
        GA_Text,      GetVString(MSG_SRV_DLG_CONNECT),
        TAG_DONE);

    Object *btn_cancel = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID,        3,
        GA_RelVerify, TRUE,
        GA_Text,      GetVString(MSG_GEN_CANCEL),
        TAG_DONE);

    Object *req_layout = IIntuition->NewObject(NULL, "layout.gadget",
        LAYOUT_Orientation,   LAYOUT_ORIENT_VERT,
        LAYOUT_SpaceOuter,    TRUE,
        LAYOUT_DeferLayout,   TRUE,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                  GA_Text,              (Tag)GetVString(MSG_SRV_DLG_PROMPT),
                                  GA_ReadOnly,          TRUE,
                                  BUTTON_Justification, BCJ_LEFT,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      gad_addr,
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "layout.gadget",
                                  LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
                                  LAYOUT_AddChild,      btn_ok,
                                  CHILD_WeightedWidth,  50,
                                  LAYOUT_AddChild,      btn_cancel,
                                  CHILD_WeightedWidth,  50,
                                  TAG_DONE),
        CHILD_WeightedHeight, 0,
        TAG_DONE);

    Object *req_win_obj = IIntuition->NewObject(NULL, "window.class",
        WA_Title,             GetVString(MSG_SRV_DLG_TITLE),
        WA_ScreenTitle,       GetVString(MSG_SRV_DLG_SCRTITLE),
        WA_CloseGadget,       TRUE,
        WA_DepthGadget,       TRUE,
        WA_DragBar,           TRUE,
        WA_Activate,          TRUE,
        WA_SmartRefresh,      TRUE,
        WA_InnerWidth,        380,
        WA_InnerHeight,       100,
        WA_IDCMP,             IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_VANILLAKEY,
        WINDOW_Position,      WPOS_CENTERSCREEN,
        WINDOW_ParentGroup,   req_layout,
        TAG_DONE);

    if (!req_win_obj) {
        return;
    }

    struct Window *req_win = (struct Window *)IIntuition->IDoMethod(req_win_obj, WM_OPEN);
    if (!req_win) {
        IIntuition->DisposeObject(req_win_obj);
        return;
    }

    BOOL done = FALSE;
    BOOL apply = FALSE;

    while (!done) {
        uint32 sigmask = 0;
        IIntuition->GetAttr(WINDOW_SigMask, req_win_obj, &sigmask);
        uint32 got_sigs = IExec->Wait(sigmask | SIGBREAKF_CTRL_C);
        if (got_sigs & SIGBREAKF_CTRL_C) {
            done = TRUE;
            app->running = FALSE;
            break;
        }
        uint32 res;
        int16 code = 0;
        while ((res = IIntuition->IDoMethod(req_win_obj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
            switch (res & WMHI_CLASSMASK) {
                case WMHI_CLOSEWINDOW:
                    done = TRUE;
                    break;

                case WMHI_VANILLAKEY: {
                    uint16 key = (uint16)(res & WMHI_KEYMASK);
                    if (key == 0x1B) { /* ESC */
                        done = TRUE;
                    } else if (key == 0x0D || key == 0x0A) { /* Enter */
                        apply = TRUE;
                        done = TRUE;
                    }
                    break;
                }

                case WMHI_GADGETUP: {
                    uint32 gid = res & WMHI_GADGETMASK;
                    if (gid == 1 || gid == 2) {
                        apply = TRUE;
                        done = TRUE;
                    } else if (gid == 3) {
                        done = TRUE;
                    }
                    break;
                }

                default:
                    break;
            }
        }
    }

    if (apply) {
        char buf[160];
        get_string_gadget_text(gad_addr, buf, sizeof(buf));
        if (buf[0] != '\0') {
            parse_host_port(buf, app->server_host, sizeof(app->server_host), &app->server_port);
            strncpy(app->prefs.server_host, app->server_host, sizeof(app->prefs.server_host) - 1);
            app->prefs.server_host[sizeof(app->prefs.server_host) - 1] = '\0';
            app->prefs.server_port = app->server_port;
            prefs_save(app, TRUE);
            if (net_connect(app, app->server_host, app->server_port)) {
                net_sync_tabs(app);
            }
        }
    }

    IIntuition->DisposeObject(req_win_obj);
}

static void handle_menu_command(VortexApp *app, uint32 item_id)
{
    if (!app) return;

    switch (item_id) {
        case MID_PROJ_NEW:
        case MID_NAV_HOME:
            if (app->active_tab_idx < VB_MAX_TABS) {
                app->restore_tab_mask &= ~(1UL << app->active_tab_idx);
            }
            gui_update_url_bar(app, "");
            net_send_url(app, "@HOME");
            break;

        case MID_PROJ_OPEN:
            if (app->gad_url && app->win) {
                IIntuition->ActivateGadget((struct Gadget *)app->gad_url, app->win, NULL);
            }
            break;

        case MID_SET_SERVER_CUSTOM:
            prompt_server_address(app);
            break;

        case MID_PROJ_CONNECT:
            if (app->server_host[0] == '\0') {
                prompt_server_address(app);
            } else {
                if (net_connect(app, app->server_host, app->server_port)) {
                    net_sync_tabs(app);
                }
            }
            break;

        case MID_PROJ_DISCONNECT:
            net_disconnect(app);
            app_set_status(GetVString(MSG_STATUS_DISCONNECTED));
            break;

        case MID_SETTINGS:
            gui_prefs_open(app);
            break;

        case MID_TOGGLE_SCREEN:
            app->prefs.resolution.use_custom_screen = !app->prefs.resolution.use_custom_screen;
            prefs_save(app, TRUE);
            app->reopen_screen_needed = TRUE;
            break;

        case MID_STREAM_RES_NATIVE:
            app->prefs.stream_res_mode = STREAM_RES_NATIVE;
            prefs_save(app, TRUE);
            app->renderer.reported_w = 0;
            app->renderer.reported_h = 0;
            renderer_sync_viewport_size(app);
            app_set_status(GetVString(MSG_STATUS_STREAM_RES_NATIVE));
            break;

        case MID_STREAM_RES_720P:
            app->prefs.stream_res_mode = STREAM_RES_720P;
            prefs_save(app, TRUE);
            app->renderer.reported_w = 0;
            app->renderer.reported_h = 0;
            renderer_sync_viewport_size(app);
            app_set_status(GetVString(MSG_STATUS_STREAM_RES_720P));
            break;

        case MID_STREAM_RES_540P:
            app->prefs.stream_res_mode = STREAM_RES_540P;
            prefs_save(app, TRUE);
            app->renderer.reported_w = 0;
            app->renderer.reported_h = 0;
            renderer_sync_viewport_size(app);
            app_set_status(GetVString(MSG_STATUS_STREAM_RES_540P));
            break;

        case MID_STREAM_RES_1080P:
            app->prefs.stream_res_mode = STREAM_RES_1080P;
            prefs_save(app, TRUE);
            app->renderer.reported_w = 0;
            app->renderer.reported_h = 0;
            renderer_sync_viewport_size(app);
            app_set_status(GetVString(MSG_STATUS_STREAM_RES_1080P));
            break;

        case MID_FPS_15:
        case MID_FPS_25:
        case MID_FPS_30:
        case MID_FPS_50:
        case MID_FPS_60:
            {
                uint8_t fps_val = 25;
                if (item_id == MID_FPS_15) fps_val = 15;
                else if (item_id == MID_FPS_25) fps_val = 25;
                else if (item_id == MID_FPS_30) fps_val = 30;
                else if (item_id == MID_FPS_50) fps_val = 50;
                else if (item_id == MID_FPS_60) fps_val = 60;
                app->prefs.target_fps = fps_val;
                prefs_save(app, TRUE);
                if (app->connected) {
                    char fcmd[32];
                    snprintf(fcmd, sizeof(fcmd), "@FPS:%u", (unsigned)fps_val);
                    net_send_url(app, fcmd);
                }
                app_set_status(GetVString(MSG_STATUS_FPS_SET), (unsigned)fps_val);
            }
            break;

        case MID_TOGGLE_DARK_THEME:
            app->prefs.dark_theme = (app->prefs.dark_theme == 1) ? 0 : 1;
            prefs_save(app, TRUE);
            if (app->prefs.dark_theme == 0 && s_orig_gui_attrs_saved && s_orig_gui_screen) {
                struct DrawInfo *dri = IIntuition->GetScreenDrawInfo(s_orig_gui_screen);
                if (dri) {
                    IIntuition->SetGUIAttrs(NULL, dri,
                        GUIA_WindowBarBackfill,        (Tag)s_orig_winbar_active_hook,
                        GUIA_WindowBarInactBackfill,    (Tag)s_orig_winbar_inact_hook,
                        GUIA_WindowBorderBackfill,      (Tag)s_orig_winborder_active_hook,
                        GUIA_WindowBorderInactBackfill, (Tag)s_orig_winborder_inact_hook,
                        GUIA_ScreenBarBackfill,        (Tag)s_orig_screenbar_hook,
                        GUIA_WindowBorderLook,          (Tag)s_orig_winborder_look,
                        GUIA_WindowBarFrameThickness,  (Tag)s_orig_winbar_thick,
                        GUIA_WindowFrameThickness,     (Tag)s_orig_winframe_thick,
                        GUIA_TabTitleBackfill,          (Tag)s_orig_tab_title_hook,
                        GUIA_TabActiveBackfill,         (Tag)s_orig_tab_active_hook,
                        GUIA_TabInactiveBackfill,       (Tag)s_orig_tab_inact_hook,
                        GUIA_ClickTabLook,             (Tag)s_orig_clicktab_look,
                        TAG_DONE);
                    IIntuition->FreeScreenDrawInfo(s_orig_gui_screen, dri);
                }
                s_orig_gui_attrs_saved = FALSE;
                s_orig_gui_screen = NULL;
            }
            if (app->connected) {
                char dcmd[32];
                snprintf(dcmd, sizeof(dcmd), "@DARKMODE:%u", (unsigned)app->prefs.dark_theme);
                net_send_url(app, dcmd);
            }
            if (app->custom_screen) {
                app->reopen_screen_needed = TRUE;
            } else {
                app->reopen_gui_needed = TRUE;
            }
            app_set_status(GetVString(MSG_STATUS_THEME_SET),
                           (app->prefs.dark_theme == 1) ? GetVString(MSG_STATUS_THEME_DARK) : GetVString(MSG_STATUS_THEME_LIGHT));
            break;

        case MID_TOGGLE_STATUSBAR:
            app->prefs.show_status_bar = !app->prefs.show_status_bar;
            prefs_save(app, TRUE);
            app->reopen_gui_needed = TRUE;
            break;

        case MID_WINDOW_SNAPSHOT:
            app->prefs.resolution.win_pos_locked = FALSE;
            prefs_capture_window_box(app);
            if (app->prefs.resolution.has_win_pos) {
                app->prefs.resolution.win_pos_locked = TRUE;
            }
            prefs_save_keep_window_box(app, TRUE);
            app_set_status(GetVString(MSG_STATUS_WIN_SNAPSHOTTED),
                           (unsigned)app->prefs.resolution.win_w,
                           (unsigned)app->prefs.resolution.win_h,
                           (int)app->prefs.resolution.win_x,
                           (int)app->prefs.resolution.win_y);
            break;

        case MID_WINDOW_UNSNAPSHOT:
            app->prefs.resolution.win_pos_locked = FALSE;
            app->prefs.resolution.has_win_pos = FALSE;
            if (app->win && app->win->WScreen) {
                struct Screen *scr = app->win->WScreen;
                int16 bar_h = (int16)(scr->BarHeight + 1);
                if (bar_h < 0 || bar_h > 120) bar_h = 24;
                uint16 def_w = scr->Width;
                uint16 def_h = (scr->Height > (uint16)bar_h) ? (uint16)(scr->Height - (uint16)bar_h) : scr->Height;
                app->prefs.resolution.win_x = 0;
                app->prefs.resolution.win_y = bar_h;
                app->prefs.resolution.win_w = def_w;
                app->prefs.resolution.win_h = def_h;
                IIntuition->ChangeWindowBox(app->win, 0, bar_h, (int16)def_w, (int16)def_h);
            }
            prefs_save_keep_window_box(app, TRUE);
            app_set_status(GetVString(MSG_STATUS_WIN_UNSNAPSHOTTED));
            break;

        case MID_QUICK_ADD_CURRENT:
            prompt_add_quicklink(app);
            break;

        case MID_PROJ_ABOUT: {
            char about_buf[1280];
            const char *conn_str = app->connected ? GetVString(MSG_ABOUT_CONNECTED) : GetVString(MSG_ABOUT_DISCONNECTED);
            const char *fmt_str = (app->renderer.src_format == FMT_YUV420P)
                ? (app->renderer.src_bm_is_yuv ? GetVString(MSG_ABOUT_YUV_HW) : GetVString(MSG_ABOUT_YUV_SW))
                : ((app->renderer.src_format == FMT_RGB565) ? "RGB565 16bpp (Direct BltBitMap)" : "ARGB32 32bpp");

            uint16 sw = 0, sh = 0, sd = 0;
            if (app->win && app->win->WScreen) {
                sw = app->win->WScreen->Width;
                sh = app->win->WScreen->Height;
                sd = (app->win->RPort && app->win->RPort->BitMap) ? (uint16)IGraphics->GetBitMapAttr(app->win->RPort->BitMap, BMA_DEPTH) : 16;
            }

            struct IBox *area = NULL;
            if (app->gad_space) {
                IIntuition->GetAttr(SPACE_AreaBox, app->gad_space, (uint32 *)&area);
            }
            uint16 vp_w = area ? (uint16)area->Width : (app->win ? (uint16)(app->win->Width - 8) : 0);
            uint16 vp_h = area ? (uint16)area->Height : (app->win ? (uint16)(app->win->Height - 110) : 0);

            const char *scr_type = app->custom_screen ? GetVString(MSG_ABOUT_SCR_CUSTOM) : GetVString(MSG_ABOUT_SCR_WB);

            const char *eng_str = app->server_engine[0] ? app->server_engine : "Chromium (Linux Headless Streaming)";
            int len = 0;
            len += snprintf(about_buf + len, sizeof(about_buf) - (size_t)len,
                GetVString(MSG_ABOUT_SEC_SERVER),
                app->server_host, (unsigned)app->server_port,
                conn_str, eng_str);
            if (len < 0) len = 0;
            if ((size_t)len < sizeof(about_buf)) {
                int n = snprintf(about_buf + len, sizeof(about_buf) - (size_t)len,
                    GetVString(MSG_ABOUT_SEC_GRAPHICS),
                    (unsigned)sw, (unsigned)sh, (unsigned)sd, scr_type,
                    app->win ? (unsigned)app->win->Width : 0, app->win ? (unsigned)app->win->Height : 0,
                    app->win ? (int)app->win->LeftEdge : 0, app->win ? (int)app->win->TopEdge : 0,
                    (unsigned)vp_w, (unsigned)vp_h,
                    (unsigned)app->renderer.src_width, (unsigned)app->renderer.src_height, fmt_str,
                    (unsigned)app->renderer.frames_decoded,
                    locale_get_active_language());
                if (n > 0) len += n;
            }
            if ((size_t)len < sizeof(about_buf)) {
                snprintf(about_buf + len, sizeof(about_buf) - (size_t)len,
                    GetVString(MSG_ABOUT_SEC_AUDIO),
                    app->renderer.has_altivec ? GetVString(MSG_ABOUT_ALTIVEC_YES) : GetVString(MSG_ABOUT_ALTIVEC_NO));
            }

            struct EasyStruct es;
            memset(&es, 0, sizeof(es));
            es.es_StructSize   = sizeof(struct EasyStruct);
            es.es_Title        = (STRPTR)GetVString(MSG_ABOUT_TITLE);
            es.es_TextFormat   = (STRPTR)"%s";
            es.es_GadgetFormat = (STRPTR)GetVString(MSG_GEN_OK);
            IIntuition->EasyRequest(app->win, &es, NULL, about_buf);
            break;
        }

        case MID_PROJ_PASSWORDS:
            gui_open_password_manager(app);
            break;

        case MID_PROJ_DOWNLOADS:
            gui_open_download_manager(app);
            break;

        case MID_TAB_NEW:
            gui_tab_add(app, NULL, "", TRUE);
            break;

        case MID_TAB_CLOSE:
            if (app->num_tabs > 0) {
                gui_tab_close(app, app->active_tab_idx);
            }
            break;

        case MID_PROJ_QUIT:
            app->running = FALSE;
            break;

        case MID_STREAM_VIDEO_AUTO: {
            app->prefs.video_stream_mode = STREAM_VIDEO_AUTO;
            prefs_save(app, FALSE);
            if (app->connected) net_send_url(app, "@STREAM_MODE:auto");
            app_set_status(GetVString(MSG_STATUS_STREAM_AUTO));
            break;
        }

        case MID_STREAM_VIDEO_LZ4: {
            app->prefs.video_stream_mode = STREAM_VIDEO_DIRTYRECT;
            prefs_save(app, FALSE);
            if (app->connected) net_send_url(app, "@STREAM_MODE:lz4");
            app_set_status(GetVString(MSG_STATUS_STREAM_LZ4));
            break;
        }

        case MID_STREAM_VIDEO_H264: {
            app->prefs.video_stream_mode = STREAM_VIDEO_H264;
            prefs_save(app, FALSE);
            if (app->connected) net_send_url(app, "@STREAM_MODE:h264");
            app_set_status(GetVString(MSG_STATUS_STREAM_H264));
            break;
        }

        case MID_EDIT_CUT:
            dispatch_edit_shortcut(app, 'x', SHORTCUT_SRC_MENU);
            break;

        case MID_EDIT_COPY:
            dispatch_edit_shortcut(app, 'c', SHORTCUT_SRC_MENU);
            break;

        case MID_EDIT_PASTE:
            dispatch_edit_shortcut(app, 'v', SHORTCUT_SRC_MENU);
            break;

        case MID_NAV_BACK:
            net_send_url(app, "@BACK");
            break;

        case MID_NAV_FORWARD:
            net_send_url(app, "@FORWARD");
            break;

        case MID_NAV_RELOAD:
            net_send_url(app, "@RELOAD");
            break;

        case MID_NAV_STOP:
            net_send_url(app, "@STOP");
            app_set_status(GetVString(MSG_STATUS_LOADING_STOPPED));
            break;

        case MID_FONT_LARGER:
            dispatch_zoom_shortcut(app, +1, SHORTCUT_SRC_MENU);
            break;

        case MID_FONT_SMALLER:
            dispatch_zoom_shortcut(app, -1, SHORTCUT_SRC_MENU);
            break;

        case MID_FONT_RESET:
            dispatch_zoom_shortcut(app, 0, SHORTCUT_SRC_MENU);
            break;

        case MID_FAV_ADD: {
            char url_buf[VB_MAX_URL_LEN];
            get_string_gadget_text(app->gad_url, url_buf, sizeof(url_buf));
            const char *u = (url_buf[0] != '\0') ? url_buf : app->current_url;
            if (u && u[0] != '\0') {
                if (favorites_add(app, u, u)) {
                    favorites_save(app);
                    favorites_rebuild_chooser_list(app);
                    app_set_status(GetVString(MSG_STATUS_BOOKMARK_ADDED), u);
                }
            }
            break;
        }

        case MID_FAV_AMIGANEWS:
            gui_update_url_bar(app, "https://www.amiga-news.de");
            net_send_url(app, "https://www.amiga-news.de");
            break;

        case MID_FAV_OS4DEPOT:
            gui_update_url_bar(app, "https://os4depot.net");
            net_send_url(app, "https://os4depot.net");
            break;

        case MID_FAV_AMIGANS:
            gui_update_url_bar(app, "https://www.amigans.net");
            net_send_url(app, "https://www.amigans.net");
            break;

        case MID_FAV_AMINET:
            gui_update_url_bar(app, "https://aminet.net");
            net_send_url(app, "https://aminet.net");
            break;

        case MID_FAV_YOUTUBE:
            gui_update_url_bar(app, "https://www.youtube.com");
            net_send_url(app, "https://www.youtube.com");
            break;

        case MID_FAV_WIKIPEDIA:
            gui_update_url_bar(app, "https://de.wikipedia.org/wiki/AmigaOS");
            net_send_url(app, "https://de.wikipedia.org/wiki/AmigaOS");
            break;

        case MID_SET_SAVE: {
            prefs_save(app, TRUE);
            favorites_save(app);
            char port_str[16];
            snprintf(port_str, sizeof(port_str), "%u", (unsigned)app->server_port);
            IDOS->SetVar("VortexRemoteBrowser/server_host", app->server_host, -1, GVF_GLOBAL_ONLY | GVF_SAVE_VAR);
            IDOS->SetVar("VortexRemoteBrowser/server_port", port_str, -1, GVF_GLOBAL_ONLY | GVF_SAVE_VAR);
            app_set_status(GetVString(MSG_STATUS_SETTINGS_SAVED));
            break;
        }

        case MID_RES_1024_768:
            prefs_set_resolution(app, 0, 0, 0);
            prefs_save(app, TRUE);
            app_set_status(GetVString(MSG_STATUS_RES_CUSTOM), "1024 x 768");
            break;

        case MID_RES_1280_720:
            prefs_set_resolution(app, 1, 0, 0);
            prefs_save(app, TRUE);
            app_set_status(GetVString(MSG_STATUS_RES_CUSTOM), "1280 x 720");
            break;

        case MID_RES_1920_1080:
            prefs_set_resolution(app, 2, 0, 0);
            prefs_save(app, TRUE);
            app_set_status(GetVString(MSG_STATUS_RES_CUSTOM), "1920 x 1080");
            break;

        case MID_RES_800_600:
            prefs_set_resolution(app, 3, 0, 0);
            prefs_save(app, TRUE);
            app_set_status(GetVString(MSG_STATUS_RES_CUSTOM), "800 x 600");
            break;

        case MID_RES_1280_1024:
            prefs_set_resolution(app, 4, 0, 0);
            prefs_save(app, TRUE);
            app_set_status(GetVString(MSG_STATUS_RES_CUSTOM), "1280 x 1024");
            break;

        case MID_RES_AUTO:
            prefs_set_resolution(app, 5, 0, 0);
            prefs_save(app, TRUE);
            app_set_status(GetVString(MSG_STATUS_RES_AUTO));
            break;

        default:
            break;
    }
}

static void open_main_menu_popup(VortexApp *app, int16_t mx, int16_t my)
{
    (void)mx;
    (void)my;
    if (!app || !app->win) return;

    uint32 selected_id = 0;
    if (IPopupMenu) {
        struct Hook pm_hook;
        memset(&pm_hook, 0, sizeof(struct Hook));
        pm_hook.h_Entry = (HOOKFUNC)ctx_menu_hook_func;
        pm_hook.h_Data  = &selected_id;

        Object *menu_obj = PMMENU(GetVString(MSG_POPUP_MAIN_TITLE)),
            PMA_MenuHandler, &pm_hook,
            PMITEM(GetVString(MSG_MENU_NEW_HOME)),          PMIA_ID, MID_PROJ_NEW, PMEND,
            PMITEM(GetVString(MSG_MENU_OPEN_URL)),          PMIA_ID, MID_PROJ_OPEN, PMEND,
            PMITEM(GetVString(MSG_MENU_TAB_NEW)),           PMIA_ID, MID_TAB_NEW, PMEND,
            PMITEM(GetVString(MSG_MENU_TAB_CLOSE)),         PMIA_ID, MID_TAB_CLOSE, PMEND,
            PMBAR(), PMEND,
            PMITEM(GetVString(MSG_MENU_PASSWORDS)),         PMIA_ID, MID_PROJ_PASSWORDS, PMEND,
            PMITEM(GetVString(MSG_MENU_DOWNLOADS)),         PMIA_ID, MID_PROJ_DOWNLOADS, PMEND,
            PMBAR(), PMEND,
            PMITEM(GetVString(MSG_MENU_SERVER_CFG)),        PMIA_ID, MID_SET_SERVER_CUSTOM, PMEND,
            PMITEM(GetVString(MSG_MENU_SERVER_CONNECT)),    PMIA_ID, MID_PROJ_CONNECT, PMEND,
            PMITEM(GetVString(MSG_MENU_SERVER_DISCONNECT)), PMIA_ID, MID_PROJ_DISCONNECT, PMEND,
            PMBAR(), PMEND,
            PMITEM(GetVString(MSG_MENU_BACK)),              PMIA_ID, MID_NAV_BACK, PMEND,
            PMITEM(GetVString(MSG_MENU_FORWARD)),           PMIA_ID, MID_NAV_FORWARD, PMEND,
            PMITEM(GetVString(MSG_MENU_RELOAD)),            PMIA_ID, MID_NAV_RELOAD, PMEND,
            PMITEM(GetVString(MSG_MENU_STOP)),              PMIA_ID, MID_NAV_STOP, PMEND,
            PMBAR(), PMEND,
            PMITEM(GetVString(MSG_MENU_ADD_BOOKMARK)),      PMIA_ID, MID_FAV_ADD, PMEND,
            PMITEM(GetVString(MSG_MENU_ADD_QUICKLINK)),     PMIA_ID, MID_QUICK_ADD_CURRENT, PMEND,
            PMITEM("Amiga-News.de"),                        PMIA_ID, MID_FAV_AMIGANEWS, PMEND,
            PMITEM("OS4Depot.net"),                         PMIA_ID, MID_FAV_OS4DEPOT, PMEND,
            PMITEM("Amigans.net"),                          PMIA_ID, MID_FAV_AMIGANS, PMEND,
            PMITEM("Aminet.net"),                           PMIA_ID, MID_FAV_AMINET, PMEND,
            PMITEM("YouTube"),                              PMIA_ID, MID_FAV_YOUTUBE, PMEND,
            PMITEM("Wikipedia"),                            PMIA_ID, MID_FAV_WIKIPEDIA, PMEND,
            PMBAR(), PMEND,
            PMITEM(app->prefs.dark_theme ? GetVString(MSG_CTX_LIGHT_MODE_FULL) : GetVString(MSG_CTX_DARK_MODE_FULL)), PMIA_ID, MID_TOGGLE_DARK_THEME, PMEND,
            PMITEM(GetVString(MSG_MENU_PREFERENCES)),       PMIA_ID, MID_SETTINGS, PMEND,
            PMITEM(GetVString(MSG_POPUP_SAVE_SETTINGS)),    PMIA_ID, MID_SET_SAVE, PMEND,
            PMITEM(GetVString(MSG_MENU_ABOUT)),             PMIA_ID, MID_PROJ_ABOUT, PMEND,
            PMITEM(GetVString(MSG_MENU_QUIT)),              PMIA_ID, MID_PROJ_QUIT, PMEND,
        PMEND;

        if (menu_obj) {
            Object *sel_item = (Object *)IIntuition->IDoMethod(menu_obj, PM_OPEN, app->win);
            if (selected_id == 0 && sel_item != NULL) {
                uint32 id_val = 0;
                if (IIntuition->GetAttr(PMIA_ID, sel_item, &id_val) && id_val != 0) {
                    selected_id = id_val;
                }
            }
            IIntuition->DisposeObject(menu_obj);
            IIntuition->CurrentTime(&s_last_click_secs, &s_last_click_micros);
        }
    } else {
        struct EasyStruct es;
        memset(&es, 0, sizeof(es));
        es.es_StructSize   = sizeof(struct EasyStruct);
        es.es_Title        = (STRPTR)GetVString(MSG_POPUP_OPT_TITLE);
        es.es_TextFormat   = (STRPTR)GetVString(MSG_POPUP_OPT_PROMPT);
        es.es_GadgetFormat = (STRPTR)GetVString(MSG_POPUP_OPT_BUTTONS);
        LONG c = IIntuition->EasyRequest(app->win, &es, NULL);
        if (c == 1) selected_id = MID_SET_SERVER_CUSTOM;
        else if (c == 2) selected_id = MID_PROJ_CONNECT;
        else if (c == 3) selected_id = MID_PROJ_DISCONNECT;
        else if (c == 4) selected_id = MID_PROJ_NEW;
        else if (c == 5) selected_id = MID_FAV_ADD;
        else if (c == 6) selected_id = MID_PROJ_ABOUT;
    }

    if (selected_id != 0) {
        handle_menu_command(app, selected_id);
    }
}

static void open_quicklink_popup(VortexApp *app, int qidx, int16_t mx, int16_t my)
{
    (void)mx;
    (void)my;
    if (!app || !app->win || qidx < 0 || qidx >= app->prefs.num_quick_links) return;

    const char *lbl = app->prefs.quick_links[qidx].label;
    const char *url = app->prefs.quick_links[qidx].url;
    if (!url || url[0] == '\0') return;

    enum {
        QL_POPUP_NONE = 0,
        QL_POPUP_OPEN_CURRENT,
        QL_POPUP_OPEN_NEW_TAB,
        QL_POPUP_COPY_URL,
        QL_POPUP_EDIT_PREFS,
        QL_POPUP_REMOVE
    };

    uint32 selected_id = QL_POPUP_NONE;
    char title_buf[96];
    snprintf(title_buf, sizeof(title_buf), GetVString(MSG_QL_POPUP_TITLE_FMT), (lbl && lbl[0]) ? lbl : url);

    if (IPopupMenu) {
        struct Hook pm_hook;
        memset(&pm_hook, 0, sizeof(struct Hook));
        pm_hook.h_Entry = (HOOKFUNC)ctx_menu_hook_func;
        pm_hook.h_Data  = &selected_id;

        Object *menu_obj = PMMENU(title_buf),
            PMA_MenuHandler, &pm_hook,
            PMITEM(GetVString(MSG_QL_POPUP_OPEN_CUR)),  PMIA_ID, QL_POPUP_OPEN_CURRENT, PMEND,
            PMITEM(GetVString(MSG_QL_POPUP_OPEN_NEW)),  PMIA_ID, QL_POPUP_OPEN_NEW_TAB, PMEND,
            PMBAR(), PMEND,
            PMITEM(GetVString(MSG_QL_POPUP_COPY_URL)),  PMIA_ID, QL_POPUP_COPY_URL,     PMEND,
            PMITEM(GetVString(MSG_QL_POPUP_EDIT)),      PMIA_ID, QL_POPUP_EDIT_PREFS,   PMEND,
            PMBAR(), PMEND,
            PMITEM(GetVString(MSG_QL_POPUP_REMOVE)),    PMIA_ID, QL_POPUP_REMOVE,       PMEND,
        PMEND;

        if (menu_obj) {
            Object *sel_item = (Object *)IIntuition->IDoMethod(menu_obj, PM_OPEN, app->win);
            if (selected_id == 0 && sel_item != NULL) {
                uint32 id_val = 0;
                if (IIntuition->GetAttr(PMIA_ID, sel_item, &id_val) && id_val != 0) {
                    selected_id = id_val;
                }
            }
            IIntuition->DisposeObject(menu_obj);
            IIntuition->CurrentTime(&s_last_click_secs, &s_last_click_micros);
        }
    } else {
        struct EasyStruct es;
        memset(&es, 0, sizeof(es));
        es.es_StructSize   = sizeof(struct EasyStruct);
        es.es_Title        = (STRPTR)title_buf;
        es.es_TextFormat   = (STRPTR)GetVString(MSG_QL_POPUP_REQ_TEXT);
        es.es_GadgetFormat = (STRPTR)GetVString(MSG_QL_POPUP_REQ_BTNS);
        LONG c = IIntuition->EasyRequest(app->win, &es, NULL, (Tag)url);
        if (c == 1) selected_id = QL_POPUP_OPEN_CURRENT;
        else if (c == 2) selected_id = QL_POPUP_OPEN_NEW_TAB;
        else if (c == 3) selected_id = QL_POPUP_COPY_URL;
        else if (c == 4) selected_id = QL_POPUP_REMOVE;
    }

    switch (selected_id) {
        case QL_POPUP_OPEN_CURRENT:
            gui_update_url_bar(app, url);
            net_send_url(app, url);
            app_set_status(GetVString(MSG_STATUS_QL_STARTED), (lbl && lbl[0]) ? lbl : url);
            break;

        case QL_POPUP_OPEN_NEW_TAB:
            gui_tab_add(app, (lbl && lbl[0]) ? lbl : NULL, (char *)url, TRUE);
            break;

        case QL_POPUP_COPY_URL:
            clipboard_write_text(url);
            app_set_status(GetVString(MSG_STATUS_QL_COPIED), url);
            break;

        case QL_POPUP_EDIT_PREFS:
            gui_prefs_open(app);
            break;

        case QL_POPUP_REMOVE: {
            char rem_name[80];
            snprintf(rem_name, sizeof(rem_name), "%s", (lbl && lbl[0]) ? lbl : url);
            prefs_quicklink_remove(app, qidx);
            prefs_save(app, TRUE);
            app_set_status(GetVString(MSG_STATUS_QL_REMOVED), rem_name);
            app->reopen_gui_needed = TRUE;
            break;
        }

        default:
            break;
    }
}

static uint32 find_rtg_screen_mode(uint16 req_w, uint16 req_h, uint16 *io_depth)
{
    uint32 best_mode = INVALID_ID;

    /* 1. Try BestModeID with RTG and 24-bit depth (truecolor, graphics.doc V54 max depth is 24) */
    best_mode = IGraphics->BestModeID(
        BIDTAG_NominalWidth,  (uint32)req_w,
        BIDTAG_NominalHeight, (uint32)req_h,
        BIDTAG_DesiredWidth,  (uint32)req_w,
        BIDTAG_DesiredHeight, (uint32)req_h,
        BIDTAG_MinDepth,      24,
        BIDTAG_DIPFMustHave,  DIPF_IS_RTG,
        TAG_DONE);

    if (best_mode != INVALID_ID) {
        if (io_depth) *io_depth = 24;
        return best_mode;
    }

    /* 2. Try BestModeID with RTG and 16-bit depth (hicolor) */
    best_mode = IGraphics->BestModeID(
        BIDTAG_NominalWidth,  (uint32)req_w,
        BIDTAG_NominalHeight, (uint32)req_h,
        BIDTAG_DesiredWidth,  (uint32)req_w,
        BIDTAG_DesiredHeight, (uint32)req_h,
        BIDTAG_MinDepth,      16,
        BIDTAG_DIPFMustHave,  DIPF_IS_RTG,
        TAG_DONE);

    if (best_mode != INVALID_ID) {
        if (io_depth) *io_depth = 16;
        return best_mode;
    }

    /* 3. Try BestModeID without MinDepth constraint */
    best_mode = IGraphics->BestModeID(
        BIDTAG_NominalWidth,  (uint32)req_w,
        BIDTAG_NominalHeight, (uint32)req_h,
        BIDTAG_DesiredWidth,  (uint32)req_w,
        BIDTAG_DesiredHeight, (uint32)req_h,
        BIDTAG_DIPFMustHave,  DIPF_IS_RTG,
        TAG_DONE);

    if (best_mode != INVALID_ID) {
        if (io_depth) *io_depth = 24;
        return best_mode;
    }

    /* 4. Exhaustive search across DisplayInfo database for matching RTG mode */
    uint32 id = INVALID_ID;
    uint32 match_24 = INVALID_ID;
    uint32 match_16 = INVALID_ID;

    while ((id = IGraphics->NextDisplayInfo(id)) != INVALID_ID) {
        struct DisplayInfo disp;
        struct DimensionInfo dims;
        memset(&disp, 0, sizeof(disp));
        memset(&dims, 0, sizeof(dims));

        if (IGraphics->GetDisplayInfoData(NULL, (uint8 *)&disp, sizeof(disp), DTAG_DISP, id) &&
            IGraphics->GetDisplayInfoData(NULL, (uint8 *)&dims, sizeof(dims), DTAG_DIMS, id)) {
            if (disp.NotAvailable == 0 && (disp.PropertyFlags & DIPF_IS_RTG)) {
                uint16 w = (uint16)(dims.Nominal.MaxX - dims.Nominal.MinX + 1);
                uint16 h = (uint16)(dims.Nominal.MaxY - dims.Nominal.MinY + 1);
                if (w == req_w && h == req_h) {
                    if (dims.MaxDepth >= 24) {
                        match_24 = id;
                        break;
                    } else if (dims.MaxDepth >= 16 && match_16 == INVALID_ID) {
                        match_16 = id;
                    }
                }
            }
        }
    }

    if (match_24 != INVALID_ID) {
        if (io_depth) *io_depth = 24;
        return match_24;
    }
    if (match_16 != INVALID_ID) {
        if (io_depth) *io_depth = 16;
        return match_16;
    }

    return INVALID_ID;
}

/* ------------------------------------------------------------------------
 * Tabbed Browsing Functions
 * ------------------------------------------------------------------------ */
/* ClickTab close-gadget tracking.
 * CLICKTAB_Closed / CLICKTAB_NodeClosed are STICKY: they keep returning the
 * last closed tab forever. The old code read CLICKTAB_Closed on every tab
 * click, so after closing one tab, every later tab click closed another tab.
 * We now remember the node we already handled and only accept a close if
 * CLICKTAB_NodeClosed points to a node that is part of the CURRENT list.
 * The list builder guarantees that no live node ever reuses the address of
 * the already-handled (freed) node. */
static struct Node *s_handled_close_node = NULL;
static uint16       s_last_tabs_win_w    = 0;

static int gui_get_clicktab_avail_width(VortexApp *app)
{
    int gad_w = (app && app->gad_clicktab) ? (int)((struct Gadget *)app->gad_clicktab)->Width : 0;
    if (gad_w > 60 && app && app->win && app->win->Width == s_last_tabs_win_w) {
        return gad_w;
    }

    int win_inner_w = 1000;
    if (app && app->win) {
        win_inner_w = (int)app->win->Width - (int)app->win->BorderLeft - (int)app->win->BorderRight;
    } else if (app) {
        if ((!app->custom_screen || app->prefs.resolution.win_pos_locked) &&
            app->prefs.resolution.has_win_pos && app->prefs.resolution.win_w >= 200) {
            win_inner_w = (int)app->prefs.resolution.win_w - 8;
        } else if (app->custom_screen) {
            win_inner_w = (int)app->custom_screen->Width - 8;
        } else if (IIntuition) {
            struct Screen *wb_scr = IIntuition->LockPubScreen(NULL);
            if (wb_scr) {
                win_inner_w = (int)wb_scr->Width - 8;
                IIntuition->UnlockPubScreen(NULL, wb_scr);
            }
        }
    }

    int plus_w = 28;
    if (app && app->gad_tab_new && ((struct Gadget *)app->gad_tab_new)->Width > 0) {
        plus_w = (int)((struct Gadget *)app->gad_tab_new)->Width;
    }
    int avail_w = win_inner_w - plus_w - 8;
    if (gad_w > avail_w) {
        avail_w = gad_w;
    }
    if (avail_w < 120) {
        avail_w = 120;
    }
    return avail_w;
}

static void tabs_build_list(VortexApp *app, struct List *out)
{
    IExec->NewList(out);
    if (!app || !IClickTab || app->num_tabs == 0) return;

    BOOL use_dark_pen = (app->prefs.dark_theme == 1 && s_dark_pens.allocated && s_dark_pens.txt_pen >= 0);

    int n = (int)app->num_tabs;
    if (n > VB_MAX_TABS) n = VB_MAX_TABS;

    /* Per clicktab.gadget V53 disassembly (GM_DOMAIN @ 0x10056b0, GM_LAYOUT @ 0x1001c10,
     * GM_RENDER @ 0x1002040 & 0x10032f4):
     * 1. clicktab.gadget ONLY renders and activates the close gadget on the currently
     *    active tab (is_active == 1), but reserves +21px of blank space in GM_DOMAIN on
     *    any node that has TNA_CloseGadget == TRUE. Passing TNA_CloseGadget only on the
     *    active tab eliminates 21px of wasted blank space per inactive tab.
     * 2. When CLICKTAB_LabelTruncate is TRUE and CLICKTAB_EvenSize is FALSE, clicktab.gadget
     *    itself shrinks the widest tabs pixel-by-pixel in GM_LAYOUT until their sum equals
     *    gad_clicktab->Width to the exact pixel (0 wasted space) and uses IGraphics->TextFit()
     *    in GM_RENDER, provided gad_clicktab->Width >= sum(node->min_w).
     * 3. node->min_w is TextExtent(first 3 chars) + 26 (or +47 for active tab), unless
     *    strlen(TNA_Text) <= 2. Only when the window is extremely narrow relative to tab
     *    count (< 52px per tab) do we shorten labels to 2 chars so sum(min_w) still fits. */
    int avail_bar_w = gui_get_clicktab_avail_width(app);
    BOOL ultra_narrow = (n > 4 && avail_bar_w < (n * 52 + 24));

    for (int i = 0; i < n; i++) {
        char disp_title[64];
        const char *raw_title = (app->tabs[i].title[0] != '\0') ? app->tabs[i].title : "Tab";
        while (*raw_title == ' ' || *raw_title == '\t') raw_title++;
        if (*raw_title == '\0') raw_title = "Tab";

        if (ultra_narrow) {
            size_t max_c = (avail_bar_w < (n * 38 + 24)) ? 1 : 2;
            size_t rlen = strlen(raw_title);
            if (rlen > max_c) rlen = max_c;
            memcpy(disp_title, raw_title, rlen);
            disp_title[rlen] = '\0';
        } else {
            snprintf(disp_title, sizeof(disp_title), "%s", raw_title);
        }

        BOOL has_close = (i == (int)app->active_tab_idx);

        struct Node *quarantine = NULL;
        struct Node *node = IClickTab->AllocClickTabNode(
            TNA_Text,        (Tag)disp_title,
            TNA_Number,      (uint16)i,
            TNA_CloseGadget, has_close,
            use_dark_pen ? TNA_TextPen : TAG_IGNORE, (uint32)(int16)s_dark_pens.txt_pen,
            TAG_DONE);
        if (node && node == s_handled_close_node) {
            /* Allocator handed us the stale address: keep it aside, get another */
            quarantine = node;
            node = IClickTab->AllocClickTabNode(
                TNA_Text,        (Tag)disp_title,
                TNA_Number,      (uint16)i,
                TNA_CloseGadget, has_close,
                use_dark_pen ? TNA_TextPen : TAG_IGNORE, (uint32)(int16)s_dark_pens.txt_pen,
                TAG_DONE);
            IClickTab->FreeClickTabNode(quarantine);
        }
        if (node) {
            IExec->AddTail(out, node);
        }
    }

    if (app->win) {
        s_last_tabs_win_w = app->win->Width;
    }
}

static int tabs_index_of_node(VortexApp *app, struct Node *node)
{
    int i = 0;
    for (struct Node *n = app->tab_list.lh_Head; n && n->ln_Succ; n = n->ln_Succ, i++) {
        if (n == node) return i;
    }
    return -1;
}

void gui_tabs_init(VortexApp *app)
{
    if (!app) return;
    s_handled_close_node = NULL;

    /* Called on every window (re)build. Keep existing tabs so the client
     * stays in sync with the server's tab list across GUI rebuilds
     * (screen change, prefs, adding quicklinks ...). */
    if (app->num_tabs == 0) {
        if (app->prefs.saved_num_tabs > 0) {
            /* Count tabs that contain a real web URL (not empty, not startpage/portal) */
            int real_tabs = 0;
            for (int i = 0; i < app->prefs.saved_num_tabs && i < VB_MAX_TABS; i++) {
                if (!prefs_is_startpage_url(app->prefs.saved_tab_urls[i])) {
                    real_tabs++;
                }
            }

            int count = 0;
            int mapped_active = -1;
            const char *home_str = GetVString(MSG_MENU_HOME);
            for (int i = 0; i < app->prefs.saved_num_tabs && i < VB_MAX_TABS; i++) {
                const char *u = app->prefs.saved_tab_urls[i];
                const char *t = app->prefs.saved_tab_titles[i];
                BOOL is_startpage = (prefs_is_startpage_url(u) ||
                                     (t && (strcmp(t, "Startseite") == 0 || (home_str && strcmp(t, home_str) == 0)) && (!u || u[0] == '\0')));
                /* If there is at least one saved web tab, never restore redundant startpage tabs */
                if (real_tabs > 0 && is_startpage) {
                    continue;
                }
                if (i == (int)app->prefs.saved_active_tab) {
                    mapped_active = count;
                }
                app->tabs[count].id = (uint32_t)count;
                strncpy(app->tabs[count].url, (!is_startpage && u) ? u : "", sizeof(app->tabs[0].url) - 1);
                app->tabs[count].url[sizeof(app->tabs[0].url) - 1] = '\0';
                if (is_startpage) {
                    snprintf(app->tabs[count].title, sizeof(app->tabs[0].title), "%s", home_str ? home_str : "Startseite");
                } else if (t && t[0]) {
                    vb_utf8_to_local(t, app->tabs[count].title, sizeof(app->tabs[0].title));
                    app->tabs[count].title[sizeof(app->tabs[0].title) - 1] = '\0';
                } else {
                    strncpy(app->tabs[count].title, "Web", sizeof(app->tabs[0].title) - 1);
                    app->tabs[count].title[sizeof(app->tabs[0].title) - 1] = '\0';
                }
                count++;
            }
            if (count == 0) {
                app->active_tab_idx = 0;
                app->tabs[0].id     = 0;
                snprintf(app->tabs[0].title, sizeof(app->tabs[0].title), "%s", home_str ? home_str : "Startseite");
                if (!prefs_is_startpage_url(app->current_url)) {
                    strncpy(app->tabs[0].url, app->current_url, sizeof(app->tabs[0].url) - 1);
                    app->tabs[0].url[sizeof(app->tabs[0].url) - 1] = '\0';
                } else {
                    app->tabs[0].url[0] = '\0';
                    app->current_url[0] = '\0';
                }
                app->num_tabs = 1;
            } else {
                app->num_tabs = (uint16_t)count;
                if (mapped_active >= 0 && mapped_active < count) {
                    app->active_tab_idx = (uint16_t)mapped_active;
                } else {
                    app->active_tab_idx = (uint16_t)(count - 1);
                }
                const char *act_url = app->tabs[app->active_tab_idx].url;
                if (!prefs_is_startpage_url(act_url)) {
                    strncpy(app->current_url, act_url, sizeof(app->current_url) - 1);
                    app->current_url[sizeof(app->current_url) - 1] = '\0';
                } else {
                    app->current_url[0] = '\0';
                }
            }
        } else {
            app->active_tab_idx = 0;
            app->tabs[0].id = 0;
            snprintf(app->tabs[0].title, sizeof(app->tabs[0].title), "%s", GetVString(MSG_MENU_HOME));
            if (!prefs_is_startpage_url(app->current_url)) {
                strncpy(app->tabs[0].url, app->current_url, sizeof(app->tabs[0].url) - 1);
                app->tabs[0].url[sizeof(app->tabs[0].url) - 1] = '\0';
            } else {
                app->tabs[0].url[0] = '\0';
                app->current_url[0] = '\0';
            }
            app->num_tabs = 1;
        }
    }
    if (app->active_tab_idx >= app->num_tabs) {
        app->active_tab_idx = (uint16)(app->num_tabs - 1);
    }
    for (int i = 0; i < app->num_tabs; i++) {
        app->tabs[i].id = (uint32_t)i;
    }

    tabs_build_list(app, &app->tab_list);
}

void gui_tabs_rebuild(VortexApp *app)
{
    if (!app || !IClickTab) return;

    /* Allocate the new list BEFORE freeing the old one */
    struct List new_list;
    tabs_build_list(app, &new_list);

    if (app->gad_clicktab && app->win) {
        IIntuition->SetGadgetAttrs((struct Gadget *)app->gad_clicktab, app->win, NULL,
                                   CLICKTAB_Labels, ~0,
                                   TAG_DONE);
    }

    IClickTab->FreeClickTabList(&app->tab_list);
    IExec->NewList(&app->tab_list);
    struct Node *n;
    while ((n = IExec->RemHead(&new_list)) != NULL) {
        IExec->AddTail(&app->tab_list, n);
    }

    if (app->gad_clicktab && app->win) {
        IIntuition->RefreshSetGadgetAttrs((struct Gadget *)app->gad_clicktab, app->win, NULL,
                                          CLICKTAB_Labels,  (Tag)&app->tab_list,
                                          CLICKTAB_Current, (uint32)app->active_tab_idx,
                                          TAG_DONE);
    }
}

/* Called after (re)connecting: the server starts every session with exactly
 * one tab, so drop our extra tabs and keep the active one as tab 0. */
void gui_tabs_reset(VortexApp *app)
{
    if (!app) return;
    s_handled_close_node = NULL;
    if (app->num_tabs > 0 && app->active_tab_idx < app->num_tabs && app->active_tab_idx != 0) {
        app->tabs[0] = app->tabs[app->active_tab_idx];
    }
    if (app->num_tabs == 0) {
        snprintf(app->tabs[0].title, sizeof(app->tabs[0].title), "%s", GetVString(MSG_MENU_HOME));
        app->tabs[0].url[0] = '\0';
    }
    app->tabs[0].id     = 0;
    app->num_tabs       = 1;
    app->active_tab_idx = 0;
    gui_tabs_rebuild(app);
}

static int tab_append_local(VortexApp *app, const char *title, const char *url, int target_idx)
{
    if (!app || app->num_tabs >= VB_MAX_TABS) return -1;
    int idx = (target_idx >= 0 && target_idx < app->num_tabs) ? target_idx : (int)app->num_tabs++;
    app->tabs[idx].id = (uint32_t)idx;
    if (title && title[0]) {
        vb_utf8_to_local(title, app->tabs[idx].title, sizeof(app->tabs[0].title));
        app->tabs[idx].title[sizeof(app->tabs[0].title) - 1] = '\0';
    } else {
        snprintf(app->tabs[idx].title, sizeof(app->tabs[0].title), "%s", GetVString(MSG_MENU_TAB_NEW));
    }
    /* Empty URL = server portal/start page (never "about:blank") */
    strncpy(app->tabs[idx].url, url ? url : "", sizeof(app->tabs[0].url) - 1);
    app->tabs[idx].url[sizeof(app->tabs[0].url) - 1] = '\0';
    return idx;
}

void gui_tab_add(VortexApp *app, const char *title, const char *url, BOOL switch_to)
{
    if (!app) return;
    if (app->num_tabs >= VB_MAX_TABS) {
        app_set_status(GetVString(MSG_STATUS_MAX_TABS), VB_MAX_TABS);
        return;
    }

    int idx = tab_append_local(app, title, url, -1);
    if (idx < 0) return;

    if (switch_to) {
        app->active_tab_idx = (uint16)idx;
        gui_update_url_bar(app, app->tabs[idx].url);
        char cmd[VB_MAX_URL_LEN + 16];
        snprintf(cmd, sizeof(cmd), "@NEWTAB:%s", app->tabs[idx].url);
        net_send_url(app, cmd);
    }

    gui_tabs_rebuild(app);
}

void gui_tab_add_remote(VortexApp *app, const char *title, const char *url, uint16 idx, BOOL switch_to)
{
    if (!app) return;
    int added = tab_append_local(app, title, url, (int)idx);
    if (added < 0) return;

    if (switch_to) {
        app->active_tab_idx = (uint16)added;
        snprintf(app->current_url, sizeof(app->current_url), "%s", app->tabs[added].url);
        gui_update_url_bar(app, app->tabs[added].url);
    }

    gui_tabs_rebuild(app);
}

static void apply_tab_switch_ui(VortexApp *app, uint16 idx)
{
    app->active_tab_idx = idx;
    snprintf(app->current_url, sizeof(app->current_url), "%s", app->tabs[idx].url);
    gui_update_url_bar(app, app->tabs[idx].url);
    app_set_status(GetVString(MSG_STATUS_TAB_SWITCH), (int)idx + 1, app->tabs[idx].title);

    gui_tabs_rebuild(app);
}

void gui_tab_switch(VortexApp *app, uint16 idx)
{
    if (!app || idx >= app->num_tabs) return;

    apply_tab_switch_ui(app, idx);
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "@SWITCHTAB:%u", (unsigned)idx);
    net_send_url(app, cmd);
}

void gui_tab_switch_remote(VortexApp *app, uint16 idx)
{
    if (!app || idx >= app->num_tabs) return;
    apply_tab_switch_ui(app, idx);
}

void gui_tab_close(VortexApp *app, uint16 idx)
{
    if (!app || idx >= app->num_tabs) return;

    if (app->num_tabs <= 1) {
        /* Last tab: don't close, go back to the start page.
         * Must be "@HOME" - a literal 127.0.0.1:7788 URL would make Chromium
         * load the VortexRemoteBrowser binary protocol port as a web page. */
        snprintf(app->tabs[0].title, sizeof(app->tabs[0].title), "%s", GetVString(MSG_MENU_HOME));
        app->tabs[0].url[0] = '\0';
        gui_update_url_bar(app, "");
        net_send_url(app, "@HOME");
        gui_tabs_rebuild(app);
        return;
    }

    char cmd[32];
    snprintf(cmd, sizeof(cmd), "@CLOSETAB:%u", (unsigned)idx);
    net_send_url(app, cmd);

    for (int i = idx; i < app->num_tabs - 1; i++) {
        app->tabs[i] = app->tabs[i + 1];
        app->tabs[i].id = (uint32_t)i;
    }
    app->num_tabs--;

    if (app->active_tab_idx > idx) {
        app->active_tab_idx--;
    } else if (app->active_tab_idx >= app->num_tabs) {
        app->active_tab_idx = (uint16)(app->num_tabs - 1);
    }

    /* Explicitly tell the server which tab is active now (keeps both sides
     * in sync even if they ever disagreed before). */
    snprintf(cmd, sizeof(cmd), "@SWITCHTAB:%u", (unsigned)app->active_tab_idx);
    net_send_url(app, cmd);

    gui_tabs_rebuild(app);
    gui_update_url_bar(app, app->tabs[app->active_tab_idx].url);
}

void gui_tab_close_remote(VortexApp *app, uint16 idx, uint16 new_active_idx)
{
    if (!app || app->num_tabs == 0) return;

    if (app->num_tabs <= 1) {
        snprintf(app->tabs[0].title, sizeof(app->tabs[0].title), "%s", GetVString(MSG_MENU_HOME));
        app->tabs[0].url[0] = '\0';
        app->active_tab_idx = 0;
        app->current_url[0] = '\0';
        gui_update_url_bar(app, "");
        gui_tabs_rebuild(app);
        return;
    }

    if (idx < app->num_tabs) {
        for (int i = idx; i < app->num_tabs - 1; i++) {
            app->tabs[i] = app->tabs[i + 1];
            app->tabs[i].id = (uint32_t)i;
        }
        app->num_tabs--;
    }

    app->active_tab_idx = (new_active_idx < app->num_tabs) ? new_active_idx : (uint16)(app->num_tabs - 1);
    snprintf(app->current_url, sizeof(app->current_url), "%s", app->tabs[app->active_tab_idx].url);
    gui_tabs_rebuild(app);
    gui_update_url_bar(app, app->tabs[app->active_tab_idx].url);
}

void gui_tab_set_title(VortexApp *app, uint16 idx, const char *title)
{
    if (!app || idx >= app->num_tabs || !title) return;
    if (strncmp(app->tabs[idx].title, title, sizeof(app->tabs[0].title) - 1) == 0) return;
    strncpy(app->tabs[idx].title, title, sizeof(app->tabs[0].title) - 1);
    app->tabs[idx].title[sizeof(app->tabs[0].title) - 1] = '\0';
    gui_tabs_rebuild(app);
}

BOOL gui_init(VortexApp *app)
{
    app->main_task = IExec->FindTask(NULL);
    if (app->mouse_sig_mask == 0) {
        app->mouse_sig_bit = IExec->AllocSignal(-1);
        if (app->mouse_sig_bit != -1) {
            app->mouse_sig_mask = (1UL << app->mouse_sig_bit);
        } else {
            app->mouse_sig_mask = 0;
        }
    }
    if (!app->app_port) {
        app->app_port = (struct MsgPort *)IExec->AllocSysObjectTags(ASOT_PORT, TAG_DONE);
    }
    app->is_iconified = FALSE;

    memset(&s_render_hook, 0, sizeof(struct Hook));
    s_render_hook.h_Entry = (HOOKFUNC)space_render_func;
    s_render_hook.h_Data  = app;

    memset(&s_idcmp_hook, 0, sizeof(struct Hook));
    s_idcmp_hook.h_Entry = (HOOKFUNC)idcmp_hook_func;
    s_idcmp_hook.h_Data  = app;

    memset(&s_post_refresh_hook, 0, sizeof(struct Hook));
    s_post_refresh_hook.h_Entry = (HOOKFUNC)post_refresh_hook_func;
    s_post_refresh_hook.h_Data  = app;

    /* 0. Open custom Public Screen if configured */
    if (app->prefs.resolution.use_custom_screen) {
        if (!app->custom_screen) {
            const char *pubname = "VORTEXREMOTEBROWSER";
            const char *title   = "VortexRemoteBrowser 1.1 (AmigaOS 4.1)";
            uint16 scr_w = app->prefs.resolution.width;
            uint16 scr_h = app->prefs.resolution.height;
            uint16 scr_d = app->prefs.resolution.depth;
            if (scr_d < 16 || scr_d > 32) scr_d = 24;
            BOOL is_clone_wb = (app->prefs.resolution.screen_mode == VB_SCREEN_CLONE_WB);

            struct Screen *wb_scr = IIntuition->LockPubScreen("Workbench");
            uint32 wb_mode_id = INVALID_ID;
            uint16 wb_w = 1280, wb_h = 720, wb_d = 24;
            UWORD pens_copy[NUMDRIPENS + 1];
            for (int p = 0; p <= NUMDRIPENS; p++) pens_copy[p] = (UWORD)~0;
            uint32 colors32[32 * 3 + 2];
            memset(colors32, 0, sizeof(colors32));
            UBYTE detail_pen = 0, block_pen = 1;

            if (wb_scr) {
                wb_mode_id = IGraphics ? IGraphics->GetVPModeID(&wb_scr->ViewPort) : INVALID_ID;
                wb_w       = wb_scr->Width;
                wb_h       = wb_scr->Height;
                wb_d       = IGraphics->GetBitMapAttr(wb_scr->RastPort.BitMap, BMA_DEPTH);
                if (wb_d < 16 || wb_d > 32) wb_d = 24;
                detail_pen = wb_scr->DetailPen;
                block_pen  = wb_scr->BlockPen;
                struct DrawInfo *dri = IIntuition->GetScreenDrawInfo(wb_scr);
                if (dri) {
                    int np = (dri->dri_NumPens < NUMDRIPENS) ? dri->dri_NumPens : NUMDRIPENS;
                    for (int p = 0; p < np; p++) {
                        pens_copy[p] = dri->dri_Pens[p];
                    }
                    pens_copy[np] = (UWORD)~0;
                    IIntuition->FreeScreenDrawInfo(wb_scr, dri);
                }
                colors32[0] = (32 << 16) | 0;
                IGraphics->GetRGB32(wb_scr->ViewPort.ColorMap, 0, 32, &colors32[1]);
                colors32[32 * 3 + 1] = 0;

                if (is_clone_wb) {
                    scr_w = wb_w;
                    scr_h = wb_h;
                    scr_d = wb_d;
                    app->prefs.resolution.width   = scr_w;
                    app->prefs.resolution.height  = scr_h;
                    app->prefs.resolution.depth   = scr_d;
                    app->prefs.resolution.mode_id = wb_mode_id;
                }
                IIntuition->UnlockPubScreen(NULL, wb_scr);
            }

            if (app->prefs.dark_theme == 1) {
                static const uint32 s_dark_colors_def[32 * 3] = {
                    /*  0: Screen / Window Background (#222428) */
                    0x22222222UL, 0x24242424UL, 0x28282828UL,
                    /*  1: Screen Bar / Menu Fill (#18191C) */
                    0x18181818UL, 0x19191919UL, 0x1C1C1C1CUL,
                    /*  2: Text / Detail (#E4E6EB) */
                    0xE4E4E4E4UL, 0xE6E6E6E6UL, 0xEBEBEBEBUL,
                    /*  3: 3D Shine / Highlight (#3A4454) */
                    0x3A3A3A3AUL, 0x44444444UL, 0x54545454UL,
                    /*  4: 3D Shadow (#121316) */
                    0x12121212UL, 0x13131313UL, 0x16161616UL,
                    /*  5: Titlebar Shine (#444C5C) */
                    0x44444444UL, 0x4C4C4C4CUL, 0x5C5C5C5CUL,
                    /*  6: Active Window Titlebar & Borders (#282C37) */
                    0x28282828UL, 0x2C2C2C2CUL, 0x37373737UL,
                    /*  7: Inactive Window Titlebar & Borders (#1E2026) */
                    0x1E1E1E1EUL, 0x20202020UL, 0x26262626UL,
                    /*  8: Selection Dark Slate (#3A4454) - non-blue tab & frame accent */
                    0x3A3A3A3AUL, 0x44444444UL, 0x54545454UL,
                    /*  9: Inactive Text (#787E8C) */
                    0x78787878UL, 0x7E7E7E7EUL, 0x8C8C8C8CUL,
                    /* 10: Button/Gadget Fill (#2E3442) */
                    0x2E2E2E2EUL, 0x34343434UL, 0x42424242UL,
                    /* 11: Disabled (#1C1E24) */
                    0x1C1C1C1CUL, 0x1E1E1E1EUL, 0x24242424UL,
                    /* 12: Half Shine (#303642) */
                    0x30303030UL, 0x36363636UL, 0x42424242UL,
                    /* 13: Half Shadow (#16181C) */
                    0x16161616UL, 0x18181818UL, 0x1C1C1C1CUL,
                    /* 14: Dark Slate (#202228) */
                    0x20202020UL, 0x22222222UL, 0x28282828UL,
                    /* 15: Deep Grey (#2A2E38) */
                    0x2A2A2A2AUL, 0x2E2E2E2EUL, 0x38383838UL,
                    /* 16: Accent Dark Slate (#3E4654) */
                    0x3E3E3E3EUL, 0x46464646UL, 0x54545454UL,
                    /* 17: Pure Black (#000000) */
                    0x00000000UL, 0x00000000UL, 0x00000000UL,
                    /* 18: Pure White (#FFFFFF) */
                    0xFFFFFFFFUL, 0xFFFFFFFFUL, 0xFFFFFFFFUL,
                    /* 19..31: Neutral darks */
                    0x25252525UL, 0x27272727UL, 0x2C2C2C2CUL,
                    0x2C2C2C2CUL, 0x2F2F2F2FUL, 0x35353535UL,
                    0x33333333UL, 0x37373737UL, 0x3E3E3E3EUL,
                    0x3B3B3B3BUL, 0x40404040UL, 0x47474747UL,
                    0x42424242UL, 0x47474747UL, 0x50505050UL,
                    0x4A4A4A4AUL, 0x50505050UL, 0x5B5B5B5BUL,
                    0x55555555UL, 0x5C5C5C5CUL, 0x66666666UL,
                    0x60606060UL, 0x67676767UL, 0x72727272UL,
                    0x70707070UL, 0x77777777UL, 0x82828282UL,
                    0x85858585UL, 0x8C8C8C8CUL, 0x96969696UL,
                    0x9B9B9B9BUL, 0xA2A2A2A2UL, 0xACACACACUL,
                    0xB5B5B5B5UL, 0xBCBCBCBCUL, 0xC6C6C6C6UL,
                    0xD0D0D0D0UL, 0xD6D6D6D6UL, 0xDFDFDFDFUL
                };

                colors32[0] = (32 << 16) | 0;
                for (int i = 0; i < 32 * 3; i++) {
                    colors32[1 + i] = s_dark_colors_def[i];
                }
                colors32[32 * 3 + 1] = 0;

                detail_pen = 2; /* Light text */
                block_pen  = 1; /* Dark block */

                pens_copy[DETAILPEN]             = 2;
                pens_copy[BLOCKPEN]              = 1;
                pens_copy[TEXTPEN]               = 2;
                pens_copy[SHINEPEN]              = 3;
                pens_copy[SHADOWPEN]             = 4;
                pens_copy[FILLPEN]               = 6; /* Active window titlebar & border fill (dark #282C37) */
                pens_copy[FILLTEXTPEN]           = 2; /* Text over FILLPEN (white #E4E6EB) */
                pens_copy[BACKGROUNDPEN]         = 0; /* Background (dark #222428) */
                pens_copy[HIGHLIGHTTEXTPEN]      = 2;
                pens_copy[BARDETAILPEN]          = 2; /* Text in screen-bar (white #E4E6EB) */
                pens_copy[BARBLOCKPEN]           = 1; /* Screen-bar fill (dark #18191C) */
                pens_copy[BARTRIMPEN]            = 4; /* Trim under screen-bar */
                pens_copy[BARCONTOURPEN]         = 4;
                pens_copy[FOREGROUNDPEN]         = 10;
                pens_copy[FORESHINEPEN]          = 3;
                pens_copy[FORESHADOWPEN]         = 4;
                pens_copy[FILLSHINEPEN]          = 5;
                pens_copy[FILLSHADOWPEN]         = 4;
                pens_copy[INACTIVEFILLPEN]       = 7; /* Inactive window titlebar & border (dark #1E2026) */
                pens_copy[INACTIVEFILLTEXTPEN]   = 9; /* Text over INACTIVEFILLPEN */
                pens_copy[INACTIVEFILLSHINEPEN]  = 3;
                pens_copy[INACTIVEFILLSHADOWPEN] = 4;
                pens_copy[DISABLEDPEN]           = 11;
                pens_copy[DISABLEDTEXTPEN]       = 9;
                pens_copy[DISABLEDSHINEPEN]      = 3;
                pens_copy[DISABLEDSHADOWPEN]     = 4;
                pens_copy[MENUBACKGROUNDPEN]     = 1;
                pens_copy[MENUTEXTPEN]           = 2;
                pens_copy[MENUSHINEPEN]          = 3;
                pens_copy[MENUSHADOWPEN]         = 4;
                pens_copy[SELECTPEN]             = 8;
                pens_copy[SELECTTEXTPEN]         = 2;
                pens_copy[SELECTSHINEPEN]        = 5;
                pens_copy[SELECTSHADOWPEN]       = 4;
                pens_copy[GLYPHPEN]              = 2;
                pens_copy[GLYPHFILLPEN]          = 6;
                pens_copy[INACTIVEGLYPHPEN]      = 9;
                pens_copy[FLATBORDERPEN]         = 4;
                pens_copy[FILLFLATPEN]           = 6;
                pens_copy[INACTIVEFILLFLATPEN]   = 7;
                pens_copy[NUMDRIPENS]            = (UWORD)~0;
            }

            if (!is_clone_wb) {
                if (scr_w == 0 || scr_h == 0) {
                    if (app->prefs.resolution.preset_idx < 5) {
                        scr_w = g_res_presets[app->prefs.resolution.preset_idx].width;
                        scr_h = g_res_presets[app->prefs.resolution.preset_idx].height;
                    } else if (app->prefs.resolution.preset_idx == 6 && app->prefs.resolution.custom_w > 0 && app->prefs.resolution.custom_h > 0) {
                        scr_w = app->prefs.resolution.custom_w;
                        scr_h = app->prefs.resolution.custom_h;
                    } else {
                        scr_w = 1024;
                        scr_h = 768;
                    }
                }
            }

            uint32 mode_id = app->prefs.resolution.mode_id;
            if (is_clone_wb && wb_mode_id != INVALID_ID) {
                mode_id = wb_mode_id;
            } else {
                if (mode_id != 0 && mode_id != INVALID_ID && IGraphics->ModeNotAvailable(mode_id) != 0) {
                    mode_id = INVALID_ID;
                }
                if (mode_id == 0 || mode_id == INVALID_ID) {
                    mode_id = find_rtg_screen_mode(scr_w, scr_h, &scr_d);
                }
                /* If requested resolution is unavailable on this RTG display, clone Workbench mode */
                if ((mode_id == 0 || mode_id == INVALID_ID) && wb_mode_id != INVALID_ID) {
                    DBG_LOG("[GUI] RTG mode %ux%u unavailable; falling back to Workbench mode %ux%u (0x%08lX)\n",
                            (unsigned)scr_w, (unsigned)scr_h, (unsigned)wb_w, (unsigned)wb_h, (unsigned long)wb_mode_id);
                    mode_id     = wb_mode_id;
                    scr_w       = wb_w;
                    scr_h       = wb_h;
                    scr_d       = wb_d;
                    is_clone_wb = TRUE;
                }
            }
            if (scr_d < 16 || scr_d > 32) scr_d = 24;
            app->prefs.resolution.depth = scr_d;

            if (mode_id != 0 && mode_id != INVALID_ID) {
                DBG_LOG("[GUI] Opening custom RTG screen '%s' (%ux%u, %u-bit, ModeID 0x%08lX, CloneWB=%d)...\n",
                       pubname, (unsigned)scr_w, (unsigned)scr_h, (unsigned)scr_d, (unsigned long)mode_id, (int)is_clone_wb);

                memset(&s_dark_backfill_hook, 0, sizeof(struct Hook));
                s_dark_backfill_hook.h_Entry = (HOOKFUNC)dark_backfill_hook_func;

                memset(&s_space_dark_render_hook, 0, sizeof(struct Hook));
                s_space_dark_render_hook.h_Entry = (HOOKFUNC)space_dark_render_hook_func;

                memset(&s_dark_winbar_active_hook, 0, sizeof(struct Hook));
                s_dark_winbar_active_hook.h_Entry = (HOOKFUNC)dark_winbar_active_hook_func;

                memset(&s_dark_winbar_inact_hook, 0, sizeof(struct Hook));
                s_dark_winbar_inact_hook.h_Entry = (HOOKFUNC)dark_winbar_inact_hook_func;

                memset(&s_dark_screenbar_hook, 0, sizeof(struct Hook));
                s_dark_screenbar_hook.h_Entry = (HOOKFUNC)dark_screenbar_hook_func;

                app->custom_screen = IIntuition->OpenScreenTags(NULL,
                    (is_clone_wb && app->prefs.dark_theme != 1) ? SA_LikeWorkbench : TAG_IGNORE, TRUE,
                    SA_DisplayID,    mode_id,
                    SA_Width,        (uint32)scr_w,
                    SA_Height,       (uint32)scr_h,
                    SA_Depth,        (uint32)scr_d,
                    SA_Pens,         (Tag)pens_copy,
                    SA_Colors32,     (Tag)colors32,
                    SA_DetailPen,    detail_pen,
                    SA_BlockPen,     block_pen,
                    SA_SysFont,      1,
                    SA_Title,        (Tag)title,
                    SA_PubName,      (Tag)pubname,
                    SA_Type,         PUBLICSCREEN,
                    SA_SharePens,    TRUE,
                    SA_Draggable,    TRUE,
                    SA_Compositing,  TRUE,
                    (app->prefs.dark_theme == 1) ? GUIA_WindowBarBackfill : TAG_IGNORE, (Tag)&s_dark_winbar_active_hook,
                    (app->prefs.dark_theme == 1) ? GUIA_WindowBarInactBackfill : TAG_IGNORE, (Tag)&s_dark_winbar_inact_hook,
                    (app->prefs.dark_theme == 1) ? GUIA_WindowBorderBackfill : TAG_IGNORE, (Tag)&s_dark_winbar_active_hook,
                    (app->prefs.dark_theme == 1) ? GUIA_WindowBorderInactBackfill : TAG_IGNORE, (Tag)&s_dark_winbar_inact_hook,
                    (app->prefs.dark_theme == 1) ? GUIA_ScreenBarBackfill : TAG_IGNORE, (Tag)&s_dark_screenbar_hook,
                    (app->prefs.dark_theme == 1) ? GUIA_WindowBorderLook : TAG_IGNORE, (Tag)FRAMEWINDOW_FLAT,
                    (app->prefs.dark_theme == 1) ? GUIA_WindowBarFrameThickness : TAG_IGNORE, 1,
                    (app->prefs.dark_theme == 1) ? GUIA_WindowFrameThickness : TAG_IGNORE, 1,
                    (app->prefs.dark_theme == 1) ? GUIA_TabTitleBackfill : TAG_IGNORE, (Tag)&s_dark_winbar_active_hook,
                    (app->prefs.dark_theme == 1) ? GUIA_TabActiveBackfill : TAG_IGNORE, (Tag)&s_dark_backfill_hook,
                    (app->prefs.dark_theme == 1) ? GUIA_TabInactiveBackfill : TAG_IGNORE, (Tag)&s_dark_winbar_inact_hook,
                    (app->prefs.dark_theme == 1) ? GUIA_ClickTabLook : TAG_IGNORE, (Tag)(CLICKTAB_ACTIVEFILLSELECT | CLICKTAB_ACTIVEBOLD | CLICKTAB_INACTIVEDARK),
                    TAG_DONE);

                if (!app->custom_screen) {
                    /* Retry without SA_Compositing in case driver rejects it */
                    app->custom_screen = IIntuition->OpenScreenTags(NULL,
                        (is_clone_wb && app->prefs.dark_theme != 1) ? SA_LikeWorkbench : TAG_IGNORE, TRUE,
                        SA_DisplayID,    mode_id,
                        SA_Width,        (uint32)scr_w,
                        SA_Height,       (uint32)scr_h,
                        SA_Depth,        (uint32)scr_d,
                        SA_Pens,         (Tag)pens_copy,
                        SA_Colors32,     (Tag)colors32,
                        SA_DetailPen,    detail_pen,
                        SA_BlockPen,     block_pen,
                        SA_SysFont,      1,
                        SA_Title,        (Tag)title,
                        SA_PubName,      (Tag)pubname,
                        SA_Type,         PUBLICSCREEN,
                        SA_SharePens,    TRUE,
                        SA_Draggable,    TRUE,
                        (app->prefs.dark_theme == 1) ? GUIA_WindowBarBackfill : TAG_IGNORE, (Tag)&s_dark_winbar_active_hook,
                        (app->prefs.dark_theme == 1) ? GUIA_WindowBarInactBackfill : TAG_IGNORE, (Tag)&s_dark_winbar_inact_hook,
                        (app->prefs.dark_theme == 1) ? GUIA_WindowBorderBackfill : TAG_IGNORE, (Tag)&s_dark_winbar_active_hook,
                        (app->prefs.dark_theme == 1) ? GUIA_WindowBorderInactBackfill : TAG_IGNORE, (Tag)&s_dark_winbar_inact_hook,
                        (app->prefs.dark_theme == 1) ? GUIA_ScreenBarBackfill : TAG_IGNORE, (Tag)&s_dark_screenbar_hook,
                        (app->prefs.dark_theme == 1) ? GUIA_WindowBorderLook : TAG_IGNORE, (Tag)FRAMEWINDOW_FLAT,
                        (app->prefs.dark_theme == 1) ? GUIA_WindowBarFrameThickness : TAG_IGNORE, 1,
                        (app->prefs.dark_theme == 1) ? GUIA_WindowFrameThickness : TAG_IGNORE, 1,
                        (app->prefs.dark_theme == 1) ? GUIA_TabTitleBackfill : TAG_IGNORE, (Tag)&s_dark_winbar_active_hook,
                        (app->prefs.dark_theme == 1) ? GUIA_TabActiveBackfill : TAG_IGNORE, (Tag)&s_dark_backfill_hook,
                        (app->prefs.dark_theme == 1) ? GUIA_TabInactiveBackfill : TAG_IGNORE, (Tag)&s_dark_winbar_inact_hook,
                        (app->prefs.dark_theme == 1) ? GUIA_ClickTabLook : TAG_IGNORE, (Tag)(CLICKTAB_ACTIVEFILLSELECT | CLICKTAB_ACTIVEBOLD | CLICKTAB_INACTIVEDARK),
                        TAG_DONE);
                }
            }

            if (app->custom_screen) {
                IIntuition->PubScreenStatus(app->custom_screen, 0);
                if (app->prefs.dark_theme == 1) {
                    struct DrawInfo *dri = IIntuition->GetScreenDrawInfo(app->custom_screen);
                    if (dri) {
                        IIntuition->SetGUIAttrs(NULL, dri,
                            GUIA_WindowBarBackfill,       (Tag)&s_dark_winbar_active_hook,
                            GUIA_WindowBarInactBackfill,   (Tag)&s_dark_winbar_inact_hook,
                            GUIA_WindowBorderBackfill,     (Tag)&s_dark_winbar_active_hook,
                            GUIA_WindowBorderInactBackfill, (Tag)&s_dark_winbar_inact_hook,
                            GUIA_ScreenBarBackfill,        (Tag)&s_dark_screenbar_hook,
                            GUIA_WindowBorderLook,         (Tag)FRAMEWINDOW_FLAT,
                            GUIA_WindowBarFrameThickness,  1,
                            GUIA_WindowFrameThickness,     1,
                            GUIA_TabTitleBackfill,         (Tag)&s_dark_winbar_active_hook,
                            GUIA_TabActiveBackfill,        (Tag)&s_dark_backfill_hook,
                            GUIA_TabInactiveBackfill,      (Tag)&s_dark_winbar_inact_hook,
                            GUIA_ClickTabLook,             (Tag)(CLICKTAB_ACTIVEFILLSELECT | CLICKTAB_ACTIVEBOLD | CLICKTAB_INACTIVEDARK),
                            TAG_DONE);
                        IIntuition->FreeScreenDrawInfo(app->custom_screen, dri);
                    }
                }
                DBG_LOG("[GUI] Successfully opened custom public screen '%s' (%ux%u, %u-bit)\n",
                       pubname, (unsigned)app->custom_screen->Width, (unsigned)app->custom_screen->Height, (unsigned)scr_d);
            } else {
                DBG_LOG("[GUI] WARNING: Could not open %ux%u custom screen. Running on Workbench screen instead!\n",
                       (unsigned)scr_w, (unsigned)scr_h);
            }
        }
    } else if (app->custom_screen) {
        /* Switched back to Workbench, close custom screen */
        IIntuition->PubScreenStatus(app->custom_screen, PSNF_PRIVATE);
        IIntuition->CloseScreen(app->custom_screen);
        app->custom_screen = NULL;
    }

    struct Screen *pub_scr = app->custom_screen;
    BOOL unlock_pub_scr = FALSE;
    if (!pub_scr) {
        pub_scr = IIntuition->LockPubScreen(NULL);
        unlock_pub_scr = TRUE;
    }

    if (app->prefs.dark_theme == 1) {
        gui_alloc_dark_pens(pub_scr);
        /* Set screen-wide GUI attributes ONLY if running on our private custom screen.
         * NEVER modify the shared Workbench screen's global GUI attributes! */
        if (app->custom_screen) {
            struct DrawInfo *dri = IIntuition->GetScreenDrawInfo(app->custom_screen);
            if (dri) {
                if (!s_orig_gui_attrs_saved) {
                    s_orig_gui_screen = app->custom_screen;
                    IIntuition->GetGUIAttrs(NULL, dri,
                        GUIA_WindowBarBackfill,        &s_orig_winbar_active_hook,
                        GUIA_WindowBarInactBackfill,    &s_orig_winbar_inact_hook,
                        GUIA_WindowBorderBackfill,      &s_orig_winborder_active_hook,
                        GUIA_WindowBorderInactBackfill, &s_orig_winborder_inact_hook,
                        GUIA_ScreenBarBackfill,        &s_orig_screenbar_hook,
                        GUIA_WindowBorderLook,          &s_orig_winborder_look,
                        GUIA_WindowBarFrameThickness,  &s_orig_winbar_thick,
                        GUIA_WindowFrameThickness,     &s_orig_winframe_thick,
                        GUIA_TabTitleBackfill,          &s_orig_tab_title_hook,
                        GUIA_TabActiveBackfill,         &s_orig_tab_active_hook,
                        GUIA_TabInactiveBackfill,       &s_orig_tab_inact_hook,
                        GUIA_ClickTabLook,             &s_orig_clicktab_look,
                        TAG_DONE);
                    s_orig_gui_attrs_saved = TRUE;
                }

                IIntuition->SetGUIAttrs(NULL, dri,
                    GUIA_WindowBarBackfill,        (Tag)&s_dark_winbar_active_hook,
                    GUIA_WindowBarInactBackfill,    (Tag)&s_dark_winbar_inact_hook,
                    GUIA_WindowBorderBackfill,      (Tag)&s_dark_winbar_active_hook,
                    GUIA_WindowBorderInactBackfill, (Tag)&s_dark_winbar_inact_hook,
                    GUIA_ScreenBarBackfill,        (Tag)&s_dark_screenbar_hook,
                    GUIA_WindowBorderLook,         (Tag)FRAMEWINDOW_FLAT,
                    GUIA_WindowBarFrameThickness,  1,
                    GUIA_WindowFrameThickness,     1,
                    GUIA_TabTitleBackfill,         (Tag)&s_dark_winbar_active_hook,
                    GUIA_TabActiveBackfill,        (Tag)&s_dark_backfill_hook,
                    GUIA_TabInactiveBackfill,      (Tag)&s_dark_winbar_inact_hook,
                    GUIA_ClickTabLook,             (Tag)(CLICKTAB_ACTIVEFILLSELECT | CLICKTAB_ACTIVEBOLD | CLICKTAB_INACTIVEDARK),
                    TAG_DONE);
                IIntuition->FreeScreenDrawInfo(app->custom_screen, dri);
            }
        }
    } else {
        if (s_orig_gui_attrs_saved && s_orig_gui_screen) {
            struct DrawInfo *dri = IIntuition->GetScreenDrawInfo(s_orig_gui_screen);
            if (dri) {
                IIntuition->SetGUIAttrs(NULL, dri,
                    GUIA_WindowBarBackfill,        (Tag)s_orig_winbar_active_hook,
                    GUIA_WindowBarInactBackfill,    (Tag)s_orig_winbar_inact_hook,
                    GUIA_WindowBorderBackfill,      (Tag)s_orig_winborder_active_hook,
                    GUIA_WindowBorderInactBackfill, (Tag)s_orig_winborder_inact_hook,
                    GUIA_ScreenBarBackfill,        (Tag)s_orig_screenbar_hook,
                    GUIA_WindowBorderLook,          (Tag)s_orig_winborder_look,
                    GUIA_WindowBarFrameThickness,  (Tag)s_orig_winbar_thick,
                    GUIA_WindowFrameThickness,     (Tag)s_orig_winframe_thick,
                    GUIA_TabTitleBackfill,          (Tag)s_orig_tab_title_hook,
                    GUIA_TabActiveBackfill,         (Tag)s_orig_tab_active_hook,
                    GUIA_TabInactiveBackfill,       (Tag)s_orig_tab_inact_hook,
                    GUIA_ClickTabLook,             (Tag)s_orig_clicktab_look,
                    TAG_DONE);
                IIntuition->FreeScreenDrawInfo(s_orig_gui_screen, dri);
            }
            s_orig_gui_attrs_saved = FALSE;
            s_orig_gui_screen = NULL;
        }
        gui_free_dark_pens();
    }

    /* AISS candidate lists */
    static const char *const c_back[]    = { "arrowleft", "arrowleftcolored", "arrowleftfilled", "previous", "back", "left" };
    static const char *const c_fwd[]     = { "arrowright", "arrowrightcolored", "arrowrightfilled", "next", "forward", "right" };
    static const char *const c_reload[]  = { "reload", "refresh", "update" };
    static const char *const c_stop[]    = { "stop", "cancel" };
    static const char *const c_home[]    = { "home", "start" };
    static const char *const c_clear[]   = { "clear", "delete", "cross" };
    static const char *const c_go[]      = { "go", "arrowright", "return" };
    static const char *const c_search[]  = { "search", "find", "glass", "lens", NULL };
    static const char *const c_connect[] = { "network", "connect" };

    /* ------------------------------------------------------------------------
     * Row 1: Navigation, URL Address Bar & Web Search Bar
     * ------------------------------------------------------------------------ */
    Object *btn_back = create_toolbar_button(GID_BACK, c_back, 6, GetVString(MSG_BTN_BACK), GetVString(MSG_HINT_BACK), pub_scr);
    Object *btn_fwd = create_toolbar_button(GID_FORWARD, c_fwd, 6, GetVString(MSG_BTN_FORWARD), GetVString(MSG_HINT_FORWARD), pub_scr);
    Object *btn_reload = create_toolbar_button(GID_RELOAD, c_reload, 3, GetVString(MSG_BTN_RELOAD), GetVString(MSG_HINT_RELOAD), pub_scr);
    Object *btn_stop = create_toolbar_button(GID_STOP, c_stop, 2, GetVString(MSG_BTN_STOP), GetVString(MSG_HINT_STOP), pub_scr);
    Object *btn_home = create_toolbar_button(GID_HOME, c_home, 2, GetVString(MSG_BTN_HOME), GetVString(MSG_HINT_HOME), pub_scr);

    /* Restore saved tabs BEFORE creating URL bar so active tab URL & favicon match immediately */
    gui_tabs_init(app);

    const char *init_url_disp = !prefs_is_startpage_url(app->current_url) ? app->current_url : "";
    struct Image *fav_img = load_favicon_image(init_url_disp, pub_scr);
    app->gad_url_favicon = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID,             GID_URL_FAVICON,
        GA_RelVerify,      TRUE,
        BUTTON_BevelStyle, BVS_NONE,
        BUTTON_Transparent,TRUE,
        fav_img ? GA_Image : TAG_IGNORE, (Tag)fav_img,
        GA_HintInfo,       GetVString(MSG_HINT_FAVICON),
        TAG_DONE);

    app->gad_url = IIntuition->NewObject(NULL, "string.gadget",
        GA_ID,            GID_URL,
        GA_RelVerify,     TRUE,
        GA_TabCycle,      FALSE,
        GA_HintInfo,      GetVString(MSG_HINT_URL),
        STRINGA_MaxChars, VB_MAX_URL_LEN - 1,
        STRINGA_TextVal,  init_url_disp,
        s_dark_pens.allocated ? STRINGA_Pens : TAG_IGNORE,
            (uint32)((s_dark_pens.inp_bg_pen << 16) | (s_dark_pens.txt_pen & 0xFFFF)),
        s_dark_pens.allocated ? STRINGA_ActivePens : TAG_IGNORE,
            (uint32)((s_dark_pens.inp_bg_pen << 16) | (s_dark_pens.txt_pen & 0xFFFF)),
        TAG_DONE);

    Object *btn_clear_url = create_toolbar_button(GID_CLEAR_URL, c_clear, 3, GetVString(MSG_BTN_CLEAR), GetVString(MSG_HINT_CLEAR_URL), pub_scr);
    Object *btn_go = create_toolbar_button(GID_GO, c_go, 3, GetVString(MSG_BTN_GO), GetVString(MSG_HINT_GO), pub_scr);

    static const char *const s_search_engine_labels[] = {
        "DuckDuckGo",
        "Google",
        "Bing",
        "Startpage",
        "Ecosia",
        "Wikipedia",
        "YouTube",
        "Aminet",
        "OS4Depot",
        NULL
    };

    uint16 init_se_sel = app->prefs.search_engine;
    if (init_se_sel >= NUM_SEARCH_ENGINES) init_se_sel = 0;

    app->gad_search_chooser = IIntuition->NewObject(NULL, "chooser.gadget",
        GA_ID,              GID_SEARCH_CHOOSER,
        GA_RelVerify,       TRUE,
        CHOOSER_PopUp,      TRUE,
        CHOOSER_LabelArray, s_search_engine_labels,
        CHOOSER_Selected,   init_se_sel,
        CHOOSER_MaxLabels,  NUM_SEARCH_ENGINES,
        GA_HintInfo,        GetVString(MSG_HINT_SEARCH_CHOOSER),
        TAG_DONE);

    app->gad_search = IIntuition->NewObject(NULL, "string.gadget",
        GA_ID,            GID_SEARCH,
        GA_RelVerify,     TRUE,
        GA_TabCycle,      FALSE,
        GA_HintInfo,      GetVString(MSG_HINT_SEARCH_INPUT),
        STRINGA_MaxChars, 255,
        s_dark_pens.allocated ? STRINGA_Pens : TAG_IGNORE,
            (uint32)((s_dark_pens.inp_bg_pen << 16) | (s_dark_pens.txt_pen & 0xFFFF)),
        s_dark_pens.allocated ? STRINGA_ActivePens : TAG_IGNORE,
            (uint32)((s_dark_pens.inp_bg_pen << 16) | (s_dark_pens.txt_pen & 0xFFFF)),
        TAG_DONE);

    Object *btn_search = create_toolbar_button(GID_SEARCH_GO, c_search, 4, GetVString(MSG_BTN_SEARCH), GetVString(MSG_HINT_SEARCH_GO), pub_scr);

    app->gad_fav_chooser = IIntuition->NewObject(NULL, "chooser.gadget",
        GA_ID,             GID_FAV_CHOOSER,
        GA_RelVerify,      TRUE,
        CHOOSER_DropDown,  TRUE,
        CHOOSER_Title,     (Tag)GetVString(MSG_CHOOSER_BOOKMARKS),
        CHOOSER_Labels,    &app->fav_chooser_list,
        CHOOSER_MaxLabels, 32,
        TAG_DONE);
    const char *const c_download[] = { "download", "load", "down", "drawer", NULL };
    Object *btn_downloads = create_toolbar_button(GID_DOWNLOADS, c_download, 4, GetVString(MSG_BTN_DOWNLOADS), GetVString(MSG_HINT_DOWNLOADS), pub_scr);
    Object *btn_connect = create_toolbar_button(GID_CONNECT, c_connect, 2, GetVString(MSG_BTN_CONNECT), GetVString(MSG_HINT_CONNECT), pub_scr);

    Object *nav_row1 = IIntuition->NewObject(NULL, "layout.gadget",
        LAYOUT_Orientation,  LAYOUT_ORIENT_HORIZ,
        s_dark_pens.allocated ? LAYOUT_LayoutBackFill : TAG_IGNORE, (Tag)&s_dark_backfill_hook,
        LAYOUT_AddChild,     btn_back,
        CHILD_WeightedWidth, 0,
        LAYOUT_AddChild,     btn_fwd,
        CHILD_WeightedWidth, 0,
        LAYOUT_AddChild,     btn_reload,
        CHILD_WeightedWidth, 0,
        LAYOUT_AddChild,     btn_stop,
        CHILD_WeightedWidth, 0,
        LAYOUT_AddChild,     btn_home,
        CHILD_WeightedWidth, 0,
        LAYOUT_AddChild,     app->gad_url_favicon,
        CHILD_WeightedWidth, 0,
        LAYOUT_AddChild,     app->gad_url,
        CHILD_WeightedWidth, 60,
        CHILD_MinWidth,      40,
        LAYOUT_AddChild,     btn_clear_url,
        CHILD_WeightedWidth, 0,
        LAYOUT_AddChild,     btn_go,
        CHILD_WeightedWidth, 0,
        LAYOUT_AddChild,     app->gad_search_chooser,
        CHILD_WeightedWidth, 0,
        LAYOUT_AddChild,     app->gad_search,
        CHILD_WeightedWidth, 40,
        CHILD_MinWidth,      30,
        LAYOUT_AddChild,     btn_search,
        CHILD_WeightedWidth, 0,
        LAYOUT_AddChild,     app->gad_fav_chooser,
        CHILD_WeightedWidth, 0,
        LAYOUT_AddChild,     btn_downloads,
        CHILD_WeightedWidth, 0,
        LAYOUT_AddChild,     btn_connect,
        CHILD_WeightedWidth, 0,
        TAG_DONE);

    /* ------------------------------------------------------------------------
     * Row 2: Schnellwahl Toolbar & Tools
     * ------------------------------------------------------------------------ */
    for (int i = 0; i < app->prefs.num_quick_links && i < VB_MAX_QUICKLINKS; i++) {
        app->gad_quick_btn[i] = create_quicklink_button(
            GID_QUICK_BASE + i,
            app->prefs.quick_links[i].label,
            app->prefs.quick_links[i].url,
            app->prefs.quick_links[i].icon,
            pub_scr
        );
        app->gad_quick_del[i] = NULL;
    }
    for (int i = app->prefs.num_quick_links; i < VB_MAX_QUICKLINKS; i++) {
        app->gad_quick_btn[i] = NULL;
        app->gad_quick_del[i] = NULL;
    }

    /* Schnellwahl '+' und '-' Buttons */
    static const char *const c_add_ql[] = { "add", "plus", "new", NULL };
    app->gad_quick_add = create_toolbar_button(GID_QUICK_ADD, c_add_ql, 3, "+", GetVString(MSG_HINT_ADD_QL), pub_scr);

    static const char *const c_rem_ql[] = { "remove", "minus", "delete", NULL };
    app->gad_quick_remove = create_toolbar_button(GID_QUICK_REMOVE, c_rem_ql, 3, "-", GetVString(MSG_HINT_REM_QL), pub_scr);



    app->gad_server_chooser = NULL;


    uint16 avail_scr_w = pub_scr ? pub_scr->Width  : 1920;
    uint16 avail_scr_h = pub_scr ? pub_scr->Height : 1080;
    int16  bar_h       = pub_scr ? (int16)(pub_scr->BarHeight + 1) : 24;
    if (bar_h < 0 || bar_h > 120) bar_h = 24;

    /* Default window layout:
     * Directly below the screen's title bar (Left=0, Top=BarHeight+1) and edge-to-edge */
    int16  win_left = 0;
    int16  win_top  = bar_h;
    uint16 win_w    = avail_scr_w;
    uint16 win_h    = (avail_scr_h > (uint16)bar_h) ? (uint16)(avail_scr_h - (uint16)bar_h) : avail_scr_h;
    BOOL   restored_win_pos = FALSE;

    if ((!app->custom_screen || app->prefs.resolution.win_pos_locked) &&
        app->prefs.resolution.has_win_pos &&
        app->prefs.resolution.win_w >= 200 &&
        app->prefs.resolution.win_h >= 150) {
        win_left = app->prefs.resolution.win_x;
        win_top  = app->prefs.resolution.win_y;
        win_w    = app->prefs.resolution.win_w;
        win_h    = app->prefs.resolution.win_h;
        restored_win_pos = TRUE;

        if (win_left < 0) win_left = 0;
        if (win_top  < 0) win_top  = bar_h;
        if (win_w > avail_scr_w) win_w = avail_scr_w;
        if (win_h > avail_scr_h) win_h = avail_scr_h;
        if ((int)win_left + (int)win_w > (int)avail_scr_w) {
            win_left = (int16)((int)avail_scr_w - (int)win_w);
            if (win_left < 0) win_left = 0;
        }
        if ((int)win_top + (int)win_h > (int)avail_scr_h) {
            win_top = (int16)((int)avail_scr_h - (int)win_h);
            if (win_top < 0) win_top = 0;
        }
    }

    uint16 init_w = win_w;

    /* Multi-row wrapping for QuickLink buttons:
     * Wrap buttons across rows so they remain clean, readable, and properly aligned.
     */
    #define MAX_QL_ROWS 8
    Object *ql_row_objs[MAX_QL_ROWS] = { NULL };
    Object *row_items[MAX_QL_ROWS][VB_MAX_QUICKLINKS + 2];
    int row_item_count[MAX_QL_ROWS] = { 0 };

    int first_row_limit = (int)init_w - 460; /* Space for right-side tools if applicable */
    if (first_row_limit < 200) first_row_limit = 200;
    int next_row_limit  = (int)init_w - 40;
    if (next_row_limit < 200) next_row_limit = 200;

    int cur_row = 0;
    int cur_w   = 0;
    int cur_limit = first_row_limit;

    for (int i = 0; i < app->prefs.num_quick_links && i < VB_MAX_QUICKLINKS; i++) {
        if (!app->gad_quick_btn[i]) continue;

        int label_len = (int)strlen(app->prefs.quick_links[i].label);
        int item_w = label_len * 8 + 32;
        if (item_w < 50) item_w = 50;

        /* Wrap into next row if width limit is reached */
        if (row_item_count[cur_row] > 0 && (cur_w + item_w > cur_limit) && cur_row < MAX_QL_ROWS - 1) {
            cur_row++;
            cur_w = 0;
            cur_limit = next_row_limit;
        }

        row_items[cur_row][row_item_count[cur_row]++] = app->gad_quick_btn[i];
        cur_w += item_w + 6;
    }

    if (app->gad_quick_add) {
        row_items[cur_row][row_item_count[cur_row]++] = app->gad_quick_add;
    }
    if (app->gad_quick_remove) {
        row_items[cur_row][row_item_count[cur_row]++] = app->gad_quick_remove;
    }

    int ql_row_count = (row_item_count[0] > 0) ? (cur_row + 1) : 0;

    for (int r = 0; r < ql_row_count; r++) {
        struct TagItem row_tags[128];
        int t = 0;
        row_tags[t].ti_Tag = LAYOUT_Orientation;
        row_tags[t++].ti_Data = LAYOUT_ORIENT_HORIZ;
        row_tags[t].ti_Tag = LAYOUT_SpaceInner;
        row_tags[t++].ti_Data = TRUE;
        row_tags[t].ti_Tag = LAYOUT_SpaceOuter;
        row_tags[t++].ti_Data = FALSE;

        for (int it = 0; it < row_item_count[r]; it++) {
            row_tags[t].ti_Tag = LAYOUT_AddChild;
            row_tags[t++].ti_Data = (Tag)row_items[r][it];
            row_tags[t].ti_Tag = CHILD_WeightedWidth;
            row_tags[t++].ti_Data = 0;
        }

        /* Trailing spacer so items remain left-aligned */
        Object *sp = IIntuition->NewObject(NULL, "space.gadget",
            SPACE_Transparent, FALSE,
            SPACE_MinWidth,    2,
            s_dark_pens.allocated ? GA_BackFill : TAG_IGNORE,      (Tag)&s_dark_backfill_hook,
            s_dark_pens.allocated ? SPACE_RenderHook : TAG_IGNORE, (Tag)&s_space_dark_render_hook,
            TAG_DONE);
        if (sp) {
            row_tags[t].ti_Tag = LAYOUT_AddChild;
            row_tags[t++].ti_Data = (Tag)sp;
            row_tags[t].ti_Tag = CHILD_WeightedWidth;
            row_tags[t++].ti_Data = 100;
        }

        if (s_dark_pens.allocated) {
            row_tags[t].ti_Tag = LAYOUT_LayoutBackFill;
            row_tags[t++].ti_Data = (Tag)&s_dark_backfill_hook;
        }

        row_tags[t].ti_Tag = TAG_DONE;
        row_tags[t++].ti_Data = 0;

        ql_row_objs[r] = IIntuition->NewObjectA(NULL, "layout.gadget", row_tags);
    }


    /* ------------------------------------------------------------------------
     * Main interactive browser viewport (space.gadget)
     * ------------------------------------------------------------------------ */
    app->gad_space = IIntuition->NewObject(NULL, "space.gadget",
        GA_ID,             GID_SPACE,
        GA_ReadOnly,       TRUE,
        GA_FollowMouse,    TRUE,
        SPACE_MinWidth,    200,
        SPACE_MinHeight,   150,
        SPACE_Transparent, TRUE,
        SPACE_BevelStyle,  BVS_NONE,
        SPACE_RenderHook,  &s_render_hook,
        TAG_DONE);

    /* ------------------------------------------------------------------------
     * Bottom status bar & download progress gauge (Optional, default OFF)
     * ------------------------------------------------------------------------ */
    Object *status_group = NULL;
    if (app->prefs.show_status_bar) {
        app->gad_status = IIntuition->NewObject(NULL, "button.gadget",
            GA_ID,                GID_STATUS,
            GA_ReadOnly,          TRUE,
            GA_Text,              app->status_text,
            BUTTON_Justification, BCJ_LEFT,
            BUTTON_BevelStyle,    BVS_THIN,
            s_dark_pens.allocated ? BUTTON_BackgroundPen : TAG_IGNORE, (uint32)s_dark_pens.bg_pen,
            s_dark_pens.allocated ? BUTTON_TextPen : TAG_IGNORE,       (uint32)s_dark_pens.txt_pen,
            TAG_DONE);

        app->gad_gauge = IIntuition->NewObject(NULL, "fuelgauge.gadget",
            GA_ID,                 GID_GAUGE,
            FUELGAUGE_Min,         0,
            FUELGAUGE_Max,         100,
            FUELGAUGE_Level,       0,
            FUELGAUGE_Percent,     TRUE,
            FUELGAUGE_Ticks,       0,
            FUELGAUGE_Orientation, FGORIENT_HORIZ,
            s_dark_pens.allocated ? GA_BackFill : TAG_IGNORE,          (Tag)&s_dark_backfill_hook,
            s_dark_pens.allocated ? FUELGAUGE_EmptyPen : TAG_IGNORE,   (uint32)s_dark_pens.inp_bg_pen,
            s_dark_pens.allocated ? FUELGAUGE_FillPen : TAG_IGNORE,    (uint32)s_dark_pens.gauge_fill_pen,
            s_dark_pens.allocated ? FUELGAUGE_PercentPen : TAG_IGNORE, (uint32)s_dark_pens.txt_pen,
            TAG_DONE);

        status_group = IIntuition->NewObject(NULL, "layout.gadget",
            LAYOUT_Orientation,  LAYOUT_ORIENT_HORIZ,
            s_dark_pens.allocated ? LAYOUT_LayoutBackFill : TAG_IGNORE, (Tag)&s_dark_backfill_hook,
            LAYOUT_SpaceInner,   FALSE,
            LAYOUT_SpaceOuter,   FALSE,
            LAYOUT_AddChild,     app->gad_status,
            CHILD_WeightedWidth, 80,
            LAYOUT_AddChild,     app->gad_gauge,
            CHILD_WeightedWidth, 20,
            CHILD_MaxWidth,      120,
            TAG_DONE);
    } else {
        app->gad_status = NULL;
        app->gad_gauge  = NULL;
    }

    /* ------------------------------------------------------------------------
     * Tabs (clicktab.gadget) — tab_list already built by gui_tabs_init() above
     * ------------------------------------------------------------------------ */
    Object *tab_close_img = gui_create_tab_close_image(pub_scr);

    app->gad_clicktab = IIntuition->NewObject(NULL, "clicktab.gadget",
        GA_ID,                  GID_CLICKTAB,
        GA_RelVerify,           TRUE,
        CLICKTAB_Labels,        (Tag)&app->tab_list,
        CLICKTAB_Current,       (uint32)app->active_tab_idx,
        CLICKTAB_AutoFit,       TRUE,
        CLICKTAB_LabelTruncate, TRUE,
        CLICKTAB_EvenSize,      FALSE,
        CLICKTAB_PageGroupBorder, FALSE,
        tab_close_img ? CLICKTAB_CloseImage : TAG_IGNORE,     (Tag)tab_close_img,
        tab_close_img ? CLICKTAB_ClosePlacement : TAG_IGNORE, PLACECLOSE_LEFT,
        s_dark_pens.allocated ? CLICKTAB_PageGroupBackFill : TAG_IGNORE, (Tag)&s_dark_backfill_hook,
        s_dark_pens.allocated ? GA_BackFill : TAG_IGNORE, (Tag)&s_dark_backfill_hook,
        TAG_DONE);

    const char *c_add_tab[] = { "add", "plus", "new" };
    app->gad_tab_new = create_toolbar_button(GID_TAB_NEW, c_add_tab, 3, "+", GetVString(MSG_HINT_NEW_TAB), pub_scr);
    app->gad_tab_close = NULL;

    Object *tab_row = IIntuition->NewObject(NULL, "layout.gadget",
        LAYOUT_Orientation,  LAYOUT_ORIENT_HORIZ,
        s_dark_pens.allocated ? LAYOUT_LayoutBackFill : TAG_IGNORE, (Tag)&s_dark_backfill_hook,
        LAYOUT_SpaceInner,   FALSE,
        LAYOUT_SpaceOuter,   FALSE,
        LAYOUT_AddChild,     app->gad_clicktab,
        CHILD_WeightedWidth, 100,
        CHILD_MinWidth,      60,
        LAYOUT_AddChild,     app->gad_tab_new,
        CHILD_WeightedWidth, 0,
        TAG_DONE);

    struct TagItem root_tags[48];
    int rt = 0;
    root_tags[rt].ti_Tag = LAYOUT_Orientation;
    root_tags[rt++].ti_Data = LAYOUT_ORIENT_VERT;
    root_tags[rt].ti_Tag = LAYOUT_SpaceOuter;
    root_tags[rt++].ti_Data = TRUE;
    root_tags[rt].ti_Tag = LAYOUT_DeferLayout;
    root_tags[rt++].ti_Data = FALSE;

    /* Row 1: Navigation, URL Address Bar & Web Search Bar */
    root_tags[rt].ti_Tag = LAYOUT_AddChild;
    root_tags[rt++].ti_Data = (Tag)nav_row1;
    root_tags[rt].ti_Tag = CHILD_WeightedHeight;
    root_tags[rt++].ti_Data = 0;

    /* Row 2: Schnellwahl (Bookmarks Toolbar) rows */
    for (int r = 0; r < ql_row_count; r++) {
        if (ql_row_objs[r]) {
            root_tags[rt].ti_Tag = LAYOUT_AddChild;
            root_tags[rt++].ti_Data = (Tag)ql_row_objs[r];
            root_tags[rt].ti_Tag = CHILD_WeightedHeight;
            root_tags[rt++].ti_Data = 0;
        }
    }

    root_tags[rt].ti_Tag = LAYOUT_AddChild;
    root_tags[rt++].ti_Data = (Tag)tab_row;
    root_tags[rt].ti_Tag = CHILD_WeightedHeight;
    root_tags[rt++].ti_Data = 0;

    root_tags[rt].ti_Tag = LAYOUT_AddChild;
    root_tags[rt++].ti_Data = (Tag)app->gad_space;
    root_tags[rt].ti_Tag = CHILD_WeightedHeight;
    root_tags[rt++].ti_Data = 100;

    if (status_group) {
        root_tags[rt].ti_Tag = LAYOUT_AddChild;
        root_tags[rt++].ti_Data = (Tag)status_group;
        root_tags[rt].ti_Tag = CHILD_WeightedHeight;
        root_tags[rt++].ti_Data = 0;
    }

    if (s_dark_pens.allocated) {
        root_tags[rt].ti_Tag = LAYOUT_LayoutBackFill;
        root_tags[rt++].ti_Data = (Tag)&s_dark_backfill_hook;
    }

    root_tags[rt].ti_Tag = TAG_DONE;
    root_tags[rt++].ti_Data = 0;

    Object *root_layout = IIntuition->NewObjectA(NULL, "layout.gadget", root_tags);

    if (unlock_pub_scr && pub_scr) {
        IIntuition->UnlockPubScreen(NULL, pub_scr);
        pub_scr = NULL;
    }

    if (!root_layout) {
        DBG_LOG("[GUI] Failed to create Reaction root layout!\n");
        if (app->custom_screen) {
            IIntuition->PubScreenStatus(app->custom_screen, PSNF_PRIVATE);
            IIntuition->CloseScreen(app->custom_screen);
            app->custom_screen = NULL;
        }
        return FALSE;
    }

    gui_localize_menu();
    snprintf(app->win_title, sizeof(app->win_title), "%s", GetVString(MSG_WIN_TITLE));

    /* Pass a fresh DiskObject owned by window.class (no WINDOW_IconNoDispose), matching OS4 SDK examples */
    struct DiskObject *win_icon = (app->app_port != NULL) ? load_app_icon() : NULL;

    app->win_obj = IIntuition->NewObject(NULL, "window.class",
        WA_Title,             app->win_title,
        (app->custom_screen != NULL) ? WA_ScreenTitle : TAG_IGNORE,
        (app->custom_screen != NULL) ? (Tag)"VortexRemoteBrowser 1.1 (AmigaOS 4.1)" : 0,
        s_dark_pens.allocated ? WA_BackFill : TAG_IGNORE,  (Tag)&s_dark_backfill_hook,
        s_dark_pens.allocated ? WA_DetailPen : TAG_IGNORE, (uint32)s_dark_pens.txt_pen,
        s_dark_pens.allocated ? WA_BlockPen : TAG_IGNORE,  (uint32)s_dark_pens.btn_bg_pen,
        WA_Activate,          TRUE,
        WA_DragBar,           TRUE,
        WA_DepthGadget,       TRUE,
        WA_CloseGadget,       TRUE,
        WA_SizeGadget,        TRUE,
        WA_SmartRefresh,      TRUE,
        WA_NewLookMenus,      TRUE,
        WA_AutoAdjust,        TRUE,
        WA_ReportMouse,       TRUE,
        WA_MouseQueue,        1,
        WA_RMBTrap,           FALSE,
        WA_Left,              (uint32)win_left,
        WA_Top,               (uint32)win_top,
        WA_Width,             (uint32)win_w,
        WA_Height,            (uint32)win_h,
        WA_MaxWidth,          ~0UL,
        WA_MaxHeight,         ~0UL,
        WA_IDCMP,             IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_GADGETDOWN |
                              IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS |
                              IDCMP_MOUSEMOVE | IDCMP_EXTENDEDMOUSE | IDCMP_NEWSIZE |
                              IDCMP_CHANGEWINDOW | IDCMP_ACTIVEWINDOW | IDCMP_INACTIVEWINDOW |
                              IDCMP_MENUPICK,
        (app->custom_screen != NULL) ? WA_CustomScreen : WA_PubScreenName,
        (app->custom_screen != NULL) ? (Tag)app->custom_screen : (Tag)"Workbench",
        WINDOW_ParentGroup,     root_layout,
        WINDOW_NewMenu,         s_main_menu,
        (app->app_port != NULL) ? WINDOW_AppPort       : TAG_IGNORE, (Tag)app->app_port,
        (app->app_port != NULL) ? WINDOW_Iconifiable   : TAG_IGNORE, TRUE,
        (app->app_port != NULL) ? WINDOW_IconifyGadget : TAG_IGNORE, TRUE,
        (app->app_port != NULL) ? WINDOW_IconTitle     : TAG_IGNORE, (Tag)"VortexRemoteBrowser",
        (app->app_port != NULL && win_icon != NULL) ? WINDOW_Icon : TAG_IGNORE, (Tag)win_icon,
        WINDOW_IDCMPHook,       &s_idcmp_hook,
        WINDOW_IDCMPHookBits,   IDCMP_MOUSEBUTTONS | IDCMP_EXTENDEDMOUSE | IDCMP_MOUSEMOVE |
                                IDCMP_CHANGEWINDOW | IDCMP_ACTIVEWINDOW | IDCMP_INACTIVEWINDOW,
        (app->prefs.dark_theme == 1) ? WINDOW_PostRefreshHook : TAG_IGNORE, (Tag)&s_post_refresh_hook,
        TAG_DONE);

    if (!app->win_obj) {
        DBG_LOG("[GUI] Failed to create Reaction window.class object!\n");
        if (win_icon && IIcon) {
            IIcon->FreeDiskObject(win_icon);
        }
        IIntuition->DisposeObject(root_layout);
        if (app->custom_screen) {
            IIntuition->PubScreenStatus(app->custom_screen, PSNF_PRIVATE);
            IIntuition->CloseScreen(app->custom_screen);
            app->custom_screen = NULL;
        }
        return FALSE;
    }

    app->win = (struct Window *)IIntuition->IDoMethod(app->win_obj, WM_OPEN);
    if (!app->win && app->custom_screen) {
        DBG_LOG("[GUI] WM_OPEN failed on custom screen; falling back to Workbench screen!\n");
        restored_win_pos = FALSE;
        IIntuition->PubScreenStatus(app->custom_screen, PSNF_PRIVATE);
        IIntuition->CloseScreen(app->custom_screen);
        app->custom_screen = NULL;
        app->prefs.resolution.use_custom_screen = FALSE;
        app->prefs.resolution.screen_mode = VB_SCREEN_WORKBENCH;
        IIntuition->SetAttrs(app->win_obj,
            WA_CustomScreen,  (Tag)NULL,
            WA_PubScreenName, (Tag)"Workbench",
            WA_InnerWidth,    1024,
            WA_InnerHeight,   700,
            TAG_DONE);
        app->win = (struct Window *)IIntuition->IDoMethod(app->win_obj, WM_OPEN);
    }
    if (!app->win) {
        DBG_LOG("[GUI] Failed to open Intuition window!\n");
        IIntuition->DisposeObject(app->win_obj);
        app->win_obj = NULL;
        if (app->custom_screen) {
            IIntuition->PubScreenStatus(app->custom_screen, PSNF_PRIVATE);
            IIntuition->CloseScreen(app->custom_screen);
            app->custom_screen = NULL;
        }
        return FALSE;
    }

    if (app->win) {
        /* Allow window to be resized freely down to 200x150, overriding layout GM_DOMAIN min constraints */
        IIntuition->WindowLimits(app->win, 200, 150, 0, 0);
        if (restored_win_pos &&
            (app->win->LeftEdge != win_left || app->win->TopEdge != win_top ||
             app->win->Width != win_w || app->win->Height != win_h)) {
            IIntuition->ChangeWindowBox(app->win, win_left, win_top, (int16)win_w, (int16)win_h);
        }

        if (app->prefs.dark_theme == 1) {
            gui_draw_dark_titlebar(app);
        }
        IIntuition->RefreshWindowFrame(app->win);
        if (app->prefs.dark_theme == 1) {
            gui_draw_dark_titlebar(app);
        }
        IIntuition->SetMouseQueue(app->win, 1);
        s_current_rmb_trap = FALSE;
    }

    if (app->custom_screen) {
        IIntuition->ScreenToFront(app->custom_screen);
    }

    /* Perform immediate initial Reaction layout pass */
    IIntuition->IDoMethod(app->win_obj, WM_RETHINK);

    /* Clean initial tab bar layout & rendering inside open window */
    gui_tabs_rebuild(app);

    /* If a decoded frame already exists in memory (rebuild/reopen), paint it immediately */
    if (app->win && app->gad_space && app->renderer.has_frame) {
        struct IBox *area = NULL;
        IIntuition->GetAttr(SPACE_AreaBox, app->gad_space, (uint32 *)&area);
        if (area && area->Width > 2 && area->Height > 2) {
            renderer_present(app, app->win->RPort, area);
        }
    }

    DBG_LOG("[GUI] Window opened (%dx%d).\n",
           (int)app->win->Width, (int)app->win->Height);

    /* Immediately sync inner rendering viewport dimensions to server */
    renderer_sync_viewport_size(app);

    return TRUE;
}

void gui_cleanup_window(VortexApp *app)
{
    if (!app) return;

    prefs_capture_window_box(app);

    if (app->pw_win_obj) {
        gui_close_password_manager(app);
    }
    if (app->dl_win_obj) {
        gui_close_download_manager(app);
    }

    if (app->gad_clicktab) {
        IIntuition->SetAttrs(app->gad_clicktab, CLICKTAB_Labels, (Tag)NULL, TAG_DONE);
    }
    if (app->gad_fav_chooser) {
        IIntuition->SetAttrs(app->gad_fav_chooser, CHOOSER_Labels, (Tag)NULL, TAG_DONE);
    }
    app->gad_clicktab  = NULL;
    app->gad_tab_new   = NULL;
    app->gad_tab_close = NULL;

    if (app->win_obj) {
        IIntuition->DisposeObject(app->win_obj);
        app->win_obj      = NULL;
        app->win          = NULL;
        app->is_iconified = FALSE;
    }
    gui_free_tab_close_image();
    if (IClickTab) {
        IClickTab->FreeClickTabList(&app->tab_list);
    }
    for (int i = 0; i < s_num_aiss_images; i++) {
        if (s_aiss_images[i]) {
            IIntuition->DisposeObject(s_aiss_images[i]);
            s_aiss_images[i] = NULL;
        }
    }
    s_num_aiss_images = 0;
    for (int i = 0; i < s_num_aiss_paths; i++) {
        if (s_aiss_paths[i]) {
            free(s_aiss_paths[i]);
            s_aiss_paths[i] = NULL;
        }
    }
    s_num_aiss_paths = 0;
    favorites_free_chooser_list(app);

    for (int i = 0; i < VB_MAX_QUICKLINKS; i++) {
        app->gad_quick_btn[i] = NULL;
        app->gad_quick_del[i] = NULL;
    }
    app->gad_quick_add = NULL;
    app->gad_quick_remove = NULL;
    app->gad_search_chooser = NULL;
    app->gad_space     = NULL;
    app->gad_status    = NULL;
    app->gad_gauge     = NULL;

    gui_free_dark_pens();
}

static void gui_remove_wb_app_icon(VortexApp *app)
{
    if (!app) return;

    if (app->wb_app_icon && IWorkbench) {
        IWorkbench->RemoveAppIcon(app->wb_app_icon);
        app->wb_app_icon = NULL;
    }
    if (app->wb_icon_dobj && IIcon) {
        IIcon->FreeDiskObject(app->wb_icon_dobj);
        app->wb_icon_dobj = NULL;
    }
    if (app->app_port) {
        struct Message *msg;
        while ((msg = IExec->GetMsg(app->app_port)) != NULL) {
            IExec->ReplyMsg(msg);
        }
    }
}

static void gui_restore_custom_screen_attrs(void)
{
    /* Restore original screen GUI attributes if modified */
    if (s_orig_gui_attrs_saved && s_orig_gui_screen) {
        struct DrawInfo *dri = IIntuition->GetScreenDrawInfo(s_orig_gui_screen);
        if (dri) {
            IIntuition->SetGUIAttrs(NULL, dri,
                GUIA_WindowBarBackfill,        (Tag)s_orig_winbar_active_hook,
                GUIA_WindowBarInactBackfill,    (Tag)s_orig_winbar_inact_hook,
                GUIA_WindowBorderBackfill,      (Tag)s_orig_winborder_active_hook,
                GUIA_WindowBorderInactBackfill, (Tag)s_orig_winborder_inact_hook,
                GUIA_ScreenBarBackfill,        (Tag)s_orig_screenbar_hook,
                GUIA_WindowBorderLook,          (Tag)s_orig_winborder_look,
                GUIA_WindowBarFrameThickness,  (Tag)s_orig_winbar_thick,
                GUIA_WindowFrameThickness,     (Tag)s_orig_winframe_thick,
                GUIA_TabTitleBackfill,          (Tag)s_orig_tab_title_hook,
                GUIA_TabActiveBackfill,         (Tag)s_orig_tab_active_hook,
                GUIA_TabInactiveBackfill,       (Tag)s_orig_tab_inact_hook,
                GUIA_ClickTabLook,             (Tag)s_orig_clicktab_look,
                TAG_DONE);
            IIntuition->FreeScreenDrawInfo(s_orig_gui_screen, dri);
        }
        s_orig_gui_attrs_saved = FALSE;
        s_orig_gui_screen = NULL;
    }
}

static BOOL gui_close_custom_screen(VortexApp *app, int max_attempts)
{
    if (!app) return TRUE;

    gui_restore_custom_screen_attrs();

    if (!app->custom_screen) {
        return TRUE;
    }

    /* Take public screen private and switch display to Workbench BEFORE calling CloseScreen()
     * so ScreenNotify/AmiDock/follow-front-screen helpers release any visitor windows first. */
    IIntuition->PubScreenStatus(app->custom_screen, PSNF_PRIVATE);
    IIntuition->ScreenToBack(app->custom_screen);
    IIntuition->WBenchToFront();

    if (max_attempts < 1) max_attempts = 1;
    for (int attempt = 0; attempt < max_attempts; attempt++) {
        IIntuition->PubScreenStatus(app->custom_screen, PSNF_PRIVATE);
        if (IIntuition->CloseScreen(app->custom_screen)) {
            app->custom_screen = NULL;
            return TRUE;
        }
        if (attempt + 1 < max_attempts && IDOS) {
            IDOS->Delay(2);
        }
    }

    /* Keep PSNF_PRIVATE set so no new visitor windows can open while closing/iconified */
    return FALSE;
}

void gui_cleanup(VortexApp *app)
{
    if (!app) return;

    gui_remove_wb_app_icon(app);
    gui_cleanup_window(app);
    gui_close_custom_screen(app, 10);

    if (app->app_port) {
        IExec->FreeSysObject(ASOT_PORT, app->app_port);
        app->app_port = NULL;
    }
    if (app->mouse_sig_mask != 0 && app->mouse_sig_bit != -1) {
        IExec->FreeSignal(app->mouse_sig_bit);
        app->mouse_sig_bit = -1;
        app->mouse_sig_mask = 0;
    }
}

void gui_rebuild_window(VortexApp *app)
{
    if (!app) return;

    DBG_LOG("[GUI] Rebuilding window layout on existing screen (no screen close)...\n");

    /* 1. Close ONLY the window and Reaction gadgets, KEEPING app->custom_screen OPEN */
    gui_cleanup_window(app);

    /* 2. Re-create window & widgets on existing screen */
    if (!gui_init(app)) {
        DBG_LOG("[GUI] Failed to rebuild window!\n");
        return;
    }

    /* 3. Rebuild chooser lists and apply preferences */
    favorites_rebuild_chooser_list(app);
    prefs_apply(app);

    renderer_sync_viewport_size(app);
    if (app->connected) {
        net_send_url(app, "@REFRESH");
    }
    if (app->win && app->gad_space && app->renderer.has_frame) {
        struct IBox *area = NULL;
        IIntuition->GetAttr(SPACE_AreaBox, app->gad_space, (uint32 *)&area);
        if (area) {
            renderer_present(app, app->win->RPort, area);
        }
    }
}

void gui_reopen_screen(VortexApp *app)
{
    if (!app) return;

    DBG_LOG("[GUI] Switching screen mode to %s...\n",
           app->prefs.resolution.use_custom_screen ? "custom public screen" : "Workbench");

    /* 1. Free renderer offscreen bitmaps and buffers */
    renderer_cleanup(app);

    /* 2. Close Intuition window, dispose Reaction objects, close custom screen */
    gui_cleanup(app);

    /* 3. Re-initialize GUI on new screen */
    if (!gui_init(app)) {
        DBG_LOG("[GUI] Failed to initialize GUI on requested screen! Falling back to Workbench...\n");
        app->prefs.resolution.use_custom_screen = FALSE;
        if (!gui_init(app)) {
            DBG_LOG("[GUI] Fatal: Could not initialize GUI on Workbench fallback!\n");
            app->running = FALSE;
            return;
        }
    }

    /* 4. Re-initialize renderer */
    renderer_init(app);

    /* 5. Rebuild chooser lists and apply preferences */
    favorites_rebuild_chooser_list(app);
    prefs_apply(app);

    /* 6. Sync viewport size and redraw */
    renderer_sync_viewport_size(app);
    if (app->connected) {
        net_send_url(app, "@REFRESH");
    }
    if (app->win && app->gad_space && app->renderer.has_frame) {
        struct IBox *area = NULL;
        IIntuition->GetAttr(SPACE_AreaBox, app->gad_space, (uint32 *)&area);
        if (area) {
            renderer_present(app, app->win->RPort, area);
        }
    }

    app_set_status(GetVString(MSG_STATUS_SCREEN_ACTIVE),
                   app->prefs.resolution.use_custom_screen ? GetVString(MSG_STATUS_SCREEN_CUSTOM) : "Workbench",
                   (unsigned)app->prefs.resolution.width,
                   (unsigned)app->prefs.resolution.height);
}

uint32 gui_get_signal_mask(VortexApp *app)
{
    uint32 sigmask = app->mouse_sig_mask;
    if (app->win_obj) {
        uint32 win_sig = 0;
        IIntuition->GetAttr(WINDOW_SigMask, app->win_obj, &win_sig);
        sigmask |= win_sig;
    }
    if (app->app_port) {
        sigmask |= (1UL << app->app_port->mp_SigBit);
    }
    if (app->applib_port) {
        sigmask |= (1UL << app->applib_port->mp_SigBit);
    }
    if (app->cx_port) {
        sigmask |= (1UL << app->cx_port->mp_SigBit);
    }
    if (app->pw_win_obj) {
        uint32 pw_sig = 0;
        IIntuition->GetAttr(WINDOW_SigMask, app->pw_win_obj, &pw_sig);
        sigmask |= pw_sig;
    }
    if (app->dl_win_obj) {
        uint32 dl_sig = 0;
        IIntuition->GetAttr(WINDOW_SigMask, app->dl_win_obj, &dl_sig);
        sigmask |= dl_sig;
    }
    return sigmask;
}

void prompt_add_quicklink(VortexApp *app)
{
    if (!app) return;

    char url_buf[VB_MAX_URL_LEN];
    get_string_gadget_text(app->gad_url, url_buf, sizeof(url_buf));
    const char *u = (url_buf[0] != '\0') ? url_buf : app->current_url;

    if (!u || u[0] == '\0') {
        gui_prefs_open(app);
        return;
    }

    char label[32];
    const char *p = u;
    if (strncmp(p, "http://", 7) == 0) p += 7;
    else if (strncmp(p, "https://", 8) == 0) p += 8;
    if (strncmp(p, "www.", 4) == 0) p += 4;
    strncpy(label, p, sizeof(label) - 1);
    label[sizeof(label) - 1] = '\0';
    char *slash = strchr(label, '/');
    if (slash) *slash = '\0';
    if (label[0] == '\0') strncpy(label, GetVString(MSG_TAB_FALLBACK_LINK), sizeof(label) - 1);

    struct EasyStruct es = {
        sizeof(struct EasyStruct),
        0,
        (STRPTR)GetVString(MSG_QL_ADD_REQ_TITLE),
        (STRPTR)GetVString(MSG_QL_ADD_REQ_TEXT),
        (STRPTR)GetVString(MSG_QL_ADD_REQ_BTNS)
    };

    LONG res = IIntuition->EasyRequest(app->win, &es, NULL, label, u);
    if (res == 1) {
        if (prefs_quicklink_add(app, label, u, "bookmark")) {
            prefs_save(app, TRUE);
            app_set_status(GetVString(MSG_STATUS_QL_ADDED), label);
            app->reopen_gui_needed = TRUE;
        } else {
            app_set_status(GetVString(MSG_STATUS_QL_FULL), VB_MAX_QUICKLINKS);
        }
    } else if (res == 2) {
        gui_prefs_open(app);
    }
}

static void remove_current_quicklink(VortexApp *app)
{
    if (!app || app->prefs.num_quick_links <= 0) {
        app_set_status(GetVString(MSG_STATUS_QL_NONE));
        return;
    }

    int target_idx = -1;

    char url_buf[VB_MAX_URL_LEN];
    get_string_gadget_text(app->gad_url, url_buf, sizeof(url_buf));
    const char *cur = (url_buf[0] != '\0') ? url_buf : app->current_url;

    if (cur && cur[0] != '\0') {
        for (int i = 0; i < app->prefs.num_quick_links; i++) {
            if (strcasecmp(app->prefs.quick_links[i].url, cur) == 0) {
                target_idx = i;
                break;
            }
        }
    }

    if (target_idx < 0 && app->active_quicklink_idx >= 0 && app->active_quicklink_idx < app->prefs.num_quick_links) {
        target_idx = app->active_quicklink_idx;
    }

    if (target_idx < 0) {
        target_idx = app->prefs.num_quick_links - 1;
    }

    char rem_name[80];
    snprintf(rem_name, sizeof(rem_name), "%s", app->prefs.quick_links[target_idx].label);

    for (int j = target_idx; j < app->prefs.num_quick_links - 1; j++) {
        app->prefs.quick_links[j] = app->prefs.quick_links[j + 1];
    }
    app->prefs.num_quick_links--;
    app->active_quicklink_idx = -1;
    prefs_save(app, TRUE);

    if (app->prefs.num_quick_links == 0) {
        /* Wenn es der einzigste war, die Startseite laden */
        gui_update_url_bar(app, "@HOME");
        net_send_url(app, "@HOME");
        app_set_status(GetVString(MSG_STATUS_QL_REMOVED_HOME), rem_name);
    } else {
        app_set_status(GetVString(MSG_STATUS_QL_REMOVED), rem_name);
    }

    gui_rebuild_window(app);
}

void gui_try_close_pending_screen(VortexApp *app)
{
    if (app && app->is_iconified && app->custom_screen && !app->win_obj) {
        if (gui_close_custom_screen(app, 1)) {
            DBG_LOG("[GUI] Deferred custom screen close succeeded\n");
        }
    }
}

void gui_to_front(VortexApp *app)
{
    if (!app) return;

    if (app->is_iconified) {
        gui_uniconify(app);
        return;
    }
    if (app->custom_screen) {
        IIntuition->ScreenToFront(app->custom_screen);
    }
    if (app->win) {
        IIntuition->WindowToFront(app->win);
        IIntuition->ActivateWindow(app->win);
    }
}

void gui_iconify(VortexApp *app)
{
    if (!app || app->is_iconified || !app->win_obj) {
        return;
    }

    prefs_capture_window_box(app);
    gui_set_rmb_trap(app, FALSE);

    if (app->pw_win_obj) {
        gui_close_password_manager(app);
    }
    if (app->dl_win_obj) {
        gui_close_download_manager(app);
    }

    /* When running on our own custom public screen, ReAction WM_ICONIFY only
     * closes the window and leaves the screen open. Tear down the window and
     * custom screen cleanly, and place an AppIcon on Workbench instead. */
    if (app->custom_screen != NULL) {
        renderer_cleanup(app);
        gui_cleanup_window(app);

        if (!gui_close_custom_screen(app, 25)) {
            DBG_LOG("[GUI] WARNING: CloseScreen returned FALSE on iconify (visitor window open)\n");
        }

        if (!app->app_port) {
            app->app_port = (struct MsgPort *)IExec->AllocSysObjectTags(ASOT_PORT, TAG_DONE);
        }
        if (IWorkbench && app->app_port) {
            struct Message *msg;
            while ((msg = IExec->GetMsg(app->app_port)) != NULL) {
                IExec->ReplyMsg(msg);
            }

            struct DiskObject *dobj = load_app_icon();
            struct AppIcon *aicon = NULL;
            if (dobj) {
                aicon = IWorkbench->AddAppIconA(1, 0, (CONST_STRPTR)"VortexRemoteBrowser",
                                                app->app_port, ZERO, dobj, NULL);
            }
            if (!aicon) {
                if (dobj && IIcon) {
                    IIcon->FreeDiskObject(dobj);
                }
                dobj = IIcon ? IIcon->GetDefDiskObject(WBTOOL) : NULL;
                if (dobj) {
                    dobj->do_CurrentX = NO_ICON_POSITION;
                    dobj->do_CurrentY = NO_ICON_POSITION;
                    aicon = IWorkbench->AddAppIconA(1, 0, (CONST_STRPTR)"VortexRemoteBrowser",
                                                    app->app_port, ZERO, dobj, NULL);
                }
            }
            if (aicon) {
                app->wb_app_icon  = aicon;
                app->wb_icon_dobj = dobj;
            } else if (dobj && IIcon) {
                IIcon->FreeDiskObject(dobj);
            }
        }

        app->win          = NULL;
        app->is_iconified = TRUE;
        app_sync_hidden_state(app, TRUE);
        DBG_LOG("[GUI] Custom-screen iconify complete (custom_screen=%p, app_icon=%p)\n",
                (void *)app->custom_screen, (void *)app->wb_app_icon);
        return;
    }

    /* Standard ReAction WM_ICONIFY path (when running directly on Workbench) */
    uint32 res = IIntuition->IDoMethod(app->win_obj, WM_ICONIFY, NULL);
    if (!res) {
        /* Fallback: let window.class use the system default icon if custom icon failed */
        IIntuition->SetAttrs(app->win_obj, WINDOW_Icon, (Tag)NULL, TAG_DONE);
        res = IIntuition->IDoMethod(app->win_obj, WM_ICONIFY, NULL);
    }

    /* Verify that Intuition actually closed the window; force WM_CLOSE if still open */
    struct Window *cur_win = NULL;
    IIntuition->GetAttr(WINDOW_Window, app->win_obj, (uint32 *)&cur_win);
    if (res && cur_win != NULL) {
        IIntuition->IDoMethod(app->win_obj, WM_CLOSE, NULL);
        cur_win = NULL;
        IIntuition->GetAttr(WINDOW_Window, app->win_obj, (uint32 *)&cur_win);
    }

    if (res || cur_win == NULL) {
        app->win          = NULL;
        app->is_iconified = TRUE;
        app_sync_hidden_state(app, TRUE);
    } else {
        app_set_status("Iconify fehlgeschlagen!");
        DBG_LOG("[GUI] WM_ICONIFY failed (res=%lu, cur_win=%p)\n", (unsigned long)res, (void *)cur_win);
    }
}

void gui_uniconify(VortexApp *app)
{
    if (!app) return;

    if (!app->is_iconified) {
        gui_to_front(app);
        return;
    }

    /* Branch A: Custom-screen iconify (window & screen were torn down) */
    if (!app->win_obj) {
        DBG_LOG("[GUI] Restoring custom screen and window...\n");
        gui_remove_wb_app_icon(app);

        if (app->custom_screen != NULL) {
            IIntuition->PubScreenStatus(app->custom_screen, 0);
            IIntuition->ScreenToFront(app->custom_screen);
        }

        app->is_iconified = FALSE;

        if (!gui_init(app)) {
            DBG_LOG("[GUI] Failed to restore custom screen! Falling back to Workbench...\n");
            app->prefs.resolution.use_custom_screen = FALSE;
            app->prefs.resolution.screen_mode = VB_SCREEN_WORKBENCH;
            if (!gui_init(app)) {
                DBG_LOG("[GUI] Fatal: Could not restore GUI window!\n");
                app->running = FALSE;
                return;
            }
        }

        renderer_init(app);
        favorites_rebuild_chooser_list(app);
        prefs_apply(app);
        if (app->num_tabs > 0 && app->active_tab_idx < app->num_tabs) {
            gui_update_url_bar(app, app->tabs[app->active_tab_idx].url);
        }
        renderer_sync_viewport_size(app);

        if (app->connected) {
            net_send_url(app, "@REFRESH");
        }
        app_sync_hidden_state(app, FALSE);
        return;
    }

    /* Branch B: Standard Workbench-mode ReAction WM_ICONIFY restore */
    if (app->custom_screen) {
        IIntuition->ScreenToFront(app->custom_screen);
    }
    app->win = (struct Window *)IIntuition->IDoMethod(app->win_obj, WM_OPEN, NULL);
    if (app->win) {
        app->is_iconified = FALSE;
        IIntuition->WindowLimits(app->win, 200, 150, 0, 0);
        if (app->prefs.dark_theme == 1) {
            gui_draw_dark_titlebar(app);
        }
        IIntuition->RefreshWindowFrame(app->win);
        if (app->prefs.dark_theme == 1) {
            gui_draw_dark_titlebar(app);
        }
        IIntuition->SetMouseQueue(app->win, 1);
        s_current_rmb_trap = FALSE;
        if (app->gad_space) {
            struct IBox *uarea = NULL;
            IIntuition->GetAttr(SPACE_AreaBox, app->gad_space, (uint32 *)&uarea);
            if (uarea && uarea->Width > 2 && uarea->Height > 2) {
                app->cached_space_box = *uarea;
            }
        }
        if (app->renderer.has_frame) {
            app->renderer.dirty    = TRUE;
            app->renderer.dirty_x1 = 0;
            app->renderer.dirty_y1 = 0;
            app->renderer.dirty_x2 = 32767;
            app->renderer.dirty_y2 = 32767;
            renderer_flush_dirty(app);
        }
        if (app->connected) {
            net_send_url(app, "@REFRESH");
        }
        app_sync_hidden_state(app, FALSE);
    }
}

void gui_handle_events(VortexApp *app)
{
    if (app->is_iconified && app->wb_app_icon && app->app_port) {
        struct Message *msg;
        while ((msg = IExec->GetMsg(app->app_port)) != NULL) {
            struct AppMessage *amsg = (struct AppMessage *)msg;
            if (amsg->am_Type == AMTYPE_APPICON) {
                app->uniconify_needed = TRUE;
            }
            IExec->ReplyMsg(msg);
        }
        return;
    }

    if (app->pw_win_obj) {
        gui_handle_password_manager_events(app);
    }
    if (app->dl_win_obj) {
        gui_handle_download_manager_events(app);
    }

    if (!app->win_obj) {
        return;
    }

    uint32 result = WMHI_LASTMSG;
    int16  code   = 0;

    while ((result = IIntuition->IDoMethod(app->win_obj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
        switch (result & WMHI_CLASSMASK) {
            case WMHI_CLOSEWINDOW:
                prefs_capture_window_box(app);
                app->running = FALSE;
                break;

            case WMHI_ICONIFY:
                app->iconify_needed = TRUE;
                break;

            case WMHI_UNICONIFY:
                app->uniconify_needed = TRUE;
                break;

            case WMHI_MENUPICK: {
                uint16 selection = (uint16)code;
                if (selection == MENUNULL) {
                    selection = (uint16)(result & WMHI_MENUMASK);
                }
                while (selection != MENUNULL && app->win) {
                    struct MenuItem *item = IIntuition->ItemAddress(app->win->MenuStrip, selection);
                    if (item) {
                        uint32 item_id = (uint32)(uintptr_t)GTMENUITEM_USERDATA(item);
                        if (item_id != 0) {
                            handle_menu_command(app, item_id);
                        }
                        selection = item->NextSelect;
                    } else {
                        break;
                    }
                }
                break;
            }

            case WMHI_ACTIVE:
            case WMHI_INACTIVE:
            case WMHI_CHANGEWINDOW:
                if (app->prefs.dark_theme == 1) {
                    gui_draw_dark_titlebar(app);
                }
                break;

            case WMHI_NEWSIZE:
                prefs_capture_window_box(app);
                if (app->win && app->win->Width != s_last_tabs_win_w) {
                    gui_tabs_rebuild(app);
                }
                if (app->win && app->gad_space) {
                    struct IBox *narea = NULL;
                    IIntuition->GetAttr(SPACE_AreaBox, app->gad_space, (uint32 *)&narea);
                    if (narea && narea->Width > 2 && narea->Height > 2) {
                        app->cached_space_box = *narea;
                    }
                    if (app->renderer.has_frame) {
                        app->renderer.dirty    = TRUE;
                        app->renderer.dirty_x1 = 0;
                        app->renderer.dirty_y1 = 0;
                        app->renderer.dirty_x2 = 32767;
                        app->renderer.dirty_y2 = 32767;
                    }
                    renderer_sync_viewport_size(app);
                }
                if (app->prefs.dark_theme == 1) {
                    gui_draw_dark_titlebar(app);
                }
                break;

            case WMHI_MOUSEMOVE:
                break;

            case WMHI_MOUSEBUTTONS:
                /* Mouse buttons are captured with microsecond precision in idcmp_hook_func (WINDOW_IDCMPHook) */
                break;

            case WMHI_VANILLAKEY: {
                /* Main window uses pure IDCMP_RAWKEY (no IDCMP_VANILLAKEY), kept as defensive fallback */
                uint16 ascii = (uint16)(result & WMHI_KEYMASK);
                uint32 wq = 0;
                if (app->win_obj) {
                    IIntuition->GetAttr(WINDOW_Qualifier, app->win_obj, &wq);
                }
                uint16 vqual = (uint16)((wq & 0xFFFFU) | ((wq >> 16) & 0xFFFFU));
                BOOL v_cmd = (vqual & (IEQUALIFIER_RCOMMAND | IEQUALIFIER_LCOMMAND)) != 0 ||
                             (((vqual & IEQUALIFIER_CONTROL) != 0) && ((vqual & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) == 0));
                if (v_cmd && ascii >= 0x20 && ascii <= 0x7E) {
                    if (dispatch_edit_shortcut(app, (char)ascii, SHORTCUT_SRC_VANILLA)) {
                        break;
                    }
                }
                if (ascii == 0x01) {
                    dispatch_edit_shortcut(app, 'a', SHORTCUT_SRC_VANILLA);
                } else if (ascii == 0x03) {
                    dispatch_edit_shortcut(app, 'c', SHORTCUT_SRC_VANILLA);
                } else if (ascii == 0x16) {
                    dispatch_edit_shortcut(app, 'v', SHORTCUT_SRC_VANILLA);
                } else if (ascii == 0x18) {
                    dispatch_edit_shortcut(app, 'x', SHORTCUT_SRC_VANILLA);
                } else if (ascii == 0x19) {
                    dispatch_edit_shortcut(app, 'y', SHORTCUT_SRC_VANILLA);
                } else if (ascii == 0x1A) {
                    dispatch_edit_shortcut(app, 'z', SHORTCUT_SRC_VANILLA);
                } else if (ascii == 0x08) {
                    net_send_key(app, INPUT_KEY_DOWN, ascii, "Backspace");
                } else if (ascii == 0x09) {
                    net_send_key(app, INPUT_KEY_DOWN, ascii, "Tab");
                } else if (ascii == 0x0D || ascii == 0x0A) {
                    net_send_key(app, INPUT_KEY_DOWN, ascii, "Enter");
                } else if (ascii == 0x1B) {
                    net_send_key(app, INPUT_KEY_DOWN, ascii, "Escape");
                } else if (ascii == 0x7F) {
                    net_send_key(app, INPUT_KEY_DOWN, ascii, "Delete");
                } else if (ascii >= 0x20 && ascii <= 0x7E) {
                    char key_str[4];
                    key_str[0] = (char)ascii;
                    key_str[1] = '\0';
                    net_send_key(app, INPUT_KEY_DOWN, ascii, key_str);
                } else if (ascii >= 0xA0 && ascii <= 0xFF) {
                    char local_ch[2] = { (char)ascii, '\0' };
                    char utf8_str[8];
                    vb_local_to_utf8(local_ch, utf8_str, sizeof(utf8_str));
                    net_send_key(app, INPUT_KEY_DOWN, ascii, utf8_str);
                }
                break;
            }

            case WMHI_RAWKEY: {
                uint16 rawkey = (uint16)(result & WMHI_KEYMASK);
                if (rawkey & IECODE_UP_PREFIX) {
                    break;
                }

                /* NewMouse / RAWKEY_NM_WHEEL_UP (0x7A) / RAWKEY_NM_WHEEL_DOWN (0x7B) */
                if ((rawkey == 0x7A || rawkey == 0x7B) && !s_got_extended_wheel && app->win) {
                    int16_t rx = 0, ry = 0;
                    if (renderer_map_window_to_remote(app, app->win->MouseX, app->win->MouseY, &rx, &ry)) {
                        int32 step = (rawkey == 0x7A) ? -120 : 120;
                        if ((step > 0 && s_wheel_accum_dy < 0) || (step < 0 && s_wheel_accum_dy > 0)) {
                            s_wheel_accum_dy = 0;
                        }
                        s_wheel_accum_dy += step;
                        if (s_wheel_accum_dy >  480) s_wheel_accum_dy =  480;
                        if (s_wheel_accum_dy < -480) s_wheel_accum_dy = -480;
                        s_wheel_win_x = app->win->MouseX;
                        s_wheel_win_y = app->win->MouseY;
                    }
                    break;
                }

                /* Ignore bare modifier / non-input rawkeys */
                if ((rawkey >= RAWKEY_LSHIFT && rawkey <= RAWKEY_RCOMMAND) ||
                    rawkey == RAWKEY_HELP || rawkey == RAWKEY_MENU ||
                    rawkey == RAWKEY_PRINTSCR || rawkey == RAWKEY_BREAK) {
                    break;
                }

                /* Query live kernel qualifiers + dead-key InputEvent from window.class */
                struct InputEvent *ev = NULL;
                uint32 wq = 0;
                if (app->win_obj) {
                    IIntuition->GetAttr(WINDOW_InputEvent, app->win_obj, (uint32 *)&ev);
                    IIntuition->GetAttr(WINDOW_Qualifier, app->win_obj, &wq);
                }
                uint16 qual = ev ? ev->ie_Qualifier : (uint16)((wq & 0xFFFFU) | ((wq >> 16) & 0xFFFFU));
                if (qual == 0 && wq != 0) {
                    qual = (uint16)((wq & 0xFFFFU) | ((wq >> 16) & 0xFFFFU));
                }

                BOOL has_ctrl   = ((qual & IEQUALIFIER_CONTROL) != 0) &&
                                  ((qual & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) == 0);
                BOOL has_ramiga = (qual & IEQUALIFIER_RCOMMAND) != 0;
                BOOL has_lamiga = (qual & IEQUALIFIER_LCOMMAND) != 0;
                BOOL has_shift  = (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0;

                /* 1. Editing & browser shortcuts via Ctrl / RAmiga / LAmiga (case- & CapsLock-independent,
                 *    works even while WFLG_RMBTRAP is active over the browser viewport!) */
                if (has_ctrl || has_ramiga || has_lamiga) {
                    char base_ch = resolve_rawkey_base_char(rawkey);
                    if (base_ch && dispatch_edit_shortcut(app, base_ch, SHORTCUT_SRC_RAWKEY)) {
                        break;
                    }
                    /* Zoom hotkeys work with BOTH Ctrl and RAmiga, including US '=' key and Numpad */
                    if (has_ctrl || has_ramiga) {
                        if (base_ch == '+' || base_ch == '=' || rawkey == 0x5E) {
                            dispatch_zoom_shortcut(app, +1, SHORTCUT_SRC_RAWKEY);
                            break;
                        } else if (base_ch == '-' || base_ch == '_' || rawkey == 0x4A) {
                            dispatch_zoom_shortcut(app, -1, SHORTCUT_SRC_RAWKEY);
                            break;
                        } else if (base_ch == '0' || rawkey == 0x0F) {
                            dispatch_zoom_shortcut(app, 0, SHORTCUT_SRC_RAWKEY);
                            break;
                        }
                    }
                    /* When WFLG_RMBTRAP suppresses Intuition's menu bar, still honor core RAmiga menu keys */
                    if (has_ramiga && base_ch) {
                        switch (base_ch) {
                            case 'o': handle_menu_command(app, MID_PROJ_OPEN);         break;
                            case 't': handle_menu_command(app, MID_TAB_NEW);           break;
                            case 'w': handle_menu_command(app, MID_TAB_CLOSE);         break;
                            case 'k': handle_menu_command(app, MID_PROJ_PASSWORDS);    break;
                            case 'd': handle_menu_command(app, MID_PROJ_DOWNLOADS);    break;
                            case 'u': handle_menu_command(app, MID_TOGGLE_DARK_THEME); break;
                            case 'b': handle_menu_command(app, MID_NAV_BACK);          break;
                            case 'f': handle_menu_command(app, MID_NAV_FORWARD);       break;
                            case 'r': handle_menu_command(app, MID_NAV_RELOAD);        break;
                            case 's': handle_menu_command(app, MID_NAV_STOP);          break;
                            case 'h': handle_menu_command(app, MID_NAV_HOME);          break;
                            case 'q': handle_menu_command(app, MID_PROJ_QUIT);         break;
                            default:  break;
                        }
                    }
                    /* Never leak stray characters into web forms while Ctrl or Amiga key is held */
                    break;
                }

                /* 2. Special non-text keys */
                if (rawkey == RAWKEY_BACKSPACE) {
                    net_send_key(app, INPUT_KEY_DOWN, 0x08, "Backspace");
                    break;
                } else if (rawkey == RAWKEY_TAB) {
                    net_send_key(app, INPUT_KEY_DOWN, 0x09, has_shift ? "Shift+Tab" : "Tab");
                    break;
                } else if (rawkey == RAWKEY_RETURN || rawkey == RAWKEY_ENTER) {
                    net_send_key(app, INPUT_KEY_DOWN, 0x0D, "Enter");
                    break;
                } else if (rawkey == RAWKEY_ESC) {
                    net_send_key(app, INPUT_KEY_DOWN, 0x1B, "Escape");
                    break;
                } else if (rawkey == RAWKEY_DEL) {
                    net_send_key(app, INPUT_KEY_DOWN, 0x7F, "Delete");
                    break;
                } else if (rawkey == RAWKEY_F11) {
                    handle_menu_command(app, MID_TOGGLE_SCREEN);
                    break;
                } else if (rawkey == RAWKEY_INSERT || rawkey == RAWKEY_F12) {
                    break;
                }

                const char *kname = NULL;
                switch (rawkey) {
                    case RAWKEY_PAGEUP:    kname = "PageUp";     break;
                    case RAWKEY_PAGEDOWN:  kname = "PageDown";   break;
                    case RAWKEY_CRSRUP:    kname = "ArrowUp";    break;
                    case RAWKEY_CRSRDOWN:  kname = "ArrowDown";  break;
                    case RAWKEY_CRSRRIGHT: kname = "ArrowRight"; break;
                    case RAWKEY_CRSRLEFT:  kname = "ArrowLeft";  break;
                    case RAWKEY_F1:        kname = "F1";         break;
                    case RAWKEY_F2:        kname = "F2";         break;
                    case RAWKEY_F3:        kname = "F3";         break;
                    case RAWKEY_F4:        kname = "F4";         break;
                    case RAWKEY_F5:        kname = "F5";         break;
                    case RAWKEY_F6:        kname = "F6";         break;
                    case RAWKEY_F7:        kname = "F7";         break;
                    case RAWKEY_F8:        kname = "F8";         break;
                    case RAWKEY_F9:        kname = "F9";         break;
                    case RAWKEY_F10:       kname = "F10";        break;
                    case RAWKEY_HOME:      kname = "Home";       break;
                    case RAWKEY_END:       kname = "End";        break;
                    default:               break;
                }
                if (kname) {
                    if (has_shift && (rawkey == RAWKEY_CRSRUP || rawkey == RAWKEY_CRSRDOWN ||
                                      rawkey == RAWKEY_CRSRRIGHT || rawkey == RAWKEY_CRSRLEFT ||
                                      rawkey == RAWKEY_HOME || rawkey == RAWKEY_END)) {
                        char sk[24];
                        snprintf(sk, sizeof(sk), "Shift+%s", kname);
                        net_send_key(app, INPUT_KEY_DOWN, rawkey, sk);
                    } else {
                        net_send_key(app, INPUT_KEY_DOWN, rawkey, kname);
                    }
                    break;
                }

                /* 3. Regular text input via IKeymap->MapRawKey (handles Shift, CapsLock, AltGr, dead keys, umlauts) */
                if (IKeymap) {
                    struct InputEvent ie;
                    char kbuf[16];
                    if (ev) {
                        ie = *ev;
                        ie.ie_Class    = IECLASS_RAWKEY;
                        ie.ie_SubClass = 0;
                        ie.ie_Code     = rawkey;
                    } else {
                        memset(&ie, 0, sizeof(ie));
                        ie.ie_Class         = IECLASS_RAWKEY;
                        ie.ie_SubClass      = 0;
                        ie.ie_Code          = rawkey;
                        ie.ie_Qualifier     = qual;
                        ie.ie_Prev1DownCode = 0x80;
                        ie.ie_Prev1DownQual = 0;
                    }
                    WORD actual = IKeymap->MapRawKey(&ie, (STRPTR)kbuf, (WORD)(sizeof(kbuf) - 1), NULL);
                    if (actual > 0) {
                        unsigned char first = (unsigned char)kbuf[0];
                        /* Ignore ANSI CSI/ESC sequences and control bytes */
                        if (first == 0x9B || first == 0x1B || first < 0x20 || first == 0x7F ||
                            (first >= 0x80 && first <= 0x9F)) {
                            break;
                        }
                        kbuf[actual] = '\0';
                        if (actual == 1 && first >= 0x20 && first <= 0x7E) {
                            net_send_key(app, INPUT_KEY_DOWN, first, kbuf);
                        } else {
                            char utf8_str[64];
                            vb_local_to_utf8(kbuf, utf8_str, sizeof(utf8_str));
                            if (utf8_str[0] != '\0') {
                                net_send_key(app, INPUT_KEY_DOWN, first, utf8_str);
                            }
                        }
                    }
                }
                break;
            }

            case WMHI_GADGETUP: {
                uint32 gid = (result & WMHI_GADGETMASK);
                if (gid == 0xFFFE) {
                    app->iconify_needed = TRUE;
                    break;
                }
                switch (gid) {
                    case GID_BACK:
                        net_send_url(app, "@BACK");
                        break;

                    case GID_FORWARD:
                        net_send_url(app, "@FORWARD");
                        break;

                    case GID_RELOAD:
                        net_send_url(app, "@RELOAD");
                        break;

                    case GID_STOP:
                        net_send_url(app, "@STOP");
                        app_set_status(GetVString(MSG_STATUS_LOADING_STOPPED));
                        break;

                    case GID_HOME:
                        if (app->active_tab_idx < VB_MAX_TABS) {
                            app->restore_tab_mask &= ~(1UL << app->active_tab_idx);
                        }
                        gui_update_url_bar(app, "");
                        net_send_url(app, "@HOME");
                        break;

                    case GID_SCROLL_UP:
                        net_send_wheel(app, 640, 360, -240);
                        break;

                    case GID_SCROLL_DOWN:
                        net_send_wheel(app, 640, 360, 240);
                        break;

                    case GID_SPACE:
                        break;

                    case GID_CONNECT: {
                        if (app->server_host[0] == '\0') {
                            prompt_server_address(app);
                        } else {
                            if (net_connect(app, app->server_host, app->server_port)) {
                                net_sync_tabs(app);
                            }
                        }
                        break;
                    }

                    case GID_URL_FAVICON:
                        gui_open_password_manager(app);
                        break;

                    case GID_DOWNLOADS:
                    case GID_GAUGE:
                        gui_open_download_manager(app);
                        break;

                    case GID_CLEAR_URL: {
                        if (app->gad_url && app->win) {
                            struct Gadget *gad = (struct Gadget *)app->gad_url;
                            if (gad->SpecialInfo) {
                                struct StringInfo *si = (struct StringInfo *)gad->SpecialInfo;
                                if (si->Buffer) {
                                    si->Buffer[0] = '\0';
                                }
                            }
                            IIntuition->SetGadgetAttrs(gad,
                                                       app->win, NULL,
                                                       STRINGA_TextVal, "",
                                                       TAG_DONE);
                            IIntuition->ActivateGadget(gad,
                                                       app->win, NULL);
                        }
                        break;
                    }

                    case GID_URL:
                    case GID_GO: {
                        char url_buf[VB_MAX_URL_LEN];
                        get_string_gadget_text(app->gad_url, url_buf, sizeof(url_buf));
                        if (url_buf[0] != '\0') {
                            if (is_url_or_command(url_buf)) {
                                gui_update_url_bar(app, url_buf);
                                net_send_url(app, url_buf);
                            } else {
                                char search_url[VB_MAX_URL_LEN];
                                build_search_url(app, url_buf, search_url, sizeof(search_url));
                                gui_update_url_bar(app, search_url);
                                net_send_url(app, search_url);
                                uint8_t se = (app->prefs.search_engine < NUM_SEARCH_ENGINES) ? app->prefs.search_engine : 0;
                                app_set_status(GetVString(MSG_SEARCH_PROMPT), g_search_engines[se].name, url_buf);
                            }
                        }
                        break;
                    }

                    case GID_SEARCH_CHOOSER: {
                        uint32 sel = 0;
                        if (app->gad_search_chooser) {
                            IIntuition->GetAttr(CHOOSER_Selected, app->gad_search_chooser, &sel);
                            if (sel < NUM_SEARCH_ENGINES) {
                                app->prefs.search_engine = (uint8_t)sel;
                                prefs_save(app, TRUE);
                                app_set_status(GetVString(MSG_STATUS_SEARCH_ENGINE_SEL), g_search_engines[sel].name);
                            }
                        }
                        /* If a query is entered in Omnibar URL bar, execute search */
                        char url_buf[VB_MAX_URL_LEN];
                        get_string_gadget_text(app->gad_url, url_buf, sizeof(url_buf));
                        if (url_buf[0] != '\0' && !is_url_or_command(url_buf)) {
                            char search_url[VB_MAX_URL_LEN];
                            build_search_url(app, url_buf, search_url, sizeof(search_url));
                            gui_update_url_bar(app, search_url);
                            net_send_url(app, search_url);
                        } else if (app->gad_url && app->win) {
                            IIntuition->ActivateGadget((struct Gadget *)app->gad_url, app->win, NULL);
                        }
                        break;
                    }

                    case GID_SEARCH:
                    case GID_SEARCH_GO: {
                        if (app->gad_search_chooser) {
                            uint32 sel = 0;
                            IIntuition->GetAttr(CHOOSER_Selected, app->gad_search_chooser, &sel);
                            if (sel < NUM_SEARCH_ENGINES) {
                                app->prefs.search_engine = (uint8_t)sel;
                            }
                        }
                        char s_buf[256];
                        get_string_gadget_text(app->gad_search ? app->gad_search : app->gad_url, s_buf, sizeof(s_buf));
                        if (s_buf[0] != '\0') {
                            char search_url[VB_MAX_URL_LEN];
                            build_search_url(app, s_buf, search_url, sizeof(search_url));
                            gui_update_url_bar(app, search_url);
                            net_send_url(app, search_url);
                            uint8_t se = (app->prefs.search_engine < NUM_SEARCH_ENGINES) ? app->prefs.search_engine : 0;
                            app_set_status(GetVString(MSG_SEARCH_PROMPT), g_search_engines[se].name, s_buf);
                        }
                        break;
                    }

                    case GID_QUICK_ADD:
                        prompt_add_quicklink(app);
                        break;

                    case GID_QUICK_REMOVE:
                        remove_current_quicklink(app);
                        break;

                    case GID_MENU: {
                        int16_t mx = app->win ? app->win->MouseX : 500;
                        int16_t my = app->win ? app->win->MouseY : 300;
                        open_main_menu_popup(app, mx, my);
                        break;
                    }

                    case GID_ADD_FAV: {
                        char url_buf[VB_MAX_URL_LEN];
                        get_string_gadget_text(app->gad_url, url_buf, sizeof(url_buf));
                        const char *u = (url_buf[0] != '\0') ? url_buf : app->current_url;
                        if (u && u[0] != '\0') {
                            if (favorites_add(app, u, u)) {
                                favorites_save(app);
                                favorites_rebuild_chooser_list(app);
                                app_set_status(GetVString(MSG_STATUS_BOOKMARK_SAVED), u);
                            }
                        }
                        break;
                    }

                    case GID_FAV_CHOOSER: {
                        int idx = (int)code;
                        if (idx >= 0 && idx < app->num_favorites) {
                            const char *fav_url = app->favorites[idx].url;
                            if (fav_url[0] != '@') {
                                gui_update_url_bar(app, fav_url);
                            } else {
                                gui_update_url_bar(app, "");
                            }
                            net_send_url(app, fav_url);
                        }
                        break;
                    }

                    case GID_CLICKTAB: {
                        struct Node *closed_node = NULL;
                        IIntuition->GetAttr(CLICKTAB_NodeClosed, app->gad_clicktab, (uint32 *)&closed_node);
                        if (closed_node && closed_node != s_handled_close_node) {
                            int close_idx = tabs_index_of_node(app, closed_node);
                            if (close_idx >= 0 && close_idx < app->num_tabs) {
                                s_handled_close_node = closed_node;
                                gui_tab_close(app, (uint16)close_idx);
                                break;
                            }
                        }
                        uint32 cur_tab = 0;
                        IIntuition->GetAttr(CLICKTAB_Current, app->gad_clicktab, &cur_tab);
                        if (cur_tab < app->num_tabs && cur_tab != app->active_tab_idx) {
                            gui_tab_switch(app, (uint16)cur_tab);
                        }
                        break;
                    }

                    case GID_TAB_NEW:
                        gui_tab_add(app, NULL, "", TRUE);
                        break;

                    case GID_TAB_CLOSE:
                        gui_tab_close(app, app->active_tab_idx);
                        break;

                    default: {
                        if (gid >= GID_QUICK_BASE && gid < GID_QUICK_BASE + app->prefs.num_quick_links) {
                            int qidx = (int)(gid - GID_QUICK_BASE);
                            const char *u = app->prefs.quick_links[qidx].url;
                            if (u && u[0] != '\0') {
                                DBG_LOG("[GUI] QuickLink %d clicked: %s (%s)\n", qidx, app->prefs.quick_links[qidx].label, u);
                                app->active_quicklink_idx = qidx;
                                gui_update_url_bar(app, u);
                                net_send_url(app, u);
                                app_set_status(GetVString(MSG_STATUS_QL_STARTED), app->prefs.quick_links[qidx].label);
                            }
                        } else if (gid >= GID_QUICK_DEL_BASE && gid < GID_QUICK_DEL_BASE + app->prefs.num_quick_links) {
                            int qidx = (int)(gid - GID_QUICK_DEL_BASE);
                            char rem_name[80];
                            snprintf(rem_name, sizeof(rem_name), "%s", app->prefs.quick_links[qidx].label);
                            DBG_LOG("[GUI] QuickLink %d delete clicked -> removing %s\n", qidx, rem_name);
                            prefs_quicklink_remove(app, qidx);
                            prefs_save(app, TRUE);
                            app_set_status(GetVString(MSG_STATUS_QL_REMOVED), rem_name);
                            app->reopen_gui_needed = TRUE;
                        }
                        break;
                    }
                }
                break;
            }

            default:
                break;
        }

        if (app->reopen_screen_needed || app->reopen_gui_needed ||
            app->iconify_needed || app->uniconify_needed ||
            !app->running || app->is_iconified) {
            break;
        }
    }

    if (s_wheel_accum_dy != 0 && app->connected) {
        int16_t rx = (int16_t)(app->renderer.src_width  ? app->renderer.src_width  / 2 : 640);
        int16_t ry = (int16_t)(app->renderer.src_height ? app->renderer.src_height / 2 : 360);
        renderer_map_window_to_remote(app, s_wheel_win_x, s_wheel_win_y, &rx, &ry);
        net_send_wheel(app, rx, ry, (int16_t)s_wheel_accum_dy);
        s_wheel_accum_dy     = 0;
        s_got_extended_wheel = FALSE;
    }

    if (app->reopen_screen_needed && app->running) {
        app->reopen_screen_needed = FALSE;
        app->reopen_gui_needed    = FALSE;
        gui_reopen_screen(app);
        return;
    }

    if (app->reopen_gui_needed && app->running) {
        app->reopen_gui_needed = FALSE;
        gui_rebuild_window(app);
        return;
    }

    if (app->ctx_menu_pending) {
        app->ctx_menu_pending = FALSE;

        /* Wait up to 1.2s for server MSG_CONTEXT_MENU target inspection response */
        for (int w = 0; w < 60 && !app->ctx_menu_ready && app->connected && app->sock_fd >= 0; w++) {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(app->sock_fd, &rfds);
            struct timeval tv;
            tv.tv_sec  = 0;
            tv.tv_usec = 20000;
            LONG r = ISocket ? ISocket->WaitSelect(app->sock_fd + 1, &rfds, NULL, NULL, &tv, NULL) : 0;
            if (r > 0 && app->sock_fd >= 0 && FD_ISSET(app->sock_fd, &rfds)) {
                do {
                    if (!net_handle_incoming(app)) {
                        break;
                    }
                } while (app->connected && !app->ctx_menu_ready && net_has_pending_data(app));
            }
        }
        renderer_flush_dirty(app);

        if (app->win) {
            if (!app->ctx_menu_ready && app->ctx_menu.link_url[0] == '\0' && app->hover_url[0] != '\0') {
                strncpy(app->ctx_menu.link_url, app->hover_url, sizeof(app->ctx_menu.link_url) - 1);
                app->ctx_menu.link_url[sizeof(app->ctx_menu.link_url) - 1] = '\0';
                if (app->ctx_menu.link_text[0] == '\0') {
                    strncpy(app->ctx_menu.link_text, app->hover_url, sizeof(app->ctx_menu.link_text) - 1);
                    app->ctx_menu.link_text[sizeof(app->ctx_menu.link_text) - 1] = '\0';
                }
            }

            enum {
                CTX_NONE = 0,
                CTX_OPEN_LINK = 1,
                CTX_OPEN_NEW_WIN,
                CTX_DOWNLOAD_LINK,
                CTX_COPY_LINK_URL,
                CTX_FAV_LINK,
                CTX_OPEN_MEDIA,
                CTX_DOWNLOAD_MEDIA,
                CTX_BACK,
                CTX_FORWARD,
                CTX_RELOAD,
                CTX_HOME,
                CTX_FAV_PAGE,
                CTX_DOWNLOAD_PAGE,
                CTX_NEW_TAB,
                CTX_CLOSE_TAB,
                CTX_MAIN_MENU,
                CTX_TOGGLE_DARK_THEME,
                CTX_EDIT_CUT,
                CTX_EDIT_COPY,
                CTX_EDIT_PASTE,
                CTX_PLAY_EXTERNAL,
                CTX_TOGGLE_AUTO_EXT_YT
            };

            BOOL has_link  = (app->ctx_menu.link_url[0] != '\0');
            BOOL has_media = (app->ctx_menu.media_url[0] != '\0' &&
                              strcmp(app->ctx_menu.media_url, app->ctx_menu.link_url) != 0);

            char hdr_buf[96];
            if (has_link) {
                const char *lbl = app->ctx_menu.link_text[0] ? app->ctx_menu.link_text : app->ctx_menu.link_url;
                snprintf(hdr_buf, sizeof(hdr_buf), GetVString(MSG_CTX_HDR_LINK), lbl);
            } else if (has_media) {
                snprintf(hdr_buf, sizeof(hdr_buf), "%s", GetVString(MSG_CTX_HDR_MEDIA));
            } else {
                snprintf(hdr_buf, sizeof(hdr_buf), "VortexRemoteBrowser");
            }

            uint32 selected_id = CTX_NONE;

            if (IPopupMenu) {
                struct Hook pm_hook;
                memset(&pm_hook, 0, sizeof(struct Hook));
                pm_hook.h_Entry = (HOOKFUNC)ctx_menu_hook_func;
                pm_hook.h_Data  = &selected_id;

                Object *menu_obj = NULL;
                if (has_link) {
                    menu_obj = PMMENU(hdr_buf),
                        PMA_MenuHandler, &pm_hook,
                        PMITEM(GetVString(MSG_CTX_OPEN_LINK)), PMIA_ID, CTX_OPEN_LINK, PMEND,
                        PMITEM(GetVString(MSG_CTX_OPEN_NEW_TAB)), PMIA_ID, CTX_OPEN_NEW_WIN, PMEND,
                        PMITEM(GetVString(MSG_CTX_DOWNLOAD_LINK)), PMIA_ID, CTX_DOWNLOAD_LINK, PMEND,
                        PMITEM(GetVString(MSG_CTX_COPY_LINK)), PMIA_ID, CTX_COPY_LINK_URL, PMEND,
                        PMITEM(GetVString(MSG_CTX_FAV_LINK)), PMIA_ID, CTX_FAV_LINK, PMEND,
                        PMITEM(GetVString(MSG_CTX_PLAY_EXTERNAL)), PMIA_ID, CTX_PLAY_EXTERNAL, PMEND,
                        PMITEM(app->prefs.auto_ext_youtube ? GetVString(MSG_CTX_AUTO_YT_ON) : GetVString(MSG_CTX_AUTO_YT_OFF)), PMIA_ID, CTX_TOGGLE_AUTO_EXT_YT, PMEND,
                        PMBAR(), PMEND,
                        PMITEM(GetVString(MSG_MENU_CUT)), PMIA_ID, CTX_EDIT_CUT, PMEND,
                        PMITEM(GetVString(MSG_MENU_COPY)), PMIA_ID, CTX_EDIT_COPY, PMEND,
                        PMITEM(GetVString(MSG_MENU_PASTE)), PMIA_ID, CTX_EDIT_PASTE, PMEND,
                        PMBAR(), PMEND,
                        PMITEM(GetVString(MSG_MENU_TAB_CLOSE)), PMIA_ID, CTX_CLOSE_TAB, PMEND,
                        PMBAR(), PMEND,
                        PMITEM(GetVString(MSG_MENU_BACK)), PMIA_ID, CTX_BACK, PMEND,
                        PMITEM(GetVString(MSG_MENU_FORWARD)), PMIA_ID, CTX_FORWARD, PMEND,
                        PMITEM(GetVString(MSG_MENU_RELOAD)), PMIA_ID, CTX_RELOAD, PMEND,
                        PMITEM(GetVString(MSG_MENU_HOME)), PMIA_ID, CTX_HOME, PMEND,
                        PMBAR(), PMEND,
                        PMITEM(app->prefs.dark_theme ? GetVString(MSG_CTX_LIGHT_ON) : GetVString(MSG_CTX_DARK_ON)), PMIA_ID, CTX_TOGGLE_DARK_THEME, PMEND,
                        PMITEM(GetVString(MSG_CTX_MAIN_MENU)), PMIA_ID, CTX_MAIN_MENU, PMEND,
                    PMEND;
                } else if (has_media) {
                    menu_obj = PMMENU(hdr_buf),
                        PMA_MenuHandler, &pm_hook,
                        PMITEM(GetVString(MSG_CTX_OPEN_MEDIA)), PMIA_ID, CTX_OPEN_MEDIA, PMEND,
                        PMITEM(GetVString(MSG_CTX_DOWNLOAD_IMAGE)), PMIA_ID, CTX_DOWNLOAD_MEDIA, PMEND,
                        PMITEM(GetVString(MSG_CTX_PLAY_EXTERNAL)), PMIA_ID, CTX_PLAY_EXTERNAL, PMEND,
                        PMITEM(app->prefs.auto_ext_youtube ? GetVString(MSG_CTX_AUTO_YT_ON) : GetVString(MSG_CTX_AUTO_YT_OFF)), PMIA_ID, CTX_TOGGLE_AUTO_EXT_YT, PMEND,
                        PMBAR(), PMEND,
                        PMITEM(GetVString(MSG_MENU_CUT)), PMIA_ID, CTX_EDIT_CUT, PMEND,
                        PMITEM(GetVString(MSG_MENU_COPY)), PMIA_ID, CTX_EDIT_COPY, PMEND,
                        PMITEM(GetVString(MSG_MENU_PASTE)), PMIA_ID, CTX_EDIT_PASTE, PMEND,
                        PMBAR(), PMEND,
                        PMITEM(GetVString(MSG_MENU_TAB_NEW)), PMIA_ID, CTX_NEW_TAB, PMEND,
                        PMITEM(GetVString(MSG_MENU_TAB_CLOSE)), PMIA_ID, CTX_CLOSE_TAB, PMEND,
                        PMBAR(), PMEND,
                        PMITEM(GetVString(MSG_MENU_BACK)), PMIA_ID, CTX_BACK, PMEND,
                        PMITEM(GetVString(MSG_MENU_FORWARD)), PMIA_ID, CTX_FORWARD, PMEND,
                        PMITEM(GetVString(MSG_MENU_RELOAD)), PMIA_ID, CTX_RELOAD, PMEND,
                        PMITEM(GetVString(MSG_MENU_HOME)), PMIA_ID, CTX_HOME, PMEND,
                        PMBAR(), PMEND,
                        PMITEM(app->prefs.dark_theme ? GetVString(MSG_CTX_LIGHT_ON) : GetVString(MSG_CTX_DARK_ON)), PMIA_ID, CTX_TOGGLE_DARK_THEME, PMEND,
                        PMITEM(GetVString(MSG_CTX_MAIN_MENU)), PMIA_ID, CTX_MAIN_MENU, PMEND,
                    PMEND;
                } else {
                    menu_obj = PMMENU(hdr_buf),
                        PMA_MenuHandler, &pm_hook,
                        PMITEM(GetVString(MSG_CTX_PLAY_EXTERNAL)), PMIA_ID, CTX_PLAY_EXTERNAL, PMEND,
                        PMITEM(app->prefs.auto_ext_youtube ? GetVString(MSG_CTX_AUTO_YT_ON) : GetVString(MSG_CTX_AUTO_YT_OFF)), PMIA_ID, CTX_TOGGLE_AUTO_EXT_YT, PMEND,
                        PMBAR(), PMEND,
                        PMITEM(GetVString(MSG_MENU_CUT)), PMIA_ID, CTX_EDIT_CUT, PMEND,
                        PMITEM(GetVString(MSG_MENU_COPY)), PMIA_ID, CTX_EDIT_COPY, PMEND,
                        PMITEM(GetVString(MSG_MENU_PASTE)), PMIA_ID, CTX_EDIT_PASTE, PMEND,
                        PMBAR(), PMEND,
                        PMITEM(GetVString(MSG_MENU_BACK)), PMIA_ID, CTX_BACK, PMEND,
                        PMITEM(GetVString(MSG_MENU_FORWARD)), PMIA_ID, CTX_FORWARD, PMEND,
                        PMITEM(GetVString(MSG_MENU_RELOAD)), PMIA_ID, CTX_RELOAD, PMEND,
                        PMITEM(GetVString(MSG_MENU_HOME)), PMIA_ID, CTX_HOME, PMEND,
                        PMBAR(), PMEND,
                        PMITEM(GetVString(MSG_MENU_TAB_NEW)), PMIA_ID, CTX_NEW_TAB, PMEND,
                        PMITEM(GetVString(MSG_MENU_TAB_CLOSE)), PMIA_ID, CTX_CLOSE_TAB, PMEND,
                        PMBAR(), PMEND,
                        PMITEM(GetVString(MSG_CTX_FAV_PAGE)), PMIA_ID, CTX_FAV_PAGE, PMEND,
                        PMITEM(GetVString(MSG_CTX_DL_PAGE)), PMIA_ID, CTX_DOWNLOAD_PAGE, PMEND,
                        PMBAR(), PMEND,
                        PMITEM(app->prefs.dark_theme ? GetVString(MSG_CTX_LIGHT_MODE_FULL) : GetVString(MSG_CTX_DARK_MODE_FULL)), PMIA_ID, CTX_TOGGLE_DARK_THEME, PMEND,
                        PMITEM(GetVString(MSG_CTX_MAIN_MENU)), PMIA_ID, CTX_MAIN_MENU, PMEND,
                    PMEND;
                }

                if (menu_obj) {
                    Object *sel_item = (Object *)IIntuition->IDoMethod(menu_obj, PM_OPEN, app->win);
                    if (selected_id == CTX_NONE && sel_item != NULL) {
                        uint32 id_val = 0;
                        if (IIntuition->GetAttr(PMIA_ID, sel_item, &id_val) && id_val != 0) {
                            selected_id = id_val;
                        }
                    }
                    IIntuition->DisposeObject(menu_obj);
                    /* Update click deduplication timestamp so selecting a menu item does not click through */
                    IIntuition->CurrentTime(&s_last_click_secs, &s_last_click_micros);
                }
            } else {
                /* Fallback Intuition EasyRequest if popupmenu.class is unavailable */
                struct EasyStruct es;
                memset(&es, 0, sizeof(es));
                es.es_StructSize   = sizeof(struct EasyStruct);
                es.es_Title        = (STRPTR)GetVString(MSG_CTX_REQ_TITLE);
                if (has_link) {
                    es.es_TextFormat   = (STRPTR)GetVString(MSG_CTX_REQ_LINK_TEXT);
                    es.es_GadgetFormat = (STRPTR)GetVString(MSG_CTX_REQ_LINK_BTNS);
                    LONG choice = IIntuition->EasyRequest(app->win, &es, NULL, app->ctx_menu.link_url);
                    if (choice == 1) selected_id = CTX_OPEN_LINK;
                    else if (choice == 2) selected_id = CTX_OPEN_NEW_WIN;
                    else if (choice == 3) selected_id = CTX_CLOSE_TAB;
                    else if (choice == 4) selected_id = CTX_DOWNLOAD_LINK;
                    else if (choice == 5) selected_id = CTX_COPY_LINK_URL;
                    else if (choice == 6) selected_id = CTX_MAIN_MENU;
                    else if (choice == 7) selected_id = CTX_BACK;
                } else {
                    es.es_TextFormat   = (STRPTR)GetVString(MSG_CTX_REQ_PAGE_TEXT);
                    es.es_GadgetFormat = (STRPTR)GetVString(MSG_CTX_REQ_PAGE_BTNS);
                    LONG choice = IIntuition->EasyRequest(app->win, &es, NULL);
                    if (choice == 1) selected_id = CTX_BACK;
                    else if (choice == 2) selected_id = CTX_FORWARD;
                    else if (choice == 3) selected_id = CTX_RELOAD;
                    else if (choice == 4) selected_id = CTX_HOME;
                    else if (choice == 5) selected_id = CTX_NEW_TAB;
                    else if (choice == 6) selected_id = CTX_CLOSE_TAB;
                    else if (choice == 7) selected_id = CTX_MAIN_MENU;
                }
            }

            switch (selected_id) {
                case CTX_EDIT_CUT:
                    dispatch_edit_shortcut(app, 'x', SHORTCUT_SRC_MENU);
                    break;

                case CTX_EDIT_COPY:
                    dispatch_edit_shortcut(app, 'c', SHORTCUT_SRC_MENU);
                    break;

                case CTX_EDIT_PASTE:
                    dispatch_edit_shortcut(app, 'v', SHORTCUT_SRC_MENU);
                    break;

                case CTX_TOGGLE_DARK_THEME:
                    handle_menu_command(app, MID_TOGGLE_DARK_THEME);
                    break;

                case CTX_MAIN_MENU:
                    open_main_menu_popup(app, app->win ? app->win->MouseX : 300, app->win ? app->win->MouseY : 200);
                    break;

                case CTX_NEW_TAB:
                    gui_tab_add(app, NULL, "", TRUE);
                    break;

                case CTX_CLOSE_TAB:
                    if (app->num_tabs > 0) {
                        gui_tab_close(app, app->active_tab_idx);
                    }
                    break;

                case CTX_OPEN_LINK:
                    gui_update_url_bar(app, app->ctx_menu.link_url);
                    net_send_url(app, app->ctx_menu.link_url);
                    break;

                case CTX_OPEN_NEW_WIN:
                    gui_tab_add(app, app->ctx_menu.link_text[0] ? app->ctx_menu.link_text : NULL, app->ctx_menu.link_url, TRUE);
                    app_set_status(GetVString(MSG_STATUS_OPENING_NEW_TAB), app->ctx_menu.link_url);
                    break;

                case CTX_DOWNLOAD_LINK: {
                    char dl_cmd[VB_MAX_URL_LEN + 16];
                    snprintf(dl_cmd, sizeof(dl_cmd), "@DOWNLOAD:%s", app->ctx_menu.link_url);
                    app_set_status(GetVString(MSG_STATUS_START_ASL_DL), app->ctx_menu.link_url);
                    net_send_url(app, dl_cmd);
                    break;
                }

                case CTX_COPY_LINK_URL:
                    gui_update_url_bar(app, app->ctx_menu.link_url);
                    clipboard_write_text(app->ctx_menu.link_url);
                    app_set_status(GetVString(MSG_STATUS_LINK_COPIED), app->ctx_menu.link_url);
                    break;

                case CTX_FAV_LINK: {
                    const char *t = app->ctx_menu.link_text[0] ? app->ctx_menu.link_text : app->ctx_menu.link_url;
                    if (favorites_add(app, t, app->ctx_menu.link_url)) {
                        favorites_save(app);
                        favorites_rebuild_chooser_list(app);
                        app_set_status(GetVString(MSG_STATUS_LINK_BOOKMARKED), t);
                    }
                    break;
                }

                case CTX_OPEN_MEDIA:
                    gui_update_url_bar(app, app->ctx_menu.media_url);
                    net_send_url(app, app->ctx_menu.media_url);
                    break;

                case CTX_DOWNLOAD_MEDIA: {
                    char dl_cmd[VB_MAX_URL_LEN + 16];
                    snprintf(dl_cmd, sizeof(dl_cmd), "@DOWNLOAD:%s", app->ctx_menu.media_url);
                    app_set_status(GetVString(MSG_STATUS_START_MEDIA_DL), app->ctx_menu.media_url);
                    net_send_url(app, dl_cmd);
                    break;
                }

                case CTX_PLAY_EXTERNAL: {
                    const char *vurl = NULL;
                    if (has_link && app->ctx_menu.link_url[0]) {
                        vurl = app->ctx_menu.link_url;
                    } else if (has_media && app->ctx_menu.media_url[0] &&
                               strncmp(app->ctx_menu.media_url, "blob:", 5) != 0 &&
                               !strstr(app->ctx_menu.media_url, ".jpg") &&
                               !strstr(app->ctx_menu.media_url, ".jpeg") &&
                               !strstr(app->ctx_menu.media_url, ".png") &&
                               !strstr(app->ctx_menu.media_url, ".webp")) {
                        vurl = app->ctx_menu.media_url;
                    } else if (app->current_url[0]) {
                        vurl = app->current_url;
                    }
                    if (vurl && vurl[0]) {
                        if (app->connected) {
                            char ext_cmd[VB_MAX_URL_LEN + 16];
                            snprintf(ext_cmd, sizeof(ext_cmd), "@EXTPLAY:%s", vurl);
                            app_set_status(GetVString(MSG_STATUS_EXT_PLAYER), vurl);
                            net_send_url(app, ext_cmd);
                        } else {
                            gui_launch_external_player(app, vurl);
                        }
                    }
                    break;
                }

                case CTX_TOGGLE_AUTO_EXT_YT: {
                    app->prefs.auto_ext_youtube = !app->prefs.auto_ext_youtube;
                    prefs_save_keep_window_box(app, TRUE);
                    if (app->connected) {
                        char epcmd[32];
                        snprintf(epcmd, sizeof(epcmd), "@EXTPLAY_AUTO:%d", app->prefs.auto_ext_youtube ? 1 : 0);
                        net_send_url(app, epcmd);
                    }
                    app_set_status("%s", app->prefs.auto_ext_youtube ? GetVString(MSG_CTX_AUTO_YT_ON) : GetVString(MSG_CTX_AUTO_YT_OFF));
                    break;
                }

                case CTX_BACK:
                    net_send_url(app, "@BACK");
                    break;

                case CTX_FORWARD:
                    net_send_url(app, "@FORWARD");
                    break;

                case CTX_RELOAD:
                    net_send_url(app, "@RELOAD");
                    break;

                case CTX_HOME:
                    if (app->active_tab_idx < VB_MAX_TABS) {
                        app->restore_tab_mask &= ~(1UL << app->active_tab_idx);
                    }
                    gui_update_url_bar(app, "");
                    net_send_url(app, "@HOME");
                    break;

                case CTX_FAV_PAGE: {
                    const char *u = app->current_url[0] ? app->current_url : "https://www.amiga-news.de";
                    if (favorites_add(app, u, u)) {
                        favorites_save(app);
                        favorites_rebuild_chooser_list(app);
                        app_set_status(GetVString(MSG_STATUS_PAGE_BOOKMARKED), u);
                    }
                    break;
                }

                case CTX_DOWNLOAD_PAGE: {
                    if (app->current_url[0] != '\0') {
                        char dl_cmd[VB_MAX_URL_LEN + 16];
                        snprintf(dl_cmd, sizeof(dl_cmd), "@DOWNLOAD:%s", app->current_url);
                        app_set_status(GetVString(MSG_STATUS_START_DL), app->current_url);
                        net_send_url(app, dl_cmd);
                    }
                    break;
                }

                default:
                    app_set_status(GetVString(MSG_STATUS_READY));
                    break;
            }
        }
    }
}

void gui_update_url_bar(VortexApp *app, const char *url)
{
    if (!app || !app->gad_url || !app->win) {
        return;
    }
    struct Gadget *gad = (struct Gadget *)app->gad_url;
    /* Do not overwrite URL bar while user is actively typing inside it */
    if (gad->Activation & GACT_ACTIVEGADGET) {
        return;
    }
    const char *disp = url ? url : "";
    if (prefs_is_startpage_url(disp)) {
        disp = "";
    }
    if (url && app->num_tabs > 0 && app->active_tab_idx < app->num_tabs) {
        strncpy(app->tabs[app->active_tab_idx].url, url, sizeof(app->tabs[0].url) - 1);
        app->tabs[app->active_tab_idx].url[sizeof(app->tabs[0].url) - 1] = '\0';
        /* Only fallback to hostname for tab title if tab title is currently completely empty */
        if (app->tabs[app->active_tab_idx].title[0] == '\0' && disp[0] != '\0' && strncmp(disp, "http", 4) == 0) {
            const char *p = disp;
            if (strncmp(p, "http://", 7) == 0) p += 7;
            else if (strncmp(p, "https://", 8) == 0) p += 8;
            if (strncmp(p, "www.", 4) == 0) p += 4;
            char host[64];
            size_t hi = 0;
            while (*p && *p != '/' && *p != ':' && hi < sizeof(host) - 1) host[hi++] = *p++;
            host[hi] = '\0';
            if (host[0] != '\0') {
                snprintf(app->tabs[app->active_tab_idx].title, sizeof(app->tabs[0].title), "%s", host);
                gui_tabs_rebuild(app);
            }
        }
    }
    if (disp[0] == '\0' && gad->SpecialInfo) {
        struct StringInfo *si = (struct StringInfo *)gad->SpecialInfo;
        if (si->Buffer) {
            si->Buffer[0] = '\0';
        }
    }
    IIntuition->SetGadgetAttrs(gad,
                               app->win, NULL,
                               STRINGA_TextVal, disp,
                               TAG_DONE);

    gui_update_url_favicon(app, url);

    /* Update clean AmigaOS style window title */
    if (app->prefs.show_status_bar) {
        if (disp[0] != '\0') {
            snprintf(app->win_title, sizeof(app->win_title), "VortexRemoteBrowser: %s", disp);
        } else {
            snprintf(app->win_title, sizeof(app->win_title), "VortexRemoteBrowser");
        }
    } else {
        const char *st = app->status_text[0] ? app->status_text : GetVString(MSG_STATUS_READY_SHORT);
        if (disp[0] != '\0') {
            snprintf(app->win_title, sizeof(app->win_title), "VortexRemoteBrowser: %s [%s]", disp, st);
        } else {
            snprintf(app->win_title, sizeof(app->win_title), "VortexRemoteBrowser [%s]", st);
        }
    }
    if (app->win) {
        STRPTR scr_title = (app->custom_screen != NULL) ? (STRPTR)"VortexRemoteBrowser 1.1 (AmigaOS 4.1)" : (STRPTR)~0;
        IIntuition->SetWindowTitles(app->win, app->win_title, scr_title);
        if (app->prefs.dark_theme == 1) {
            gui_draw_dark_titlebar(app);
        }
    }
}

void app_set_status(const char *fmt, ...)
{
    if (!g_app.prefs.show_status_bar) {
        return; /* When status bar is disabled, do not perform expensive Intuition updates or title bar redraws */
    }

    char new_status[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(new_status, sizeof(new_status), fmt, args);
    va_end(args);

    if (strcmp(g_app.status_text, new_status) == 0) {
        return; /* No change, skip all expensive gadget and title redraws */
    }
    strncpy(g_app.status_text, new_status, sizeof(g_app.status_text) - 1);
    g_app.status_text[sizeof(g_app.status_text) - 1] = '\0';

    if (g_app.gad_status && g_app.win) {
        IIntuition->SetGadgetAttrs((struct Gadget *)g_app.gad_status,
                                   g_app.win, NULL,
                                   GA_Text, g_app.status_text,
                                   TAG_DONE);
    }
}

void app_set_gauge(int percent)
{
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;

    static int s_last_gauge_pct = -1;
    if (s_last_gauge_pct == percent) {
        return;
    }
    s_last_gauge_pct = percent;

    if (g_app.gad_gauge && g_app.win) {
        IIntuition->SetGadgetAttrs((struct Gadget *)g_app.gad_gauge,
                                   g_app.win, NULL,
                                   FUELGAUGE_Level, percent,
                                   TAG_DONE);
    }
}

void gui_launch_external_player(VortexApp *app, const char *video_url)
{
    if (!app || !video_url || !video_url[0]) return;

    const char *tpl = app->prefs.external_player_cmd;
    if (!tpl || !tpl[0]) {
        tpl = "APPDIR:V-MPlayer \"%s\"";
    }

    /* If user kept default APPDIR:V-MPlayer, probe common AmigaOS 4 players as fallbacks */
    char resolved_tpl[300];
    strncpy(resolved_tpl, tpl, sizeof(resolved_tpl) - 1);
    resolved_tpl[sizeof(resolved_tpl) - 1] = '\0';

    if (strncmp(resolved_tpl, "APPDIR:V-MPlayer", 16) == 0) {
        BPTR lock = IDOS->Lock("APPDIR:V-MPlayer", SHARED_LOCK);
        if (lock) {
            IDOS->UnLock(lock);
        } else {
            static const char * const fallbacks[] = {
                "APPDIR:MPlayer",
                "APPDIR:MickJT-MPlayer",
                "APPDIR:Emotion",
                "C:V-MPlayer",
                "C:MPlayer",
                NULL
            };
            for (int i = 0; fallbacks[i] != NULL; i++) {
                BPTR flock = IDOS->Lock(fallbacks[i], SHARED_LOCK);
                if (flock) {
                    IDOS->UnLock(flock);
                    snprintf(resolved_tpl, sizeof(resolved_tpl), "%s \"%%s\"", fallbacks[i]);
                    break;
                }
            }
        }
    }

    char cmd[2400];
    const char *placeholder = strstr(resolved_tpl, "%s");
    if (placeholder) {
        size_t prefix_len = (size_t)(placeholder - resolved_tpl);
        if (prefix_len > sizeof(cmd) - 1) prefix_len = sizeof(cmd) - 1;
        memcpy(cmd, resolved_tpl, prefix_len);
        cmd[prefix_len] = '\0';
        snprintf(cmd + prefix_len, sizeof(cmd) - prefix_len, "%s%s",
                 video_url, placeholder + 2);
    } else {
        snprintf(cmd, sizeof(cmd), "%s \"%s\"", resolved_tpl, video_url);
    }

    app_set_status("%s: %.96s", GetVString(MSG_STATUS_EXT_PLAYER), video_url);

    BPTR nil_in  = IDOS->Open("NIL:", MODE_OLDFILE);
    BPTR nil_out = IDOS->Open("NIL:", MODE_NEWFILE);
    int32 rc = IDOS->SystemTags(cmd,
                                SYS_Asynch,    TRUE,
                                SYS_Input,     nil_in,
                                SYS_Output,    nil_out,
                                NP_StackSize,  524288,
                                TAG_DONE);
    if (rc != 0) {
        if (nil_in)  IDOS->Close(nil_in);
        if (nil_out) IDOS->Close(nil_out);
    }
}

