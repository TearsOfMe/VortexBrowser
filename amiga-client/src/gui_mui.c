#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/bsdsocket.h>
#include <proto/muimaster.h>
#include <inline4/muimaster.h>
#include <proto/popupmenu.h>
#include <classes/popupmenu.h>
#include <libraries/mui.h>
#include <libraries/gadtools.h>
#include <devices/inputevent.h>
#include <sys/select.h>
#include <sys/time.h>

#include "app.h"
#include "gui_mui.h"
#include "gui_prefs.h"
#include "net.h"
#include "renderer.h"
#include "favorites.h"
#include "passwords.h"
#include "clipboard.h"

extern struct SocketIFace *ISocket;
extern struct PopupMenuIFace *IPopupMenu;

#ifndef DoMethod
#define DoMethod(obj, ...) IIntuition->IDoMethod((Object *)(obj), __VA_ARGS__)
#endif
#ifndef DoSuperMethodA
#define DoSuperMethodA(cl, obj, msg) IIntuition->IDoSuperMethodA((cl), (Object *)(obj), (Msg)(msg))
#endif
#undef set
#define set(obj, attr, val) IIntuition->SetAttrs((Object *)(obj), (attr), (val), TAG_DONE)
#undef get
#define get(obj, attr, valptr) IIntuition->GetAttr((attr), (Object *)(obj), (uint32 *)(valptr))

static inline Object *MakeVBar(void)
{
    return (Object *)RectangleObject,
        MUIA_Rectangle_VBar, TRUE,
        MUIA_Weight, 0,
    End;
}

static const char *s_search_engine_labels[] = {
    "DuckDuckGo",
    "Google",
    "Wikipedia",
    "Aminet",
    "OS4Depot",
    "YouTube",
    NULL
};

static const char *s_search_engine_urls[] = {
    "https://duckduckgo.com/?q=",
    "https://www.google.com/search?q=",
    "https://de.wikipedia.org/wiki/Special:Search?search=",
    "https://aminet.net/search?query=",
    "https://os4depot.net/index.php?function=search&search=",
    "https://www.youtube.com/results?search_query="
};

static const char *s_server_entries[] = {
    "Standard Server (127.0.0.1:7788)",
    "Sekundaerer Server (127.0.0.1:9757)",
    "Server manuell...",
    NULL
};

static struct NewMenu s_mui_menu[] = {
    { NM_TITLE, (char *)"Projekt", NULL, 0, 0, (APTR)MID_PROJECT },
    { NM_ITEM,  (char *)"Neuer Tab", (char *)"T", 0, 0, (APTR)MID_NEW_TAB },
    { NM_ITEM,  (char *)"Tab schliessen", (char *)"W", 0, 0, (APTR)MID_CLOSE_TAB },
    { NM_ITEM,  NM_BARLABEL, NULL, 0, 0, (APTR)0 },
    { NM_ITEM,  (char *)"Einstellungen...", (char *)"E", 0, 0, (APTR)MID_SETTINGS },
    { NM_ITEM,  (char *)"Passwort-Manager...", (char *)"P", 0, 0, (APTR)MID_PASSWORDS },
    { NM_ITEM,  (char *)"Download-Manager...", (char *)"D", 0, 0, (APTR)MID_DOWNLOADS },
    { NM_ITEM,  NM_BARLABEL, NULL, 0, 0, (APTR)0 },
    { NM_ITEM,  (char *)"Cache leeren", (char *)"K", 0, 0, (APTR)MID_CLEAR_CACHE },
    { NM_ITEM,  (char *)"Ueber VortexBrowse...", (char *)"?", 0, 0, (APTR)MID_ABOUT },
    { NM_ITEM,  NM_BARLABEL, NULL, 0, 0, (APTR)0 },
    { NM_ITEM,  (char *)"Beenden", (char *)"Q", 0, 0, (APTR)MID_QUIT },

    { NM_TITLE, (char *)"Bearbeiten", NULL, 0, 0, (APTR)MID_EDIT },
    { NM_ITEM,  (char *)"Ausschneiden", (char *)"x", 0, 0, (APTR)MID_EDIT_CUT },
    { NM_ITEM,  (char *)"Kopieren",     (char *)"c", 0, 0, (APTR)MID_EDIT_COPY },
    { NM_ITEM,  (char *)"Einfuegen",    (char *)"v", 0, 0, (APTR)MID_EDIT_PASTE },

    { NM_TITLE, (char *)"Navigation", NULL, 0, 0, (APTR)MID_NAV },
    { NM_ITEM,  (char *)"Zurueck", (char *)"Z", 0, 0, (APTR)MID_BACK },
    { NM_ITEM,  (char *)"Vorwaerts", (char *)"F", 0, 0, (APTR)MID_FORWARD },
    { NM_ITEM,  (char *)"Seite neu laden", (char *)"R", 0, 0, (APTR)MID_RELOAD },
    { NM_ITEM,  (char *)"Ladevorgang stoppen", (char *)"S", 0, 0, (APTR)MID_STOP },
    { NM_ITEM,  (char *)"Startseite", (char *)"H", 0, 0, (APTR)MID_HOME },

    { NM_TITLE, (char *)"Lesezeichen", NULL, 0, 0, (APTR)MID_BOOKMARKS },
    { NM_ITEM,  (char *)"Aktuelle Seite hinzufuegen (+Fav)", (char *)"+", 0, 0, (APTR)MID_ADD_FAV },
    { NM_ITEM,  (char *)"Schnellwahl hinzufuegen (+Quick)", NULL, 0, 0, (APTR)MID_QUICK_ADD },
    { NM_ITEM,  (char *)"Schnellwahl entfernen (-Quick)", NULL, 0, 0, (APTR)MID_QUICK_REMOVE },
    { NM_ITEM,  NM_BARLABEL, NULL, 0, 0, (APTR)0 },
    { NM_ITEM,  (char *)"Amiga-News.de", (char *)"1", 0, 0, (APTR)MID_BM_AMIGANEWS },
    { NM_ITEM,  (char *)"OS4Depot.net", (char *)"2", 0, 0, (APTR)MID_BM_OS4DEPOT },
    { NM_ITEM,  (char *)"Amigans.net", (char *)"3", 0, 0, (APTR)MID_BM_AMIGANS },
    { NM_ITEM,  (char *)"Aminet.net", (char *)"4", 0, 0, (APTR)MID_BM_AMINET },
    { NM_ITEM,  (char *)"YouTube", (char *)"5", 0, 0, (APTR)MID_BM_YOUTUBE },
    { NM_ITEM,  (char *)"Wikipedia (AmigaOS)", (char *)"6", 0, 0, (APTR)MID_BM_WIKI },

    { NM_TITLE, (char *)"Server", NULL, 0, 0, (APTR)MID_SERVER },
    { NM_ITEM,  (char *)"Standard Server (127.0.0.1:7788)", NULL, 0, 0, (APTR)MID_SERVER_RPI },
    { NM_ITEM,  (char *)"Sekundaerer Server (127.0.0.1:9757)", NULL, 0, 0, (APTR)MID_SERVER_PC },
    { NM_ITEM,  (char *)"Serveradresse manuell eingeben...", NULL, 0, 0, (APTR)MID_SERVER_CUSTOM },
    { NM_ITEM,  NM_BARLABEL, NULL, 0, 0, (APTR)0 },
    { NM_ITEM,  (char *)"Verbindung trennen", NULL, 0, 0, (APTR)MID_DISCONNECT },

    { NM_TITLE, (char *)"Ansicht", NULL, 0, 0, (APTR)MID_VIEW },
    { NM_ITEM,  (char *)"Video-Qualitaet / Bitrate:", NULL, NM_ITEMDISABLED, 0, (APTR)0 },
    { NM_ITEM,  (char *)"  Bitrate: 2000 kbps (Niedrig)", NULL, 0, 0, (APTR)MID_BITRATE_2000 },
    { NM_ITEM,  (char *)"  Bitrate: 4000 kbps (Mittel)", NULL, 0, 0, (APTR)MID_BITRATE_4000 },
    { NM_ITEM,  (char *)"  Bitrate: 6000 kbps (Hoch)", NULL, 0, 0, (APTR)MID_BITRATE_6000 },
    { NM_ITEM,  (char *)"  Bitrate: 8000 kbps (Standard / 8 Mbit)", NULL, 0, 0, (APTR)MID_BITRATE_8000 },
    { NM_ITEM,  (char *)"  Bitrate: 10000 kbps (Ultra-HD)", NULL, 0, 0, (APTR)MID_BITRATE_10000 },
    { NM_ITEM,  NM_BARLABEL, NULL, 0, 0, (APTR)0 },
    { NM_ITEM,  (char *)"Framerate:", NULL, NM_ITEMDISABLED, 0, (APTR)0 },
    { NM_ITEM,  (char *)"  15 FPS (Stromsparend)", NULL, 0, 0, (APTR)MID_FPS_15 },
    { NM_ITEM,  (char *)"  25 FPS (Standard / PAL fl\374ssig)", NULL, 0, 0, (APTR)MID_FPS_25 },
    { NM_ITEM,  (char *)"  30 FPS (Empfohlen f\374r YouTube)", NULL, 0, 0, (APTR)MID_FPS_30 },
    { NM_ITEM,  (char *)"  50 FPS (PAL High-Speed)", NULL, 0, 0, (APTR)MID_FPS_50 },
    { NM_ITEM,  (char *)"  60 FPS (Maximal / Gaming)", NULL, 0, 0, (APTR)MID_FPS_60 },

    { NM_END,   NULL, NULL, 0, 0, (APTR)0 }
};

static const char *s_throbber_frames[] = {
    "\33c\33b[ ● ]",
    "\33c\33b[ ◐ ]",
    "\33c\33b[ ◓ ]",
    "\33c\33b[ ◑ ]",
    "\33c\33b[ ◒ ]"
};
#define NUM_THROBBER_FRAMES 5

static void gui_mui_throbber_tick(VortexApp *app, BOOL active)
{
    if (!app || !app->mui_throbber) return;
    if (active) {
        app->mui_throbber_step = (app->mui_throbber_step + 1) % NUM_THROBBER_FRAMES;
        set(app->mui_throbber, MUIA_Text_Contents, s_throbber_frames[app->mui_throbber_step]);
    } else {
        set(app->mui_throbber, MUIA_Text_Contents, "\33c[ ◈ ]");
    }
}

/* -------------------------------------------------------------------------
 * Custom MUI Viewport Class (MUIC_Area Subclass)
 * ------------------------------------------------------------------------- */
struct ViewportData {
    struct MUI_EventHandlerNode eh;
    VortexApp                  *app;
};

static uint32 Viewport_AskMinMax(struct IClass *cl, Object *obj, struct MUIP_AskMinMax *msg)
{
    DoSuperMethodA(cl, obj, (Msg)msg);

    msg->MinMaxInfo->MinWidth  += 640;
    msg->MinMaxInfo->DefWidth  += 1024;
    msg->MinMaxInfo->MaxWidth  += MUI_MAXMAX;

    msg->MinMaxInfo->MinHeight += 400;
    msg->MinMaxInfo->DefHeight += 640;
    msg->MinMaxInfo->MaxHeight += MUI_MAXMAX;

    return 0;
}

static uint32 Viewport_Draw(struct IClass *cl, Object *obj, struct MUIP_Draw *msg)
{
    DoSuperMethodA(cl, obj, (Msg)msg);

    if (msg->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) {
        struct ViewportData *data = INST_DATA(cl, obj);
        VortexApp *app = data->app ? data->app : &g_app;
        struct RastPort *rp = _rp(obj);
        if (app && rp) {
            struct IBox box;
            box.Left   = _mleft(obj);
            box.Top    = _mtop(obj);
            box.Width  = _mwidth(obj);
            box.Height = _mheight(obj);

            app->mui_viewport_box = box;
            app->mui_rp           = rp;

            /* Check if viewport size changed and notify server */
            uint16_t w = (uint16_t)(box.Width & ~1);
            uint16_t h = (uint16_t)(box.Height & ~1);
            if (w > 1280) w = 1280;
            if (h > 900)  h = 900;
            if (w >= 320 && h >= 200 && app->connected &&
                (w != app->renderer.reported_w || h != app->renderer.reported_h)) {
                app->renderer.reported_w = w;
                app->renderer.reported_h = h;
                char cmd[64];
                snprintf(cmd, sizeof(cmd), "@RESIZE:%ux%u", (unsigned)w, (unsigned)h);
                net_send_url(app, cmd);
            }

            renderer_present(app, rp, &box);
            app->renderer.dirty = FALSE;
        }
    }
    return 0;
}

static uint32 Viewport_Setup(struct IClass *cl, Object *obj, Msg msg)
{
    struct ViewportData *data = INST_DATA(cl, obj);
    if (!DoSuperMethodA(cl, obj, msg)) {
        return FALSE;
    }

    data->app = &g_app;
    data->eh.ehn_Class    = cl;
    data->eh.ehn_Object   = obj;
    data->eh.ehn_Events   = IDCMP_MOUSEBUTTONS | IDCMP_RAWKEY | IDCMP_VANILLAKEY | IDCMP_EXTENDEDMOUSE;
    data->eh.ehn_Flags    = MUI_EHF_GUIMODE;
    data->eh.ehn_Priority = -1;

    if (_win(obj)) {
        DoMethod(_win(obj), MUIM_Window_AddEventHandler, &data->eh);
    }
    return TRUE;
}

static uint32 Viewport_Cleanup(struct IClass *cl, Object *obj, Msg msg)
{
    struct ViewportData *data = INST_DATA(cl, obj);
    if (_win(obj)) {
        DoMethod(_win(obj), MUIM_Window_RemEventHandler, &data->eh);
    }
    return DoSuperMethodA(cl, obj, msg);
}

static uint32 Viewport_HandleEvent(struct IClass *cl, Object *obj, struct MUIP_HandleEvent *msg)
{
    struct ViewportData *data = INST_DATA(cl, obj);
    VortexApp *app = data->app ? data->app : &g_app;
    if (!app || !app->connected || !msg->imsg) {
        return 0;
    }

    struct IntuiMessage *imsg = msg->imsg;

    #define _between(a,x,b) ((x)>=(a) && (x)<=(b))
    #define _isinobject(x,y) (_between(_mleft(obj),(x),_mright(obj)) && _between(_mtop(obj),(y),_mbottom(obj)))

    switch (imsg->Class) {
        case IDCMP_MOUSEBUTTONS: {
            if (_isinobject(imsg->MouseX, imsg->MouseY)) {
                int32_t rel_x = (int32_t)(imsg->MouseX - _mleft(obj));
                int32_t rel_y = (int32_t)(imsg->MouseY - _mtop(obj));
                int32_t obj_w = (int32_t)_mwidth(obj);
                int32_t obj_h = (int32_t)_mheight(obj);

                if (obj_w > 0 && obj_h > 0) {
                    uint16_t sw = app->renderer.src_width  ? app->renderer.src_width  : VORTEXBROWSE_DEFAULT_WIDTH;
                    uint16_t sh = app->renderer.src_height ? app->renderer.src_height : VORTEXBROWSE_DEFAULT_HEIGHT;

                    int32_t rx = (rel_x * (int32_t)sw) / obj_w;
                    int32_t ry = (rel_y * (int32_t)sh) / obj_h;
                    if (rx < 0) { rx = 0; }
                    if (rx >= sw) { rx = sw - 1; }
                    if (ry < 0) { ry = 0; }
                    if (ry >= sh) { ry = sh - 1; }

                    if (imsg->Code == SELECTDOWN) {
                        net_send_click(app, rx, ry, 1);
                        return MUI_EventHandlerRC_Eat;
                    } else if (imsg->Code == MIDDLEDOWN) {
                        net_send_click(app, rx, ry, 2);
                        return MUI_EventHandlerRC_Eat;
                    } else if (imsg->Code == MENUDOWN) {
                        memset(&app->ctx_menu, 0, sizeof(MsgContextMenu));
                        app->ctx_menu_ready   = FALSE;
                        app->ctx_menu_pending = TRUE;
                        app_set_status("Kontextmenue wird geoeffnet (%d, %d)...", (int)rx, (int)ry);
                        net_send_click(app, rx, ry, 3);
                        return MUI_EventHandlerRC_Eat;
                    }
                }
            }
            break;
        }

        case IDCMP_EXTENDEDMOUSE: {
            if (imsg->Code & IMSGCODE_INTUIWHEELDATA) {
                if (_isinobject(imsg->MouseX, imsg->MouseY)) {
                    struct IntuiWheelData *wd = (struct IntuiWheelData *)imsg->IAddress;
                    if (wd && wd->WheelY != 0) {
                        int16_t dy = (wd->WheelY > 0) ? 120 : -120;
                        int32_t rel_x = (int32_t)(imsg->MouseX - _mleft(obj));
                        int32_t rel_y = (int32_t)(imsg->MouseY - _mtop(obj));
                        int32_t obj_w = (int32_t)_mwidth(obj);
                        int32_t obj_h = (int32_t)_mheight(obj);
                        uint16_t sw = app->renderer.src_width ? app->renderer.src_width : VORTEXBROWSE_DEFAULT_WIDTH;
                        uint16_t sh = app->renderer.src_height ? app->renderer.src_height : VORTEXBROWSE_DEFAULT_HEIGHT;
                        int32_t rx = obj_w > 0 ? (rel_x * (int32_t)sw) / obj_w : sw / 2;
                        int32_t ry = obj_h > 0 ? (rel_y * (int32_t)sh) / obj_h : sh / 2;
                        net_send_wheel(app, rx, ry, dy);
                        return MUI_EventHandlerRC_Eat;
                    }
                }
            }
            break;
        }

        case IDCMP_VANILLAKEY: {
            if (_isinobject(imsg->MouseX, imsg->MouseY)) {
                uint16 ascii = imsg->Code;
                if (ascii == 0x08) {
                    net_send_key(app, INPUT_KEY_DOWN, ascii, "Backspace");
                    return MUI_EventHandlerRC_Eat;
                } else if (ascii == 0x09) {
                    net_send_key(app, INPUT_KEY_DOWN, ascii, "Tab");
                    return MUI_EventHandlerRC_Eat;
                } else if (ascii == 0x0D || ascii == 0x0A) {
                    net_send_key(app, INPUT_KEY_DOWN, ascii, "Enter");
                    return MUI_EventHandlerRC_Eat;
                } else if (ascii == 0x1B) {
                    net_send_key(app, INPUT_KEY_DOWN, ascii, "Escape");
                    return MUI_EventHandlerRC_Eat;
                } else if (ascii == 0x7F) {
                    net_send_key(app, INPUT_KEY_DOWN, ascii, "Delete");
                    return MUI_EventHandlerRC_Eat;
                } else if (ascii >= 0x20 && ascii <= 0x7E) {
                    char key_str[4];
                    key_str[0] = (char)ascii;
                    key_str[1] = '\0';
                    net_send_key(app, INPUT_KEY_DOWN, ascii, key_str);
                    return MUI_EventHandlerRC_Eat;
                }
            }
            break;
        }

        case IDCMP_RAWKEY: {
            if (_isinobject(imsg->MouseX, imsg->MouseY)) {
                uint16 rawkey = imsg->Code;
                if ((rawkey & IECODE_UP_PREFIX) == 0) {
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
                        return MUI_EventHandlerRC_Eat;
                    }
                }
            }
            break;
        }

        default:
            break;
    }

    return 0;
}

static uint32 Viewport_Dispatcher(struct IClass *cl, Object *obj, Msg msg)
{
    switch (msg->MethodID) {
        case MUIM_AskMinMax:   return Viewport_AskMinMax(cl, obj, (struct MUIP_AskMinMax *)msg);
        case MUIM_Draw:        return Viewport_Draw(cl, obj, (struct MUIP_Draw *)msg);
        case MUIM_Setup:       return Viewport_Setup(cl, obj, msg);
        case MUIM_Cleanup:     return Viewport_Cleanup(cl, obj, msg);
        case MUIM_HandleEvent: return Viewport_HandleEvent(cl, obj, (struct MUIP_HandleEvent *)msg);
        default:               return DoSuperMethodA(cl, obj, msg);
    }
}

static struct MUI_CustomClass *s_viewport_mcc = NULL;

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
        strncpy(out_host, input, host_cap - 1);
        out_host[host_cap - 1] = '\0';
    }
}

static void prompt_server_address_mui(VortexApp *app)
{
    APTR str_obj, btn_ok, btn_cancel, wnd;
    char current_str[160];
    snprintf(current_str, sizeof(current_str), "%s:%u", app->server_host, (unsigned)app->server_port);

    wnd = WindowObject,
        MUIA_Window_Title, "Serveradresse eingeben",
        MUIA_Window_ID, MAKE_ID('S','R','V','R'),
        WindowContents, VGroup,
            Child, Label("VortexBrowse Server (IP:Port):"),
            Child, str_obj = StringObject,
                MUIA_String_Contents, current_str,
                MUIA_String_MaxLen, 128,
            End,
            Child, HGroup,
                Child, btn_ok = SimpleButton("Verbinden"),
                Child, btn_cancel = SimpleButton("Abbrechen"),
            End,
        End,
    End;

    if (!wnd) return;

    DoMethod(app->mui_app, OM_ADDMEMBER, wnd);
    set(wnd, MUIA_Window_Open, TRUE);

    DoMethod(wnd, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, app->mui_app, 2, MUIM_Application_ReturnID, 1002);
    DoMethod(btn_cancel, MUIM_Notify, MUIA_Pressed, FALSE, app->mui_app, 2, MUIM_Application_ReturnID, 1002);
    DoMethod(btn_ok, MUIM_Notify, MUIA_Pressed, FALSE, app->mui_app, 2, MUIM_Application_ReturnID, 1001);
    DoMethod(str_obj, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, app->mui_app, 2, MUIM_Application_ReturnID, 1001);

    ULONG sigs = 0;
    BOOL loop = TRUE;
    BOOL apply = FALSE;

    while (loop) {
        ULONG ret = DoMethod(app->mui_app, MUIM_Application_NewInput, &sigs);
        if (ret == 1001) {
            apply = TRUE;
            loop = FALSE;
        } else if (ret == 1002 || ret == MUIV_Application_ReturnID_Quit) {
            loop = FALSE;
        } else if (sigs) {
            sigs = IExec->Wait(sigs | SIGBREAKF_CTRL_C);
            if (sigs & SIGBREAKF_CTRL_C) break;
        }
    }

    if (apply) {
        char *text = NULL;
        get(str_obj, MUIA_String_Contents, &text);
        if (text && text[0] != '\0') {
            parse_host_port(text, app->server_host, sizeof(app->server_host), &app->server_port);
            net_connect(app, app->server_host, app->server_port);
        }
    }

    set(wnd, MUIA_Window_Open, FALSE);
    DoMethod(app->mui_app, OM_REMMEMBER, wnd);
    MUI_DisposeObject(wnd);
}

void prompt_server_address(VortexApp *app)
{
    prompt_server_address_mui(app);
}

void gui_mui_rebuild_fav_cycle(VortexApp *app)
{
    if (!app) return;

    if (app->mui_fav_entries) {
        for (int i = 0; i < app->mui_fav_entries_count; i++) {
            if (app->mui_fav_entries[i]) free(app->mui_fav_entries[i]);
        }
        free(app->mui_fav_entries);
        app->mui_fav_entries = NULL;
        app->mui_fav_entries_count = 0;
    }

    int count = app->num_favorites;
    if (count <= 0) {
        app->mui_fav_entries = calloc(2, sizeof(char *));
        app->mui_fav_entries[0] = strdup("Keine Lesezeichen");
        app->mui_fav_entries[1] = NULL;
        app->mui_fav_entries_count = 1;
    } else {
        app->mui_fav_entries = calloc(count + 1, sizeof(char *));
        for (int i = 0; i < count; i++) {
            const char *label = app->favorites[i].title[0] ? app->favorites[i].title : app->favorites[i].url;
            app->mui_fav_entries[i] = strdup(label);
        }
        app->mui_fav_entries[count] = NULL;
        app->mui_fav_entries_count = count;
    }

    if (app->mui_fav_cycle) {
        set(app->mui_fav_cycle, MUIA_Cycle_Entries, app->mui_fav_entries);
        set(app->mui_fav_cycle, MUIA_Cycle_Active, 0);
    }
}

static BOOL resolve_icon_path(const char *icon_name, const char *url, char *out_spec, size_t max_len)
{
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

    const char *cand[10];
    int num_c = 0;
    if (icon_name && icon_name[0] != '\0') cand[num_c++] = icon_name;
    if (domain_cand[0] != '\0')            cand[num_c++] = domain_cand;
    cand[num_c++] = "bookmark";
    cand[num_c++] = "globe";
    cand[num_c++] = "drawer";
    cand[num_c++] = "favorite";

    const char *prefixes[2] = { "PROGDIR:icons/", "TBIMAGES:" };

    for (int i = 0; i < num_c; i++) {
        for (int d = 0; d < 2; d++) {
            char path[160];
            snprintf(path, sizeof(path), "%s%s.png", prefixes[d], cand[i]);
            BPTR lk = IDOS ? IDOS->Lock(path, SHARED_LOCK) : ZERO;
            if (lk != ZERO) {
                IDOS->UnLock(lk);
                snprintf(out_spec, max_len, "5:%s", path);
                return TRUE;
            }
            snprintf(path, sizeof(path), "%s%s", prefixes[d], cand[i]);
            lk = IDOS ? IDOS->Lock(path, SHARED_LOCK) : ZERO;
            if (lk != ZERO) {
                IDOS->UnLock(lk);
                snprintf(out_spec, max_len, "5:%s", path);
                return TRUE;
            }
        }
    }
    return FALSE;
}

static APTR make_quicklink_button(const char *label, const char *url, const char *icon_name)
{
    char icon_spec[180];
    BOOL has_icon = resolve_icon_path(icon_name, url, icon_spec, sizeof(icon_spec));

    APTR btn = NULL;
    if (has_icon) {
        APTR img = ImageObject,
            MUIA_Image_Spec, icon_spec,
            MUIA_Image_CopySpec, TRUE,
            MUIA_Image_FontMatchHeight, TRUE,
            MUIA_Image_FreeHoriz, FALSE,
            MUIA_Image_FreeVert, FALSE,
        End;

        btn = HGroup,
            MUIA_Frame, MUIV_Frame_Button,
            MUIA_Background, MUII_ButtonBack,
            MUIA_InputMode, MUIV_InputMode_RelVerify,
            MUIA_ShortHelp, url ? (STRPTR)url : NULL,
            Child, img,
            Child, TextObject,
                MUIA_Text_Contents, label ? (STRPTR)label : "",
                MUIA_Text_PreParse, "\33c",
            End,
        End;
    } else {
        btn = TextObject,
            ButtonFrame,
            MUIA_Font, MUIV_Font_Button,
            MUIA_Text_Contents, label ? (STRPTR)label : "",
            MUIA_Text_PreParse, "\33c",
            MUIA_InputMode, MUIV_InputMode_RelVerify,
            MUIA_Background, MUII_ButtonBack,
            MUIA_ShortHelp, url ? (STRPTR)url : NULL,
        End;
    }
    return btn;
}

void gui_mui_rebuild_quicklinks(VortexApp *app)
{
    if (!app || !app->mui_quick_group) return;

    if (DoMethod(app->mui_quick_group, MUIM_Group_InitChange)) {
        for (int i = 0; i < VB_MAX_QUICKLINKS; i++) {
            if (app->mui_quick_btn[i]) {
                DoMethod(app->mui_quick_group, OM_REMMEMBER, app->mui_quick_btn[i]);
                MUI_DisposeObject(app->mui_quick_btn[i]);
                app->mui_quick_btn[i] = NULL;
            }
        }
        if (app->mui_btn_add_ql) {
            DoMethod(app->mui_quick_group, OM_REMMEMBER, app->mui_btn_add_ql);
            MUI_DisposeObject(app->mui_btn_add_ql);
            app->mui_btn_add_ql = NULL;
        }
        if (app->mui_btn_rem_ql) {
            DoMethod(app->mui_quick_group, OM_REMMEMBER, app->mui_btn_rem_ql);
            MUI_DisposeObject(app->mui_btn_rem_ql);
            app->mui_btn_rem_ql = NULL;
        }
        if (app->mui_btn_prefs) {
            DoMethod(app->mui_quick_group, OM_REMMEMBER, app->mui_btn_prefs);
            MUI_DisposeObject(app->mui_btn_prefs);
            app->mui_btn_prefs = NULL;
        }

        for (int i = 0; i < app->prefs.num_quick_links && i < VB_MAX_QUICKLINKS; i++) {
            app->mui_quick_btn[i] = make_quicklink_button(
                app->prefs.quick_links[i].label,
                app->prefs.quick_links[i].url,
                app->prefs.quick_links[i].icon
            );
            if (app->mui_quick_btn[i]) {
                DoMethod(app->mui_quick_group, OM_ADDMEMBER, app->mui_quick_btn[i]);
                DoMethod(app->mui_quick_btn[i], MUIM_Notify, MUIA_Pressed, FALSE,
                         app->mui_app, 2, MUIM_Application_ReturnID, GID_QUICK_BASE + i);
            }
        }

        app->mui_btn_add_ql = SimpleButton("+");
        if (app->mui_btn_add_ql) {
            DoMethod(app->mui_quick_group, OM_ADDMEMBER, app->mui_btn_add_ql);
            DoMethod(app->mui_btn_add_ql, MUIM_Notify, MUIA_Pressed, FALSE,
                     app->mui_app, 2, MUIM_Application_ReturnID, GID_QUICK_ADD);
        }

        app->mui_btn_rem_ql = SimpleButton("-");
        if (app->mui_btn_rem_ql) {
            DoMethod(app->mui_quick_group, OM_ADDMEMBER, app->mui_btn_rem_ql);
            DoMethod(app->mui_btn_rem_ql, MUIM_Notify, MUIA_Pressed, FALSE,
                     app->mui_app, 2, MUIM_Application_ReturnID, GID_QUICK_REMOVE);
        }

        app->mui_btn_prefs = SimpleButton("Prefs");
        if (app->mui_btn_prefs) {
            DoMethod(app->mui_quick_group, OM_ADDMEMBER, app->mui_btn_prefs);
            DoMethod(app->mui_btn_prefs, MUIM_Notify, MUIA_Pressed, FALSE,
                     app->mui_app, 2, MUIM_Application_ReturnID, GID_PREFS);
        }

        DoMethod(app->mui_quick_group, MUIM_Group_ExitChange);
    }
}

static void prompt_add_quicklink_mui(VortexApp *app)
{
    if (!app) return;

    char *url_buf = NULL;
    get(app->mui_url, MUIA_String_Contents, &url_buf);
    const char *u = (url_buf && url_buf[0] != '\0') ? url_buf : app->current_url;

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
            gui_mui_rebuild_quicklinks(app);
        } else {
            app_set_status("Schnellwahl-Leiste voll (max. %d)!", VB_MAX_QUICKLINKS);
        }
    } else if (res == 2) {
        gui_prefs_open(app);
    }
}

static void remove_current_quicklink_mui(VortexApp *app)
{
    if (!app || app->prefs.num_quick_links <= 0) {
        app_set_status("Keine Schnellwahl zum Entfernen vorhanden.");
        return;
    }

    int target_idx = -1;
    char *url_buf = NULL;
    get(app->mui_url, MUIA_String_Contents, &url_buf);
    const char *cur = (url_buf && url_buf[0] != '\0') ? url_buf : app->current_url;

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

    gui_mui_rebuild_quicklinks(app);

    if (app->prefs.num_quick_links == 0) {
        gui_mui_update_url_bar(app, "@HOME");
        net_send_url(app, "@HOME");
        app_set_status("Schnellwahl entfernt: %s (Startseite geladen)", rem_name);
    } else {
        app_set_status("Schnellwahl entfernt: %s", rem_name);
    }
}

/* -------------------------------------------------------------------------
 * Tab Bar Management (Mirroring Odyssey Web Browser)
 * ------------------------------------------------------------------------- */
void gui_mui_rebuild_tabs(VortexApp *app)
{
    if (!app || !app->mui_tab_bar_group) return;

    if (DoMethod(app->mui_tab_bar_group, MUIM_Group_InitChange)) {
        for (int i = 0; i < VB_MAX_TABS; i++) {
            if (app->mui_tab_item[i]) {
                DoMethod(app->mui_tab_bar_group, OM_REMMEMBER, app->mui_tab_item[i]);
                MUI_DisposeObject(app->mui_tab_item[i]);
                app->mui_tab_item[i]      = NULL;
                app->mui_tab_btn[i]       = NULL;
                app->mui_tab_close_btn[i] = NULL;
            }
        }
        if (app->mui_btn_add_tab) {
            DoMethod(app->mui_tab_bar_group, OM_REMMEMBER, app->mui_btn_add_tab);
            MUI_DisposeObject(app->mui_btn_add_tab);
            app->mui_btn_add_tab = NULL;
        }
        if (app->mui_tab_spacer) {
            DoMethod(app->mui_tab_bar_group, OM_REMMEMBER, app->mui_tab_spacer);
            MUI_DisposeObject(app->mui_tab_spacer);
            app->mui_tab_spacer = NULL;
        }

        if (app->num_tabs == 0) {
            app->tabs[0].id = 0;
            strncpy(app->tabs[0].title, "Startseite", sizeof(app->tabs[0].title) - 1);
            app->tabs[0].url[0] = '\0';
            app->num_tabs = 1;
            app->active_tab_idx = 0;
        }

        for (int i = 0; i < app->num_tabs && i < VB_MAX_TABS; i++) {
            BOOL is_active = (i == app->active_tab_idx);
            char label_buf[96];
            if (is_active) {
                snprintf(label_buf, sizeof(label_buf), "\33c\33b %d: %s ", i + 1, app->tabs[i].title);
            } else {
                snprintf(label_buf, sizeof(label_buf), "\33c %d: %s ", i + 1, app->tabs[i].title);
            }

            app->mui_tab_btn[i] = TextObject,
                ButtonFrame,
                MUIA_Font, MUIV_Font_Button,
                MUIA_Text_Contents, label_buf,
                MUIA_InputMode, MUIV_InputMode_RelVerify,
                MUIA_Background, is_active ? MUII_RegisterBack : MUII_ButtonBack,
                MUIA_ShortHelp, app->tabs[i].url[0] ? (STRPTR)app->tabs[i].url : (STRPTR)"Startseite",
            End;

            app->mui_tab_close_btn[i] = TextObject,
                MUIA_Frame, MUIV_Frame_ImageButton,
                MUIA_Text_Contents, "\33cx",
                MUIA_InputMode, MUIV_InputMode_RelVerify,
                MUIA_Background, is_active ? MUII_RegisterBack : MUII_ButtonBack,
                MUIA_FixWidthTxt, " x ",
                MUIA_ShortHelp, "Tab schliessen",
            End;

            app->mui_tab_item[i] = HGroup,
                MUIA_Group_Horiz, TRUE,
                MUIA_InnerBottom, 0,
                MUIA_InnerTop, 0,
                MUIA_InnerLeft, 0,
                MUIA_InnerRight, 0,
                MUIA_Group_Spacing, 0,
                Child, app->mui_tab_btn[i],
                Child, app->mui_tab_close_btn[i],
            End;

            if (app->mui_tab_item[i]) {
                DoMethod(app->mui_tab_bar_group, OM_ADDMEMBER, app->mui_tab_item[i]);
                DoMethod(app->mui_tab_btn[i], MUIM_Notify, MUIA_Pressed, FALSE,
                         app->mui_app, 2, MUIM_Application_ReturnID, GID_TAB_BASE + i);
                DoMethod(app->mui_tab_close_btn[i], MUIM_Notify, MUIA_Pressed, FALSE,
                         app->mui_app, 2, MUIM_Application_ReturnID, GID_TAB_CLOSE_BASE + i);
            }
        }

        app->mui_btn_add_tab = TextObject,
            MUIA_Frame, MUIV_Frame_ImageButton,
            MUIA_Text_Contents, "\33c + ",
            MUIA_InputMode, MUIV_InputMode_RelVerify,
            MUIA_Background, MUII_ButtonBack,
            MUIA_FixWidthTxt, " + ",
            MUIA_ShortHelp, "Neuen Tab oeffnen",
        End;
        if (app->mui_btn_add_tab) {
            DoMethod(app->mui_tab_bar_group, OM_ADDMEMBER, app->mui_btn_add_tab);
            DoMethod(app->mui_btn_add_tab, MUIM_Notify, MUIA_Pressed, FALSE,
                     app->mui_app, 2, MUIM_Application_ReturnID, GID_TAB_ADD);
        }

        app->mui_tab_spacer = RectangleObject,
            MUIA_Weight, 100,
        End;
        if (app->mui_tab_spacer) {
            DoMethod(app->mui_tab_bar_group, OM_ADDMEMBER, app->mui_tab_spacer);
        }

        DoMethod(app->mui_tab_bar_group, MUIM_Group_ExitChange);
    }
}

void gui_mui_tab_add(VortexApp *app, const char *title, const char *url)
{
    if (!app || app->num_tabs >= VB_MAX_TABS) {
        app_set_status("Maximale Tab-Anzahl erreicht (%d)!", VB_MAX_TABS);
        return;
    }
    int idx = app->num_tabs++;
    app->tabs[idx].id = (uint32)idx;
    strncpy(app->tabs[idx].title, title ? title : "Neuer Tab", sizeof(app->tabs[0].title) - 1);
    app->tabs[idx].title[sizeof(app->tabs[0].title) - 1] = '\0';
    strncpy(app->tabs[idx].url, url ? url : "", sizeof(app->tabs[0].url) - 1);
    app->tabs[idx].url[sizeof(app->tabs[0].url) - 1] = '\0';
    app->active_tab_idx = (uint16)idx;

    if (app->connected) {
        char cmd[VB_MAX_URL_LEN + 16];
        snprintf(cmd, sizeof(cmd), "@NEWTAB:%s", app->tabs[idx].url[0] ? app->tabs[idx].url : "@HOME");
        net_send_url(app, cmd);
    }
    gui_mui_update_url_bar(app, app->tabs[idx].url);
    gui_mui_rebuild_tabs(app);
    app_set_status("Neuer Tab geoeffnet: Tab %d", idx + 1);
}

void gui_mui_tab_switch(VortexApp *app, uint16 idx)
{
    if (!app || idx >= app->num_tabs) return;
    app->active_tab_idx = idx;

    if (app->connected) {
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "@SWITCHTAB:%u", (unsigned)idx);
        net_send_url(app, cmd);
    }
    gui_mui_update_url_bar(app, app->tabs[idx].url);
    gui_mui_rebuild_tabs(app);
    app_set_status("Tab %d: %s", (int)idx + 1, app->tabs[idx].title);
}

void gui_mui_tab_close(VortexApp *app, uint16 idx)
{
    if (!app || idx >= app->num_tabs) return;

    if (app->num_tabs <= 1) {
        strncpy(app->tabs[0].title, "Startseite", sizeof(app->tabs[0].title) - 1);
        app->tabs[0].title[sizeof(app->tabs[0].title) - 1] = '\0';
        app->tabs[0].url[0] = '\0';
        if (app->connected) {
            net_send_url(app, "@HOME");
        }
        gui_mui_update_url_bar(app, "");
        gui_mui_rebuild_tabs(app);
        app_set_status("Tab zurueckgesetzt auf Startseite.");
        return;
    }

    if (app->connected) {
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "@CLOSETAB:%u", (unsigned)idx);
        net_send_url(app, cmd);
    }

    for (int i = idx; i < app->num_tabs - 1; i++) {
        app->tabs[i] = app->tabs[i + 1];
    }
    app->num_tabs--;

    if (app->active_tab_idx == idx) {
        if (app->active_tab_idx >= app->num_tabs) {
            app->active_tab_idx = (uint16)(app->num_tabs - 1);
        }
    } else if (app->active_tab_idx > idx) {
        app->active_tab_idx--;
    }

    if (app->connected) {
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "@SWITCHTAB:%u", (unsigned)app->active_tab_idx);
        net_send_url(app, cmd);
    }
    gui_mui_update_url_bar(app, app->tabs[app->active_tab_idx].url);
    gui_mui_rebuild_tabs(app);
    app_set_status("Tab geschlossen. Aktiver Tab: %d", app->active_tab_idx + 1);
}

void gui_tabs_add(VortexApp *app, const char *title, const char *url)
{
    gui_mui_tab_add(app, title, url);
}

void gui_tabs_switch(VortexApp *app, uint16 idx)
{
    gui_mui_tab_switch(app, idx);
}

void gui_tabs_close(VortexApp *app, uint16 idx)
{
    gui_mui_tab_close(app, idx);
}

void gui_tabs_update_title(VortexApp *app, uint16 idx, const char *title)
{
    if (!app || idx >= app->num_tabs || !title) return;
    strncpy(app->tabs[idx].title, title, sizeof(app->tabs[0].title) - 1);
    app->tabs[idx].title[sizeof(app->tabs[0].title) - 1] = '\0';
    gui_mui_rebuild_tabs(app);
}

void gui_tabs_init(VortexApp *app)
{
    gui_mui_rebuild_tabs(app);
}

void gui_tabs_rebuild(VortexApp *app)
{
    gui_mui_rebuild_tabs(app);
}

void gui_tabs_reset(VortexApp *app)
{
    if (!app) return;
    if (app->num_tabs > 0 && app->active_tab_idx < app->num_tabs && app->active_tab_idx != 0) {
        app->tabs[0] = app->tabs[app->active_tab_idx];
    }
    if (app->num_tabs == 0) {
        strncpy(app->tabs[0].title, "Startseite", sizeof(app->tabs[0].title) - 1);
        app->tabs[0].title[sizeof(app->tabs[0].title) - 1] = '\0';
        app->tabs[0].url[0] = '\0';
    }
    app->tabs[0].id = 0;
    app->num_tabs = 1;
    app->active_tab_idx = 0;
    gui_mui_rebuild_tabs(app);
}

void gui_mui_update_status_bar(VortexApp *app)
{
    if (!app || !app->mui_fps_info) return;

    const char *sm_mode = (app->prefs.video_stream_mode == STREAM_VIDEO_H264) ? "H.264" :
                          (app->prefs.video_stream_mode == STREAM_VIDEO_DIRTYRECT) ? "LZ4" : "Auto";

    char codec_info[48];
    if (app->renderer.last_codec == CODEC_H264) {
        if (app->renderer.has_va && app->renderer.is_h264_active) {
            snprintf(codec_info, sizeof(codec_info), "H.264 VA-API HW");
        } else if (app->renderer.has_altivec) {
            snprintf(codec_info, sizeof(codec_info), "H.264 AltiVec");
        } else {
            snprintf(codec_info, sizeof(codec_info), "H.264 SW");
        }
    } else if (app->renderer.last_codec == CODEC_LZ4_RECT) {
        snprintf(codec_info, sizeof(codec_info), "LZ4 (2D Rect)");
    } else if (app->renderer.last_codec == CODEC_LZ4) {
        snprintf(codec_info, sizeof(codec_info), "LZ4 (Full)");
    } else if (app->renderer.last_codec == CODEC_JPEG) {
        snprintf(codec_info, sizeof(codec_info), "JPEG");
    } else {
        if (app->renderer.has_va) {
            snprintf(codec_info, sizeof(codec_info), "Bereit (VA-API HW)");
        } else if (app->renderer.has_altivec) {
            snprintf(codec_info, sizeof(codec_info), "Bereit (AltiVec)");
        } else {
            snprintf(codec_info, sizeof(codec_info), "Bereit (SW)");
        }
    }

    uint32 br = app->prefs.video_bitrate_k ? app->prefs.video_bitrate_k : 8000;
    uint8 fps = app->prefs.target_fps ? app->prefs.target_fps : 25;

    char buf[128];
    snprintf(buf, sizeof(buf), "\33cStream: %s [%s] | %u FPS | %u kbps", sm_mode, codec_info, (unsigned)fps, (unsigned)br);
    set(app->mui_fps_info, MUIA_Text_Contents, buf);
}

void gui_mui_update_url_bar(VortexApp *app, const char *url)
{
    if (!app || !app->mui_url) {
        return;
    }
    const char *disp = url ? url : "";
    if (strncmp(disp, "http://127.0.0.1:", 17) == 0) {
        disp = "";
    }
    set(app->mui_url, MUIA_String_Contents, (STRPTR)disp);

    /* Update Security Badge and Zone Indicator */
    if (app->mui_security_badge) {
        if (strncmp(disp, "https://", 8) == 0) {
            set(app->mui_security_badge, MUIA_Text_Contents, "\33c\33b[🔒 https]");
            if (app->mui_zone_indicator) {
                set(app->mui_zone_indicator, MUIA_Text_Contents, "\33c🔒 Sicher (TLS)");
            }
        } else if (strncmp(disp, "http://", 7) == 0) {
            set(app->mui_security_badge, MUIA_Text_Contents, "\33c[🌐 http]");
            if (app->mui_zone_indicator) {
                set(app->mui_zone_indicator, MUIA_Text_Contents, "\33c🌐 Nicht sicher");
            }
        } else {
            set(app->mui_security_badge, MUIA_Text_Contents, "\33c[⚙ portal]");
            if (app->mui_zone_indicator) {
                set(app->mui_zone_indicator, MUIA_Text_Contents, "\33c⚙ Amiga Intern");
            }
        }
    }

    if (disp[0] != '\0') {
        snprintf(app->win_title, sizeof(app->win_title), "%s (MUI) - %s", VB_APP_TITLE, disp);
    } else {
        snprintf(app->win_title, sizeof(app->win_title), "%s (MUI)", VB_APP_TITLE);
    }
    if (app->mui_win) {
        set(app->mui_win, MUIA_Window_Title, (STRPTR)app->win_title);
    }

    /* Update active tab's URL and title */
    if (app->num_tabs > 0 && app->active_tab_idx < app->num_tabs) {
        if (disp[0] != '\0') {
            strncpy(app->tabs[app->active_tab_idx].url, disp, sizeof(app->tabs[0].url) - 1);
            app->tabs[app->active_tab_idx].url[sizeof(app->tabs[0].url) - 1] = '\0';

            if (strcmp(app->tabs[app->active_tab_idx].title, "Startseite") == 0 ||
                strcmp(app->tabs[app->active_tab_idx].title, "Neuer Tab") == 0 ||
                app->tabs[app->active_tab_idx].title[0] == '\0') {
                const char *h = disp;
                if (strncmp(h, "http://", 7) == 0) h += 7;
                else if (strncmp(h, "https://", 8) == 0) h += 8;
                if (strncmp(h, "www.", 4) == 0) h += 4;
                char host[64];
                strncpy(host, h, sizeof(host) - 1);
                host[sizeof(host) - 1] = '\0';
                char *sl = strchr(host, '/');
                if (sl) *sl = '\0';
                if (host[0] != '\0') {
                    snprintf(app->tabs[app->active_tab_idx].title, sizeof(app->tabs[0].title), "%s", host);
                    gui_mui_rebuild_tabs(app);
                }
            }
        }
    }
}

void gui_update_url_bar(VortexApp *app, const char *url)
{
    gui_mui_update_url_bar(app, url);
}

void app_set_status(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_app.status_text, sizeof(g_app.status_text), fmt, args);
    va_end(args);

    if (g_app.mui_status) {
        set(g_app.mui_status, MUIA_Text_Contents, (STRPTR)g_app.status_text);
    }
}

void app_set_gauge(int percent)
{
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;

    if (g_app.mui_gauge) {
        set(g_app.mui_gauge, MUIA_Gauge_Current, percent);
    }

    if (percent > 0 && percent < 100) {
        g_app.mui_loading = TRUE;
        gui_mui_throbber_tick(&g_app, TRUE);
    } else {
        g_app.mui_loading = FALSE;
        gui_mui_throbber_tick(&g_app, FALSE);
    }
}

/* -------------------------------------------------------------------------
 * GUI Initialization (MUI Edition matching Odyssey Web Browser)
 * ------------------------------------------------------------------------- */
BOOL gui_mui_init(VortexApp *app)
{
    if (!IMUIMaster) {
        DBG_LOG("[GUI-MUI] ERROR: IMUIMaster interface is NULL!\n");
        return FALSE;
    }

    s_viewport_mcc = MUI_CreateCustomClass(
        NULL, MUIC_Area, NULL, sizeof(struct ViewportData), (APTR)Viewport_Dispatcher);

    if (!s_viewport_mcc) {
        DBG_LOG("[GUI-MUI] ERROR: Failed to create custom MUI viewport class!\n");
        return FALSE;
    }

    gui_mui_rebuild_fav_cycle(app);

    APTR btn_back, btn_fwd, btn_reload, btn_stop, btn_home;
    APTR btn_downloads, btn_passwords, btn_new_tab;
    APTR btn_clear_url, btn_go, btn_search_go;
    APTR btn_add_fav, btn_bookmarks, btn_connect;
    APTR nav_group, url_group, fastlink_group, addressbar_group, searchbar_group, page_group, status_row;

    snprintf(app->win_title, sizeof(app->win_title), "%s (MUI)", VB_APP_TITLE);

    APTR menustrip = MUI_MakeObject(MUIO_MenustripNM, s_mui_menu, 0);

    /* ------------------------------------------------------------------------
     * Row 1: Navigation Toolbar (Odyssey navigationgroup)
     * ------------------------------------------------------------------------ */
    nav_group = HGroup,
        Child, btn_back      = SimpleButton("<"),
        Child, btn_fwd       = SimpleButton(">"),
        Child, btn_reload    = SimpleButton("Neu"),
        Child, btn_stop      = SimpleButton("Stopp"),
        Child, btn_home      = SimpleButton("Start"),
        Child, MakeVBar(),
        Child, btn_downloads = SimpleButton("Downloads"),
        Child, btn_passwords = SimpleButton("Passw\366rter"),
        Child, RectangleObject,
            MUIA_Weight, 100,
        End,
        Child, app->mui_throbber = TextObject,
            ButtonFrame,
            MUIA_Font, MUIV_Font_Button,
            MUIA_Text_Contents, "\33c[ ◈ ]",
            MUIA_FixWidthTxt, " [ ◈ ] ",
            MUIA_ShortHelp, "Ladeindikator (Throbber)",
        End,
    End;

    /* ------------------------------------------------------------------------
     * Row 2: Fastlinks / Quicklinks Bar (Odyssey fastlinkparentgroup)
     * ------------------------------------------------------------------------ */
    app->mui_quick_group = HGroup,
        MUIA_Group_Horiz, TRUE,
    End;

    for (int i = 0; i < VB_MAX_QUICKLINKS; i++) {
        app->mui_quick_btn[i] = NULL;
    }
    app->mui_btn_add_ql = NULL;
    app->mui_btn_rem_ql = NULL;
    app->mui_btn_prefs  = NULL;

    fastlink_group = HGroup,
        Child, app->mui_quick_group,
        Child, RectangleObject,
            MUIA_Weight, 100,
        End,
        Child, app->mui_server_cycle = CycleObject,
            MUIA_Cycle_Entries, s_server_entries,
            MUIA_Cycle_Active, 0,
            MUIA_CycleChain, 1,
            MUIA_ShortHelp, "VortexBrowser-Server waehlen",
        End,
        Child, btn_connect          = SimpleButton("Verbinden"),
    End;

    /* ------------------------------------------------------------------------
     * Row 3: Address & Search Group (Odyssey urlgroup + BalanceObject)
     * ------------------------------------------------------------------------ */
    addressbar_group = HGroup,
        Child, btn_new_tab   = SimpleButton("+ Tab"),
        Child, app->mui_security_badge = TextObject,
            ButtonFrame,
            MUIA_Font, MUIV_Font_Button,
            MUIA_Text_Contents, "\33c[⚙ portal]",
            MUIA_FixWidthTxt, " [🔒 https] ",
            MUIA_ShortHelp, "Sicherheitsstatus der Verbindung",
        End,
        Child, app->mui_url = StringObject,
            MUIA_String_Contents, app->current_url[0] ? app->current_url : "",
            MUIA_String_MaxLen, VB_MAX_URL_LEN - 1,
            MUIA_CycleChain, 1,
            MUIA_ShortHelp, "Webadresse (URL) eingeben",
        End,
        Child, btn_clear_url = SimpleButton("X"),
        Child, btn_go        = SimpleButton("Los"),
        Child, MakeVBar(),
        Child, btn_add_fav   = SimpleButton("+Lesez."),
        Child, btn_bookmarks = SimpleButton("Lesezeichen"),
        Child, app->mui_fav_cycle = CycleObject,
            MUIA_Cycle_Entries, app->mui_fav_entries,
            MUIA_CycleChain, 1,
            MUIA_ShortHelp, "Gespeicherte Lesezeichen",
        End,
    End;

    searchbar_group = HGroup,
        Child, app->mui_search_engine_cycle = CycleObject,
            MUIA_Cycle_Entries, s_search_engine_labels,
            MUIA_Cycle_Active, 0,
            MUIA_CycleChain, 1,
            MUIA_ShortHelp, "Suchmaschine auswaehlen",
        End,
        Child, app->mui_search = StringObject,
            MUIA_String_Contents, "",
            MUIA_String_MaxLen, 128,
            MUIA_CycleChain, 1,
            MUIA_ShortHelp, "Suchbegriff eingeben und Enter druecken",
        End,
        Child, btn_search_go = SimpleButton("Suche"),
    End;

    url_group = HGroup,
        Child, addressbar_group,
        Child, BalanceObject,
            MUIA_ObjectID, MAKE_ID('V','B','B','A'),
            MUIA_Balance_Quiet, TRUE,
        End,
        Child, searchbar_group,
    End;

    /* ------------------------------------------------------------------------
     * Row 4: Dedicated Tab Bar (Odyssey pagegroup / titlegroup)
     * ------------------------------------------------------------------------ */
    app->mui_tab_bar_group = HGroup,
        MUIA_Group_Horiz, TRUE,
        MUIA_InnerBottom, 0,
        MUIA_InnerTop, 2,
        MUIA_InnerLeft, 2,
        MUIA_InnerRight, 2,
        MUIA_Group_Spacing, 2,
    End;

    for (int i = 0; i < VB_MAX_TABS; i++) {
        app->mui_tab_item[i]      = NULL;
        app->mui_tab_btn[i]       = NULL;
        app->mui_tab_close_btn[i] = NULL;
    }
    app->mui_btn_add_tab = NULL;
    app->mui_tab_spacer  = NULL;

    /* ------------------------------------------------------------------------
     * Row 5: Custom Viewport Object
     * ------------------------------------------------------------------------ */
    app->mui_viewport = IIntuition->NewObject(s_viewport_mcc->mcc_Class, NULL,
        MUIA_InputMode, MUIV_InputMode_RelVerify,
        TAG_DONE);

    if (!app->mui_viewport) {
        DBG_LOG("[GUI-MUI] ERROR: Failed to instantiate custom viewport object!\n");
        return FALSE;
    }

    /* ------------------------------------------------------------------------
     * Unified Page Group (Odyssey pagegroup containing Tabs + Viewport)
     * ------------------------------------------------------------------------ */
    page_group = VGroup,
        MUIA_Background, MUII_RegisterBack,
        MUIA_Frame, MUIV_Frame_Register,
        MUIA_InnerBottom, 0,
        MUIA_InnerTop, 0,
        MUIA_InnerLeft, 0,
        MUIA_InnerRight, 0,
        MUIA_Group_Spacing, 0,
        Child, app->mui_tab_bar_group,
        Child, app->mui_viewport,
    End;

    /* ------------------------------------------------------------------------
     * Row 6: Segmented Status Bar (Odyssey statusgroup)
     * ------------------------------------------------------------------------ */
    status_row = HGroup,
        Child, app->mui_zone_indicator = TextObject,
            MUIA_Frame, MUIV_Frame_ReadList,
            MUIA_Text_Contents, "\33c⚙ Amiga Intern",
            MUIA_FixWidthTxt, " 🔒 Sicher (TLS) ",
            MUIA_Weight, 15,
        End,
        Child, MakeVBar(),
        Child, app->mui_status = TextObject,
            MUIA_Text_Contents, app->status_text,
            MUIA_Frame, MUIV_Frame_ReadList,
            MUIA_Weight, 55,
        End,
        Child, MakeVBar(),
        Child, app->mui_gauge = GaugeObject,
            MUIA_Gauge_Horiz, TRUE,
            MUIA_Gauge_Max, 100,
            MUIA_Gauge_Current, 0,
            MUIA_FixWidth, 140,
            MUIA_Weight, 12,
        End,
        Child, MakeVBar(),
        Child, app->mui_fps_info = TextObject,
            MUIA_Frame, MUIV_Frame_ReadList,
            MUIA_Text_Contents, "\33cStream: Auto [Bereit] | 25 FPS | 8000 kbps",
            MUIA_FixWidthTxt, " Stream: Auto [H.264 VA-API HW] | 60 FPS | 10000 kbps ",
            MUIA_Weight, 25,
        End,
    End;

    /* ------------------------------------------------------------------------
     * Root Window & Application
     * ------------------------------------------------------------------------ */
    app->mui_app = ApplicationObject,
        MUIA_Application_Title, "VortexBrowse",
        MUIA_Application_Version, "$VER: VortexBrowse-MUI 1.0 (AmigaOS 4.1 PowerPC)",
        MUIA_Application_Copyright, "(c) 2026 TearsOfMe",
        MUIA_Application_Author, "TearsOfMe / Antigravity",
        MUIA_Application_Description, "Modern Remote Web Browser for AmigaOS 4 (MUI Edition)",
        MUIA_Application_Base, "VORTEXBROWSE",
        MUIA_Application_Menustrip, menustrip,

        SubWindow, app->mui_win = WindowObject,
            MUIA_Window_Title, app->win_title,
            MUIA_Window_ID, MAKE_ID('V','B','M','U'),
            WindowContents, VGroup,
                Child, nav_group,
                Child, fastlink_group,
                Child, url_group,
                Child, page_group,
                Child, status_row,
            End,
        End,
    End;

    if (!app->mui_app) {
        DBG_LOG("[GUI-MUI] ERROR: Failed to create ApplicationObject!\n");
        return FALSE;
    }

    /* Set up Notifications for Toolbar Buttons */
    DoMethod(btn_back,      MUIM_Notify, MUIA_Pressed, FALSE, app->mui_app, 2, MUIM_Application_ReturnID, GID_BACK);
    DoMethod(btn_fwd,       MUIM_Notify, MUIA_Pressed, FALSE, app->mui_app, 2, MUIM_Application_ReturnID, GID_FORWARD);
    DoMethod(btn_reload,    MUIM_Notify, MUIA_Pressed, FALSE, app->mui_app, 2, MUIM_Application_ReturnID, GID_RELOAD);
    DoMethod(btn_stop,      MUIM_Notify, MUIA_Pressed, FALSE, app->mui_app, 2, MUIM_Application_ReturnID, GID_STOP);
    DoMethod(btn_home,      MUIM_Notify, MUIA_Pressed, FALSE, app->mui_app, 2, MUIM_Application_ReturnID, GID_HOME);
    DoMethod(btn_downloads, MUIM_Notify, MUIA_Pressed, FALSE, app->mui_app, 2, MUIM_Application_ReturnID, GID_DOWNLOADS);
    DoMethod(btn_passwords, MUIM_Notify, MUIA_Pressed, FALSE, app->mui_app, 2, MUIM_Application_ReturnID, GID_PASSWORDS);
    DoMethod(btn_new_tab,   MUIM_Notify, MUIA_Pressed, FALSE, app->mui_app, 2, MUIM_Application_ReturnID, GID_NEW_TAB);

    /* Row 3 Address & Search */
    DoMethod(btn_clear_url, MUIM_Notify, MUIA_Pressed, FALSE, app->mui_app, 2, MUIM_Application_ReturnID, GID_CLEAR_URL);
    DoMethod(btn_go,        MUIM_Notify, MUIA_Pressed, FALSE, app->mui_app, 2, MUIM_Application_ReturnID, GID_GO);
    DoMethod(btn_search_go, MUIM_Notify, MUIA_Pressed, FALSE, app->mui_app, 2, MUIM_Application_ReturnID, GID_SEARCH_GO);
    DoMethod(app->mui_url,    MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, app->mui_app, 2, MUIM_Application_ReturnID, GID_GO);
    DoMethod(app->mui_search, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, app->mui_app, 2, MUIM_Application_ReturnID, GID_SEARCH_GO);
    DoMethod(btn_add_fav,   MUIM_Notify, MUIA_Pressed, FALSE, app->mui_app, 2, MUIM_Application_ReturnID, GID_ADD_FAV);
    DoMethod(btn_bookmarks, MUIM_Notify, MUIA_Pressed, FALSE, app->mui_app, 2, MUIM_Application_ReturnID, GID_BOOKMARKS);
    DoMethod(app->mui_fav_cycle, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, app->mui_app, 2, MUIM_Application_ReturnID, GID_FAV_CHOOSER);

    /* Row 2 Quicklinks & Server */
    DoMethod(btn_connect,          MUIM_Notify, MUIA_Pressed, FALSE, app->mui_app, 2, MUIM_Application_ReturnID, GID_CONNECT);
    DoMethod(app->mui_server_cycle, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, app->mui_app, 2, MUIM_Application_ReturnID, GID_SERVER_CHOOSER);

    /* Menu Actions */
    DoMethod(app->mui_app, MUIM_Notify, MUIA_Application_MenuAction, MUIV_EveryTime, app->mui_app, 2, MUIM_Application_ReturnID, MUIV_TriggerValue);

    /* Window Close Request */
    DoMethod(app->mui_win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, app->mui_app, 2, MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);

    /* Rebuild dynamic contents */
    gui_mui_rebuild_quicklinks(app);
    gui_mui_rebuild_tabs(app);
    gui_mui_update_status_bar(app);
    gui_mui_update_url_bar(app, app->current_url);

    /* Open the window */
    set(app->mui_win, MUIA_Window_Open, TRUE);

    /* Obtain Intuition window pointer */
    get(app->mui_win, MUIA_Window_Window, &app->win);

    return TRUE;
}

void gui_mui_cleanup(VortexApp *app)
{
    if (app->mui_app) {
        set(app->mui_win, MUIA_Window_Open, FALSE);
        MUI_DisposeObject(app->mui_app);
        app->mui_app = NULL;
        app->mui_win = NULL;
        app->win     = NULL;
        app->mui_tab_spacer = NULL;
    }

    if (s_viewport_mcc) {
        MUI_DeleteCustomClass(s_viewport_mcc);
        s_viewport_mcc = NULL;
    }

    if (app->mui_fav_entries) {
        for (int i = 0; i < app->mui_fav_entries_count; i++) {
            if (app->mui_fav_entries[i]) free(app->mui_fav_entries[i]);
        }
        free(app->mui_fav_entries);
        app->mui_fav_entries = NULL;
        app->mui_fav_entries_count = 0;
    }
}

void gui_mui_flush_dirty(VortexApp *app)
{
    if (app && app->mui_viewport) {
        MUI_Redraw((Object *)app->mui_viewport, MADF_DRAWUPDATE);
    }
}

void gui_mui_handle_action(VortexApp *app, uint32 action_id)
{
    switch (action_id) {
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

        case GID_BACK:
        case MID_BACK:
            net_send_url(app, "@BACK");
            break;

        case GID_FORWARD:
        case MID_FORWARD:
            net_send_url(app, "@FORWARD");
            break;

        case GID_RELOAD:
        case MID_RELOAD:
            net_send_url(app, "@RELOAD");
            break;

        case GID_STOP:
        case MID_STOP:
            net_send_url(app, "@STOP");
            app_set_status("Ladevorgang angehalten.");
            break;

        case GID_HOME:
        case MID_HOME:
            gui_mui_update_url_bar(app, "");
            net_send_url(app, "@HOME");
            break;

        case GID_CLEAR_URL:
            if (app->mui_url) {
                set(app->mui_url, MUIA_String_Contents, "");
            }
            break;

        case GID_GO: {
            char *url_buf = NULL;
            get(app->mui_url, MUIA_String_Contents, &url_buf);
            if (url_buf && url_buf[0] != '\0') {
                net_send_url(app, url_buf);
            }
            break;
        }

        case GID_SEARCH:
        case GID_SEARCH_GO: {
            char *s_buf = NULL;
            get(app->mui_search, MUIA_String_Contents, &s_buf);
            if (s_buf && s_buf[0] != '\0') {
                ULONG s_idx = 0;
                if (app->mui_search_engine_cycle) {
                    get(app->mui_search_engine_cycle, MUIA_Cycle_Active, &s_idx);
                }
                if (s_idx >= 6) s_idx = 0;
                char full_search[384];
                snprintf(full_search, sizeof(full_search), "%s%s", s_search_engine_urls[s_idx], s_buf);
                gui_mui_update_url_bar(app, full_search);
                net_send_url(app, full_search);
                app_set_status("Suche (%s): %s", s_search_engine_labels[s_idx], s_buf);
            }
            break;
        }

        case GID_NEW_TAB:
        case MID_NEW_TAB:
        case GID_TAB_ADD:
            gui_mui_tab_add(app, "Neuer Tab", "@HOME");
            break;

        case GID_CLOSE_TAB:
        case MID_CLOSE_TAB:
            gui_mui_tab_close(app, app->active_tab_idx);
            break;

        case GID_BOOKMARKS:
        case MID_BOOKMARKS:
            app_set_status("Lesezeichen: %d Schnellwahl-Links geladen", app->prefs.num_quick_links);
            break;

        case GID_DOWNLOADS:
        case MID_DOWNLOADS: {
            char dl_msg[512];
            snprintf(dl_msg, sizeof(dl_msg),
                     "Downloads-Ordner: %s\n"
                     "Aktive Downloads: %d\n\n"
                     "Moechten Sie den Download-Ordner auf der Workbench oeffnen?",
                     app->prefs.download_dir[0] ? app->prefs.download_dir : "RAM:",
                     app->download.active ? 1 : 0);
            LONG r = MUI_Request(app->mui_app, app->mui_win, 0, "Download-Manager", "Ordner oeffnen|Abbrechen", dl_msg);
            if (r == 1) {
                char open_cmd[300];
                snprintf(open_cmd, sizeof(open_cmd), "Open \"%s\"", app->prefs.download_dir[0] ? app->prefs.download_dir : "RAM:");
                IDOS->SystemTags(open_cmd, TAG_DONE);
            }
            break;
        }

        case GID_PASSWORDS:
        case MID_PASSWORDS: {
            int count = app->password_store ? app->password_store->count : 0;
            if (count <= 0) {
                MUI_Request(app->mui_app, app->mui_win, 0, "Passwort-Manager", "OK",
                            "Keine gespeicherten Passwoerter vorhanden.\n\n"
                            "Passwoerter koennen ueber das Formular-Autologin oder Einstellungen gespeichert werden.");
            } else {
                char list_buf[1024];
                int offset = snprintf(list_buf, sizeof(list_buf), "Gespeicherte Zugangsdaten (%d Eintraege):\n\n", count);
                for (int i = 0; i < count && i < 10; i++) {
                    const PasswordEntry *e = &app->password_store->entries[i];
                    if (e && offset < (int)sizeof(list_buf) - 80) {
                        offset += snprintf(list_buf + offset, sizeof(list_buf) - offset, "• %s  (Benutzer: %s)\n", e->domain, e->username);
                    }
                }
                MUI_Request(app->mui_app, app->mui_win, 0, "Passwort-Manager", "Schliessen", list_buf);
            }
            break;
        }

        case GID_BM_AMIGANEWS:
        case MID_BM_AMIGANEWS:
            net_send_url(app, "https://www.amiga-news.de");
            break;

        case GID_BM_OS4DEPOT:
        case MID_BM_OS4DEPOT:
            net_send_url(app, "https://os4depot.net");
            break;

        case GID_BM_AMIGANS:
        case MID_BM_AMIGANS:
            net_send_url(app, "https://www.amigans.net");
            break;

        case GID_BM_AMINET:
        case MID_BM_AMINET:
            net_send_url(app, "https://aminet.net");
            break;

        case GID_BM_YOUTUBE:
        case MID_BM_YOUTUBE:
            net_send_url(app, "https://www.youtube.com");
            break;

        case GID_BM_WIKI:
        case MID_BM_WIKI:
            net_send_url(app, "https://de.wikipedia.org/wiki/AmigaOS");
            break;

        case GID_ADD_FAV:
        case MID_ADD_FAV: {
            char *url_buf = NULL;
            get(app->mui_url, MUIA_String_Contents, &url_buf);
            const char *u = (url_buf && url_buf[0] != '\0') ? url_buf : app->current_url;
            if (u && u[0] != '\0') {
                if (favorites_add(app, u, u)) {
                    favorites_save(app);
                    gui_mui_rebuild_fav_cycle(app);
                    app_set_status("Zu Lesezeichen hinzugefuegt (ENVARC:): %s", u);
                }
            }
            break;
        }

        case GID_FAV_CHOOSER: {
            ULONG idx = 0;
            get(app->mui_fav_cycle, MUIA_Cycle_Active, &idx);
            if ((int)idx >= 0 && (int)idx < app->num_favorites) {
                const char *fav_url = app->favorites[idx].url;
                if (fav_url[0] != '@') {
                    gui_mui_update_url_bar(app, fav_url);
                } else {
                    gui_mui_update_url_bar(app, "");
                }
                net_send_url(app, fav_url);
            }
            break;
        }

        case GID_SERVER_CHOOSER: {
            ULONG sel = 0;
            get(app->mui_server_cycle, MUIA_Cycle_Active, &sel);
            if (sel == 0) {
                strncpy(app->server_host, VORTEXBROWSE_RPI_HOST, sizeof(app->server_host) - 1);
                app->server_port = VORTEXBROWSE_RPI_PORT;
                app_set_status("Server gewaehlt: Raspberry Pi 4 (%s:%u)", app->server_host, app->server_port);
                net_connect(app, app->server_host, app->server_port);
            } else if (sel == 1) {
                strncpy(app->server_host, VORTEXBROWSE_HOSTPC_HOST, sizeof(app->server_host) - 1);
                app->server_port = VORTEXBROWSE_HOSTPC_PORT;
                app_set_status("Server gewaehlt: Host-PC (%s:%u)", app->server_host, app->server_port);
                net_connect(app, app->server_host, app->server_port);
            } else if (sel == 2) {
                prompt_server_address_mui(app);
            }
            break;
        }

        case GID_CONNECT:
            net_connect(app, app->server_host, app->server_port);
            break;

        case GID_MENU: {
            memset(&app->ctx_menu, 0, sizeof(MsgContextMenu));
            app->ctx_menu_ready   = FALSE;
            app->ctx_menu_pending = TRUE;
            app_set_status("Kontextmenue wird geoeffnet...");
            net_send_click(app, 640, 360, 3);
            break;
        }

        case MID_SERVER_RPI:
            set(app->mui_server_cycle, MUIA_Cycle_Active, 0);
            strncpy(app->server_host, VORTEXBROWSE_RPI_HOST, sizeof(app->server_host) - 1);
            app->server_port = VORTEXBROWSE_RPI_PORT;
            app_set_status("Server: Raspberry Pi 4 (%s:%u)", app->server_host, app->server_port);
            net_connect(app, app->server_host, app->server_port);
            break;

        case MID_SERVER_PC:
            set(app->mui_server_cycle, MUIA_Cycle_Active, 1);
            strncpy(app->server_host, VORTEXBROWSE_HOSTPC_HOST, sizeof(app->server_host) - 1);
            app->server_port = VORTEXBROWSE_HOSTPC_PORT;
            app_set_status("Server: Host-PC (%s:%u)", app->server_host, app->server_port);
            net_connect(app, app->server_host, app->server_port);
            break;

        case MID_SERVER_CUSTOM:
            prompt_server_address_mui(app);
            break;

        case MID_DISCONNECT:
            net_disconnect(app);
            app_set_status("Verbindung getrennt.");
            break;

        case MID_CLEAR_CACHE:
            net_send_url(app, "@CLEARCACHE");
            app_set_status("Cache-Bereinigung angefordert.");
            break;

        case MID_BITRATE_2000:
        case MID_BITRATE_4000:
        case MID_BITRATE_6000:
        case MID_BITRATE_8000:
        case MID_BITRATE_10000: {
            uint32 br = 4000;
            if (action_id == MID_BITRATE_2000) br = 2000;
            else if (action_id == MID_BITRATE_4000) br = 4000;
            else if (action_id == MID_BITRATE_6000) br = 6000;
            else if (action_id == MID_BITRATE_8000) br = 8000;
            else if (action_id == MID_BITRATE_10000) br = 10000;
            app->prefs.video_bitrate_k = br;
            if (app->connected) {
                char brcmd[32];
                snprintf(brcmd, sizeof(brcmd), "@VIDEO_BITRATE:%u", (unsigned)br);
                net_send_url(app, brcmd);
            }
            gui_mui_update_status_bar(app);
            app_set_status("Video-Bitrate gesetzt: %u kbps", (unsigned)br);
            break;
        }

        case MID_FPS_15:
        case MID_FPS_25:
        case MID_FPS_30:
        case MID_FPS_50:
        case MID_FPS_60: {
            uint8_t fps_val = 25;
            if (action_id == MID_FPS_15) fps_val = 15;
            else if (action_id == MID_FPS_25) fps_val = 25;
            else if (action_id == MID_FPS_30) fps_val = 30;
            else if (action_id == MID_FPS_50) fps_val = 50;
            else if (action_id == MID_FPS_60) fps_val = 60;
            app->prefs.target_fps = fps_val;
            if (app->connected) {
                char fcmd[32];
                snprintf(fcmd, sizeof(fcmd), "@FPS:%u", (unsigned)fps_val);
                net_send_url(app, fcmd);
            }
            gui_mui_update_status_bar(app);
            app_set_status("Framerate gesetzt: %u FPS", (unsigned)fps_val);
            break;
        }

        case MID_ABOUT:
            MUI_Request(app->mui_app, app->mui_win, 0,
                        "Ueber VortexBrowse (MUI)",
                        "OK",
                        "VortexBrowse 1.0 (MUI Edition)\n"
                        "AmigaOS 4.1 PowerPC\n\n"
                        "Moderner Remote-Browser fuer AmigaOS 4,\n"
                        "Odyssey-Web-Browser Style MUI Interface,\n"
                        "Compositing Video-Engine, AltiVec SIMD & Hardware VA-API.\n\n"
                        "(c) 2026 TearsOfMe");
            break;

        case MID_QUIT:
        case MUIV_Application_ReturnID_Quit:
            app->running = FALSE;
            break;

        case GID_QUICK_ADD:
        case MID_QUICK_ADD:
            prompt_add_quicklink_mui(app);
            break;

        case GID_QUICK_REMOVE:
        case MID_QUICK_REMOVE:
            remove_current_quicklink_mui(app);
            break;

        case GID_PREFS:
        case MID_SETTINGS:
            gui_prefs_open(app);
            break;

        default:
            if (action_id >= GID_QUICK_BASE && action_id < GID_QUICK_BASE + app->prefs.num_quick_links) {
                int qidx = (int)(action_id - GID_QUICK_BASE);
                const char *u = app->prefs.quick_links[qidx].url;
                if (u && u[0] != '\0') {
                    app->active_quicklink_idx = qidx;
                    gui_mui_update_url_bar(app, u);
                    net_send_url(app, u);
                    app_set_status("Schnellstart: %s", app->prefs.quick_links[qidx].label);
                }
            } else if (action_id >= GID_TAB_BASE && action_id < GID_TAB_BASE + app->num_tabs) {
                uint16 t_idx = (uint16)(action_id - GID_TAB_BASE);
                gui_mui_tab_switch(app, t_idx);
            } else if (action_id >= GID_TAB_CLOSE_BASE && action_id < GID_TAB_CLOSE_BASE + app->num_tabs) {
                uint16 c_idx = (uint16)(action_id - GID_TAB_CLOSE_BASE);
                gui_mui_tab_close(app, c_idx);
            }
            break;
    }
}

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

void gui_mui_handle_events(VortexApp *app)
{
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
        gui_mui_flush_dirty(app);

        struct Window *target_win = app->win;
        if (!target_win && app->mui_win) {
            get(app->mui_win, MUIA_Window_Window, &target_win);
        }

        if (target_win) {
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
                CTX_DOWNLOAD_PAGE
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
                snprintf(hdr_buf, sizeof(hdr_buf), "VortexBrowse");
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
                        PMITEM("Aktuelle Seite zu Lesezeichen (+Fav)"), PMIA_ID, CTX_FAV_PAGE, PMEND,
                        PMITEM("Aktuelle Adresse herunterladen (ASL)..."), PMIA_ID, CTX_DOWNLOAD_PAGE, PMEND,
                    PMEND;
                }

                if (menu_obj) {
                    Object *sel_item = (Object *)IIntuition->IDoMethod(menu_obj, PM_OPEN, target_win);
                    if (selected_id == CTX_NONE && sel_item != NULL) {
                        uint32 id_val = 0;
                        if (IIntuition->GetAttr(PMIA_ID, sel_item, &id_val) && id_val != 0) {
                            selected_id = id_val;
                        }
                    }
                    IIntuition->DisposeObject(menu_obj);
                }
            } else {
                struct EasyStruct es;
                memset(&es, 0, sizeof(es));
                es.es_StructSize   = sizeof(struct EasyStruct);
                es.es_Title        = (STRPTR)"VortexBrowse Kontextmenue";
                if (has_link) {
                    es.es_TextFormat   = (STRPTR)"Link: %s";
                    es.es_GadgetFormat = (STRPTR)"Link oeffnen|Neuer Tab|Herunterladen (ASL)|In URL-Leiste|Zurueck|Abbrechen";
                    LONG choice = IIntuition->EasyRequest(target_win, &es, NULL, app->ctx_menu.link_url);
                    if (choice == 1) selected_id = CTX_OPEN_LINK;
                    else if (choice == 2) selected_id = CTX_OPEN_NEW_WIN;
                    else if (choice == 3) selected_id = CTX_DOWNLOAD_LINK;
                    else if (choice == 4) selected_id = CTX_COPY_LINK_URL;
                    else if (choice == 5) selected_id = CTX_BACK;
                } else {
                    es.es_TextFormat   = (STRPTR)"Seiten-Aktion waehlen:";
                    es.es_GadgetFormat = (STRPTR)"Zurueck|Vorwaerts|Neu laden|Startseite|Abbrechen";
                    LONG choice = IIntuition->EasyRequest(target_win, &es, NULL);
                    if (choice == 1) selected_id = CTX_BACK;
                    else if (choice == 2) selected_id = CTX_FORWARD;
                    else if (choice == 3) selected_id = CTX_RELOAD;
                    else if (choice == 4) selected_id = CTX_HOME;
                }
            }

            switch (selected_id) {
                case CTX_OPEN_LINK:
                    gui_mui_update_url_bar(app, app->ctx_menu.link_url);
                    net_send_url(app, app->ctx_menu.link_url);
                    break;

                case CTX_OPEN_NEW_WIN:
                    gui_mui_tab_add(app, "Neuer Tab", app->ctx_menu.link_url);
                    break;

                case CTX_DOWNLOAD_LINK: {
                    char dl_cmd[VB_MAX_URL_LEN + 16];
                    snprintf(dl_cmd, sizeof(dl_cmd), "@DOWNLOAD:%s", app->ctx_menu.link_url);
                    app_set_status("Starte ASL-Download: %s", app->ctx_menu.link_url);
                    net_send_url(app, dl_cmd);
                    break;
                }

                case CTX_COPY_LINK_URL:
                    gui_mui_update_url_bar(app, app->ctx_menu.link_url);
                    app_set_status("Link in URL-Leiste kopiert: %s", app->ctx_menu.link_url);
                    break;

                case CTX_FAV_LINK: {
                    const char *t = app->ctx_menu.link_text[0] ? app->ctx_menu.link_text : app->ctx_menu.link_url;
                    if (favorites_add(app, t, app->ctx_menu.link_url)) {
                        favorites_save(app);
                        gui_mui_rebuild_fav_cycle(app);
                        app_set_status("Link zu Lesezeichen hinzugefuegt: %s", t);
                    }
                    break;
                }

                case CTX_OPEN_MEDIA:
                    gui_mui_update_url_bar(app, app->ctx_menu.media_url);
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
                    gui_mui_update_url_bar(app, "");
                    net_send_url(app, "@HOME");
                    break;

                case CTX_FAV_PAGE: {
                    const char *u = app->current_url[0] ? app->current_url : "https://www.amiga-news.de";
                    if (favorites_add(app, u, u)) {
                        favorites_save(app);
                        gui_mui_rebuild_fav_cycle(app);
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

/* Stubs and helpers for Download & Password Managers in MUI edition */
void gui_download_manager_update(VortexApp *app)
{
    (void)app;
}

void gui_open_download_manager(VortexApp *app)
{
    if (app) gui_mui_handle_action(app, GID_DOWNLOADS);
}

void gui_close_download_manager(VortexApp *app)
{
    (void)app;
}

void gui_open_password_manager(VortexApp *app)
{
    if (app) gui_mui_handle_action(app, GID_PASSWORDS);
}
