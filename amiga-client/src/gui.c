#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/chooser.h>
#include <proto/bsdsocket.h>
#include <proto/popupmenu.h>

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
#include <intuition/gadgetclass.h>
#include <intuition/icclass.h>
#include <intuition/pointerclass.h>
#include <devices/inputevent.h>
#include <sys/select.h>
#include <sys/time.h>

#include <proto/clicktab.h>

#include <images/bitmap.h>
#include <images/label.h>
#include <libraries/gadtools.h>
#include <graphics/modeid.h>
#include <graphics/displayinfo.h>

#include "gui.h"
#include "net.h"
#include "renderer.h"
#include "favorites.h"
#include "gui_prefs.h"
#include "gui_passwords.h"
#include "clipboard.h"

extern struct SocketIFace *ISocket;

static void handle_menu_command(VortexApp *app, uint32 item_id);
static void open_main_menu_popup(VortexApp *app, int16_t mx, int16_t my);
static void prompt_server_address(VortexApp *app);

/* Amiga Standard Pull-Down Menu definition */
static struct NewMenu s_main_menu[] = {
    { NM_TITLE, (STRPTR)"Projekt",                                     NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Neu (Startseite)",                            (STRPTR)"N", 0, 0, (APTR)MID_PROJ_NEW },
    { NM_ITEM,  (STRPTR)"URL oeffnen...",                              (STRPTR)"O", 0, 0, (APTR)MID_PROJ_OPEN },
    { NM_ITEM,  (STRPTR)"Neuer Tab",                                   (STRPTR)"T", 0, 0, (APTR)MID_TAB_NEW },
    { NM_ITEM,  (STRPTR)"Aktuellen Tab schliessen",                    (STRPTR)"W", 0, 0, (APTR)MID_TAB_CLOSE },
    { NM_ITEM,  NM_BARLABEL,                                           NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Passwort-Manager...",                         (STRPTR)"K", 0, 0, (APTR)MID_PROJ_PASSWORDS },
    { NM_ITEM,  (STRPTR)"Auto-Login fuer Seite...",                    (STRPTR)"L", 0, 0, (APTR)MID_PROJ_AUTOLOGIN },
    { NM_ITEM,  NM_BARLABEL,                                           NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Mit Raspberry Pi verbinden (192.168.178.2:7788)", (STRPTR)"1", 0, 0, (APTR)MID_SET_SERVER_RPI4 },
    { NM_ITEM,  (STRPTR)"Mit Host-PC verbinden (192.168.178.60:9757)", (STRPTR)"2", 0, 0, (APTR)MID_SET_SERVER_HOSTPC },
    { NM_ITEM,  (STRPTR)"Verbindung trennen",                          (STRPTR)"D", 0, 0, (APTR)MID_PROJ_DISCONNECT },
    { NM_ITEM,  NM_BARLABEL,                                           NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Einstellungen...",                            (STRPTR)"E", 0, 0, (APTR)MID_SETTINGS },
    { NM_ITEM,  (STRPTR)"Ueber VortexBrowse...",                       (STRPTR)"?", 0, 0, (APTR)MID_PROJ_ABOUT },
    { NM_ITEM,  NM_BARLABEL,                                           NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Beenden",                                     (STRPTR)"Q", 0, 0, (APTR)MID_PROJ_QUIT },

    { NM_TITLE, (STRPTR)"Bearbeiten",                                  NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Ausschneiden",                                (STRPTR)"X", 0, 0, (APTR)MID_EDIT_CUT },
    { NM_ITEM,  (STRPTR)"Kopieren",                                    (STRPTR)"C", 0, 0, (APTR)MID_EDIT_COPY },
    { NM_ITEM,  (STRPTR)"Einfuegen",                                   (STRPTR)"V", 0, 0, (APTR)MID_EDIT_PASTE },

    { NM_TITLE, (STRPTR)"Navigation",                                  NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Zurueck",                                     (STRPTR)"B", 0, 0, (APTR)MID_NAV_BACK },
    { NM_ITEM,  (STRPTR)"Vorwaerts",                                   (STRPTR)"F", 0, 0, (APTR)MID_NAV_FORWARD },
    { NM_ITEM,  (STRPTR)"Neu laden",                                   (STRPTR)"R", 0, 0, (APTR)MID_NAV_RELOAD },
    { NM_ITEM,  (STRPTR)"Stopp",                                       (STRPTR)"S", 0, 0, (APTR)MID_NAV_STOP },
    { NM_ITEM,  (STRPTR)"Startseite",                                  (STRPTR)"H", 0, 0, (APTR)MID_NAV_HOME },
    { NM_ITEM,  NM_BARLABEL,                                           NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Schrift groesser (+)",                        (STRPTR)"+", 0, 0, (APTR)MID_FONT_LARGER },
    { NM_ITEM,  (STRPTR)"Schrift kleiner (-)",                         (STRPTR)"-", 0, 0, (APTR)MID_FONT_SMALLER },
    { NM_ITEM,  (STRPTR)"Standard-Schriftgroesse (16px)",              (STRPTR)"0", 0, 0, (APTR)MID_FONT_RESET },

    { NM_TITLE, (STRPTR)"Lesezeichen",                                 NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Lesezeichen hinzufuegen",                     (STRPTR)"A", 0, 0, (APTR)MID_FAV_ADD },
    { NM_ITEM,  (STRPTR)"Zu Schnellwahl hinzufuegen",                  (STRPTR)"D", 0, 0, (APTR)MID_QUICK_ADD_CURRENT },
    { NM_ITEM,  NM_BARLABEL,                                           NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Amiga-News.de",                               NULL,        0, 0, (APTR)MID_FAV_AMIGANEWS },
    { NM_ITEM,  (STRPTR)"OS4Depot.net",                                NULL,        0, 0, (APTR)MID_FAV_OS4DEPOT },
    { NM_ITEM,  (STRPTR)"Amigans.net",                                 NULL,        0, 0, (APTR)MID_FAV_AMIGANS },
    { NM_ITEM,  (STRPTR)"Aminet.net",                                  NULL,        0, 0, (APTR)MID_FAV_AMINET },
    { NM_ITEM,  (STRPTR)"YouTube",                                     NULL,        0, 0, (APTR)MID_FAV_YOUTUBE },
    { NM_ITEM,  (STRPTR)"Wikipedia",                                   NULL,        0, 0, (APTR)MID_FAV_WIKIPEDIA },

    { NM_END,   NULL,                                                  NULL,        0, 0, NULL }
};

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
            TAG_DONE);
    } else {
        return IIntuition->NewObject(NULL, "button.gadget",
            GA_ID,        gid,
            GA_RelVerify, TRUE,
            GA_Text,      fallback_text,
            GA_HintInfo,  hint,
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

    static const char *const dir_prefixes[] = {
        "PROGDIR:icons/",
        "PROGDIR:vortexbrowser/icons/",
        "Work2:vortexbrowser/icons/",
        "Work2:icons/",
        "TBIMAGES:"
    };

    for (int i = 0; i < num_c; i++) {
        const char *cand = candidates[i];
        if (!cand || cand[0] == '\0') continue;

        char path[160];
        BPTR lk = ZERO;
        for (size_t d = 0; d < sizeof(dir_prefixes)/sizeof(dir_prefixes[0]); d++) {
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
    }
    return NULL;
}

static struct Image *load_favicon_image(const char *url, struct Screen *screen)
{
    return load_quicklink_image(NULL, url, screen);
}

static void gui_update_url_favicon(VortexApp *app, const char *url)
{
    if (!app || !app->gad_url_favicon || !app->win) return;

    struct Screen *scr = (app->win && app->win->WScreen) ? app->win->WScreen : app->custom_screen;
    struct Image *img = load_favicon_image(url, scr);
    if (img) {
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

    /* Clean horizontal button padding for text, matching Odyssey Web Browser */
    snprintf(s_quick_btn_labels[idx], sizeof(s_quick_btn_labels[idx]), "  %s  ", label ? label : "");
    snprintf(s_quick_btn_hints[idx], sizeof(s_quick_btn_hints[idx]), "%s", url ? url : "");

    return IIntuition->NewObject(NULL, "button.gadget",
        GA_ID,                 gid,
        GA_RelVerify,          TRUE,
        GA_Text,               (Tag)s_quick_btn_labels[idx],
        BUTTON_BevelStyle,     BVS_THIN,
        BUTTON_Justification,  BCJ_CENTER,
        BUTTON_HorizAlignment, BALIGN_CENTER,
        BUTTON_VertAlignment,  BALIGN_CENTER,
        BUTTON_DomainString,   (Tag)s_quick_btn_labels[idx],
        GA_HintInfo,           (Tag)s_quick_btn_hints[idx],
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

static void dispatch_viewport_mouse_btn(VortexApp *app, int16_t win_x, int16_t win_y, uint16_t btn, BOOL is_down,
                                        uint32 secs, uint32 micros)
{
    if (!app || !app->connected) {
        return;
    }

    int16_t rx = 0, ry = 0;
    if (!renderer_map_window_to_remote(app, win_x, win_y, &rx, &ry)) {
        if (!is_down && s_mouse_lmb_down && btn == 1) {
            s_mouse_lmb_down = FALSE;
            net_send_mouse_button(app, rx, ry, 1, FALSE);
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

    if (btn == 3 && is_down) {
        memset(&app->ctx_menu, 0, sizeof(MsgContextMenu));
        app->ctx_menu_ready   = FALSE;
        app->ctx_menu_pending = TRUE;
        app_set_status("Kontextmenue wird geoeffnet (%d, %d)...", (int)rx, (int)ry);
        net_send_click(app, rx, ry, 3);
        return;
    }

    if (btn == 1) {
        s_mouse_lmb_down = is_down;
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
        if (app->renderer.has_frame && app->renderer.src_bm[0]) {
            renderer_present(app, gpr->gpr_RPort, area);
        } else {
            IGraphics->SetAPen(gpr->gpr_RPort, 0);
            IGraphics->RectFill(gpr->gpr_RPort, area->Left, area->Top, area->Left + area->Width - 1, area->Top + area->Height - 1);
        }
    }
    return 0;
}

/* WINDOW_IDCMPHook callback: captures mouse buttons, Odyssey mouse wheel, and window geometry inside viewport */
static uint32 idcmp_hook_func(struct Hook *hook, Object *win_obj, struct IntuiMessage *imsg)
{
    (void)win_obj;
    VortexApp *app = (VortexApp *)hook->h_Data;
    if (!app || !imsg) {
        return 0;
    }

    if (imsg->Class == IDCMP_CHANGEWINDOW) {
        prefs_capture_window_box(app);
        return 0;
    }

    if (!app->connected) {
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
            /* Odyssey Web Browser (owbbrowserclass.cpp + BCPlatformWheelEventMorphOS.cpp):
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
            if (app->connected) {
                app->pending_win_mouse_x = imsg->MouseX;
                app->pending_win_mouse_y = imsg->MouseY;
                app->pending_mouse_move  = TRUE;
            }
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

static void init_server_chooser(VortexApp *app)
{
    IExec->NewList(&app->server_chooser_list);

    struct Node *n0 = IChooser->AllocChooserNode(
        CNA_CopyText, TRUE,
        CNA_Text,     "RasPi 4 (192.168.178.2:7788)",
        TAG_DONE);
    if (n0) IExec->AddTail(&app->server_chooser_list, n0);

    struct Node *n1 = IChooser->AllocChooserNode(
        CNA_CopyText, TRUE,
        CNA_Text,     "Host-PC (192.168.178.60:9757)",
        TAG_DONE);
    if (n1) IExec->AddTail(&app->server_chooser_list, n1);

    struct Node *n2 = IChooser->AllocChooserNode(
        CNA_CopyText, TRUE,
        CNA_Text,     "Server manuell...",
        TAG_DONE);
    if (n2) IExec->AddTail(&app->server_chooser_list, n2);
}

static void free_server_chooser(VortexApp *app)
{
    struct Node *node = IExec->RemHead(&app->server_chooser_list);
    while (node) {
        IChooser->FreeChooserNode(node);
        node = IExec->RemHead(&app->server_chooser_list);
    }
}

static void prompt_server_address(VortexApp *app)
{
    if (!app) return;

    char current_addr[160];
    snprintf(current_addr, sizeof(current_addr), "%s:%u",
             app->server_host, (unsigned)app->server_port);

    Object *gad_addr = IIntuition->NewObject(NULL, "string.gadget",
        GA_ID,            1,
        GA_RelVerify,     TRUE,
        GA_TabCycle,      FALSE,
        GA_HintInfo,      "Server IP und Port (z.B. 192.168.178.2:7788)",
        STRINGA_MaxChars, 128,
        STRINGA_TextVal,  current_addr,
        TAG_DONE);

    Object *btn_ok = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID,        2,
        GA_RelVerify, TRUE,
        GA_Text,      "Verbinden",
        TAG_DONE);

    Object *btn_cancel = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID,        3,
        GA_RelVerify, TRUE,
        GA_Text,      "Abbrechen",
        TAG_DONE);

    Object *req_layout = IIntuition->NewObject(NULL, "layout.gadget",
        LAYOUT_Orientation,   LAYOUT_ORIENT_VERT,
        LAYOUT_SpaceOuter,    TRUE,
        LAYOUT_DeferLayout,   TRUE,
        LAYOUT_AddChild,      IIntuition->NewObject(NULL, "button.gadget",
                                  GA_Text,              (Tag)"VortexBrowse Server-Adresse eingeben (IP:Port):",
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
        WA_Title,             "Server konfigurieren",
        WA_ScreenTitle,       "VortexBrowse Einstellungen",
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
        IExec->Wait(sigmask | SIGBREAKF_CTRL_C);
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
            if (app->gad_server_chooser && app->win) {
                uint16 sel = 2; /* manual */
                if (strcmp(app->server_host, VORTEXBROWSE_RPI_HOST) == 0 && app->server_port == VORTEXBROWSE_RPI_PORT) sel = 0;
                else if (strcmp(app->server_host, VORTEXBROWSE_HOSTPC_HOST) == 0 && app->server_port == VORTEXBROWSE_HOSTPC_PORT) sel = 1;
                IIntuition->SetGadgetAttrs((struct Gadget *)app->gad_server_chooser,
                                           app->win, NULL,
                                           CHOOSER_Selected, sel,
                                           TAG_DONE);
            }
            net_connect(app, app->server_host, app->server_port);
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
            gui_update_url_bar(app, "");
            net_send_url(app, "@HOME");
            break;

        case MID_PROJ_OPEN:
            if (app->gad_url && app->win) {
                IIntuition->ActivateGadget((struct Gadget *)app->gad_url, app->win, NULL);
            }
            break;

        case MID_SET_SERVER_RPI4:
            strncpy(app->server_host, VORTEXBROWSE_RPI_HOST, sizeof(app->server_host) - 1);
            app->server_port = VORTEXBROWSE_RPI_PORT;
            if (app->gad_server_chooser && app->win) {
                IIntuition->SetGadgetAttrs((struct Gadget *)app->gad_server_chooser, app->win, NULL,
                                           CHOOSER_Selected, 0,
                                           TAG_DONE);
            }
            net_connect(app, app->server_host, app->server_port);
            break;

        case MID_SET_SERVER_HOSTPC:
            strncpy(app->server_host, VORTEXBROWSE_HOSTPC_HOST, sizeof(app->server_host) - 1);
            app->server_port = VORTEXBROWSE_HOSTPC_PORT;
            if (app->gad_server_chooser && app->win) {
                IIntuition->SetGadgetAttrs((struct Gadget *)app->gad_server_chooser, app->win, NULL,
                                           CHOOSER_Selected, 1,
                                           TAG_DONE);
            }
            net_connect(app, app->server_host, app->server_port);
            break;

        case MID_SET_SERVER_CUSTOM:
            prompt_server_address(app);
            break;

        case MID_PROJ_DISCONNECT:
            net_disconnect(app);
            app_set_status("Verbindung getrennt.");
            break;

        case MID_SETTINGS:
            gui_prefs_open(app);
            break;

        case MID_TOGGLE_SCREEN:
            app->prefs.resolution.use_custom_screen = !app->prefs.resolution.use_custom_screen;
            prefs_save(app, TRUE);
            app->reopen_screen_needed = TRUE;
            break;

        case MID_QUICK_ADD_CURRENT:
            prompt_add_quicklink(app);
            break;

        case MID_PROJ_ABOUT: {
            char about_buf[1024];
            const char *conn_str = app->connected ? "Verbunden" : "Getrennt";
            const char *fmt_str = (app->renderer.src_format == FMT_YUV420P)
                ? (app->renderer.src_bm_is_yuv ? "Planar YUV420p 12bpp (Hardware DMA+CompositeTags)" : "Planar YUV420p 12bpp (Software RGB565)")
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

            const char *scr_type = app->custom_screen ? "Eigener RTG-Bildschirm" : "Workbench-Bildschirm";

            snprintf(about_buf, sizeof(about_buf),
                "VortexBrowse fuer AmigaOS 4\n"
                "Version 2.0 (Odyssey Edition)\n\n"
                "=== SERVER-INFORMATIONEN ===\n"
                "Server-Adresse: %s:%u\n"
                "Verbindungsstatus: %s\n"
                "Server-Engine: Chromium (Linux Headless Streaming)\n\n"
                "=== AUFLOESUNG & GRAFIK ===\n"
                "Bildschirm: %ux%u (%u-Bit, %s)\n"
                "Fenstergroesse: %ux%u (Pos: %d,%d)\n"
                "Browser-Viewport: %ux%u Pixel (1:1 Auto-Sync)\n"
                "Server-Stream: %ux%u (%s)\n"
                "Dekodierte Frames: %u\n\n"
                "=== AUDIO & OPTIMIERUNG ===\n"
                "Audio-Engine: AHI 16-Bit Stereo (22050 Hz PCM Low-Latency)\n"
                "AltiVec SIMD: Aktiviert (PPC 7450 / 7455)\n"
                "Kompression: LZ4 Realtime Stream Decompressor",
                app->server_host, (unsigned)app->server_port,
                conn_str,
                (unsigned)sw, (unsigned)sh, (unsigned)sd, scr_type,
                app->win ? (unsigned)app->win->Width : 0, app->win ? (unsigned)app->win->Height : 0,
                app->win ? (int)app->win->LeftEdge : 0, app->win ? (int)app->win->TopEdge : 0,
                (unsigned)vp_w, (unsigned)vp_h,
                (unsigned)app->renderer.src_width, (unsigned)app->renderer.src_height, fmt_str,
                (unsigned)app->renderer.frames_decoded);

            struct EasyStruct es;
            memset(&es, 0, sizeof(es));
            es.es_StructSize   = sizeof(struct EasyStruct);
            es.es_Title        = (STRPTR)"Ueber VortexBrowse";
            es.es_TextFormat   = (STRPTR)"%s";
            es.es_GadgetFormat = (STRPTR)"OK";
            IIntuition->EasyRequest(app->win, &es, NULL, about_buf);
            break;
        }

        case MID_PROJ_PASSWORDS:
            gui_open_password_manager(app);
            break;

        case MID_PROJ_AUTOLOGIN:
            gui_do_autologin_current_domain(app);
            break;

        case MID_TAB_NEW:
            gui_tab_add(app, "Neuer Tab", "", TRUE);
            break;

        case MID_TAB_CLOSE:
            if (app->num_tabs > 0) {
                gui_tab_close(app, app->active_tab_idx);
            }
            break;

        case MID_PROJ_QUIT:
            app->running = FALSE;
            break;

        case MID_EDIT_CUT:
            net_send_key(app, INPUT_KEY_DOWN, 24, "Cut");
            break;

        case MID_EDIT_COPY:
            net_send_key(app, INPUT_KEY_DOWN, 3, "Copy");
            break;

        case MID_EDIT_PASTE: {
            char *clip_str = clipboard_read_text();
            if (clip_str) {
                net_send_clipboard(app, clip_str);
                free(clip_str);
            }
            break;
        }

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
            app_set_status("Ladevorgang angehalten.");
            break;

        case MID_FONT_LARGER: {
            uint16_t fs = app->prefs.font.web_font_size ? app->prefs.font.web_font_size : 16;
            if (fs < 32) fs += 2;
            app->prefs.font.web_font_size = fs;
            prefs_save(app, TRUE);
            if (app->connected) {
                char cmd[32];
                snprintf(cmd, sizeof(cmd), "@FONTSIZE:%u", (unsigned)fs);
                net_send_url(app, cmd);
            }
            app_set_status("Browser-Schriftgroesse: %u px", (unsigned)fs);
            break;
        }

        case MID_FONT_SMALLER: {
            uint16_t fs = app->prefs.font.web_font_size ? app->prefs.font.web_font_size : 16;
            if (fs > 10) fs -= 2;
            app->prefs.font.web_font_size = fs;
            prefs_save(app, TRUE);
            if (app->connected) {
                char cmd[32];
                snprintf(cmd, sizeof(cmd), "@FONTSIZE:%u", (unsigned)fs);
                net_send_url(app, cmd);
            }
            app_set_status("Browser-Schriftgroesse: %u px", (unsigned)fs);
            break;
        }

        case MID_FONT_RESET: {
            app->prefs.font.web_font_size = 16;
            app->prefs.font.web_zoom_pct  = 100;
            prefs_save(app, TRUE);
            if (app->connected) {
                net_send_url(app, "@ZOOM:100");
                net_send_url(app, "@FONTSIZE:16");
            }
            app_set_status("Browser-Schriftgroesse auf Standard (16 px / 100%%) zurueckgesetzt.");
            break;
        }

        case MID_FAV_ADD: {
            char url_buf[VB_MAX_URL_LEN];
            get_string_gadget_text(app->gad_url, url_buf, sizeof(url_buf));
            const char *u = (url_buf[0] != '\0') ? url_buf : app->current_url;
            if (u && u[0] != '\0') {
                if (favorites_add(app, u, u)) {
                    favorites_save(app);
                    favorites_rebuild_chooser_list(app);
                    app_set_status("Zu Lesezeichen hinzugefuegt: %s", u);
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
            IDOS->SetVar("VortexBrowse/server_host", app->server_host, -1, GVF_GLOBAL_ONLY | GVF_SAVE_VAR);
            IDOS->SetVar("VortexBrowse/server_port", port_str, -1, GVF_GLOBAL_ONLY | GVF_SAVE_VAR);
            app_set_status("Einstellungen dauerhaft gespeichert (ENVARC:).");
            break;
        }

        case MID_RES_1024_768:
            prefs_set_resolution(app, 0, 0, 0);
            prefs_save(app, TRUE);
            app_set_status("Aufloesung: 1024 x 768");
            break;

        case MID_RES_1280_720:
            prefs_set_resolution(app, 1, 0, 0);
            prefs_save(app, TRUE);
            app_set_status("Aufloesung: 1280 x 720");
            break;

        case MID_RES_1920_1080:
            prefs_set_resolution(app, 2, 0, 0);
            prefs_save(app, TRUE);
            app_set_status("Aufloesung: 1920 x 1080");
            break;

        case MID_RES_800_600:
            prefs_set_resolution(app, 3, 0, 0);
            prefs_save(app, TRUE);
            app_set_status("Aufloesung: 800 x 600");
            break;

        case MID_RES_1280_1024:
            prefs_set_resolution(app, 4, 0, 0);
            prefs_save(app, TRUE);
            app_set_status("Aufloesung: 1280 x 1024");
            break;

        case MID_RES_AUTO:
            prefs_set_resolution(app, 5, 0, 0);
            prefs_save(app, TRUE);
            app_set_status("Aufloesung: Automatisch");
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

        Object *menu_obj = PMMENU("VortexBrowse Menue"),
            PMA_MenuHandler, &pm_hook,
            PMITEM("Startseite / Neu"),               PMIA_ID, MID_PROJ_NEW, PMEND,
            PMITEM("URL oeffnen..."),                 PMIA_ID, MID_PROJ_OPEN, PMEND,
            PMITEM("Neuen Tab oeffnen"),              PMIA_ID, MID_TAB_NEW, PMEND,
            PMITEM("Aktuellen Tab schliessen"),       PMIA_ID, MID_TAB_CLOSE, PMEND,
            PMBAR(), PMEND,
            PMITEM("Passwort-Manager..."),            PMIA_ID, MID_PROJ_PASSWORDS, PMEND,
            PMITEM("Auto-Login fuer Seite..."),       PMIA_ID, MID_PROJ_AUTOLOGIN, PMEND,
            PMBAR(), PMEND,
            PMITEM("Server: Raspberry Pi 4 (192.168.178.2:7788)"),  PMIA_ID, MID_SET_SERVER_RPI4, PMEND,
            PMITEM("Server: Host-PC (192.168.178.60:9757)"),       PMIA_ID, MID_SET_SERVER_HOSTPC, PMEND,
            PMITEM("Server manuell konfigurieren..."), PMIA_ID, MID_SET_SERVER_CUSTOM, PMEND,
            PMITEM("Verbindung trennen"),             PMIA_ID, MID_PROJ_DISCONNECT, PMEND,
            PMBAR(), PMEND,
            PMITEM("Zurueck"),                        PMIA_ID, MID_NAV_BACK, PMEND,
            PMITEM("Vorwaerts"),                      PMIA_ID, MID_NAV_FORWARD, PMEND,
            PMITEM("Neu laden"),                      PMIA_ID, MID_NAV_RELOAD, PMEND,
            PMITEM("Stopp"),                          PMIA_ID, MID_NAV_STOP, PMEND,
            PMBAR(), PMEND,
            PMITEM("Lesezeichen hinzufuegen"),        PMIA_ID, MID_FAV_ADD, PMEND,
            PMITEM("Zu Schnellwahl hinzufuegen"),     PMIA_ID, MID_QUICK_ADD_CURRENT, PMEND,
            PMITEM("Amiga-News.de"),                  PMIA_ID, MID_FAV_AMIGANEWS, PMEND,
            PMITEM("OS4Depot.net"),                   PMIA_ID, MID_FAV_OS4DEPOT, PMEND,
            PMITEM("Amigans.net"),                    PMIA_ID, MID_FAV_AMIGANS, PMEND,
            PMITEM("Aminet.net"),                     PMIA_ID, MID_FAV_AMINET, PMEND,
            PMITEM("YouTube"),                        PMIA_ID, MID_FAV_YOUTUBE, PMEND,
            PMITEM("Wikipedia"),                      PMIA_ID, MID_FAV_WIKIPEDIA, PMEND,
            PMBAR(), PMEND,
            PMITEM("Voreinstellungen..."),            PMIA_ID, MID_SETTINGS, PMEND,
            PMITEM("Einstellungen speichern"),        PMIA_ID, MID_SET_SAVE, PMEND,
            PMITEM("Ueber VortexBrowse..."),          PMIA_ID, MID_PROJ_ABOUT, PMEND,
            PMITEM("Beenden"),                        PMIA_ID, MID_PROJ_QUIT, PMEND,
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
        es.es_Title        = (STRPTR)"VortexBrowse Optionen";
        es.es_TextFormat   = (STRPTR)"Aktion waehlen:";
        es.es_GadgetFormat = (STRPTR)"RasPi 4 (7788)|Host-PC (9757)|Manuell...|Startseite|Lesez.+|Ueber|Abbrechen";
        LONG c = IIntuition->EasyRequest(app->win, &es, NULL);
        if (c == 1) selected_id = MID_SET_SERVER_RPI4;
        else if (c == 2) selected_id = MID_SET_SERVER_HOSTPC;
        else if (c == 3) selected_id = MID_SET_SERVER_CUSTOM;
        else if (c == 4) selected_id = MID_PROJ_NEW;
        else if (c == 5) selected_id = MID_FAV_ADD;
        else if (c == 6) selected_id = MID_PROJ_ABOUT;
    }

    if (selected_id != 0) {
        handle_menu_command(app, selected_id);
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
 * Odyssey-style Tabbed Browsing Functions
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

static void tabs_build_list(VortexApp *app, struct List *out)
{
    IExec->NewList(out);
    if (!IClickTab) return;

    for (int i = 0; i < app->num_tabs; i++) {
        struct Node *quarantine = NULL;
        struct Node *n = IClickTab->AllocClickTabNode(
            TNA_Text,        (Tag)app->tabs[i].title,
            TNA_Number,      (uint16)i,
            TNA_CloseGadget, (app->num_tabs > 1),
            TAG_DONE);
        if (n && n == s_handled_close_node) {
            /* Allocator handed us the stale address: keep it aside, get another */
            quarantine = n;
            n = IClickTab->AllocClickTabNode(
                TNA_Text,        (Tag)app->tabs[i].title,
                TNA_Number,      (uint16)i,
                TNA_CloseGadget, (app->num_tabs > 1),
                TAG_DONE);
            IClickTab->FreeClickTabNode(quarantine);
        }
        if (n) {
            IExec->AddTail(out, n);
        }
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
        app->active_tab_idx = 0;
        app->tabs[0].id = 0;
        strncpy(app->tabs[0].title, "Startseite", sizeof(app->tabs[0].title) - 1);
        app->tabs[0].title[sizeof(app->tabs[0].title) - 1] = '\0';
        strncpy(app->tabs[0].url, app->current_url, sizeof(app->tabs[0].url) - 1);
        app->tabs[0].url[sizeof(app->tabs[0].url) - 1] = '\0';
        app->num_tabs = 1;
    }
    if (app->active_tab_idx >= app->num_tabs) {
        app->active_tab_idx = (uint16)(app->num_tabs - 1);
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
        IIntuition->SetGadgetAttrs((struct Gadget *)app->gad_clicktab, app->win, NULL,
                                   CLICKTAB_Labels,  (Tag)&app->tab_list,
                                   CLICKTAB_Current, (uint32)app->active_tab_idx,
                                   TAG_DONE);
        IIntuition->RefreshGList((struct Gadget *)app->gad_clicktab, app->win, NULL, 1);
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
        strncpy(app->tabs[0].title, "Startseite", sizeof(app->tabs[0].title) - 1);
        app->tabs[0].title[sizeof(app->tabs[0].title) - 1] = '\0';
        app->tabs[0].url[0] = '\0';
    }
    app->tabs[0].id     = 0;
    app->num_tabs       = 1;
    app->active_tab_idx = 0;
    gui_tabs_rebuild(app);
}

void gui_tab_add(VortexApp *app, const char *title, const char *url, BOOL switch_to)
{
    if (!app) return;
    if (app->num_tabs >= VB_MAX_TABS) {
        app_set_status("Maximal %d Tabs moeglich.", VB_MAX_TABS);
        return;
    }

    int idx = app->num_tabs++;
    app->tabs[idx].id = (uint32_t)idx;
    strncpy(app->tabs[idx].title, title ? title : "Neuer Tab", sizeof(app->tabs[0].title) - 1);
    app->tabs[idx].title[sizeof(app->tabs[0].title) - 1] = '\0';
    /* Empty URL = server portal/start page (never "about:blank") */
    strncpy(app->tabs[idx].url, url ? url : "", sizeof(app->tabs[0].url) - 1);
    app->tabs[idx].url[sizeof(app->tabs[0].url) - 1] = '\0';

    if (switch_to) {
        app->active_tab_idx = (uint16)idx;
        gui_update_url_bar(app, app->tabs[idx].url);
        char cmd[VB_MAX_URL_LEN + 16];
        snprintf(cmd, sizeof(cmd), "@NEWTAB:%s", app->tabs[idx].url);
        net_send_url(app, cmd);
    }

    gui_tabs_rebuild(app);
}

void gui_tab_switch(VortexApp *app, uint16 idx)
{
    if (!app || idx >= app->num_tabs) return;

    app->active_tab_idx = idx;
    gui_update_url_bar(app, app->tabs[idx].url);
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "@SWITCHTAB:%u", (unsigned)idx);
    net_send_url(app, cmd);
    app_set_status("Tab %d: %s", (int)idx + 1, app->tabs[idx].title);

    if (app->gad_clicktab && app->win) {
        IIntuition->SetGadgetAttrs((struct Gadget *)app->gad_clicktab, app->win, NULL,
                                   CLICKTAB_Current, (uint32)idx,
                                   TAG_DONE);
        IIntuition->RefreshGList((struct Gadget *)app->gad_clicktab, app->win, NULL, 1);
    }
}

void gui_tab_close(VortexApp *app, uint16 idx)
{
    if (!app || idx >= app->num_tabs) return;

    if (app->num_tabs <= 1) {
        /* Last tab: don't close, go back to the start page.
         * Must be "@HOME" - a literal 127.0.0.1:7788 URL would make Chromium
         * load the VortexBrowse binary protocol port as a web page. */
        strncpy(app->tabs[0].title, "Startseite", sizeof(app->tabs[0].title) - 1);
        app->tabs[0].title[sizeof(app->tabs[0].title) - 1] = '\0';
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
    memset(&s_render_hook, 0, sizeof(struct Hook));
    s_render_hook.h_Entry = (HOOKFUNC)space_render_func;
    s_render_hook.h_Data  = app;

    memset(&s_idcmp_hook, 0, sizeof(struct Hook));
    s_idcmp_hook.h_Entry = (HOOKFUNC)idcmp_hook_func;
    s_idcmp_hook.h_Data  = app;

    /* 0. Open custom Public Screen if configured */
    if (app->prefs.resolution.use_custom_screen) {
        if (!app->custom_screen) {
            const char *pubname = "VORTEXBROWSE";
            const char *title   = "VortexBrowse 2.0 (AmigaOS 4.1)";
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

                app->custom_screen = IIntuition->OpenScreenTags(NULL,
                    is_clone_wb ? SA_LikeWorkbench : TAG_IGNORE, TRUE,
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
                    TAG_DONE);

                if (!app->custom_screen) {
                    /* Retry without SA_Compositing in case driver rejects it */
                    app->custom_screen = IIntuition->OpenScreenTags(NULL,
                        is_clone_wb ? SA_LikeWorkbench : TAG_IGNORE, TRUE,
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
                        TAG_DONE);
                }
            }

            if (app->custom_screen) {
                IIntuition->PubScreenStatus(app->custom_screen, 0);
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

    /* AISS candidate lists */
    static const char *const c_back[]    = { "arrowleft", "arrowleftcolored", "arrowleftfilled", "previous", "back", "left" };
    static const char *const c_fwd[]     = { "arrowright", "arrowrightcolored", "arrowrightfilled", "next", "forward", "right" };
    static const char *const c_reload[]  = { "reload", "refresh", "update" };
    static const char *const c_stop[]    = { "stop", "cancel" };
    static const char *const c_home[]    = { "home", "start" };
    static const char *const c_clear[]   = { "clear", "delete", "cross" };
    static const char *const c_go[]      = { "go", "arrowright", "return" };
    static const char *const c_search[]  = { "search", "find" };
    static const char *const c_add_fav[] = { "bookmark_add", "add", "bookmark" };
    static const char *const c_connect[] = { "network", "connect" };

    /* ------------------------------------------------------------------------
     * Row 1: Odyssey Navigation, URL Address Bar & Web Search Bar
     * ------------------------------------------------------------------------ */
    Object *btn_back = create_toolbar_button(GID_BACK, c_back, 6, "<", "Zurueck (Historie)", pub_scr);
    Object *btn_fwd = create_toolbar_button(GID_FORWARD, c_fwd, 6, ">", "Vorwaerts (Historie)", pub_scr);
    Object *btn_reload = create_toolbar_button(GID_RELOAD, c_reload, 3, "Neu", "Seite neu laden", pub_scr);
    Object *btn_stop = create_toolbar_button(GID_STOP, c_stop, 2, "Stopp", "Ladevorgang stoppen", pub_scr);
    Object *btn_home = create_toolbar_button(GID_HOME, c_home, 2, "Start", "Startseite / Schnellwahl", pub_scr);

    struct Image *fav_img = load_favicon_image(app->current_url, pub_scr);
    app->gad_url_favicon = IIntuition->NewObject(NULL, "button.gadget",
        GA_ID,             GID_URL_FAVICON,
        GA_RelVerify,      TRUE,
        BUTTON_BevelStyle, BVS_NONE,
        BUTTON_Transparent,TRUE,
        fav_img ? GA_Image : TAG_IGNORE, (Tag)fav_img,
        GA_HintInfo,       "Webseiten-Favicon (Klick fuer Passwort-Manager / Auto-Login)",
        TAG_DONE);

    app->gad_url = IIntuition->NewObject(NULL, "string.gadget",
        GA_ID,            GID_URL,
        GA_RelVerify,     TRUE,
        GA_TabCycle,      FALSE,
        GA_HintInfo,      "Webadresse (URL) eingeben und Enter druecken",
        STRINGA_MaxChars, VB_MAX_URL_LEN - 1,
        STRINGA_TextVal,  app->current_url[0] ? app->current_url : "",
        TAG_DONE);

    Object *btn_clear_url = create_toolbar_button(GID_CLEAR_URL, c_clear, 3, "X", "Adresseingabe leeren", pub_scr);
    Object *btn_go = create_toolbar_button(GID_GO, c_go, 3, "Los", "URL aufrufen", pub_scr);

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

    Object *lbl_search = IIntuition->NewObject(NULL, "button.gadget",
        GA_Text,              (Tag)"Suche:",
        GA_ReadOnly,          TRUE,
        BUTTON_BevelStyle,    BVS_NONE,
        BUTTON_Transparent,   TRUE,
        BUTTON_Justification, BCJ_RIGHT,
        TAG_DONE);

    uint16 init_se_sel = app->prefs.search_engine;
    if (init_se_sel >= NUM_SEARCH_ENGINES) init_se_sel = 0;

    app->gad_search_chooser = IIntuition->NewObject(NULL, "chooser.gadget",
        GA_ID,              GID_SEARCH_CHOOSER,
        GA_RelVerify,       TRUE,
        CHOOSER_PopUp,      TRUE,
        CHOOSER_LabelArray, s_search_engine_labels,
        CHOOSER_Selected,   init_se_sel,
        CHOOSER_MaxLabels,  NUM_SEARCH_ENGINES,
        GA_HintInfo,        "Suchmaschine waehlen (Standard: DuckDuckGo)",
        TAG_DONE);

    app->gad_search = IIntuition->NewObject(NULL, "string.gadget",
        GA_ID,            GID_SEARCH,
        GA_RelVerify,     TRUE,
        GA_TabCycle,      FALSE,
        GA_HintInfo,      "Suchbegriff eingeben und Enter druecken",
        STRINGA_MaxChars, 128,
        STRINGA_TextVal,  "",
        TAG_DONE);

    Object *btn_search_go = create_toolbar_button(GID_SEARCH_GO, c_search, 2, "Los", "Web mit gewaehlter Suchmaschine durchsuchen", pub_scr);

    Object *nav_row1 = IIntuition->NewObject(NULL, "layout.gadget",
        LAYOUT_Orientation,  LAYOUT_ORIENT_HORIZ,
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
        CHILD_WeightedWidth, 48,
        LAYOUT_AddChild,     btn_clear_url,
        CHILD_WeightedWidth, 0,
        LAYOUT_AddChild,     btn_go,
        CHILD_WeightedWidth, 0,
        LAYOUT_AddChild,     lbl_search,
        CHILD_WeightedWidth, 0,
        LAYOUT_AddChild,     app->gad_search_chooser,
        CHILD_WeightedWidth, 0,
        LAYOUT_AddChild,     app->gad_search,
        CHILD_WeightedWidth, 20,
        LAYOUT_AddChild,     btn_search_go,
        CHILD_WeightedWidth, 0,
        TAG_DONE);

    /* ------------------------------------------------------------------------
     * Row 2: Odyssey Bookmarks Toolbar (Schnellwahl) & Tools
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

    const char *c_add_quick[] = { "list_add", "add", "plus", "new" };
    app->gad_quick_add = create_toolbar_button(GID_QUICK_ADD, c_add_quick, 4, "+", "Schnellwahl-Button hinzufuegen", pub_scr);

    Object *btn_add_fav = create_toolbar_button(GID_ADD_FAV, c_add_fav, 3, "+Lesez.", "Zu Lesezeichen hinzufuegen", pub_scr);

    app->gad_fav_chooser = IIntuition->NewObject(NULL, "chooser.gadget",
        GA_ID,             GID_FAV_CHOOSER,
        GA_RelVerify,      TRUE,
        CHOOSER_DropDown,  TRUE,
        CHOOSER_Title,     "Lesezeichen",
        CHOOSER_Labels,    &app->fav_chooser_list,
        CHOOSER_MaxLabels, 32,
        TAG_DONE);

    init_server_chooser(app);

    uint16 init_server_sel = 1; /* Default: Host-PC (192.168.178.60:9757) */
    if (strcmp(app->server_host, VORTEXBROWSE_RPI_HOST) == 0) {
        init_server_sel = 0;
    } else if (strcmp(app->server_host, VORTEXBROWSE_HOSTPC_HOST) == 0) {
        init_server_sel = 1;
    }

    app->gad_server_chooser = IIntuition->NewObject(NULL, "chooser.gadget",
        GA_ID,             GID_SERVER_CHOOSER,
        GA_RelVerify,      TRUE,
        CHOOSER_DropDown,  TRUE,
        CHOOSER_Labels,    &app->server_chooser_list,
        CHOOSER_Selected,  init_server_sel,
        CHOOSER_MaxLabels, 8,
        GA_HintInfo,       "Server waehlen: Raspberry Pi 4 (192.168.178.2:7788), Host-PC oder manuell",
        TAG_DONE);

    Object *btn_connect = create_toolbar_button(GID_CONNECT, c_connect, 2, "Verbinden", "Mit Server verbinden", pub_scr);

    uint16 avail_scr_w = pub_scr ? pub_scr->Width  : 1920;
    uint16 avail_scr_h = pub_scr ? pub_scr->Height : 1080;
    int16  bar_h       = pub_scr ? (int16)(pub_scr->BarHeight + 1) : 24;
    if (bar_h < 0 || bar_h > 120) bar_h = 24;

    /* Default Odyssey Web Browser window layout:
     * Directly below the screen's title bar (Left=0, Top=BarHeight+1) and edge-to-edge */
    int16  win_left = 0;
    int16  win_top  = bar_h;
    uint16 win_w    = avail_scr_w;
    uint16 win_h    = (avail_scr_h > (uint16)bar_h) ? (uint16)(avail_scr_h - (uint16)bar_h) : avail_scr_h;

    if (!app->custom_screen &&
        app->prefs.resolution.has_win_pos &&
        app->prefs.resolution.win_w >= 400 &&
        app->prefs.resolution.win_h >= 300) {
        win_left = app->prefs.resolution.win_x;
        win_top  = app->prefs.resolution.win_y;
        win_w    = app->prefs.resolution.win_w;
        win_h    = app->prefs.resolution.win_h;

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
     * If the buttons exceed the available row width, wrap into a new line (neue Zeile).
     */
    #define MAX_QL_ROWS 4
    Object *ql_row_objs[MAX_QL_ROWS] = { NULL };
    Object *row_items[MAX_QL_ROWS][VB_MAX_QUICKLINKS + 2];
    int row_item_count[MAX_QL_ROWS] = { 0 };

    int first_row_limit = (int)init_w - 460; /* Space for right-side tools */
    if (first_row_limit < 180) first_row_limit = 180;
    int next_row_limit  = (int)init_w - 40;  /* Full width for wrapped lines */
    if (next_row_limit < 200) next_row_limit = 200;

    int cur_row = 0;
    int cur_w   = 0;
    int cur_limit = first_row_limit;

    for (int i = 0; i < app->prefs.num_quick_links && i < VB_MAX_QUICKLINKS; i++) {
        if (!app->gad_quick_btn[i]) continue;

        int label_len = (int)strlen(app->prefs.quick_links[i].label);
        int item_w = label_len * 8 + 32;
        if (item_w < 40) item_w = 40;

        /* If adding this button exceeds current row width, start a new row */
        if (row_item_count[cur_row] > 0 && (cur_w + item_w > cur_limit) && cur_row < MAX_QL_ROWS - 1) {
            cur_row++;
            cur_w = 0;
            cur_limit = next_row_limit;
        }

        row_items[cur_row][row_item_count[cur_row]++] = app->gad_quick_btn[i];
        cur_w += item_w + 6;
    }

    if (app->gad_quick_add) {
        int add_w = 32;
        if (row_item_count[cur_row] > 0 && (cur_w + add_w > cur_limit) && cur_row < MAX_QL_ROWS - 1) {
            cur_row++;
            cur_w = 0;
            cur_limit = next_row_limit;
        }
        row_items[cur_row][row_item_count[cur_row]++] = app->gad_quick_add;
        cur_w += add_w;
    }

    int ql_row_count = cur_row + 1;

    for (int r = 0; r < ql_row_count; r++) {
        struct TagItem row_tags[64];
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

        if (r > 0) {
            /* Overflow rows get a trailing spacer so items remain left-aligned */
            Object *sp = IIntuition->NewObject(NULL, "space.gadget",
                SPACE_Transparent, TRUE,
                SPACE_MinWidth,    4,
                TAG_DONE);
            if (sp) {
                row_tags[t].ti_Tag = LAYOUT_AddChild;
                row_tags[t++].ti_Data = (Tag)sp;
                row_tags[t].ti_Tag = CHILD_WeightedWidth;
                row_tags[t++].ti_Data = 100;
            }
        }

        row_tags[t].ti_Tag = TAG_DONE;
        row_tags[t++].ti_Data = 0;

        ql_row_objs[r] = IIntuition->NewObjectA(NULL, "layout.gadget", row_tags);
    }

    Object *row2_spacer = IIntuition->NewObject(NULL, "space.gadget",
        SPACE_Transparent, TRUE,
        SPACE_MinWidth,    4,
        TAG_DONE);

    Object *nav_row2 = IIntuition->NewObject(NULL, "layout.gadget",
        LAYOUT_Orientation,  LAYOUT_ORIENT_HORIZ,
        LAYOUT_SpaceInner,   TRUE,
        LAYOUT_SpaceOuter,   FALSE,
        LAYOUT_AddChild,     ql_row_objs[0],
        CHILD_WeightedWidth, 0,
        LAYOUT_AddChild,     row2_spacer,
        CHILD_WeightedWidth, 100, /* Absorbs all remaining width -> keeps buttons strictly left-aligned */
        LAYOUT_AddChild,     btn_add_fav,
        CHILD_WeightedWidth, 0,
        LAYOUT_AddChild,     app->gad_fav_chooser,
        CHILD_WeightedWidth, 0,
        LAYOUT_AddChild,     app->gad_server_chooser,
        CHILD_WeightedWidth, 0,
        LAYOUT_AddChild,     btn_connect,
        CHILD_WeightedWidth, 0,
        TAG_DONE);

    /* ------------------------------------------------------------------------
     * Main interactive browser viewport (space.gadget)
     * ------------------------------------------------------------------------ */
    app->gad_space = IIntuition->NewObject(NULL, "space.gadget",
        GA_ID,             GID_SPACE,
        GA_ReadOnly,       TRUE,
        SPACE_MinWidth,    640,
        SPACE_MinHeight,   400,
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
            TAG_DONE);

        app->gad_gauge = IIntuition->NewObject(NULL, "fuelgauge.gadget",
            GA_ID,                 GID_GAUGE,
            FUELGAUGE_Min,         0,
            FUELGAUGE_Max,         100,
            FUELGAUGE_Level,       0,
            FUELGAUGE_Percent,     TRUE,
            FUELGAUGE_Ticks,       0,
            FUELGAUGE_Orientation, FGORIENT_HORIZ,
            TAG_DONE);

        status_group = IIntuition->NewObject(NULL, "layout.gadget",
            LAYOUT_Orientation,  LAYOUT_ORIENT_HORIZ,
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
     * Odyssey-style Tabs (clicktab.gadget)
     * ------------------------------------------------------------------------ */
    gui_tabs_init(app);

    app->gad_clicktab = IIntuition->NewObject(NULL, "clicktab.gadget",
        GA_ID,              GID_CLICKTAB,
        GA_RelVerify,       TRUE,
        CLICKTAB_Labels,    (Tag)&app->tab_list,
        CLICKTAB_Current,   (uint32)app->active_tab_idx,
        CLICKTAB_AutoFit,   TRUE,
        TAG_DONE);

    const char *c_add_tab[] = { "add", "plus", "new" };
    app->gad_tab_new = create_toolbar_button(GID_TAB_NEW, c_add_tab, 3, "+", "Neuen Tab oeffnen", pub_scr);

    Object *tab_row = IIntuition->NewObject(NULL, "layout.gadget",
        LAYOUT_Orientation,  LAYOUT_ORIENT_HORIZ,
        LAYOUT_SpaceInner,   FALSE,
        LAYOUT_SpaceOuter,   FALSE,
        LAYOUT_AddChild,     app->gad_clicktab,
        CHILD_WeightedWidth, 100,
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

    root_tags[rt].ti_Tag = LAYOUT_AddChild;
    root_tags[rt++].ti_Data = (Tag)nav_row1;
    root_tags[rt].ti_Tag = CHILD_WeightedHeight;
    root_tags[rt++].ti_Data = 0;

    root_tags[rt].ti_Tag = LAYOUT_AddChild;
    root_tags[rt++].ti_Data = (Tag)nav_row2;
    root_tags[rt].ti_Tag = CHILD_WeightedHeight;
    root_tags[rt++].ti_Data = 0;

    /* Add wrapped quicklink rows (neue Zeilen) if buttons exceeded width */
    for (int r = 1; r < ql_row_count; r++) {
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

    snprintf(app->win_title, sizeof(app->win_title), "VortexBrowse");

    app->win_obj = IIntuition->NewObject(NULL, "window.class",
        WA_Title,             app->win_title,
        (app->custom_screen != NULL) ? WA_ScreenTitle : TAG_IGNORE,
        (app->custom_screen != NULL) ? (Tag)"VortexBrowse 2.0 (AmigaOS 4.1)" : 0,
        WA_Activate,          TRUE,
        WA_DragBar,           TRUE,
        WA_DepthGadget,       TRUE,
        WA_CloseGadget,       TRUE,
        WA_SizeGadget,        TRUE,
        WA_SmartRefresh,      TRUE,
        WA_NewLookMenus,      TRUE,
        WA_AutoAdjust,        TRUE,
        WA_ReportMouse,       TRUE,
        WA_Left,              (uint32)win_left,
        WA_Top,               (uint32)win_top,
        WA_Width,             (uint32)win_w,
        WA_Height,            (uint32)win_h,
        WA_IDCMP,             IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_GADGETDOWN |
                              IDCMP_RAWKEY | IDCMP_VANILLAKEY | IDCMP_MOUSEBUTTONS |
                              IDCMP_MOUSEMOVE | IDCMP_EXTENDEDMOUSE | IDCMP_NEWSIZE |
                              IDCMP_CHANGEWINDOW | IDCMP_MENUPICK,
        (app->custom_screen != NULL) ? WA_CustomScreen : WA_PubScreenName,
        (app->custom_screen != NULL) ? (Tag)app->custom_screen : (Tag)"Workbench",
        WINDOW_ParentGroup,   root_layout,
        WINDOW_NewMenu,       s_main_menu,
        WINDOW_IDCMPHook,     &s_idcmp_hook,
        WINDOW_IDCMPHookBits, IDCMP_MOUSEBUTTONS | IDCMP_EXTENDEDMOUSE | IDCMP_MOUSEMOVE | IDCMP_CHANGEWINDOW,
        TAG_DONE);

    if (!app->win_obj) {
        DBG_LOG("[GUI] Failed to create Reaction window.class object!\n");
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

    if (app->custom_screen) {
        IIntuition->ScreenToFront(app->custom_screen);
    }

    /* Perform immediate initial Reaction layout pass */
    IIntuition->IDoMethod(app->win_obj, WM_RETHINK);

    /* If a decoded frame already exists in memory (rebuild/reopen), paint it immediately */
    if (app->win && app->gad_space && app->renderer.has_frame) {
        struct IBox *area = NULL;
        IIntuition->GetAttr(SPACE_AreaBox, app->gad_space, (uint32 *)&area);
        if (area && area->Width > 2 && area->Height > 2) {
            renderer_present(app, app->win->RPort, area);
        }
    }

    DBG_LOG("[GUI] Odyssey Web Browser style window opened (%dx%d).\n",
           (int)app->win->Width, (int)app->win->Height);

    /* Immediately sync inner rendering viewport dimensions to server */
    renderer_sync_viewport_size(app);

    return TRUE;
}

void gui_cleanup_window(VortexApp *app)
{
    if (!app) return;

    prefs_capture_window_box(app);

    if (app->gad_clicktab) {
        IIntuition->SetAttrs(app->gad_clicktab, CLICKTAB_Labels, (Tag)NULL, TAG_DONE);
    }
    if (app->gad_fav_chooser) {
        IIntuition->SetAttrs(app->gad_fav_chooser, CHOOSER_Labels, (Tag)NULL, TAG_DONE);
    }
    if (app->gad_server_chooser) {
        IIntuition->SetAttrs(app->gad_server_chooser, CHOOSER_Labels, (Tag)NULL, TAG_DONE);
    }
    app->gad_clicktab = NULL;
    app->gad_tab_new  = NULL;

    if (app->win_obj) {
        IIntuition->DisposeObject(app->win_obj);
        app->win_obj = NULL;
        app->win     = NULL;
    }
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
    free_server_chooser(app);
    favorites_free_chooser_list(app);

    for (int i = 0; i < VB_MAX_QUICKLINKS; i++) {
        app->gad_quick_btn[i] = NULL;
        app->gad_quick_del[i] = NULL;
    }
    app->gad_quick_add = NULL;
    app->gad_search_chooser = NULL;
    app->gad_space     = NULL;
    app->gad_status    = NULL;
    app->gad_gauge     = NULL;
}

void gui_cleanup(VortexApp *app)
{
    if (!app) return;

    gui_cleanup_window(app);

    /* Close custom public screen if one was opened */
    if (app->custom_screen) {
        IIntuition->PubScreenStatus(app->custom_screen, PSNF_PRIVATE);
        IIntuition->CloseScreen(app->custom_screen);
        app->custom_screen = NULL;
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

    app_set_status("Bildschirm aktiviert: %s (%ux%u)",
                   app->prefs.resolution.use_custom_screen ? "Eigener Screen" : "Workbench",
                   (unsigned)app->prefs.resolution.width,
                   (unsigned)app->prefs.resolution.height);
}

uint32 gui_get_signal_mask(VortexApp *app)
{
    uint32 sigmask = 0;
    if (app->win_obj) {
        IIntuition->GetAttr(WINDOW_SigMask, app->win_obj, &sigmask);
    }
    if (app->pw_win_obj) {
        uint32 pw_sig = 0;
        IIntuition->GetAttr(WINDOW_SigMask, app->pw_win_obj, &pw_sig);
        sigmask |= pw_sig;
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
    if (label[0] == '\0') strncpy(label, "Link", sizeof(label) - 1);

    struct EasyStruct es = {
        sizeof(struct EasyStruct),
        0,
        "Schnellwahl hinzufuegen",
        "Moechten Sie diese Seite zur Schnellwahl hinzufuegen?\n\nTitel: %s\nURL: %s",
        "Hinzufuegen|Schnellwahl anpassen...|Abbrechen"
    };

    LONG res = IIntuition->EasyRequest(app->win, &es, NULL, label, u);
    if (res == 1) {
        if (prefs_quicklink_add(app, label, u, "bookmark")) {
            prefs_save(app, TRUE);
            app_set_status("Schnellwahl hinzugefuegt: %s", label);
            app->reopen_gui_needed = TRUE;
        } else {
            app_set_status("Schnellwahl-Leiste voll (max. %d)!", VB_MAX_QUICKLINKS);
        }
    } else if (res == 2) {
        gui_prefs_open(app);
    }
}

void gui_handle_events(VortexApp *app)
{
    if (app->pw_win_obj) {
        gui_handle_password_manager_events(app);
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

            case WMHI_MENUPICK: {
                uint16 selection = (uint16)code;
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

            case WMHI_NEWSIZE:
                prefs_capture_window_box(app);
                if (app->win && app->gad_space) {
                    renderer_sync_viewport_size(app);
                }
                break;

            case WMHI_MOUSEBUTTONS: {
                if (!app->win) break;
                uint16 mcode = (uint16)(result & WMHI_KEYMASK);
                uint32 secs = 0, micros = 0;
                IIntuition->CurrentTime(&secs, &micros);
                if (mcode == SELECTDOWN) {
                    dispatch_viewport_mouse_btn(app, app->win->MouseX, app->win->MouseY, 1, TRUE, secs, micros);
                } else if (mcode == SELECTUP) {
                    dispatch_viewport_mouse_btn(app, app->win->MouseX, app->win->MouseY, 1, FALSE, secs, micros);
                } else if (mcode == MIDDLEDOWN) {
                    dispatch_viewport_mouse_btn(app, app->win->MouseX, app->win->MouseY, 2, TRUE, secs, micros);
                } else if (mcode == MIDDLEUP) {
                    dispatch_viewport_mouse_btn(app, app->win->MouseX, app->win->MouseY, 2, FALSE, secs, micros);
                } else if (mcode == MENUDOWN) {
                    dispatch_viewport_mouse_btn(app, app->win->MouseX, app->win->MouseY, 3, TRUE, secs, micros);
                }
                break;
            }

            case WMHI_VANILLAKEY: {
                uint16 ascii = (uint16)(result & WMHI_KEYMASK);
                if (ascii == 0x03) { /* Ctrl+C: Copy */
                    net_send_key(app, INPUT_KEY_DOWN, 3, "Copy");
                } else if (ascii == 0x16) { /* Ctrl+V: Paste */
                    char *clip_str = clipboard_read_text();
                    if (clip_str) {
                        net_send_clipboard(app, clip_str);
                        free(clip_str);
                    }
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
                    char utf8_str[4];
                    utf8_str[0] = (char)(0xC0 | ((ascii >> 6) & 0x1F));
                    utf8_str[1] = (char)(0x80 | (ascii & 0x3F));
                    utf8_str[2] = '\0';
                    net_send_key(app, INPUT_KEY_DOWN, ascii, utf8_str);
                }
                break;
            }

            case WMHI_RAWKEY: {
                uint16 rawkey = (uint16)(result & WMHI_KEYMASK);
                if ((rawkey & IECODE_UP_PREFIX) == 0) {
                    /* Odyssey Web Browser RAWKEY_NM_WHEEL_UP (0x7A) / RAWKEY_NM_WHEEL_DOWN (0x7B) */
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
                    const char *kname = NULL;
                    switch (rawkey) {
                        case 0x48: kname = "PageUp";     break;
                        case 0x49: kname = "PageDown";   break;
                        case 0x4C: kname = "ArrowUp";    break;
                        case 0x4D: kname = "ArrowDown";  break;
                        case 0x4E: kname = "ArrowRight"; break;
                        case 0x4F: kname = "ArrowLeft";  break;
                        case 0x50: kname = "F1";         break;
                        case 0x51: kname = "F2";         break;
                        case 0x52: kname = "F3";         break;
                        case 0x53: kname = "F4";         break;
                        case 0x54: kname = "F5";         break;
                        case 0x55: kname = "F6";         break;
                        case 0x56: kname = "F7";         break;
                        case 0x57: kname = "F8";         break;
                        case 0x58: kname = "F9";         break;
                        case 0x59: kname = "F10";        break;
                        case 0x70: kname = "Home";       break;
                        case 0x71: kname = "End";        break;
                        default:   break;
                    }
                    if (kname) {
                        net_send_key(app, INPUT_KEY_DOWN, rawkey, kname);
                    }
                }
                break;
            }

            case WMHI_GADGETUP: {
                uint32 gid = (result & WMHI_GADGETMASK);
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
                        app_set_status("Ladevorgang angehalten.");
                        break;

                    case GID_HOME:
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
                        if (app->win) {
                            uint32 secs = 0, micros = 0;
                            IIntuition->CurrentTime(&secs, &micros);
                            dispatch_viewport_mouse_btn(app, app->win->MouseX, app->win->MouseY, 1, TRUE, secs, micros);
                        }
                        break;

                    case GID_SERVER_CHOOSER: {
                        uint32 sel = 0;
                        if (app->gad_server_chooser) {
                            IIntuition->GetAttr(CHOOSER_Selected, app->gad_server_chooser, &sel);
                            if (sel == 0) {
                                strncpy(app->server_host, VORTEXBROWSE_RPI_HOST, sizeof(app->server_host) - 1);
                                app->server_port = VORTEXBROWSE_RPI_PORT;
                                net_connect(app, app->server_host, app->server_port);
                            } else if (sel == 1) {
                                strncpy(app->server_host, VORTEXBROWSE_HOSTPC_HOST, sizeof(app->server_host) - 1);
                                app->server_port = VORTEXBROWSE_HOSTPC_PORT;
                                net_connect(app, app->server_host, app->server_port);
                            } else if (sel == 2) {
                                prompt_server_address(app);
                            }
                        }
                        break;
                    }

                    case GID_CONNECT: {
                        uint32 sel = 0;
                        if (app->gad_server_chooser) {
                            IIntuition->GetAttr(CHOOSER_Selected, app->gad_server_chooser, &sel);
                            if (sel == 0) {
                                strncpy(app->server_host, VORTEXBROWSE_RPI_HOST, sizeof(app->server_host) - 1);
                                app->server_port = VORTEXBROWSE_RPI_PORT;
                            } else if (sel == 1) {
                                strncpy(app->server_host, VORTEXBROWSE_HOSTPC_HOST, sizeof(app->server_host) - 1);
                                app->server_port = VORTEXBROWSE_HOSTPC_PORT;
                            } else if (sel == 2) {
                                prompt_server_address(app);
                                break;
                            }
                        }
                        net_connect(app, app->server_host, app->server_port);
                        break;
                    }

                    case GID_URL_FAVICON:
                        gui_open_password_manager(app);
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
                            net_send_url(app, url_buf);
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
                                app_set_status("Suchmaschine gewaehlt: %s", g_search_engines[sel].name);
                            }
                        }
                        /* If a search term is already entered, execute search with the newly chosen engine */
                        char s_buf[256];
                        get_string_gadget_text(app->gad_search, s_buf, sizeof(s_buf));
                        if (s_buf[0] != '\0') {
                            char enc_buf[512];
                            size_t ei = 0;
                            for (size_t si = 0; s_buf[si] != '\0' && ei + 4 < sizeof(enc_buf); si++) {
                                unsigned char c = (unsigned char)s_buf[si];
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
                            uint8_t se = (app->prefs.search_engine < NUM_SEARCH_ENGINES) ? app->prefs.search_engine : 0;
                            char search_url[VB_MAX_URL_LEN];
                            snprintf(search_url, sizeof(search_url), g_search_engines[se].query_url_fmt, enc_buf);
                            gui_update_url_bar(app, search_url);
                            net_send_url(app, search_url);
                        } else if (app->gad_search && app->win) {
                            IIntuition->ActivateGadget((struct Gadget *)app->gad_search, app->win, NULL);
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
                        get_string_gadget_text(app->gad_search, s_buf, sizeof(s_buf));
                        if (s_buf[0] != '\0') {
                            char enc_buf[512];
                            size_t ei = 0;
                            for (size_t si = 0; s_buf[si] != '\0' && ei + 4 < sizeof(enc_buf); si++) {
                                unsigned char c = (unsigned char)s_buf[si];
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
                            uint8_t se = (app->prefs.search_engine < NUM_SEARCH_ENGINES) ? app->prefs.search_engine : 0;
                            char search_url[VB_MAX_URL_LEN];
                            snprintf(search_url, sizeof(search_url), g_search_engines[se].query_url_fmt, enc_buf);
                            gui_update_url_bar(app, search_url);
                            net_send_url(app, search_url);
                        }
                        break;
                    }

                    case GID_QUICK_ADD:
                        prompt_add_quicklink(app);
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
                                app_set_status("Zu Lesezeichen hinzugefuegt (ENVARC:): %s", u);
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
                        gui_tab_add(app, "Neuer Tab", "", TRUE);
                        break;

                    default: {
                        if (gid >= GID_QUICK_BASE && gid < GID_QUICK_BASE + app->prefs.num_quick_links) {
                            int qidx = (int)(gid - GID_QUICK_BASE);
                            const char *u = app->prefs.quick_links[qidx].url;
                            if (u && u[0] != '\0') {
                                DBG_LOG("[GUI] QuickLink %d clicked: %s (%s)\n", qidx, app->prefs.quick_links[qidx].label, u);
                                gui_update_url_bar(app, u);
                                net_send_url(app, u);
                                app_set_status("Schnellstart: %s", app->prefs.quick_links[qidx].label);
                            }
                        } else if (gid >= GID_QUICK_DEL_BASE && gid < GID_QUICK_DEL_BASE + app->prefs.num_quick_links) {
                            int qidx = (int)(gid - GID_QUICK_DEL_BASE);
                            char rem_name[80];
                            snprintf(rem_name, sizeof(rem_name), "%s", app->prefs.quick_links[qidx].label);
                            DBG_LOG("[GUI] QuickLink %d delete clicked -> removing %s\n", qidx, rem_name);
                            prefs_quicklink_remove(app, qidx);
                            prefs_save(app, TRUE);
                            app_set_status("Schnellwahl entfernt: %s", rem_name);
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

        if (app->reopen_screen_needed || app->reopen_gui_needed || !app->running) {
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
                CTX_AUTOLOGIN
            };

            BOOL has_link  = (app->ctx_menu.link_url[0] != '\0');
            BOOL has_media = (app->ctx_menu.media_url[0] != '\0' &&
                              strcmp(app->ctx_menu.media_url, app->ctx_menu.link_url) != 0);

            char hdr_buf[96];
            if (has_link) {
                const char *lbl = app->ctx_menu.link_text[0] ? app->ctx_menu.link_text : app->ctx_menu.link_url;
                snprintf(hdr_buf, sizeof(hdr_buf), "Link: %.50s", lbl);
            } else if (has_media) {
                snprintf(hdr_buf, sizeof(hdr_buf), "Bild / Medium");
            } else {
                snprintf(hdr_buf, sizeof(hdr_buf), "Odyssey / VortexBrowse");
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
                        PMITEM("Link oeffnen"), PMIA_ID, CTX_OPEN_LINK, PMEND,
                        PMITEM("Link in neuem Tab oeffnen"), PMIA_ID, CTX_OPEN_NEW_WIN, PMEND,
                        PMITEM("Verknuepftes Ziel speichern unter (ASL)..."), PMIA_ID, CTX_DOWNLOAD_LINK, PMEND,
                        PMITEM("Link-Adresse in URL-Leiste kopieren"), PMIA_ID, CTX_COPY_LINK_URL, PMEND,
                        PMITEM("Link zu Lesezeichen hinzufuegen"), PMIA_ID, CTX_FAV_LINK, PMEND,
                        PMBAR(), PMEND,
                        PMITEM("Aktuellen Tab schliessen"), PMIA_ID, CTX_CLOSE_TAB, PMEND,
                        PMITEM("Auto-Login fuer Seite"), PMIA_ID, CTX_AUTOLOGIN, PMEND,
                        PMBAR(), PMEND,
                        PMITEM("Zurueck"), PMIA_ID, CTX_BACK, PMEND,
                        PMITEM("Vorwaerts"), PMIA_ID, CTX_FORWARD, PMEND,
                        PMITEM("Seite neu laden"), PMIA_ID, CTX_RELOAD, PMEND,
                        PMITEM("Startseite (Speed-Dial)"), PMIA_ID, CTX_HOME, PMEND,
                    PMEND;
                } else if (has_media) {
                    menu_obj = PMMENU(hdr_buf),
                        PMA_MenuHandler, &pm_hook,
                        PMITEM("Bild / Medium anzeigen"), PMIA_ID, CTX_OPEN_MEDIA, PMEND,
                        PMITEM("Bild / Medium speichern unter (ASL)..."), PMIA_ID, CTX_DOWNLOAD_MEDIA, PMEND,
                        PMBAR(), PMEND,
                        PMITEM("Neuen Tab oeffnen"), PMIA_ID, CTX_NEW_TAB, PMEND,
                        PMITEM("Aktuellen Tab schliessen"), PMIA_ID, CTX_CLOSE_TAB, PMEND,
                        PMITEM("Auto-Login fuer Seite"), PMIA_ID, CTX_AUTOLOGIN, PMEND,
                        PMBAR(), PMEND,
                        PMITEM("Zurueck"), PMIA_ID, CTX_BACK, PMEND,
                        PMITEM("Vorwaerts"), PMIA_ID, CTX_FORWARD, PMEND,
                        PMITEM("Seite neu laden"), PMIA_ID, CTX_RELOAD, PMEND,
                        PMITEM("Startseite (Speed-Dial)"), PMIA_ID, CTX_HOME, PMEND,
                    PMEND;
                } else {
                    menu_obj = PMMENU(hdr_buf),
                        PMA_MenuHandler, &pm_hook,
                        PMITEM("Zurueck"), PMIA_ID, CTX_BACK, PMEND,
                        PMITEM("Vorwaerts"), PMIA_ID, CTX_FORWARD, PMEND,
                        PMITEM("Seite neu laden"), PMIA_ID, CTX_RELOAD, PMEND,
                        PMITEM("Startseite (Speed-Dial)"), PMIA_ID, CTX_HOME, PMEND,
                        PMBAR(), PMEND,
                        PMITEM("Neuen Tab oeffnen"), PMIA_ID, CTX_NEW_TAB, PMEND,
                        PMITEM("Aktuellen Tab schliessen"), PMIA_ID, CTX_CLOSE_TAB, PMEND,
                        PMBAR(), PMEND,
                        PMITEM("Auto-Login (Zugangsdaten ausfuellen)"), PMIA_ID, CTX_AUTOLOGIN, PMEND,
                        PMITEM("Aktuelle Seite zu Lesezeichen (+Fav)"), PMIA_ID, CTX_FAV_PAGE, PMEND,
                        PMITEM("Aktuelle Adresse herunterladen (ASL)..."), PMIA_ID, CTX_DOWNLOAD_PAGE, PMEND,
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
                es.es_Title        = (STRPTR)"Odyssey / VortexBrowse Kontextmenue";
                if (has_link) {
                    es.es_TextFormat   = (STRPTR)"Link: %s";
                    es.es_GadgetFormat = (STRPTR)"Link oeffnen|Neuer Tab|Tab schliessen|Auto-Login|Herunterladen (ASL)|In URL-Leiste|Zurueck|Abbrechen";
                    LONG choice = IIntuition->EasyRequest(app->win, &es, NULL, app->ctx_menu.link_url);
                    if (choice == 1) selected_id = CTX_OPEN_LINK;
                    else if (choice == 2) selected_id = CTX_OPEN_NEW_WIN;
                    else if (choice == 3) selected_id = CTX_CLOSE_TAB;
                    else if (choice == 4) selected_id = CTX_AUTOLOGIN;
                    else if (choice == 5) selected_id = CTX_DOWNLOAD_LINK;
                    else if (choice == 6) selected_id = CTX_COPY_LINK_URL;
                    else if (choice == 7) selected_id = CTX_BACK;
                } else {
                    es.es_TextFormat   = (STRPTR)"Seiten-Aktion waehlen:";
                    es.es_GadgetFormat = (STRPTR)"Zurueck|Vorwaerts|Neu laden|Startseite|Neuer Tab|Tab schliessen|Auto-Login|Abbrechen";
                    LONG choice = IIntuition->EasyRequest(app->win, &es, NULL);
                    if (choice == 1) selected_id = CTX_BACK;
                    else if (choice == 2) selected_id = CTX_FORWARD;
                    else if (choice == 3) selected_id = CTX_RELOAD;
                    else if (choice == 4) selected_id = CTX_HOME;
                    else if (choice == 5) selected_id = CTX_NEW_TAB;
                    else if (choice == 6) selected_id = CTX_CLOSE_TAB;
                    else if (choice == 7) selected_id = CTX_AUTOLOGIN;
                }
            }

            switch (selected_id) {
                case CTX_NEW_TAB:
                    gui_tab_add(app, "Neuer Tab", "", TRUE);
                    break;

                case CTX_CLOSE_TAB:
                    if (app->num_tabs > 0) {
                        gui_tab_close(app, app->active_tab_idx);
                    }
                    break;

                case CTX_AUTOLOGIN:
                    gui_do_autologin_current_domain(app);
                    break;
                case CTX_OPEN_LINK:
                    gui_update_url_bar(app, app->ctx_menu.link_url);
                    net_send_url(app, app->ctx_menu.link_url);
                    break;

                case CTX_OPEN_NEW_WIN:
                    gui_tab_add(app, app->ctx_menu.link_text[0] ? app->ctx_menu.link_text : "Neuer Tab", app->ctx_menu.link_url, TRUE);
                    app_set_status("Oeffne neuen Tab: %s", app->ctx_menu.link_url);
                    break;

                case CTX_DOWNLOAD_LINK: {
                    char dl_cmd[VB_MAX_URL_LEN + 16];
                    snprintf(dl_cmd, sizeof(dl_cmd), "@DOWNLOAD:%s", app->ctx_menu.link_url);
                    app_set_status("Starte ASL-Download: %s", app->ctx_menu.link_url);
                    net_send_url(app, dl_cmd);
                    break;
                }

                case CTX_COPY_LINK_URL:
                    gui_update_url_bar(app, app->ctx_menu.link_url);
                    app_set_status("Link in URL-Leiste kopiert: %s", app->ctx_menu.link_url);
                    break;

                case CTX_FAV_LINK: {
                    const char *t = app->ctx_menu.link_text[0] ? app->ctx_menu.link_text : app->ctx_menu.link_url;
                    if (favorites_add(app, t, app->ctx_menu.link_url)) {
                        favorites_save(app);
                        favorites_rebuild_chooser_list(app);
                        app_set_status("Link zu Lesezeichen hinzugefuegt: %s", t);
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
                    app_set_status("Starte Medien-Download: %s", app->ctx_menu.media_url);
                    net_send_url(app, dl_cmd);
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
                    gui_update_url_bar(app, "");
                    net_send_url(app, "@HOME");
                    break;

                case CTX_FAV_PAGE: {
                    const char *u = app->current_url[0] ? app->current_url : "https://www.amiga-news.de";
                    if (favorites_add(app, u, u)) {
                        favorites_save(app);
                        favorites_rebuild_chooser_list(app);
                        app_set_status("Seite zu Lesezeichen hinzugefuegt: %s", u);
                    }
                    break;
                }

                case CTX_DOWNLOAD_PAGE: {
                    if (app->current_url[0] != '\0') {
                        char dl_cmd[VB_MAX_URL_LEN + 16];
                        snprintf(dl_cmd, sizeof(dl_cmd), "@DOWNLOAD:%s", app->current_url);
                        app_set_status("Starte Download: %s", app->current_url);
                        net_send_url(app, dl_cmd);
                    }
                    break;
                }

                default:
                    app_set_status("Bereit.");
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
    if (gad->Flags & GFLG_SELECTED) {
        return;
    }
    const char *disp = url ? url : "";
    if (strncmp(disp, "http://127.0.0.1:", 17) == 0) {
        disp = "";
    }
    if (url && app->num_tabs > 0 && app->active_tab_idx < app->num_tabs) {
        strncpy(app->tabs[app->active_tab_idx].url, url, sizeof(app->tabs[0].url) - 1);
        app->tabs[app->active_tab_idx].url[sizeof(app->tabs[0].url) - 1] = '\0';
        if (disp[0] != '\0' && strncmp(disp, "http", 4) == 0) {
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
            snprintf(app->win_title, sizeof(app->win_title), "VortexBrowse: %s", disp);
        } else {
            snprintf(app->win_title, sizeof(app->win_title), "VortexBrowse");
        }
    } else {
        const char *st = app->status_text[0] ? app->status_text : "Bereit";
        if (disp[0] != '\0') {
            snprintf(app->win_title, sizeof(app->win_title), "VortexBrowse: %s [%s]", disp, st);
        } else {
            snprintf(app->win_title, sizeof(app->win_title), "VortexBrowse [%s]", st);
        }
    }
    if (app->win) {
        STRPTR scr_title = (app->custom_screen != NULL) ? (STRPTR)"VortexBrowse 2.0 (AmigaOS 4.1)" : (STRPTR)~0;
        IIntuition->SetWindowTitles(app->win, app->win_title, scr_title);
    }
}

void app_set_status(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_app.status_text, sizeof(g_app.status_text), fmt, args);
    va_end(args);

    if (g_app.gad_status && g_app.win && g_app.prefs.show_status_bar) {
        IIntuition->SetGadgetAttrs((struct Gadget *)g_app.gad_status,
                                   g_app.win, NULL,
                                   GA_Text, g_app.status_text,
                                   TAG_DONE);
    } else if (g_app.win && !g_app.prefs.show_status_bar) {
        const char *u = g_app.current_url[0] ? g_app.current_url : "";
        if (strncmp(u, "http://127.0.0.1:", 17) == 0) u = "";
        if (u[0] != '\0') {
            snprintf(g_app.win_title, sizeof(g_app.win_title), "VortexBrowse: %.256s [%.128s]", u, g_app.status_text);
        } else {
            snprintf(g_app.win_title, sizeof(g_app.win_title), "VortexBrowse [%.128s]", g_app.status_text);
        }
        STRPTR scr_title = (g_app.custom_screen != NULL) ? (STRPTR)"VortexBrowse 2.0 (AmigaOS 4.1)" : (STRPTR)~0;
        IIntuition->SetWindowTitles(g_app.win, g_app.win_title, scr_title);
    }
}

void app_set_gauge(int percent)
{
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;

    if (g_app.gad_gauge && g_app.win) {
        IIntuition->SetGadgetAttrs((struct Gadget *)g_app.gad_gauge,
                                   g_app.win, NULL,
                                   FUELGAUGE_Level, percent,
                                   TAG_DONE);
    }
}
